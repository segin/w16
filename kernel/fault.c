/*
 * fault.c - while booting Windows, report a processor exception (which
 * module and segment faulted, and the registers) and leave to DOS,
 * instead of DOSX's "Fault outside of MS-DOS Extender". A debugging aid
 * for the drop-in boot (tests/dropin.sh); Windows' own KERNEL hooks these
 * exceptions too (Pietrek, ch. 1), to handle them, which w16 does not
 * do yet.
 */
#include "kernel.h"

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

/* " MODULE:n" for the module segment sel belongs to */
static void where(u16 sel)
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

extern u32 __cdecl wd_orig8, wd_next8;
extern u16 __cdecl wd_ticks, wd_limit, wd_pos;
extern u16 __cdecl wd_samples[128];
extern void __far wd_int08(void);
#pragma aux wd_int08 "*"

static void print_int21_ring(u16 pos);

void __cdecl fault_report(void)
{
    u16 *f = fault_regs, pos = i21_pos;
    kmemcpy(ring, i21_ring, sizeof ring);
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
   0 or none: off). While Windows boots, SECONDS without progress (a boot
   log line, a module loaded) make KERNEL report where the processor has
   been (timer samples) and leave to DOS. */
void wd_start(u16 seconds)
{
    regs16 r;
    if (!seconds)
        return;
    r.ax = 0x0204;
    r.bx = 0x08;
    r.ds = r.es = 0;
    dpmi31(&r);
    wd_orig8 = wd_next8 = ((u32)r.cx << 16) | r.dx;
    wd_ticks = 0;
    wd_limit = seconds * 18 + (seconds >> 2) - (seconds >> 4);  /* x 18.2 */
    r.ax = 0x0205;
    r.bx = 0x08;
    r.cx = FP_SEG(wd_int08);
    r.dx = FP_OFF(wd_int08);
    r.ds = r.es = 0;
    dpmi31(&r);
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
    kexit_to_dos(0xFE);
}
