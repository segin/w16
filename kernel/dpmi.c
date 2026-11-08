/*
 * dpmi.c - wrappers for the DPMI 0.9 services KERNEL uses (INT 31h).
 * Source: DPMI Specification 0.9 (DPMI Committee, 1990), publicly available.
 */
#include "kernel.h"

u16 cpl;
u16 sel_inc;
u16 sel_shift;

static int call(regs16 *r)
{
    r->ds = r->es = 0;
    return !dpmi31(r);              /* 1 = success */
}

u16 dpmi_alloc_desc(u16 n)
{
    regs16 r;
    r.ax = 0x0000;
    r.cx = n;
    return call(&r) ? r.ax : 0;
}

void dpmi_free_desc(u16 sel)
{
    regs16 r;
    r.ax = 0x0001;
    r.bx = sel;
    call(&r);
}

int dpmi_set_desc(u16 sel, u32 base, u32 limit, int code)
{
    regs16 r;
    r.ax = 0x0007;
    r.bx = sel;
    r.cx = (u16)(base >> 16);
    r.dx = (u16)base;
    if (!call(&r))
        return 0;
    r.ax = 0x0008;
    r.bx = sel;
    r.cx = (u16)(limit >> 16);
    r.dx = (u16)limit;
    if (!call(&r))
        return 0;
    r.ax = 0x0009;
    r.bx = sel;
    r.cx = (code ? 0x9A : 0x92) | (cpl << 5);
    return call(&r);
}

u32 dpmi_get_base(u16 sel)
{
    regs16 r;
    r.ax = 0x0006;
    r.bx = sel;
    if (!call(&r))
        return 0;
    return ((u32)r.cx << 16) | r.dx;
}

u16 dpmi_seg_to_desc(u16 para)
{
    regs16 r;
    r.ax = 0x0002;
    r.bx = para;
    return call(&r) ? r.ax : 0;
}

int dpmi_alloc_mem(u32 size, u32 *lin, u32 *handle)
{
    regs16 r;
    r.ax = 0x0501;
    r.bx = (u16)(size >> 16);
    r.cx = (u16)size;
    if (!call(&r))
        return 0;
    *lin = ((u32)r.bx << 16) | r.cx;
    *handle = ((u32)r.si << 16) | r.di;
    return 1;
}

int dpmi_free_mem(u32 handle)
{
    regs16 r;
    r.ax = 0x0502;
    r.si = (u16)(handle >> 16);
    r.di = (u16)handle;
    return call(&r);
}

int dpmi_resize_mem(u32 handle, u32 size, u32 *lin, u32 *newhandle)
{
    regs16 r;
    r.ax = 0x0503;
    r.bx = (u16)(size >> 16);
    r.cx = (u16)size;
    r.si = (u16)(handle >> 16);
    r.di = (u16)handle;
    if (!call(&r))
        return 0;
    *lin = ((u32)r.bx << 16) | r.cx;
    *newhandle = ((u32)r.si << 16) | r.di;
    return 1;
}

/* Processor type from DPMI 0400h: 2 = 286, 3 = 386, 4 = 486, ... */
u16 dpmi_cpu_type(void)
{
    regs16 r;
    r.ax = 0x0400;
    return call(&r) ? (r.cx & 0xFF) : 2;
}
