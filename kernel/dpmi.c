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

/* FS and GS on a 386 (the C code never uses them: KRNL286 is 286 code,
   KRNL386 is built with -zfp -zgp) */
static u16 fs_of(void);
#pragma aux fs_of = 0x8C 0xE0 value [ax] modify exact [ax];          /* mov ax, fs */
static u16 gs_of(void);
#pragma aux gs_of = 0x8C 0xE8 value [ax] modify exact [ax];          /* mov ax, gs */
static void fs_zero(void);
#pragma aux fs_zero = 0x31 0xC0 0x8E 0xE0 modify exact [ax];         /* xor ax, ax; mov fs, ax */
static void gs_zero(void);
#pragma aux gs_zero = 0x31 0xC0 0x8E 0xE8 modify exact [ax];         /* xor ax, ax; mov gs, ax */
static u16 has_fsgs = 0xFFFF;   /* a 386 or later (unknown: FFFFh) */

/* A selector still in FS or GS when it is freed: zeroed first. The DOS
   extender reloads FS and GS when its INT 31h returns (w16's DOSX at the
   POP FS of its call gate's exit, Microsoft's in its ring-3 code) and at
   every interrupt, so a dead one there ended Windows ("Standard Mode:
   Fault in MS-DOS Extender", error code the selector). Windows 3.1's
   KERNEL puts a freed selector on its own free list (Pietrek, ch. 2,
   Free_Sel) and never hands it back to the host, so this never comes up
   there: QuickTime's Movie Player ends with one of its blocks in FS, and
   KRNL286 freed that block with the task (docs/notes/
   quicktime-handover.md, "Closing the player"). */
void dpmi_free_desc(u16 sel)
{
    regs16 r;
    if (has_fsgs == 0xFFFF)
        has_fsgs = dpmi_cpu_type() >= 3;
    if (has_fsgs) {
        if (!((fs_of() ^ sel) & 0xFFFC))
            fs_zero();
        if (!((gs_of() ^ sel) & 0xFFFC))
            gs_zero();
    }
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
    /* accessed set, as Windows 3.1 sets it (observed access F3h, FBh) */
    r.cx = (code ? 0x9B : 0x93) | (cpl << 5);
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

/* DPMI 000Bh: the 8-byte descriptor of sel into d; 1 = success */
int dpmi_get_ldt(u16 sel, u8 FAR *d)
{
    regs16 r;
    r.ax = 0x000B;
    r.bx = sel;
    r.di = FP_OFF(d);
    r.ds = 0;
    r.es = FP_SEG(d);
    return !dpmi31(&r);
}

/* DPMI 000Ch: set the descriptor of sel from d; 1 = success */
int dpmi_set_ldt(u16 sel, const u8 FAR *d)
{
    regs16 r;
    r.ax = 0x000C;
    r.bx = sel;
    r.di = FP_OFF(d);
    r.ds = 0;
    r.es = FP_SEG(d);
    return !dpmi31(&r);
}

/* DPMI 0008h alone: set the limit of sel; 1 = success */
int dpmi_set_limit(u16 sel, u32 limit)
{
    regs16 r;
    r.ax = 0x0008;
    r.bx = sel;
    r.cx = (u16)(limit >> 16);
    r.dx = (u16)limit;
    return call(&r);
}

/* DPMI 0007h alone: set the base of sel; 1 = success */
int dpmi_set_base(u16 sel, u32 base)
{
    regs16 r;
    r.ax = 0x0007;
    r.bx = sel;
    r.cx = (u16)(base >> 16);
    r.dx = (u16)base;
    return call(&r);
}

/* DPMI 0009h alone: access byte (low) and the 386 flags nibble (high byte,
   bits 4-7) of sel; 1 = success */
int dpmi_set_rights(u16 sel, u16 rights)
{
    regs16 r;
    r.ax = 0x0009;
    r.bx = sel;
    r.cx = rights;
    return call(&r);
}
