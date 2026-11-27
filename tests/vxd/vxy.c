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

/* the debug exception's frame: PUSHAD, then the CPU's (same ring) */
struct sframe {
    vu32 edi, esi, ebp, esp0, ebx, edx, ecx, eax;
    vu32 eip, cs, eflags;
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

vu32 __cdecl vxy_called(struct vregs *r)
{
    vu32 *sp = (vu32 *)(r->esp0 + 4), i, lg;
    hook_hits++;
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

/* [386Enh] VXYWatch=address, VXYWatchLen=bytes (hex): from Init_Complete
 * on, those pages are not present (their page table entries reached
 * through _MapPhysToLinear of CR3, as VXW does); every access to them
 * faults into vxy_pf, which writes it (EIP, the offset in the area, read
 * or write, the dword there) when EIP is not below VXYQuiet (the VMM's own
 * code is left out), the first 6 times for each EIP, offset in a 16-byte record and
 * read or write, and lets
 * it through for one instruction */
static vu32 get_cr3(void);
static vu32 get_cr2(void);
static void set_cr3(vu32 v);
#pragma aux get_cr3 = "mov eax, cr3" value [eax] modify exact [eax];
#pragma aux get_cr2 = "mov eax, cr2" value [eax] modify exact [eax];
#pragma aux set_cr3 = "mov cr3, eax" parm [eax] modify exact [];
vu32 __cdecl vmm_map_phys(vu32 phys, vu32 n, vu32 flags);
extern vu32 vxy_pf_on, vxy_pf_armed;   /* vxyddb.asm */
static vu32 w_lin, w_len, w_quiet, *w_pte[256], w_np, w_logged;
#define NWH 2048
static vu32 wkey[NWH], wcount[NWH];

static void watch_arm(void)
{
    vu32 *pd = (vu32 *)vmm_map_phys(get_cr3() & ~0xFFFu, 4096, 0), a;
    for (a = w_lin & ~0xFFFu; a < w_lin + w_len && w_np < 256; a += 4096) {
        vu32 *pt = (vu32 *)vmm_map_phys(pd[a >> 22] & ~0xFFFu, 4096, 0);
        w_pte[w_np++] = pt + ((a >> 12) & 0x3FF);
    }
    puts_("VXY watching ");
    hex(w_lin, 8);
    puts_(" pages ");
    hex(w_np, 4);
    puts_("\r\n");
    for (a = 0; a < w_np; a++) *w_pte[a] &= ~1u;
    set_cr3(get_cr3());
    vxy_pf_on = 1;
}

/* the page fault: 1 ours (made present, TF set), 0 not */
vu32 __cdecl vxy_pf(struct sframe *f, vu32 err)
{
    vu32 a = get_cr2(), i, h, n;
    if (a < (w_lin & ~0xFFFu) || a >= w_lin + w_len) return 0;
    for (i = 0; i < w_np; i++) *w_pte[i] |= 1;
    set_cr3(get_cr3());
    if (f->eip >= w_quiet && w_logged < 6000) {
        vu32 key = f->eip ^ (((a - w_lin) & 0xF) << 28) ^ (err & 2) << 26;   /* (16-byte records) */
        h = (key * 2654435761u) >> 21;
        for (n = 0; n < NWH; n++, h = (h + 1) & (NWH - 1)) {
            if (wkey[h] == key) break;
            if (!wkey[h]) { wkey[h] = key; wcount[h] = 0; break; }
        }
        if (n < NWH && wcount[h]++ < 6) {
            w_logged++;
            puts_("VXY pf ");
            hex(f->eip, 8);
            puts_(err & 2 ? " w " : " r ");
            hex(a - w_lin, 6);
            puts_(" = ");
            hex(*(vu32 *)(a & ~3u), 8);
            puts_(" a "); hex(f->eax, 8);
            puts_(" b "); hex(f->ebx, 8);
            puts_(" c "); hex(f->ecx, 8);
            puts_(" d "); hex(f->edx, 8);
            puts_(" si "); hex(f->esi, 8);
            puts_(" di "); hex(f->edi, 8);
            puts_("\r\n");
        }
    }
    f->eflags |= 0x100;
    vxy_pf_armed = 1;
    return 1;
}

/* the instruction after: not present again */
void __cdecl vxy_pf_step(struct sframe *f)
{
    vu32 i;
    for (i = 0; i < w_np; i++) *w_pte[i] &= ~1u;
    set_cr3(get_cr3());
    f->eflags &= ~0x100u;
    vxy_pf_armed = 0;
}

void __cdecl vxy_break(struct sframe *f)
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

void __cdecl vxy_step(struct sframe *f)
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

void __cdecl vxy_control(struct vregs *r)
{
    if (r->eax == DEVICE_INIT) {
        /* ESP at the control procedure's entry: above PUSHAD, the flags,
         * the argument word and ENTRY's function */
        entry_sp = r->esp0 + 12;
        bp_addr = vmm_profile_hex(0, "VXYBreak");
        hook_at = vmm_profile_hex(0, "VXYHook");
        w_lin = vmm_profile_hex(0, "VXYWatch");
        w_len = vmm_profile_hex(0, "VXYWatchLen");
        w_quiet = vmm_profile_hex(0, "VXYQuiet");
        if (w_len) vxy_hook();
        if (!vmm_profile_hex(1, "VXYStep")) {   /* [386Enh] VXYStep=0: the breakpoint only */
            vxy_hook();
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
    if (r->eax == INIT_COMPLETE && w_len) watch_arm();
    if (r->eax == INIT_COMPLETE && hook_at) {
        /* [386Enh] VXYHook=address: a procedure pointer there (one the
         * stepped device put in) now leads to vxy_thunk, which writes the
         * call (vxy_called) and goes on to it */
        vxy_next = *(vu32 *)hook_at;
        *(vu32 *)hook_at = (vu32)vxy_thunk;
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
