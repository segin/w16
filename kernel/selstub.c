/*
 * selstub.c - minimal selector services, ONLY so that Microsoft's drivers,
 * GDI and USER can boot on w16's KERNEL (kernel/winboot.c).
 *
 * TEMPORARY: the selector services (AllocSelector, PrestoChangoSelector,
 * the alias functions, Set/GetSelectorBase/Limit, GlobalDOSAlloc, ...)
 * are being implemented properly, from observations on Windows 3.1
 * (probe TK17), elsewhere; when that work is merged, this file and its
 * block in kernel.lnk go. Nothing here has been checked against Windows
 * 3.1 beyond the SDK's descriptions (Windows 3.1 SDK, Programmer's
 * Reference vol. 2): UNVERIFIED throughout, e.g. what each returns for a
 * bad selector, and the descriptor bits it copies.
 *
 * Descriptors are copied whole with DPMI 000Bh/000Ch, which DOSX and
 * HDPMI16 both support (TK18).
 */
#include "kernel.h"

typedef struct { u8 b[8]; } desc8;

static int get_desc(u16 sel, desc8 *d)
{
    regs16 r;
    r.ax = 0x000B;
    r.bx = sel;
    r.di = FP_OFF((void FAR *)d);
    r.es = FP_SEG((void FAR *)d);
    r.ds = 0;
    return !dpmi31(&r);
}

static int set_desc(u16 sel, desc8 *d)
{
    regs16 r;
    d->b[5] = (u8)((d->b[5] & ~0x60) | (cpl << 5) | 0x10);
    r.ax = 0x000C;
    r.bx = sel;
    r.di = FP_OFF((void FAR *)d);
    r.es = FP_SEG((void FAR *)d);
    r.ds = 0;
    return !dpmi31(&r);
}

/* a new selector like sel, made code (X/R) or data (R/W) */
static u16 alias(u16 sel, int code)
{
    desc8 d;
    u16 n;
    if (!get_desc(sel, &d))
        return 0;
    n = dpmi_alloc_desc(1);
    if (!n)
        return 0;
    d.b[5] = (u8)((d.b[5] & 0xF0) | (code ? 0x0A : 0x02));
    if (!set_desc(n, &d)) {
        dpmi_free_desc(n);
        return 0;
    }
    return n;
}

u16 KAPI AllocCStoDSAlias(u16 sel)                            /* KERNEL.170 */
{
    return alias(sel, 0);
}

u16 KAPI AllocDStoCSAlias(u16 sel)                            /* KERNEL.171 */
{
    return alias(sel, 1);
}

/* AllocSelector(0): a new, empty selector; else a copy of sel */
u16 KAPI AllocSelector(u16 sel)                               /* KERNEL.175 */
{
    desc8 d;
    u16 n;
    if (!sel)
        return dpmi_alloc_desc(1);
    if (!get_desc(sel, &d))
        return 0;
    n = dpmi_alloc_desc(1);
    if (n && !set_desc(n, &d)) {
        dpmi_free_desc(n);
        n = 0;
    }
    return n;
}

u16 KAPI FreeSelector(u16 sel)                                /* KERNEL.176 */
{
    dpmi_free_desc(sel);
    return 0;
}

/* dst becomes src's memory with the other type (code <-> data) */
u16 KAPI PrestoChangoSelector(u16 src, u16 dst)               /* KERNEL.177 */
{
    desc8 d;
    if (!get_desc(src, &d))
        return 0;
    d.b[5] ^= 0x08;
    if (d.b[5] & 0x08)
        d.b[5] = (u8)((d.b[5] & 0xF0) | 0x0A);
    else
        d.b[5] = (u8)((d.b[5] & 0xF0) | 0x02);
    return set_desc(dst, &d) ? dst : 0;
}

u16 KAPI SetSelectorBase(u16 sel, u32 base)                   /* KERNEL.187 */
{
    regs16 r;
    r.ax = 0x0007;
    r.bx = sel;
    r.cx = (u16)(base >> 16);
    r.dx = (u16)base;
    r.ds = r.es = 0;
    return dpmi31(&r) ? 0 : sel;
}

u32 KAPI GetSelectorLimit(u16 sel)                            /* KERNEL.188 */
{
    desc8 d;
    if (!get_desc(sel, &d))
        return 0;
    return ((u32)(d.b[6] & 0x0F) << 16) | d.b[0] | ((u16)d.b[1] << 8);
}

u16 KAPI SetSelectorLimit(u16 sel, u32 limit)                 /* KERNEL.189 */
{
    regs16 r;
    r.ax = 0x0008;
    r.bx = sel;
    r.cx = (u16)(limit >> 16);
    r.dx = (u16)limit;
    r.ds = r.es = 0;
    return dpmi31(&r) ? 1 : 0;          /* UNVERIFIED: the result */
}

/* GlobalDOSAlloc: DOS memory; high word the segment, low the selector */
u32 KAPI GlobalDOSAlloc(u32 size)                             /* KERNEL.184 */
{
    u16 seg, sel;
    if (size > 0xFFF0UL)
        return 0;                       /* TEMPORARY: one tile only */
    sel = galloc_dos((u16)size, task_pdb(), &seg);
    return sel ? ((u32)seg << 16) | sel : 0;
}

u16 KAPI GlobalDOSFree(u16 sel)                               /* KERNEL.185 */
{
    return GlobalFree(sel);
}
