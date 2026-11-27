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
extern vu32 vxy_pf_on, vxy_pf_armed;   /* vxyddb.asm */
#define NWP 512
static vu32 w_lo[3], w_hi[3], w_nr, w_quiet, w_max, w_logged, w_skiplo, w_skiphi;
static vu32 *w_pte[NWP], w_page[NWP], w_armed[NWP], w_np;
static vu32 *w_pd;
static vu32 s_addr, s_before, s_eip, s_err, s_r[6];

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
extern vu32 vxy_svc_prev[16];           /* vxyddb.asm */
extern vu32 vxy_svc_stub[16];
extern vu32 vxy_svc_after_addr;
vu32 __cdecl vmm_hook_service(vu32 id, vu32 proc);   /* the previous; 0: failed */
static vu32 svc_lo, svc_hi;
struct svcregs {
    vu32 edi, esi, ebp, esp0, ebx, edx, ecx, eax, eflags, idx, ret, args[6];
};
static vu32 sv_slot[64], sv_ret[64], sv_idx[64], sv_n;

vu32 __cdecl vxy_svc_called(struct svcregs *r)
{
    vu32 i;
    t_check();
    if (r->ret < svc_lo || r->ret >= svc_hi || sv_n == 64) return 0;
    puts_("VXY svc ");
    hex(svc_ids[r->idx] & 0xFFFF, 4);
    puts_(" from ");
    hex(r->ret, 8);
    regs7((struct vregs *)r);
    puts_(" args");
    for (i = 0; i < 6; i++) { putc_(' '); hex(r->args[i], 8); }
    puts_("\r\n");
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
    hex(svc_ids[sv_idx[n]] & 0xFFFF, 4);
    regs7(r);
    puts_("\r\n");
    sv_n = n;
    return ret;
}

static void svc_hook_all(void)
{
    vu32 i;
    for (i = 0; i < NSVC; i++) {
        vxy_svc_prev[i] = vmm_hook_service(svc_ids[i], vxy_svc_stub[i]);
        if (!vxy_svc_prev[i]) { puts_("VXY hook failed "); hex(svc_ids[i], 8); puts_("\r\n"); }
    }
    puts_("VXY services hooked from ");
    hex(svc_lo, 8);
    puts_(" to ");
    hex(svc_hi, 8);
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
        vu32 w32s_ctl = d ? d->control : 0;
        for (; d; d = (struct vxd_ddb *)d->next) {
            vu32 k;
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
        if (vmm_profile_hex(0, "VXYSvc")) {
            svc_lo = vmm_profile_hex(w32s_ctl - 0x4000, "VXYSvcLo");
            svc_hi = vmm_profile_hex(w32s_ctl + 0x4000, "VXYSvcHi");
            svc_hook_all();
        }
    }
    if (r->eax == INIT_COMPLETE && w_nr && !vxy_pf_on) watch_arm();
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
