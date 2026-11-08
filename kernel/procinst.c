/*
 * procinst.c - MakeProcInstance, FreeProcInstance, GetInstanceData.
 *
 * Observed on Windows 3.1 (tests/apps/tk5.c, tk15.c; Standard and 386
 * Enhanced mode agree), in protected mode:
 *  - Called with DS = a program's DGROUP, MakeProcInstance returns a new
 *    8-byte thunk every time (also for the same procedure):
 *        B8 lo hi       mov ax, <caller's DS>
 *        EA off seg     jmp far <procedure>
 *    The word it loads is the caller's DS (the selector, bit 0 set), not
 *    the hInstance argument: hInstance, hInstance | 1, 0 and a DLL's
 *    hInstance all gave the same thunk.
 *  - Called with DS = a DLL's DGROUP it returns the procedure itself,
 *    whatever the procedure and the hInstance argument.
 *  - Called with DS = a global block of no module, it makes a thunk that
 *    loads that DS.
 *  - The thunks are in the current task's TDB (Pietrek, "Windows
 *    Internals", Table 3-2; checked by tk15.c), through its code alias
 *    (TDB B0h). A thunk block: next block (code selector), 'PT', 0, the
 *    first free slot, then seven 8-byte thunks. The TDB's block starts at
 *    B2h (thunks from BAh); more blocks are 40h-byte segments of their own
 *    (the first one's selector at TDB B2h, each the next's in its word 0),
 *    added when the blocks before are full. A slot is a thunk's offset +
 *    6; a free slot's word holds the next free slot (0 ends the list), so
 *    a new TDB has C0h -> C8h -> ... -> F0h -> 0. Taking a slot writes the
 *    thunk over it; FreeProcInstance zeroes the thunk's first 6 bytes and
 *    puts its slot first (so it is the next thunk handed out).
 * UNVERIFIED: whether a third block goes at the end of the chain (here)
 * or the front; whether an empty block is ever freed (not here: with the
 * task).
 *
 * Without a task (a DLL's initialisation while the first program loads)
 * the thunks go to segments of KERNEL's own (UNVERIFIED: what Windows
 * does then).
 */
#include "kernel.h"

#define THUNK_LEN   8
#define PT_MAGIC    0x5450              /* "PT" */
#define TDB_THUNKS  0xB2                /* the TDB's thunk block */
#define TDB_ALIAS   0xB0
#define XBLK_SIZE   0x40                /* a further thunk block */
#define NTHUNKS     7

#define W(s, o)     (*(u16 FAR *)MK_FP((s), (o)))

/* Chain free slots base+0Eh .. base+3Eh of a block at `base` in seg. */
static void blk_init(u16 seg, u16 base)
{
    u16 i;
    W(seg, base) = 0;
    W(seg, base + 2) = PT_MAGIC;
    W(seg, base + 4) = 0;
    W(seg, base + 6) = base + 0x0E;
    for (i = 0; i < NTHUNKS; i++)
        W(seg, base + 0x0E + THUNK_LEN * i) =
            i + 1 < NTHUNKS ? base + 0x0E + THUNK_LEN * (i + 1) : 0;
}

void procinst_init_tdb(u16 t)
{
    blk_init(t, TDB_THUNKS);
}

/* a data selector for a thunk segment (by its code selector); free it */
static u16 data_alias(u16 code)
{
    u16 d = dpmi_alloc_desc(1);
    if (d && !dpmi_set_desc(d, dpmi_get_base(code), XBLK_SIZE - 1, 0)) {
        dpmi_free_desc(d);
        d = 0;
    }
    return d;
}

static void put_thunk(u8 FAR *c, u16 ds, u32 fn)
{
    c[0] = 0xB8;                        /* mov ax, ds */
    c[1] = (u8)ds;
    c[2] = (u8)(ds >> 8);
    c[3] = 0xEA;                        /* jmp far fn */
    c[4] = (u8)fn;
    c[5] = (u8)(fn >> 8);
    c[6] = (u8)(fn >> 16);
    c[7] = (u8)(fn >> 24);
}

/* take free slot `slot` of the block at seg:base for a thunk */
static void write_thunk(u16 seg, u16 base, u16 slot, u16 ds, u32 fn)
{
    W(seg, base + 6) = W(seg, slot);
    put_thunk(MK_FP(seg, slot - 6), ds, fn);
}

static u32 task_thunk(u16 t, u16 ds, u32 fn)
{
    u16 slot, code, prev = 0, d, nd, nc, pdb = W(t, 0x60);

    slot = W(t, TDB_THUNKS + 6);
    if (slot) {
        write_thunk(t, TDB_THUNKS, slot, ds, fn);
        return ((u32)W(t, TDB_ALIAS) << 16) | (slot - 6);
    }
    for (code = W(t, TDB_THUNKS); code; code = W(code, 0)) {
        prev = code;
        if (W(code, 6)) {
            d = data_alias(code);
            if (!d)
                return 0;
            slot = W(d, 6);
            write_thunk(d, 0, slot, ds, fn);
            dpmi_free_desc(d);
            return ((u32)code << 16) | (slot - 6);
        }
    }
    /* a new block, owned by the task (freed with it) */
    if (!galloc(GMEM_FIXED, XBLK_SIZE, pdb, &nd))
        return 0;
    blk_init(nd, 0);
    nc = galias_code(nd, pdb);
    if (!nc) {
        GlobalFree(nd);
        return 0;
    }
    slot = W(nd, 6);
    write_thunk(nd, 0, slot, ds, fn);
    if (!prev) {
        W(t, TDB_THUNKS) = nc;
    } else {
        d = data_alias(prev);
        if (d) {
            W(d, 0) = nc;
            dpmi_free_desc(d);
        }
    }
    return ((u32)nc << 16) | (slot - 6);
}

/* Free the thunk at sel:off if it is a live one of task t's blocks;
   1 if it was t's. */
static int task_free(u16 t, u16 sel, u16 off)
{
    u16 seg = 0, base = 0, code, d = 0, slot;
    u8 FAR *c;
    int r = 0;

    if ((sel | 3) == (W(t, TDB_ALIAS) | 3)) {
        seg = t;
        base = TDB_THUNKS;
    } else {
        for (code = W(t, TDB_THUNKS); code; code = W(code, 0))
            if ((sel | 3) == (code | 3)) {
                seg = d = data_alias(code);
                break;
            }
        if (!seg)
            return 0;
    }
    if (off >= base + 8 && off < base + 8 + NTHUNKS * THUNK_LEN &&
        (off - base - 8) % THUNK_LEN == 0) {
        c = MK_FP(seg, off);
        r = 1;
        if (c[0] == 0xB8) {             /* a live thunk */
            slot = off + 6;
            c[0] = c[1] = c[2] = c[3] = c[4] = c[5] = 0;
            W(seg, slot) = W(seg, base + 6);
            W(seg, base + 6) = slot;
        }
    }
    if (d)
        dpmi_free_desc(d);
    return r;
}

/* ---- without a task: segments of KERNEL's own ---- */

#define TB_SIZE     0x400       /* bytes per thunk segment; more as needed */
#define TB_HEAD     8           /* header, then the thunks */

/* header of a thunk segment, at offset 0 (through the data alias) */
typedef struct tblock {
    u16 next;                   /* code selector of the next segment */
    u16 alias;                  /* writable alias of this one */
    u16 free;                   /* first freed thunk, 0 if none */
    u16 top;                    /* first never-used offset */
} tblock;

static u16 tb_first;            /* code selector of the first segment */

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
    t = MK_FP(alias, 0);
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
        u16 a = W(s, 2);                /* code segments are readable */
        if ((s | 3) == (sel | 3))
            return a;
        s = W(s, 0);
    }
    return 0;
}

static u32 kernel_thunk(u16 ds, u32 fn)
{
    u16 s = tb_first, prev = 0, off;
    tblock FAR *t;

    for (;;) {
        if (!s) {
            s = tb_new();
            if (!s)
                return 0;
            if (prev)
                ((tblock FAR *)MK_FP(alias_of(prev), 0))->next = s;
            else
                tb_first = s;
        }
        t = MK_FP(W(s, 2), 0);
        if (t->free || t->top + THUNK_LEN <= TB_SIZE)
            break;
        prev = s;
        s = t->next;
    }
    if (t->free) {
        off = t->free;
        t->free = W(t->alias, off + 1);
    } else {
        off = t->top;
        t->top += THUNK_LEN;
    }
    put_thunk(MK_FP(t->alias, off), ds, fn);
    return ((u32)s << 16) | off;
}

static void kernel_free(u16 sel, u16 off)
{
    u16 alias = alias_of(sel);
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

/* ---- API ---- */

/* KERNEL.51, through kentry.asm (ds = the caller's DS) */
u32 KAPI MPI(u16 ds, u32 fn, u16 hinst)
{
    module FAR *m = module_of(ds);
    (void)hinst;
    if (!fn || (m && (m->flags & NEF_LIBRARY)))
        return fn;
    if (cur_task)
        return task_thunk(cur_task, ds, fn);
    return kernel_thunk(ds, fn);
}

void KAPI FreeProcInstance(u32 p)                              /* KERNEL.52 */
{
    u16 sel = (u16)(p >> 16), off = (u16)p, t;

    if (cur_task && task_free(cur_task, sel, off))
        return;
    for (t = (u16)(GetCurrentTask() >> 16); t; t = W(t, 0))
        if (task_free(t, sel, off))
            return;
    kernel_free(sel, off);
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
