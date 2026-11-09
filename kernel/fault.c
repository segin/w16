/*
 * fault.c - while booting Windows, report a processor exception (which
 * module and segment faulted, and the registers) and leave to DOS,
 * instead of DOSX's "Fault outside of MS-DOS Extender". A debugging aid
 * for the drop-in boot (tests/dropin.sh); Windows' own KERNEL hooks these
 * exceptions too (Pietrek, ch. 1), to handle them. w16 handles one kind:
 * a fault inside a module's "__GP" range resumes at its recovery code
 * (gp_handler below); the rest are reported.
 */
#include "kernel.h"

u32 KAPI GetProcAddress(u16 h, const char FAR *name);

extern u16 __cdecl fault_regs[16];
extern u16 __cdecl i21_ring[80];
extern u16 __cdecl i21_pos;
extern void __far fault_entry_00h(void);
extern void __far fault_entry_06h(void);
extern void __far fault_entry_0Bh(void);
extern void __far fault_entry_0Ch(void);
extern void __far fault_entry_0Dh(void);
extern void __far fault_entry_0Eh(void);
#pragma aux fault_entry_00h "*"
#pragma aux fault_entry_06h "*"
#pragma aux fault_entry_0Bh "*"
#pragma aux fault_entry_0Ch "*"
#pragma aux fault_entry_0Dh "*"
#pragma aux fault_entry_0Eh "*"

/* Back to text mode before a report goes to the screen, as Windows'
   own KERNEL does before "Error loading ..." (Pietrek, TextMode: USER's
   DisableOEMLayer if it can, else INT 10h AX=0003h). w16 uses INT 10h
   (in real mode, through DPMI 0300h): KERNEL leaves to DOS right after,
   so the display driver's state is not needed any more. */
void text_mode(void)
{
    static struct {
        u32 edi, esi, ebp, res, ebx, edx, ecx, eax;
        u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
    } rmc;
    regs16 r;
    if (!winboot_active || !module_find("DISPLAY"))
        return;
    kmemset(&rmc, 0, sizeof rmc);
    rmc.eax = 0x0003;
    r.ax = 0x0300;
    r.bx = 0x10;
    r.cx = 0;
    r.di = FP_OFF((void FAR *)&rmc);
    r.es = FP_SEG((void FAR *)&rmc);
    r.ds = 0;
    dpmi31(&r);
}

static void hook(u8 n, void (__far *h)(void))
{
    regs16 r;
    r.ax = 0x0203;
    r.bx = n;
    r.cx = FP_SEG(h);
    r.dx = FP_OFF(h);
    r.ds = r.es = 0;
    dpmi31(&r);
}

void fault_init(void)
{
    hook(0x00, fault_entry_00h);
    hook(0x06, fault_entry_06h);
    hook(0x0B, fault_entry_0Bh);
    hook(0x0C, fault_entry_0Ch);
    hook(0x0D, fault_entry_0Dh);
    hook(0x0E, fault_entry_0Eh);
}

static u16 lsl(u16 sel);
#pragma aux lsl = \
    "xor ax, ax" \
    "lsl ax, dx" \
    parm [dx] value [ax] modify exact [ax];

/* ---- "__GP" tables (Schulman, Undocumented Windows ch. 5, _GP and
   HasGPHandler): a module that validates parameters by letting a load
   fault exports "__GP", a table of { code selector, low offset, high
   offset, recovery offset } ending with a 0 selector; a fault at an
   offset from low up to (not including) high resumes at the recovery
   offset in the same segment. TK38 (3.1): KERNEL (35 entries, as
   KERNEL.342), USER (121), GDI (69) have one, DISPLAY none;
   HasGPHandler(seg:low ... seg:high-1) returns the recovery offset,
   seg:high and seg:low-1 give 0. TK40 (3.1, a stand-in module named
   PENWIN whose resident "__GP" covers a faulting load): the recovery
   code gets the fault's registers, DS, ES and SS, SP 4 lower with the
   faulting IP and then the error code pushed, flags 3202h
   (kentry.asm, fault_exit). Only the resident name is looked at. */
static u16 gp_find(u16 cs, u16 ip)
{
    module FAR *m;
    u16 i;
    u32 t;
    const u16 FAR *e;
    for (m = module_first; m; m = m->next)
        for (i = 0; i < m->seg_count; i++)
            if (m->seg[i].sel && (m->seg[i].sel | 7) == (cs | 7))
                goto found;
    return 0;
found:
    t = GetProcAddress(m->ne_sel, "__GP");
    if (!t)
        return 0;
    for (e = (const u16 FAR *)t; e[0] && lsl(FP_SEG(e)) >= FP_OFF(e) + 7; e += 4)
        if ((e[0] | 7) == (cs | 7) && ip >= e[1] && ip < e[2])
            return e[3];
    return 0;
}

/* KERNEL's own "__GP" (KERNEL.342): empty. 3.1's KERNEL lets its
   string and validation functions fault and recovers through its table
   (35 entries); w16's check pointers before using them (string.c,
   kmisc.c), so it has no ranges to list. */
u16 gp_table[4];
#pragma aux gp_table "__GP"

/* KERNEL.338 */
u16 KAPI HasGPHandler(u32 addr)
{
    return gp_find((u16)(addr >> 16), (u16)addr);
}

static u16 seg_ok(u16 sel);
#pragma aux seg_ok = \
    "xor ax, ax" \
    "verr dx" \
    "jnz no" \
    "inc ax" \
    "no:" \
    parm [dx] value [ax] modify exact [ax];

/* called by fault_exit (kentry.asm): the recovery offset for a GP,
   stack or not-present fault in a "__GP" range, else 0 */
u16 __cdecl __far gp_handler(void)
{
    u16 *f = fault_regs, r;
    if (f[0] != 0x0B && f[0] != 0x0C && f[0] != 0x0D)
        return 0;
    r = gp_find(f[3], f[2]);
    if (r) {
        if (f[14] && !seg_ok(f[14]))
            f[14] = 0;
        if (f[15] && !seg_ok(f[15]))
            f[15] = 0;
    }
    return r;
}

/* " MODULE:n" for the module segment sel belongs to */
void where(u16 sel)
{
    module FAR *m;
    u16 i;
    for (m = module_first; m; m = m->next)
        for (i = 0; i < m->seg_count; i++)
            if (m->seg[i].sel && (m->seg[i].sel | 7) == (sel | 7)) {
                kputs(" (");
                kputs(m->name);
                kputs(":");
                kputdec(i + 1);
                kputs(" base ");
                {
                    u32 b = dpmi_get_base(sel);
                    kputhex((u16)(b >> 16));
                    kputhex((u16)b);
                }
                kputs(" limit ");
                kputhex(lsl(sel));
                kputs(")");
                return;
            }
    for (m = module_first; m; m = m->next)
        if ((m->ne_sel | 7) == (sel | 7)) {
            kputs(" (module table of ");
            kputs(m->name);
            kputs(")");
            return;
        }
    if (sel) {
        u32 b = dpmi_get_base(sel);
        kputs(" (base ");
        kputhex((u16)(b >> 16));
        kputhex((u16)b);
        kputs(" limit ");
        kputhex(lsl(sel));
        kputs(")");
    }
}

static void reg(const char *name, u16 v)
{
    kputs(" ");
    kputs(name);
    kputs("=");
    kputhex(v);
}

static u16 ring[80];

extern u16 __cdecl wd_ticks, wd_limit, wd_pos;
extern u16 __cdecl wd_samples[128];
extern void __far wd_int70(void);
#pragma aux wd_int70 "*"

static void print_int21_ring(u16 pos);

void __cdecl fault_report(void)
{
    u16 *f = fault_regs, pos = i21_pos;
    kmemcpy(ring, i21_ring, sizeof ring);
    text_mode();
    kputs("w16: exception ");
    kputhex(f[0]);
    kputs(" at ");
    kputhex(f[3]);
    kputs(":");
    kputhex(f[2]);
    where(f[3]);
    reg("err", f[1]);
    kputs("\r\n");
    reg("AX", f[7]);
    reg("BX", f[8]);
    reg("CX", f[9]);
    reg("DX", f[10]);
    reg("SI", f[11]);
    reg("DI", f[12]);
    reg("BP", f[13]);
    reg("FL", f[4]);
    kputs("\r\n");
    reg("DS", f[14]);
    where(f[14]);
    reg("ES", f[15]);
    where(f[15]);
    reg("SS", f[6]);
    where(f[6]);
    reg("SP", f[5]);
    kputs("\r\n");
    print_int21_ring(pos);
    trace_print();
    kexit_to_dos(0xFF);
}

static void print_int21_ring(u16 pos)
{
    {
        u16 i, k = pos;
        kputs("last INT 21h calls (oldest first):\r\n");
        for (i = 0; i < 16; i++) {
            k = k + 10 >= 160 ? 0 : k + 10;
            if (!ring[k / 2 + 4])
                continue;
            reg("AX", ring[k / 2]);
            reg("BX", ring[k / 2 + 1]);
            reg("DX", ring[k / 2 + 2]);
            kputs(" from ");
            kputhex(ring[k / 2 + 4]);
            kputs(":");
            kputhex(ring[k / 2 + 3]);
            where(ring[k / 2 + 4]);
            kputs("\r\n");
        }
    }
}

/* The boot watchdog: SYSTEM.INI [w16] watchdog=SECONDS (w16's own key;
   0 or none: off). While Windows runs, SECONDS without progress (a boot
   log line, a module loaded) make KERNEL report where the processor has
   been (timer samples) and leave to DOS. */
static u8 inb(u16 port);
#pragma aux inb = "in al, dx" parm [dx] value [al];
static void outb(u16 port, u8 v);
#pragma aux outb = "out dx, al" parm [dx] [al];

void wd_start(u16 seconds)
{
    regs16 r;
    if (!seconds)
        return;
    wd_ticks = 0;
    wd_limit = seconds * 16;
    r.ax = 0x0205;
    r.bx = 0x70;
    r.cx = FP_SEG(wd_int70);
    r.dx = FP_OFF(wd_int70);
    r.ds = r.es = 0;
    dpmi31(&r);
    /* RTC: 16 periodic interrupts a second (rate 12), enabled; IRQ 8 and
       the cascade unmasked */
    outb(0x70, 0x0A);
    outb(0x71, (u8)((inb(0x71) & 0xF0) | 0x0C));
    outb(0x70, 0x0B);
    outb(0x71, (u8)(inb(0x71) | 0x40));
    outb(0x70, 0x0C);
    inb(0x71);
    outb(0xA1, (u8)(inb(0xA1) & ~0x01));
    outb(0x21, (u8)(inb(0x21) & ~0x04));
}

void wd_progress(void)
{
    wd_ticks = 0;
}

void wd_stop(void)
{
    wd_limit = 0;
}

void __cdecl wd_report(void)
{
    static u16 s[128];
    u16 i, j, pos = wd_pos, ipos = i21_pos;
    kmemcpy(ring, i21_ring, sizeof ring);
    kmemcpy(s, wd_samples, sizeof s);
    text_mode();
    kputs("w16: no boot progress; timer samples (CS:IP, most recent last):\r\n");
    for (i = 0; i < 64; i++) {
        j = ((pos + 4 + i * 4) & 255) / 2;
        if (!s[j + 1])
            continue;
        kputhex(s[j + 1]);
        kputs(":");
        kputhex(s[j]);
        where(s[j + 1]);
        kputs("\r\n");
    }
    print_int21_ring(ipos);
    trace_print();
    kexit_to_dos(0xFE);
}

/* K327 (kentry.asm): note the first parameter errors on standard output */
void __cdecl k327_note(u16 err, u16 ip, u16 cs)
{
    static u16 count;
    if (++count > 8)
        return;
    kputs("w16: parameter error ");
    kputhex(err);
    kputs(" at ");
    kputhex(cs);
    kputs(":");
    kputhex(ip);
    where(cs);
    kputs("\r\n");
}

/* INT 21h AH=4Ch with no current task (kentry.asm): the KERNEL process
   itself ends. While booting Windows, say from where (the stack holds
   AX, DS, then the INT's IRET frame: IP, CS, FLAGS). */
void __cdecl dos_exit_note(u16 FAR *sp)
{
    if (!winboot_active)
        return;
    kputs("w16: INT 21h AH=4Ch with no task, from ");
    kputhex(sp[3]);
    kputs(":");
    kputhex(sp[2]);
    where(sp[3]);
    kputs(" AX=");
    kputhex(sp[0]);
    kputs("\r\n");
    trace_print();
}
