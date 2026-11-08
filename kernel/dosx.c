/*
 * dosx.c - running on the DPMI host of Windows 3.1 Standard mode (DOSX).
 *
 * Observed with tests/dos/tk18.asm (TK18 in place of SYSTEM\KRNL286.EXE,
 * ARCHITECTURE.md 9.4): DOSX is a DPMI 0.90 host for 16-bit clients, but
 * its INT 31h 0100h (allocate DOS memory) fails with AX=0008h and BX=0
 * even with 530 KB of DOS memory free, and INT 21h AH=48h from protected
 * mode returns a selector to extended memory. Windows' own KERNEL does
 * not need them: it manages DOS memory itself (Pietrek, ch. 1-2). w16's
 * KERNEL does use 0100h-0102h (GlobalDOSAlloc, TDBs), so when the host
 * refuses 0100h at boot they are done here instead: real-mode INT 21h
 * 48h/49h/4Ah through INT 31h 0300h (which DOSX supports), plus
 * descriptors from 0000h/0007h-0009h. Everything else goes to the host.
 *
 * Source for the DPMI functions' interface: DPMI Specification 0.9.
 */
#include "kernel.h"

extern int __cdecl dpmi31_host(regs16 FAR *r);  /* kentry.asm: INT 31h */

static int emulate;             /* the host has no working 0100h */
static u16 scratch;             /* a data selector for arena headers */

static struct {
    u32 edi, esi, ebp, res, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} rmc;

/* Real-mode INT 21h with AX, BX, ES; returns the carry flag, AX and BX
   in *ax, *bx. */
static int rm21(u16 ax, u16 bx, u16 es, u16 *oax, u16 *obx)
{
    regs16 r;
    kmemset(&rmc, 0, sizeof rmc);
    rmc.eax = ax;
    rmc.ebx = bx;
    rmc.es = es;
    r.ax = 0x0300;
    r.bx = 0x21;
    r.cx = 0;
    r.di = FP_OFF((void FAR *)&rmc);
    r.es = FP_SEG((void FAR *)&rmc);
    r.ds = 0;
    r.dx = r.si = 0;
    if (dpmi31_host(&r)) {
        *oax = 8;
        *obx = 0;
        return 1;
    }
    *oax = (u16)rmc.eax;
    *obx = (u16)rmc.ebx;
    return rmc.flags & FLAG_CARRY;
}

static u16 tiles(u32 paras)
{
    return (u16)((paras + 0xFFFUL) >> 12);
}

/* descriptors for paras paragraphs at real-mode segment seg: one per
   64 KB, each covering what is left of the block (at most 64 KB) */
static void set_tiles(u16 sel, u16 seg, u32 paras)
{
    u16 i, n = tiles(paras);
    for (i = 0; i < n; i++) {
        u32 left = (paras - ((u32)i << 12)) << 4;
        dpmi_set_desc(sel + i * sel_inc, ((u32)seg << 4) + ((u32)i << 16),
                      (left > 0x10000UL ? 0x10000UL : left) - 1, 0);
    }
}

/* the size of DOS block seg, from its arena header */
static u16 block_paras(u16 seg)
{
    if (!scratch || !dpmi_set_desc(scratch, (u32)(seg - 1) << 4, 15, 0))
        return 0;
    return *(u16 FAR *)MK_FP(scratch, 3);
}

static int dos_alloc(regs16 FAR *r)
{
    u16 seg, max, sel, n = tiles(r->bx ? r->bx : 1);
    if (rm21(0x4800, r->bx, 0, &seg, &max)) {
        r->ax = seg;
        r->bx = max;
        return 1;
    }
    sel = dpmi_alloc_desc(n);
    if (!sel) {
        rm21(0x4900, 0, seg, &max, &max);
        r->ax = 8;
        r->bx = 0;
        return 1;
    }
    set_tiles(sel, seg, r->bx);
    r->ax = seg;
    r->dx = sel;
    return 0;
}

static int dos_free(regs16 FAR *r)
{
    u16 sel = r->dx, seg = (u16)(dpmi_get_base(sel) >> 4);
    u16 i, n = tiles(block_paras(seg)), ax, bx;
    if (rm21(0x4900, 0, seg, &ax, &bx)) {
        r->ax = ax;
        return 1;
    }
    for (i = 0; i < n; i++)
        dpmi_free_desc(sel + i * sel_inc);
    return 0;
}

static int dos_resize(regs16 FAR *r)
{
    u16 sel = r->dx, seg = (u16)(dpmi_get_base(sel) >> 4), ax, bx;
    if (tiles(r->bx) != tiles(block_paras(seg))) {
        r->ax = 8;              /* would need other descriptors */
        r->bx = 0;
        return 1;
    }
    if (rm21(0x4A00, r->bx, seg, &ax, &bx)) {
        r->ax = ax;
        r->bx = bx;
        return 1;
    }
    set_tiles(sel, seg, r->bx);
    return 0;
}

/* INT 31h for KERNEL's C code (kernel.h): returns the carry flag. */
int __cdecl dpmi31(regs16 FAR *r)
{
    if (emulate) {
        switch (r->ax) {
        case 0x0100: return dos_alloc(r);
        case 0x0101: return dos_free(r);
        case 0x0102: return dos_resize(r);
        }
    }
    return dpmi31_host(r);
}

/* At boot: does the host's 0100h work? */
void dosx_init(void)
{
    regs16 r;
    r.ax = 0x0100;
    r.bx = 1;
    r.ds = r.es = 0;
    if (dpmi31_host(&r)) {
        emulate = 1;
        scratch = dpmi_alloc_desc(1);
        return;
    }
    r.ax = 0x0101;
    r.ds = r.es = 0;
    dpmi31_host(&r);            /* DX = the selector it gave */
}
