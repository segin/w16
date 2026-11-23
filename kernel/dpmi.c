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

/* What happens to a freed selector (Windows 3.1, black-box: TK61,
   tests/apps/tk61.c; ARCHITECTURE.md 9.2):
 *  - KRNL286 (Standard mode) gives it back to DOSX at once with INT 31h
 *    0001h: the next 0000h hands it out again, and its descriptor is
 *    DOSX's free one. FS and GS keep it (FreeSelector, GlobalFree, a
 *    task's end): w16 does the same.
 *  - KRNL386 (both modes) keeps it on its own list (Pietrek, "Windows
 *    Internals", ch. 2, Get_Sel and Free_Sel), never given back to the
 *    DPMI host: written through the LDT's own selector (INT 2Fh 168Ah
 *    "MS-DOS", 0100h; k386c.c) as access byte 0, byte 6 0Fh (so not
 *    free to the host either), base 0, and in limit bits 0-15 the LDT
 *    offset of the next free one (FFFFh at the end). LAR, LSL, VERR and
 *    VERW fail on it. The last one freed is the first handed out (also
 *    to AllocSelector(0) and GlobalAlloc); several at once are the first
 *    on the list that starts a run of free ones going up. Without the
 *    LDT's selector (a host without the vendor API) KRNL386 would give
 *    them back as KRNL286 does (Pietrek: Free_Sel, GDTDsc 0).
 * A freed selector left in FS or GS: the DOS extender's business
 * (dosx/ring0.inc fsgs_restore, win386/vmm/vmmasm.asm fixsel). */
#ifdef KRNL386
u16 ldt_alias;                  /* k386c.c: the LDT's selector, 0: none */
static u16 free_head = 0xFFFF;  /* LDT offset of the first free one */

static u16 lsl16(u16 sel);
#pragma aux lsl16 = \
    "xor ax, ax"                                            \
    0x0F 0x03 0xC3              /* lsl ax, bx */            \
    parm [bx] value [ax] modify exact [ax];

static u8 FAR *ldt_at(u16 off)
{
    return (u8 FAR *)MK_FP(ldt_alias, off);
}

/* the descriptor at LDT offset off is within the alias */
static int ldt_ok(u16 off)
{
    u16 lim = lsl16(ldt_alias);
    return off <= lim && lim - off >= 7;
}

static int on_list(u16 off)
{
    u8 FAR *d;
    if (!ldt_ok(off))
        return 0;
    d = ldt_at(off);
    return d[5] == 0 && d[6] == 0x0F;
}

static u16 next_of(u16 off)
{
    return *(u16 FAR *)ldt_at(off);
}

/* takes off out of the list (prev: the one before it, FFFFh: the head) */
static void unlink_sel(u16 off, u16 prev)
{
    if (prev == 0xFFFF)
        free_head = next_of(off);
    else
        *(u16 FAR *)ldt_at(prev) = next_of(off);
}

/* n selectors from the list, or 0 */
static u16 list_take(u16 n)
{
    u16 e, prev = 0xFFFF, i, steps = 0, p, q;
    u8 FAR *d;
    if (!ldt_alias || !n)
        return 0;
    for (e = free_head; e != 0xFFFF; prev = e, e = next_of(e)) {
        if (++steps > 8192 || !ldt_ok(e))
            return 0;
        for (i = 1; i < n; i++)         /* (not wrapping past FFF8h: */
            if ((u32)e + 8UL * i > 0xFFF8UL || !on_list(e + 8 * i))
                break;                  /* KERNEL audit, H17) */
        if (i == n)
            break;
    }
    if (e == 0xFFFF)
        return 0;
    unlink_sel(e, prev);
    for (i = 1; i < n; i++) {
        /* the run's others, wherever they are on the list */
        for (q = 0xFFFF, p = free_head; p != 0xFFFF && p != e + 8 * i;
             q = p, p = next_of(p))
            ;
        if (p != 0xFFFF)
            unlink_sel(p, q);
    }
    for (i = 0; i < n; i++) {
        /* as a fresh descriptor from the host: data, present, base and
           limit 0 */
        d = ldt_at(e + 8 * i);
        d[0] = d[1] = d[2] = d[3] = d[4] = d[6] = d[7] = 0;
        d[5] = (u8)(0x93 | (cpl << 5));
    }
    return e | 4 | cpl;
}

/* sel onto the list; 0 when it cannot be (then the host gets it) */
static int list_put(u16 sel)
{
    u16 off = sel & 0xFFF8;
    u8 FAR *d;
    if (!ldt_alias || !(sel & 4) || !ldt_ok(off))
        return 0;
    d = ldt_at(off);
    if (d[5] == 0 && d[6] == 0x0F)
        return 1;                       /* already free: w16 keeps the
                                           list whole (3.1 handed such a
                                           selector out twice, TK17) */
    d[5] = 0;
    d[6] = 0x0F;
    d[2] = d[3] = d[4] = d[7] = 0;
    *(u16 FAR *)d = free_head;
    free_head = off;
    return 1;
}
#endif

u16 dpmi_alloc_desc(u16 n)
{
    regs16 r;
#ifdef KRNL386
    u16 s = list_take(n);
    if (s)
        return s;
#endif
    r.ax = 0x0000;
    r.cx = n;
    return call(&r) ? r.ax : 0;
}

void dpmi_free_desc(u16 sel)
{
    regs16 r;
#ifdef KRNL386
    if (list_put(sel))
        return;
#endif
    r.ax = 0x0001;
    r.bx = sel;
    call(&r);
}

int dpmi_set_desc(u16 sel, u32 base, u32 limit, int code)
{
    u8 desc[8];
    if (limit > 0xFFFFFUL) {
        if ((limit & 0xFFFUL) != 0xFFFUL)
            return 0;
        limit >>= 12;
        desc[6] = (u8)(0x80 | ((limit >> 16) & 0x0F));
    } else {
        desc[6] = (u8)((limit >> 16) & 0x0F);
    }
    desc[0] = (u8)limit;
    desc[1] = (u8)(limit >> 8);
    desc[2] = (u8)base;
    desc[3] = (u8)(base >> 8);
    desc[4] = (u8)(base >> 16);
    /* accessed set, as Windows 3.1 sets it (observed access F3h, FBh) */
    desc[5] = (u8)((code ? 0x9B : 0x93) | (cpl << 5));
    desc[7] = (u8)(base >> 24);
#ifdef KRNL386
    if (ldt_alias && (sel & 4) && ldt_ok(sel & 0xFFF8)) {
        u8 FAR *p = ldt_at(sel & 0xFFF8);
        *(u32 FAR *)p = *(u32 *)desc;
        *(u32 FAR *)(p + 4) = *(u32 *)(desc + 4);
        return 1;
    }
#endif
    return dpmi_set_ldt(sel, (const u8 FAR *)desc);
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
