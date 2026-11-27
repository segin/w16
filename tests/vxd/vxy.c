/* vxy.c - VXY.386, a probe VxD of w16's own (test only): follows the
 * next device's Device_Init instruction by instruction (the trap flag)
 * and writes each step's address and registers to COM1 (DOSBox-X
 * serial1=file), every line starting "VXY " (ARCHITECTURE.md 12.18:
 * what Win32s's W32S.386 does at Device_Init under Windows 3.1's and
 * Windows for Workgroups 3.11's VMMs).
 *
 * Its init order is BFFFFFFFh, just before W32S.386's C0000000h. At
 * Device_Init it takes over the debug exception's IDT gate (the 386's
 * SIDT) and returns with the trap flag set; each step until the VMM calls
 * the next control procedure (seen as the first step, after VXY's own
 * RET, with the stack where it was when VXY's own control procedure was
 * entered) is only counted.
 * Inside that control procedure every step within 64 KB of its entry is
 * written (EIP, EAX..EDI, ESP, the flags), each address at most 16 times
 * (loops are cut short; "VXY loop" says which), and the first step out of
 * that range after one in it (a call into the VMM: only where it goes).
 * When the control procedure returns (to its caller's return address)
 * the result is written and the trap flag cleared. Black box: addresses
 * and registers, never the device's or the VMM's code bytes.
 *
 * Besides (each below, by its [386Enh] keys): VXYHook (a procedure
 * pointer's calls), VXYWatch (every access to pages of data, the values
 * before and after), VXYSvc (the VMM's memory services called from a
 * range). The exception handlers pass faults from V86 mode and ring 3
 * straight on to the VMM's (DS may be null there).
 */
#include "w16vxd.h"

#pragma data_seg("_TEXT", "CODE")

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];

static vu32 get_dr6(void);
static void set_dr6(vu32 v);
static void set_dr0(vu32 v);
static void set_dr7(vu32 v);
#pragma aux get_dr6 = "mov eax, dr6" value [eax] modify exact [eax];
#pragma aux set_dr6 = "mov dr6, eax" parm [eax] modify exact [];
#pragma aux set_dr0 = "mov dr0, eax" parm [eax] modify exact [];
#pragma aux set_dr7 = "mov dr7, eax" parm [eax] modify exact [];

void __cdecl vxy_hook(void);            /* vxyddb.asm: the IDT gate */
extern vu32 vxy_stepping;               /* vxyddb.asm: the handler's */
extern vu32 vxy_bp;                     /* vxyddb.asm: an execution breakpoint is armed */
vu32 __cdecl vmm_profile_hex(vu32 deflt, const char *key);  /* Get_Profile_Hex_Int, [386Enh] */
extern struct vxd_ddb VXY_DDB;          /* vxyddb.asm */

/* the debug exception's frame: PUSHAD, then the CPU's (same ring) */
struct sframe {
    vu32 edi, esi, ebp, esp0, ebx, edx, ecx, eax;
    vu32 eip, cs, eflags;
};
/* the page fault's: PUSHAD, the error code, then the CPU's */
struct pfframe {
    vu32 edi, esi, ebp, esp0, ebx, edx, ecx, eax;
    vu32 err, eip, cs, eflags;
};

static void putc_(int c)
{
    int i;
    for (i = 0; i < 100000 && !(inb(0x3FD) & 0x20); i++) ;
    outb(0x3F8, c);
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex(vu32 v, int n)
{
    while (n--) putc_("0123456789ABCDEF"[(v >> (n * 4)) & 15]);
}

static vu32 entry_sp;                   /* ESP where control procedures start */
static vu32 target, ret_addr;           /* the stepped control procedure's entry, its caller's */
static vu32 waited, lines, outside, own_ret;
#define NHASH 4096
static vu32 hkey[NHASH], hcount[NHASH];

static vu32 *count_of(vu32 eip)
{
    vu32 h = (eip * 2654435761u) >> 20, n;
    for (n = 0; n < NHASH; n++, h = (h + 1) & (NHASH - 1)) {
        if (hkey[h] == eip) return &hcount[h];
        if (!hkey[h]) { hkey[h] = eip; hcount[h] = 0; return &hcount[h]; }
    }
    return 0;
}

static void regline(const char *what, struct sframe *f)
{
    puts_("VXY ");
    puts_(what);
    hex(f->eip, 8);
    puts_(" a "); hex(f->eax, 8);
    puts_(" b "); hex(f->ebx, 8);
    puts_(" c "); hex(f->ecx, 8);
    puts_(" d "); hex(f->edx, 8);
    puts_(" si "); hex(f->esi, 8);
    puts_(" di "); hex(f->edi, 8);
    puts_(" bp "); hex(f->ebp, 8);
    puts_(" sp "); hex((vu32)&f->eflags + 4, 8);
    puts_(" f "); hex(f->eflags & 0xFFF, 3);
    puts_("\r\n");
}

/* [386Enh] VXYBreak=address (hex): once the stepped procedure has
 * returned, an execution breakpoint there (DR0): each of its first 400
 * hits is written with the registers and 8 dwords of the stack (where it
 * was called from) */
static vu32 bp_addr, bp_hits;

static void stop(struct sframe *f)
{
    f->eflags &= ~0x100u;
    vxy_stepping = 0;
    if (bp_addr) {
        set_dr0(bp_addr);
        set_dr6(0);
        set_dr7(0x203);                 /* L0 G0, execution, GE */
        vxy_bp = 1;
    }
}

/* (VXYHook) */
static vu32 hook_at, hook_hits;
extern vu32 vxy_next;                   /* vxyddb.asm: where vxy_thunk goes on */
void __cdecl vxy_thunk(void);

static void regs7(struct vregs *r)
{
    puts_(" a "); hex(r->eax, 8);
    puts_(" b "); hex(r->ebx, 8);
    puts_(" c "); hex(r->ecx, 8);
    puts_(" d "); hex(r->edx, 8);
    puts_(" si "); hex(r->esi, 8);
    puts_(" di "); hex(r->edi, 8);
    puts_(" bp "); hex(r->ebp, 8);
    puts_(" f "); hex(r->eflags & 0xFFF, 3);
}

/* each call: the registers as the procedure is called (PUSHAD, then the
 * return address) and 24 dwords of the caller's stack, and the registers
 * it returns; a call for the page after the last one's, from the same
 * place, is only counted (a run of pages), unless among the first 40 */
static vu32 retstack[16], logged[16], depth, last_page, last_from, last_esi, run;

/* the procedure pointer at VXYHook changed since it was last seen (a
 * device put its own there): written once for each new value */
static vu32 t_last;
static void t_check(void)
{
    vu32 t;
    if (!hook_at || !t_last) return;
    t = *(vu32 *)hook_at;
    if (t == t_last) return;
    t_last = t;
    puts_("VXY now at ");
    hex(hook_at, 8);
    puts_(" ");
    hex(t, 8);
    puts_("\r\n");
}

vu32 __cdecl vxy_called(struct vregs *r)
{
    vu32 *sp = (vu32 *)(r->esp0 + 4), i, lg;
    hook_hits++;
    t_check();
    lg = !(r->eax >> 12 == last_page + 1 && sp[0] == last_from && r->esi == last_esi) || hook_hits < 40;
    if (!lg) run++;
    else {
        if (run) { puts_("VXY  and "); hex(run, 8); puts_(" pages after it\r\n"); run = 0; }
        puts_("VXY call from ");
        hex(sp[0], 8);
        regs7(r);
        puts_("\r\nVXY  stack");
        for (i = 1; i < 25; i++) { putc_(' '); hex(sp[i], 8); }
        puts_("\r\n");
    }
    last_page = r->eax >> 12;
    last_from = sp[0];
    last_esi = r->esi;
    if (depth == 16) return 0;
    logged[depth] = lg;
    retstack[depth++] = sp[0];
    return 1;
}

vu32 __cdecl vxy_returned(struct vregs *r)
{
    depth--;
    if (logged[depth]) {
        puts_("VXY  back");
        regs7(r);
        puts_("\r\n");
    }
    return retstack[depth];
}

/* [386Enh] VXYWatch=address, VXYWatchLen=bytes (hex; also VXYWatch2/
 * VXYWatchLen2, VXYWatch3/VXYWatchLen3): from Init_Complete on, the
 * present pages there are made not present (their page table entries
 * reached through _MapPhysToLinear of CR3, as VXW does); every access to
 * them faults into vxy_pf, which lets it through for one instruction and,
 * after it (vxy_pf_step), writes one line: EIP, read or write, the linear
 * address, the dword there before and after (">" when it changed), the
 * registers before. Code below VXYQuiet is left out (0: none). Pages not
 * present at the start are left to the VMM; after each access every page
 * of the ranges that is present then is watched again (pages the VMM
 * made present since too). At most VXYWatchMax lines (default 80000h),
 * no other limit. */
static vu32 get_cr3(void);
static vu32 get_cr2(void);
static void set_cr3(vu32 v);
#pragma aux get_cr3 = "mov eax, cr3" value [eax] modify exact [eax];
#pragma aux get_cr2 = "mov eax, cr2" value [eax] modify exact [eax];
#pragma aux set_cr3 = "mov cr3, eax" parm [eax] modify exact [];
vu32 __cdecl vmm_map_phys(vu32 phys, vu32 n, vu32 flags);
extern vu32 vxy_pf_on, vxy_pf_armed, vxy_pf_user;   /* vxyddb.asm */
#define NWP 512
static vu32 w_lo[3], w_hi[3], w_nr, w_quiet, w_max, w_logged, w_skiplo, w_skiphi;
static vu32 *w_pte[NWP], w_page[NWP], w_armed[NWP], w_np;
static vu32 *w_pd, w_sysvm;
static vu32 s_addr, s_before, s_eip, s_cs, s_err, s_r[6];

static void watch_rescan(void)
{
    vu32 i;
    for (i = 0; i < w_np; i++) {
        if (*w_pte[i] & 1) { *w_pte[i] &= ~1u; w_armed[i] = 1; }
    }
    set_cr3(get_cr3());
}

static void watch_arm(void)
{
    vu32 a, r;
    w_pd = (vu32 *)vmm_map_phys(get_cr3() & ~0xFFFu, 4096, 0);
    for (r = 0; r < w_nr; r++) {
        for (a = w_lo[r] & ~0xFFFu; a < w_hi[r] && w_np < NWP; a += 4096) {
            vu32 *pt;
            if (!(w_pd[a >> 22] & 1)) continue;
            pt = (vu32 *)vmm_map_phys(w_pd[a >> 22] & ~0xFFFu, 4096, 0);
            w_pte[w_np] = pt + ((a >> 12) & 0x3FF);
            w_page[w_np] = a;
            puts_("VXY page ");
            hex(a, 8);
            puts_(" pte ");
            hex(*w_pte[w_np], 8);
            puts_("\r\n");
            w_np++;
        }
    }
    puts_("VXY watching pages ");
    hex(w_np, 4);
    puts_("\r\n");
    watch_rescan();
    vxy_pf_on = 1;
}

/* the page fault: 1 ours (made present, TF set), 0 not */
vu32 __cdecl vxy_pf(struct pfframe *f)
{
    vu32 err = f->err;
    vu32 a = get_cr2(), i, r;
    for (r = 0; r < w_nr; r++)
        if (a >= (w_lo[r] & ~0xFFFu) && a < w_hi[r]) break;
    if (r == w_nr) return 0;
    for (i = 0; i < w_np; i++)
        if (w_page[i] == (a & ~0xFFFu)) break;
    if (i == w_np || !w_armed[i]) return 0;             /* the VMM's */
    for (i = 0; i < w_np; i++)
        if (w_armed[i]) { *w_pte[i] |= 1; w_armed[i] = 0; }
    set_cr3(get_cr3());
    s_addr = a;
    s_before = *(vu32 *)(a & ~3u);
    s_eip = f->eip;
    s_cs = f->cs;
    s_err = err;
    s_r[0] = f->eax; s_r[1] = f->ebx; s_r[2] = f->ecx;
    s_r[3] = f->edx; s_r[4] = f->esi; s_r[5] = f->edi;
    f->eflags |= 0x100;
    vxy_pf_armed = 1;
    return 1;
}

/* the instruction after: its line, then not present again */
static void vxy_pf_step(struct sframe *f)
{
    if (s_eip >= w_quiet && !(s_eip >= w_skiplo && s_eip < w_skiphi) && w_logged < w_max) {
        vu32 after = *(vu32 *)(s_addr & ~3u);
        w_logged++;
        puts_("VXY pf ");
        if (s_cs & 3) { hex(s_cs, 4); putc_(':'); }
        hex(s_eip, 8);
        puts_(s_err & 2 ? " w " : " r ");
        hex(s_addr, 8);
        puts_(" ");
        hex(s_before, 8);
        puts_(after != s_before ? " > " : " = ");
        hex(after, 8);
        puts_(" a "); hex(s_r[0], 8);
        puts_(" b "); hex(s_r[1], 8);
        puts_(" c "); hex(s_r[2], 8);
        puts_(" d "); hex(s_r[3], 8);
        puts_(" si "); hex(s_r[4], 8);
        puts_(" di "); hex(s_r[5], 8);
        puts_("\r\n");
    }
    watch_rescan();
    f->eflags &= ~0x100u;
    vxy_pf_armed = 0;
}

/* [386Enh] VXYSvc=1: from Init_Complete on, the VMM services in svc_ids
 * are hooked (Hook_Device_Service); each call whose return address is in
 * VXYSvcLo..VXYSvcHi (hex; default: 16 KB either side of the next
 * device's control procedure) is written with the registers and six
 * dwords of the stack after the return address (the C services'
 * arguments), and what it returns. Lines "VXY svc" and "VXY  ret". */
static const vu32 svc_ids[] = {
    0x10053, 0x10054, 0x10055, 0x10056, 0x10057, 0x10058, 0x10059,
    0x1005D, 0x1005E, 0x10060, 0x1006C, 0x1006F, 0x1004F, 0x10051
};
#define NSVC (sizeof svc_ids / sizeof svc_ids[0])
#define NSVCMAX 256
static vu32 svc_id[NSVCMAX], nsvc;      /* the services hooked */
extern vu32 vxy_svc_prev[NSVCMAX];      /* vxyddb.asm */
extern vu32 vxy_svc_stub[NSVCMAX];
extern vu32 vxy_svc_after_addr;
vu32 __cdecl vmm_hook_service(vu32 id, vu32 proc);   /* the previous; 0: failed */
static vu32 svc_lo, svc_hi;
struct svcregs {
    vu32 edi, esi, ebp, esp0, ebx, edx, ecx, eax, eflags, idx, ret, args[6];
};
static vu32 sv_slot[64], sv_ret[64], sv_idx[64], sv_n;

static void cregs(const struct client_regs *c);

vu32 __cdecl vxy_svc_called(struct svcregs *r)
{
    vu32 i;
    t_check();
    if (r->ret < svc_lo || r->ret >= svc_hi || sv_n == 64) return 0;
    puts_("VXY svc ");
    hex(svc_id[r->idx] >> 16, 2);
    hex(svc_id[r->idx] & 0xFFFF, 4);
    puts_(" from ");
    hex(r->ret, 8);
    regs7((struct vregs *)r);
    puts_(" args");
    for (i = 0; i < 6; i++) { putc_(' '); hex(r->args[i], 8); }
    puts_("\r\n");
    if (svc_id[r->idx] == 0x10084 && r->ebx >= 0x80000000u) {  /* Exec_Int: the VM's client registers */
        puts_("VXY  client");
        cregs((const struct client_regs *)CB_CLIENT(r->ebx));
    }
    sv_slot[sv_n] = (vu32)&r->ret;
    sv_ret[sv_n] = r->ret;
    sv_idx[sv_n++] = r->idx;
    r->ret = vxy_svc_after_addr;
    return 1;
}

/* r: the registers as the service returned; slot: where its return
 * address was */
vu32 __cdecl vxy_svc_returned(struct vregs *r, vu32 slot)
{
    vu32 n = sv_n, ret;
    while (n && sv_slot[n - 1] != slot) n--;
    if (!n) { puts_("VXY  lost return\r\n"); for (;;) ; }
    n--;
    ret = sv_ret[n];
    puts_("VXY  ret ");
    hex(svc_id[sv_idx[n]] >> 16, 2);
    hex(svc_id[sv_idx[n]] & 0xFFFF, 4);
    regs7(r);
    puts_("\r\n");
    if (svc_id[sv_idx[n]] == 0x10084 && r->ebx >= 0x80000000u) {
        puts_("VXY  client");
        cregs((const struct client_regs *)CB_CLIENT(r->ebx));
    }
    sv_n = n;
    return ret;
}

static void svc_hook_all(vu32 all)
{
    vu32 i;
    nsvc = 0;
    if (all) {
        /* VXYSvc=2: every VMM service but those that read their caller
         * (the debug ones, Log_Proc_Call) and Hook_Device_Service, then
         * PageSwap's */
        for (i = 0; i < 0xF2 && nsvc < NSVCMAX; i++)
            if (i != 0x90 && !(i >= 0xC1 && i <= 0xCC)) svc_id[nsvc++] = 0x10000 + i;
        for (i = 0; i < 7 && nsvc < NSVCMAX; i++) svc_id[nsvc++] = 0x70000 + i;
    } else
        for (i = 0; i < NSVC; i++) svc_id[nsvc++] = svc_ids[i];
    for (i = 0; i < nsvc; i++) {
        vxy_svc_prev[i] = vmm_hook_service(svc_id[i], vxy_svc_stub[i]);
        if (!vxy_svc_prev[i]) { puts_("VXY hook failed "); hex(svc_id[i], 8); puts_("\r\n"); }
    }
    puts_("VXY services hooked from ");
    hex(svc_lo, 8);
    puts_(" to ");
    hex(svc_hi, 8);
    puts_("\r\n");
}

/* [386Enh] VXYApi=1: from Init_Complete on, the next device's PM API
 * procedure (W32S.386's) is hooked with Hook_Device_PM_API; every call
 * is written with the client's registers before ("VXY api N in") and
 * after ("VXY api N out"), the top of the client's stack, the descriptors
 * of the segment registers and of LDT selectors handed back (each when
 * first seen or changed), and VXYApiDump bytes (default 20h) at DS:ESI,
 * ES:EDI, DS:EBX, ES:EBX, DS:EDX and at registers holding a ring-0
 * linear address, before and, when changed, after. Only present pages
 * are read (nothing is faulted in). */
extern vu32 vxy_api_prev;               /* vxyddb.asm */
void __cdecl vxy_api_entry(void);
vu32 __cdecl vmm_hook_pm_api(vu32 id, vu32 proc);    /* the previous; 0: failed */
static void sgdt_(void *p);
static vu32 sldt_(void);
#pragma aux sgdt_ = "sgdt [eax]" parm [eax] modify exact [];
#pragma aux sldt_ = "sldt ax" "movzx eax, ax" value [eax] modify exact [eax];
static vu32 api_n, api_dump = 0x20, api_depth, api_id[16];
#define NPTR 5
#define NLIN 7
#define NALL (NPTR + NLIN)
static vu8 api_save[16][NALL][256];
static vu32 api_addr[16][NALL];
static vu32 sc_sel[256], sc_d0[256], sc_d1[256], sc_n;

static int present(vu32 lin)
{
    vu32 *pd = (vu32 *)vmm_map_phys(get_cr3() & ~0xFFFu, 4096, 0), *pt;
    if (!pd || !(pd[lin >> 22] & 1)) return 0;
    pt = (vu32 *)vmm_map_phys(pd[lin >> 22] & ~0xFFFu, 4096, 0);
    return pt && (pt[(lin >> 12) & 0x3FF] & 1);
}

static vu32 d_base(const vu32 *d) { return d[0] >> 16 | (d[1] & 0xFF) << 16 | (d[1] & 0xFF000000u); }
static vu32 d_limit(const vu32 *d)
{
    vu32 l = (d[0] & 0xFFFF) | (d[1] & 0xF0000);
    return d[1] & 0x800000 ? l << 12 | 0xFFF : l;
}

#pragma pack(1)
struct dtr { vu16 lim; vu32 base; };
#pragma pack()

/* the descriptor of selector sel (GDT or the current LDT): 0 if none or
 * not present */
static int desc(vu32 sel, vu32 *d)
{
    struct dtr g;
    vu32 *t, lim;
    sgdt_(&g);
    if ((sel & ~3u) == 0) return 0;
    t = (vu32 *)g.base;
    lim = g.lim;
    if (sel & 4) {
        vu32 l = sldt_(), *ld;
        if (!l || (l | 7) > lim) return 0;
        ld = t + (l >> 3 << 1);
        t = (vu32 *)d_base(ld);
        lim = d_limit(ld);
    }
    if ((sel | 7) > lim || !present((vu32)&t[sel >> 3 << 1])) return 0;
    d[0] = t[sel >> 3 << 1];
    d[1] = t[(sel >> 3 << 1) + 1];
    return (d[1] & 0x8000) != 0;
}

/* a selector's descriptor, written when first seen or changed */
static void sel_note(vu32 sel)
{
    vu32 d[2], i;
    sel &= 0xFFFF;
    if (!desc(sel, d)) return;
    for (i = 0; i < sc_n; i++) if (sc_sel[i] == sel) break;
    if (i < sc_n && sc_d0[i] == d[0] && sc_d1[i] == d[1]) return;
    if (i == sc_n) { if (sc_n == 256) return; sc_n++; }
    sc_sel[i] = sel; sc_d0[i] = d[0]; sc_d1[i] = d[1];
    puts_("VXY sel "); hex(sel, 4);
    puts_(" base "); hex(d_base(d), 8);
    puts_(" limit "); hex(d_limit(d), 8);
    puts_(" attr "); hex(d[1] >> 8 & 0xF0FF, 4);
    puts_("\r\n");
}

/* the linear address of sel:off (off's low word when the whole one is
 * past the limit); 0 if none */
static vu32 ptr_lin(vu32 sel, vu32 off)
{
    vu32 d[2], l;
    if (!desc(sel & 0xFFFF, d)) return 0;
    if ((d[1] & 0x800) && !(d[1] & 0x200)) return 0;    /* code, not readable */
    l = d_limit(d);
    if (off > l) off &= 0xFFFF;
    if (off > l) return 0;
    return d_base(d) + off;
}

static int readable(vu32 lin, vu32 n)
{
    return lin && present(lin) && present(lin + n - 1);
}

static void dump(const char *what, vu32 lin, const vu8 *p, vu32 n)
{
    vu32 i;
    puts_("VXY  ");
    puts_(what);
    putc_(' ');
    hex(lin, 8);
    for (i = 0; i < n; i += 4) { putc_(' '); hex(*(const vu32 *)(p + i), 8); }
    puts_("\r\n");
}

static void cregs(const struct client_regs *c)
{
    puts_(" a "); hex(c->eax, 8);
    puts_(" b "); hex(c->ebx, 8);
    puts_(" c "); hex(c->ecx, 8);
    puts_(" d "); hex(c->edx, 8);
    puts_(" si "); hex(c->esi, 8);
    puts_(" di "); hex(c->edi, 8);
    puts_(" bp "); hex(c->ebp, 8);
    puts_(" sp "); hex(c->ss, 4); putc_(':'); hex(c->esp, 8);
    puts_(" cs "); hex(c->cs, 4); putc_(':'); hex(c->eip, 8);
    puts_(" ds "); hex(c->ds, 4);
    puts_(" es "); hex(c->es, 4);
    puts_(" fs "); hex(c->fs, 4);
    puts_(" gs "); hex(c->gs, 4);
    puts_(" f "); hex(c->eflags, 5);
    puts_("\r\n");
}

static const char *const pname[NALL] = {
    "ds:si", "es:di", "ds:bx", "es:bx", "ds:dx",
    "lin di", "lin si", "lin bp", "lin bx", "lin dx", "lin cx", "lin ax"
};
static void api_ptrs(const struct client_regs *c, vu32 *a)
{
    static const unsigned char ri[NLIN] = { 0, 1, 2, 4, 5, 6, 7 };   /* (not ESP) */
    const vu32 *rv = &c->edi;
    vu32 i;
    a[0] = ptr_lin(c->ds, c->esi);
    a[1] = ptr_lin(c->es, c->edi);
    a[2] = ptr_lin(c->ds, c->ebx);
    a[3] = ptr_lin(c->es, c->ebx);
    a[4] = ptr_lin(c->ds, c->edx);
    for (i = 0; i < NLIN; i++) {
        vu32 v = rv[ri[i]];
        a[NPTR + i] = (v >= 0x80000000u || (ri[i] == 5 && v >= 0x1000)) && v < 0xFFFFF000u ? v : 0;
    }
}

static void seg_notes(const struct client_regs *c)
{
    sel_note(c->cs); sel_note(c->ds); sel_note(c->es);
    sel_note(c->fs); sel_note(c->gs); sel_note(c->ss);
}

/* [386Enh] VXYApiPte=first directory entry (hex, e.g. 211): at each API
 * call and return, the page directory entries from there to 3FFh and
 * the page table entries under them are compared with a copy and every
 * change written ("VXY pde N old > new", "VXY pte LINEAR old > new"),
 * so that what a call (or a fault between calls) did to the page tables
 * shows. At most 48 tables are followed. */
#define NPT 48
static vu32 pt_first, pt_pde[1024], pt_slot[1024], pt_copy[NPT][1024], pt_used;

static void pte_scan(const char *when)
{
    vu32 *pd, i, j;
    int said = 0;
    if (!pt_first) return;
    pd = (vu32 *)vmm_map_phys(get_cr3() & ~0xFFFu, 4096, 0);
    if (!pd) return;
    for (i = pt_first; i < 1024; i++) {
        vu32 e = pd[i], *pt, s;
        if (e != pt_pde[i]) {
            if (!said++) { puts_("VXY ptes "); puts_(when); puts_("\r\n"); }
            puts_("VXY pde "); hex(i, 3); putc_(' ');
            hex(pt_pde[i], 8); puts_(" > "); hex(e, 8); puts_("\r\n");
            if (!(pt_pde[i] & 1) || (pt_pde[i] & ~0xFFFu) != (e & ~0xFFFu)) {
                /* a new table: from an empty copy */
                if (pt_slot[i] == 0 && pt_used < NPT) pt_slot[i] = ++pt_used;
                if (pt_slot[i]) for (j = 0; j < 1024; j++) pt_copy[pt_slot[i] - 1][j] = 0;
            }
            pt_pde[i] = e;
        }
        if (!(e & 1) || !(s = pt_slot[i])) continue;
        pt = (vu32 *)vmm_map_phys(e & ~0xFFFu, 4096, 0);
        if (!pt) continue;
        for (j = 0; j < 1024; j++) {
            if (pt[j] == pt_copy[s - 1][j]) continue;
            if (!said++) { puts_("VXY ptes "); puts_(when); puts_("\r\n"); }
            puts_("VXY pte "); hex(i << 22 | j << 12, 8); putc_(' ');
            hex(pt_copy[s - 1][j], 8); puts_(" > "); hex(pt[j], 8); puts_("\r\n");
            pt_copy[s - 1][j] = pt[j];
        }
    }
}

/* [386Enh] VXYCurTdb=SSSSOOOO (hex: a selector and offset where a word
 * holds the current task's TDB selector, KERNEL's THHOOK + 10h): at each
 * API call the TDB's first 100h bytes and those of its PDB (the selector
 * at TDB + 60h) are written when they changed ("VXY tdb", "VXY pdb") */
static vu32 cur_tdb_at;
static vu8 tdb_seen[0x200], pdb_seen[0x100];

static void dump_block(const char *what, vu32 lin, vu8 *seen, vu32 n)
{
    vu32 i, j;
    for (i = 0; i < n; i++) if (seen[i] != ((const vu8 *)lin)[i]) break;
    if (i == n) return;
    for (i = 0; i < n; i += 32) {
        puts_("VXY ");
        puts_(what);
        putc_(' ');
        hex(i, 3);
        for (j = 0; j < 32; j += 4) { putc_(' '); hex(*(const vu32 *)(lin + i + j), 8); }
        puts_("\r\n");
    }
    for (i = 0; i < n; i++) seen[i] = ((const vu8 *)lin)[i];
}

static void tdb_note(void)
{
    vu32 a, t, p;
    if (!cur_tdb_at) return;
    a = ptr_lin(cur_tdb_at >> 16, cur_tdb_at & 0xFFFF);
    if (!readable(a, 2)) return;
    t = ptr_lin(*(const vu16 *)a, 0);
    if (!readable(t, 0x200)) return;
    dump_block("tdb", t, tdb_seen, 0x200);
    {
        /* every task (from HeadTDB, THHOOK + 0Eh): its name and its PDB's
         * 40h-6Fh, once */
        static vu32 listed;
        vu32 h = ptr_lin(cur_tdb_at >> 16, (cur_tdb_at & 0xFFFF) - 2), n = 0, s;
        if ((api_n == 0 || api_n == 8) && readable(h, 2)) {
            listed = 1;
            {
                /* TopPDB (THHOOK + 08h, KERNEL's own PSP) */
                vu32 tp = ptr_lin(cur_tdb_at >> 16, (cur_tdb_at & 0xFFFF) - 8), pl, i;
                if (readable(tp, 2) && readable(pl = ptr_lin(*(const vu16 *)tp, 0), 0x70)) {
                    puts_("VXY toppdb ");
                    hex(*(const vu16 *)tp, 4);
                    for (i = 0x40; i < 0x70; i += 4) { putc_(' '); hex(*(const vu32 *)(pl + i), 8); }
                    puts_("\r\n");
                }
            }
            for (s = *(const vu16 *)h; s && n < 32; n++) {
                vu32 tl = ptr_lin(s, 0), pl, i;
                if (!readable(tl, 0x100)) break;
                puts_("VXY task ");
                hex(s, 4);
                putc_(' ');
                for (i = 0; i < 8 && ((const char *)tl)[0xF2 + i]; i++) putc_(((const char *)tl)[0xF2 + i]);
                pl = ptr_lin(*(const vu16 *)(tl + 0x60), 0);
                if (readable(pl, 0x70))
                    for (i = 0x40; i < 0x70; i += 4) { putc_(' '); hex(*(const vu32 *)(pl + i), 8); }
                puts_("\r\n");
                s = *(const vu16 *)tl;
            }
        }
    }
    p = ptr_lin(*(const vu16 *)(t + 0x60), 0);
    if (!readable(p, 0x100)) return;
    dump_block("pdb", p, pdb_seen, 0x100);
}

void __cdecl vxy_api_in(struct vregs *r)
{
    const struct client_regs *c = (const struct client_regs *)r->ebp;
    vu32 i, j, a[NALL], d = api_depth < 16 ? api_depth : 15, sl;
    t_check();
    pte_scan("before");
    tdb_note();
    api_id[d] = ++api_n;
    puts_("VXY api ");
    hex(api_n, 5);
    puts_(" in ");
    cregs(c);
    seg_notes(c);
    sl = ptr_lin(c->ss, c->esp);
    if (readable(sl, 16)) dump("stack", sl, (const vu8 *)sl, 16);
    api_ptrs(c, a);
    for (i = 0; i < NALL; i++) {
        api_addr[d][i] = 0;
        if (!readable(a[i], api_dump)) continue;
        for (j = 0; j < i; j++) if (api_addr[d][j] == a[i]) break;
        if (j < i) continue;                    /* (written already) */
        api_addr[d][i] = a[i];
        for (j = 0; j < api_dump; j++) api_save[d][i][j] = ((const vu8 *)a[i])[j];
        dump(pname[i], a[i], (const vu8 *)a[i], api_dump);
    }
    api_depth++;
}

void __cdecl vxy_api_out(struct vregs *r)
{
    const struct client_regs *c = (const struct client_regs *)r->ebp;
    const vu32 *rv = &c->edi;
    vu32 i, j, d, a2[NALL];
    if (api_depth) api_depth--;
    d = api_depth < 16 ? api_depth : 15;
    puts_("VXY api ");
    hex(api_id[d], 5);
    puts_(" out");
    cregs(c);
    pte_scan("after");
    seg_notes(c);
    for (i = 0; i < 8; i++) if (i != 3 && (rv[i] & 4)) sel_note(rv[i]);
    for (i = 0; i < NALL; i++) {
        vu32 a = api_addr[d][i];
        if (!a) continue;
        if (!readable(a, api_dump)) { puts_("VXY  gone "); hex(a, 8); puts_("\r\n"); continue; }
        for (j = 0; j < api_dump; j++) if (api_save[d][i][j] != ((const vu8 *)a)[j]) break;
        if (j < api_dump) dump(pname[i], a, (const vu8 *)a, api_dump);
    }
    /* where the registers point now, if not written above */
    api_ptrs(c, a2);
    for (i = 0; i < NALL; i++) {
        for (j = 0; j < NALL; j++) if (api_addr[d][j] == a2[i]) break;
        if (j < NALL || !readable(a2[i], api_dump)) continue;
        dump(pname[i], a2[i], (const vu8 *)a2[i], api_dump);
    }
}

/* [386Enh] VXYFaults=1: from Init_Complete on, VXY's handlers are put
 * in front of the protected-mode fault hooks (Hook_PM_Fault) of the
 * exceptions below; each fault is written with the client's registers
 * ("VXY fault NN", CR2 for 0Eh) and goes on to the handler before.
 * Not-present page faults (the VMM's demand paging) only with
 * VXYFaultsNP=1; faults before the first API call only with
 * VXYFaultsEarly=1.
 * At most VXYFaultMax lines (default 2000). */
static const vu32 fault_nums[8] = { 0x00, 0x01, 0x03, 0x05, 0x06, 0x0C, 0x0D, 0x0E };
extern vu32 vxy_fault_prev[8];          /* vxyddb.asm */
extern vu32 vxy_fault_stub[8];
vu32 __cdecl vmm_hook_pm_fault(vu32 n, vu32 proc);
static vu32 fault_n, fault_max, fault_np, fault_early, fault_dumpcs;

void __cdecl vxy_fault(struct vregs *r)
{
    const struct client_regs *c = (const struct client_regs *)r->ebp;
    vu32 sl;
    /* not-present page faults (demand paging) only with VXYFaultsNP=1 */
    if (fault_nums[r->arg & 7] == 0x0E && !(c->error & 1) && !fault_np) return;
    if (!api_n && !fault_early) return;         /* (from the first API call on) */
    if (fault_n++ >= fault_max) return;
    puts_("VXY fault ");
    hex(fault_nums[r->arg & 7], 2);
    puts_(" err ");
    hex(c->error, 8);
    if (fault_nums[r->arg & 7] == 0x0E) { puts_(" cr2 "); hex(get_cr2(), 8); }
    cregs(c);
    seg_notes(c);
    sl = ptr_lin(c->ss, c->esp);
    if (readable(sl, 32)) dump("stack", sl, (const vu8 *)sl, 32);
    if (fault_nums[r->arg & 7] == 0x0D && (c->error || c->cs == fault_dumpcs)) {
        static vu8 none[0x100];
        vu32 e = ptr_lin(c->es, 0), f = ptr_lin(c->fs, 0);
        vu32 i;
        for (i = 0; i < 0x100; i++) none[i] = 0xEE;
        if (readable(e, 0x100)) dump_block("es", e, none, 0x100);
        for (i = 0; i < 0x100; i++) none[i] = 0xEE;
        if (readable(f, 0x100)) dump_block("fs", f, none, 0x100);
        if (c->cs == fault_dumpcs) {
            vu32 d = ptr_lin(c->ds, 0);
            for (i = 0; i < 0x100; i++) none[i] = 0xEE;
            if (readable(d, 0x100)) dump_block("ds", d, none, 0x100);
        }
    }
}

static void fault_hook(void)
{
    vu32 i;
    fault_max = vmm_profile_hex(2000, "VXYFaultMax");
    fault_np = vmm_profile_hex(0, "VXYFaultsNP");
    fault_early = vmm_profile_hex(0, "VXYFaultsEarly");
    fault_dumpcs = vmm_profile_hex(0, "VXYFaultDumpCS");    /* ES, FS and DS blocks at its GP faults */
    for (i = 0; i < 8; i++) {
        vxy_fault_prev[i] = vmm_hook_pm_fault(fault_nums[i], vxy_fault_stub[i]);
        puts_("VXY fault hook ");
        hex(fault_nums[i], 2);
        puts_(" prev ");
        hex(vxy_fault_prev[i], 8);
        puts_("\r\n");
    }
}

static void api_hook(vu32 ddb)
{
    const struct vxd_ddb *d = (const struct vxd_ddb *)ddb;
    if (!d) return;
    api_dump = vmm_profile_hex(0x20, "VXYApiDump");
    if (api_dump > 256) api_dump = 256;
    api_dump &= ~3u;
    pt_first = vmm_profile_hex(0, "VXYApiPte") & 0x3FF;
    cur_tdb_at = vmm_profile_hex(0, "VXYCurTdb");
    pte_scan("at the start");
    if (!api_dump) api_dump = 4;
    vxy_api_prev = vmm_hook_pm_api(d->device_id, (vu32)vxy_api_entry);
    puts_("VXY api hook ");
    hex(d->device_id, 4);
    puts_(" prev ");
    hex(vxy_api_prev, 8);
    puts_("\r\n");
}

static void vxy_break(struct sframe *f)
{
    vu32 *sp = (vu32 *)((vu32)&f->eflags + 4), i;
    set_dr6(0);
    f->eflags |= 0x10000;               /* RF: on with the instruction */
    if (bp_hits++ >= 400) { set_dr7(0); vxy_bp = 0; return; }
    regline("bp ", f);
    puts_("VXY  stack");
    for (i = 0; i < 8; i++) { putc_(' '); hex(sp[i], 8); }
    puts_("\r\n");
}

static void vxy_step(struct sframe *f)
{
    vu32 sp = (vu32)&f->eflags + 4, *n;
    if (!target) {
        /* (the first step there is VXY's own RET) */
        if (sp == entry_sp && !own_ret) own_ret = f->eip;
        else if (sp == entry_sp && f->eip != own_ret) {
            target = f->eip;
            ret_addr = *(vu32 *)sp;
            regline("enter ", f);
        } else if (++waited > 4000000) {
            puts_("VXY no control procedure was entered\r\n");
            stop(f);
            return;
        }
        if (!target) return;
    }
    if (f->eip == ret_addr && sp > entry_sp) {
        regline("return ", f);
        stop(f);
        return;
    }
    if (f->eip - target + 0x10000 < 0x20000) {
        outside = 0;
        n = count_of(f->eip);
        if (n && *n < 16 && lines < 60000) {
            regline("", f);
            lines++;
        } else if (n && *n == 16) {
            puts_("VXY loop ");
            hex(f->eip, 8);
            puts_("\r\n");
        }
        if (n) (*n)++;
    } else if (!outside) {
        outside = 1;
        n = count_of(f->eip);
        if (n && *n < 16 && lines < 60000) {
            regline("-> ", f);
            lines++;
        }
        if (n) (*n)++;
    }
}

/* the debug exception from ring 0: 1 handled here, 0 the VMM's */
vu32 __cdecl vxy_int1(struct sframe *f)
{
    if (vxy_pf_armed) { vxy_pf_step(f); return 1; }
    if (vxy_stepping) { vxy_step(f); return 1; }
    if (vxy_bp && (get_dr6() & 1)) { vxy_break(f); return 1; }
    return 0;
}

void __cdecl vxy_control(struct vregs *r)
{
    if (r->eax == DEVICE_INIT) {
        /* COM1 at 115200 baud (at the BIOS's rate the log is what holds
         * Windows up) */
        outb(0x3FB, 0x80);
        outb(0x3F8, 1);
        outb(0x3F9, 0);
        outb(0x3FB, 3);
        /* ESP at the control procedure's entry: above PUSHAD, the flags,
         * the argument word and ENTRY's function */
        entry_sp = r->esp0 + 12;
        bp_addr = vmm_profile_hex(0, "VXYBreak");
        hook_at = vmm_profile_hex(0, "VXYHook");
        {
            static const char *wk[3] = { "VXYWatch", "VXYWatch2", "VXYWatch3" };
            static const char *lk[3] = { "VXYWatchLen", "VXYWatchLen2", "VXYWatchLen3" };
            vu32 k;
            for (k = 0; k < 3; k++) {
                vu32 lo = vmm_profile_hex(0, wk[k]), n = vmm_profile_hex(0, lk[k]);
                if (n) { w_lo[w_nr] = lo; w_hi[w_nr++] = lo + n; }
            }
        }
        w_quiet = vmm_profile_hex(0, "VXYQuiet");
        w_skiplo = vmm_profile_hex(0, "VXYSkipLo");     /* EIPs not written */
        w_skiphi = vmm_profile_hex(0, "VXYSkipHi");
        w_max = vmm_profile_hex(0x80000, "VXYWatchMax");
        vxy_pf_user = vmm_profile_hex(0, "VXYWatchUser");  /* V86 and ring-3 accesses too */
        w_sysvm = vmm_profile_hex(0, "VXYWatchSysVM");
        if (w_nr) vxy_hook();
        if (w_nr && vmm_profile_hex(0, "VXYWatchEarly")) watch_arm();   /* from Device_Init on */
        if (!vmm_profile_hex(1, "VXYStep")) {   /* [386Enh] VXYStep=0: the breakpoint only */
            if (bp_addr) vxy_hook();
            r->eflags &= ~1u;
            return;
        }
        puts_("VXY Device_Init at sp ");
        hex(entry_sp, 8);
        puts_("\r\n");
        vxy_hook();
        vxy_stepping = 1;
        r->eflags |= 0x100;             /* from the POPFD on the way out */
    }
    if (r->eax == INIT_COMPLETE) {
        /* the devices after VXY (the DDB chain: W32S.386's first) */
        struct vxd_ddb *d = (struct vxd_ddb *)VXY_DDB.next;
        vu32 w32s_ctl = d ? d->control : 0, k;
        for (; d; d = (struct vxd_ddb *)d->next) {
            puts_("VXY ddb ");
            hex((vu32)d, 8);
            putc_(' ');
            for (k = 0; k < 8; k++) putc_(d->name[k]);
            puts_(" control ");
            hex(d->control, 8);
            puts_("\r\n");
        }
        if (hook_at) {
            puts_("VXY at ");
            hex(hook_at, 8);
            puts_(" ");
            hex(*(vu32 *)hook_at, 8);
            puts_("\r\n");
        }
        if ((k = vmm_profile_hex(0, "VXYSvc")) != 0) {
            svc_lo = vmm_profile_hex(w32s_ctl - 0x4000, "VXYSvcLo");
            svc_hi = vmm_profile_hex(w32s_ctl + 0x4000, "VXYSvcHi");
            svc_hook_all(k == 2);
        }
        if (vmm_profile_hex(0, "VXYApi")) api_hook(VXY_DDB.next);
        if (vmm_profile_hex(0, "VXYFaults")) fault_hook();
    }
    /* VXYWatchSysVM=1: armed at Sys_VM_Init instead, after the VMM and V86MMGR
     * have mapped the System VM's memory (its first megabyte) */
    if (r->eax == (w_sysvm ? SYS_VM_INIT : INIT_COMPLETE) && w_nr && !vxy_pf_on) watch_arm();
    if (r->eax == INIT_COMPLETE && hook_at) {
        /* [386Enh] VXYHook=address: a procedure pointer there (one the
         * stepped device put in) now leads to vxy_thunk, which writes the
         * call (vxy_called) and goes on to it */
        vxy_next = *(vu32 *)hook_at;
        *(vu32 *)hook_at = (vu32)vxy_thunk;
        t_last = (vu32)vxy_thunk;
        puts_("VXY hooked ");
        hex(hook_at, 8);
        puts_(" ");
        hex(vxy_next, 8);
        puts_("\r\n");
    }
    if (r->eax == INIT_COMPLETE && (vxy_stepping || (bp_addr && !vxy_bp))) {
        /* (the stepped procedure's return was not seen) */
        puts_("VXY Init_Complete\r\n");
        vxy_stepping = 0;
        r->eflags &= ~0x100u;
        if (bp_addr) {
            set_dr0(bp_addr);
            set_dr6(0);
            set_dr7(0x203);
            vxy_bp = 1;
        }
    }
    r->eflags &= ~1u;
}
