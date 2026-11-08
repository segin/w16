/*
 * procinst.c - MakeProcInstance, FreeProcInstance, GetInstanceData.
 *
 * Observed on Windows 3.1 (tests/apps/tk5.c, Standard and 386 Enhanced
 * mode agree), in protected mode:
 *  - Called with DS = a program's DGROUP, MakeProcInstance returns a new
 *    8-byte thunk every time (also for the same procedure):
 *        B8 lo hi       mov ax, <caller's DS>
 *        EA off seg     jmp far <procedure>
 *    The word it loads is the caller's DS (the selector, bit 0 set), not
 *    the hInstance argument: hInstance, hInstance | 1, 0 and a DLL's
 *    hInstance all gave the same thunk.
 *  - Called with DS = a DLL's DGROUP it returns the procedure itself,
 *    whatever the procedure and the hInstance argument.
 *  - Thunks are consecutive, 8 bytes apart, in a segment of their own
 *    (another selector than the program's code, RPL 3, a global block for
 *    GlobalHandle); FreeProcInstance makes a thunk the next one handed
 *    out.
 *  - Called with DS = a global block of no module, it makes a thunk that
 *    loads that DS.
 * UNVERIFIED: where in its segment the first thunk lies (offset BAh on
 * Windows, with other data before it; here 8).
 */
#include "kernel.h"

#define TB_SIZE     0x400       /* bytes per thunk segment; more as needed */
#define TB_HEAD     8           /* header, then the thunks */
#define THUNK_LEN   8

/* header of a thunk segment, at offset 0 (through the data alias) */
typedef struct tblock {
    u16 next;                   /* code selector of the next segment */
    u16 alias;                  /* writable alias of this one */
    u16 free;                   /* first freed thunk, 0 if none */
    u16 top;                    /* first never-used offset */
} tblock;

static u16 tb_first;            /* code selector of the first segment */

static tblock FAR *head_of(u16 code_sel, u16 alias)
{
    (void)code_sel;
    return MK_FP(alias, 0);
}

static u16 tb_new(void)
{
    u16 sel, alias;
    tblock FAR *t;

    if (!galloc(GMEM_FIXED, TB_SIZE, 0, &sel))
        return 0;
    alias = dpmi_alloc_desc(1);
    if (!alias || !dpmi_set_desc(alias, dpmi_get_base(sel), TB_SIZE - 1, 0)) {
        if (alias)
            dpmi_free_desc(alias);
        GlobalFree(sel);
        return 0;
    }
    gset_code(sel);
    t = head_of(sel, alias);
    t->next = 0;
    t->alias = alias;
    t->free = 0;
    t->top = TB_HEAD;
    return sel;
}

/* The writable alias of thunk segment `sel`, or 0 if it is not one. */
static u16 alias_of(u16 sel)
{
    u16 s = tb_first;
    while (s) {
        u16 a = *(u16 FAR *)MK_FP(s, 2);      /* code segments are readable */
        if ((s | 3) == (sel | 3))
            return a;
        s = *(u16 FAR *)MK_FP(s, 0);
    }
    return 0;
}

static u32 thunk_alloc(u16 ds, u32 fn)
{
    u16 s = tb_first, prev = 0, off;
    tblock FAR *t;
    u8 FAR *c;

    for (;;) {
        if (!s) {
            s = tb_new();
            if (!s)
                return 0;
            if (prev)
                head_of(prev, alias_of(prev))->next = s;
            else
                tb_first = s;
        }
        t = head_of(s, *(u16 FAR *)MK_FP(s, 2));
        if (t->free || t->top + THUNK_LEN <= TB_SIZE)
            break;
        prev = s;
        s = t->next;
    }
    if (t->free) {
        off = t->free;
        t->free = *(u16 FAR *)MK_FP(t->alias, off + 1);
    } else {
        off = t->top;
        t->top += THUNK_LEN;
    }
    c = MK_FP(t->alias, off);
    c[0] = 0xB8;                        /* mov ax, ds */
    c[1] = (u8)ds;
    c[2] = (u8)(ds >> 8);
    c[3] = 0xEA;                        /* jmp far fn */
    c[4] = (u8)fn;
    c[5] = (u8)(fn >> 8);
    c[6] = (u8)(fn >> 16);
    c[7] = (u8)(fn >> 24);
    return ((u32)s << 16) | off;
}

/* KERNEL.51, through kentry.asm (ds = the caller's DS) */
u32 KAPI MPI(u16 ds, u32 fn, u16 hinst)
{
    module FAR *m = module_of(ds);
    (void)hinst;
    if (!fn || (m && (m->flags & NEF_LIBRARY)))
        return fn;
    return thunk_alloc(ds, fn);
}

void KAPI FreeProcInstance(u32 p)                              /* KERNEL.52 */
{
    u16 sel = (u16)(p >> 16), off = (u16)p, alias = alias_of(sel);
    tblock FAR *t;
    u8 FAR *c;

    if (!alias || off < TB_HEAD || (off - TB_HEAD) % THUNK_LEN)
        return;
    t = MK_FP(alias, 0);
    c = MK_FP(alias, off);
    if (off >= t->top || c[0] != 0xB8)
        return;                         /* not a live thunk */
    c[0] = 0;
    *(u16 FAR *)(c + 1) = t->free;
    t->free = off;
}

/* KERNEL.54, through kentry.asm (ds = the caller's DS). Copies n bytes at
   offset off of the instance's data segment to the same offset in the
   caller's. Observed: returns n (also for a DLL's instance).
   UNVERIFIED: the result for a handle that is not a segment (0 here). */
u16 KAPI GIDATA(u16 ds, u16 h, u16 off, u16 n)
{
    if (!h || !gsize_of_sel(h | 1))
        return 0;
    kmemcpy(MK_FP(ds, off), MK_FP(h | 1, off), n);
    return n;
}
