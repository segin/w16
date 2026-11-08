/*
 * global.c - global heap (GlobalAlloc and friends), first version.
 *
 * M1 implementation: every block is its own DPMI memory block (INT 31h
 * 0501h) with one descriptor per 64K tile, consecutive by the selector
 * increment (__AHINCR). This is enough for the API semantics. The real
 * Windows global arena (sub-allocation, compaction order, discarding, the
 * arena structures TOOLHELP walks) comes later.
 *
 * Behaviour observed on Windows 3.1 (Standard and 386 Enhanced mode, with
 * tests/apps/tk1.c; see ARCHITECTURE.md 9.2):
 *  - every block's handle, fixed or moveable, is its selector with bit 0
 *    cleared; GlobalLock sets bit 0 again.
 *  - sizes are rounded up to 32 bytes (GlobalSize(1) = 20h).
 *  - only discardable blocks keep a lock count; GlobalFlags of a locked
 *    fixed or moveable block reads 0, of a locked discardable one 0101h.
 *  - GlobalAlloc(GMEM_MOVEABLE, 0) gives a block that is already
 *    discarded: GlobalFlags = GMEM_DISCARDED, GlobalSize = 0, GlobalLock
 *    returns NULL.
 *  - GlobalFree of a handle that is not a block returned 0.
 */
#include "kernel.h"

#define ARENA_CHUNK 64          /* block records per chunk (see achunk) */

typedef struct garena {
    u16 sel;            /* first selector; 0 = free slot */
    u16 nsel;           /* number of 64K tiles */
    u32 lin;            /* linear base address */
    u32 size;           /* size reported by GlobalSize */
    u32 dpmi;           /* DPMI memory block handle */
    u16 flags;          /* GMEM_* flags */
    u16 lock;           /* lock count (moveable blocks) */
    u16 owner;
    u16 kind;           /* GK_* */
    u16 pglock;         /* GlobalPageLock count (selector.c) */
} garena;

#define GK_NORMAL   0
#define GK_DOS      1   /* DOS memory (DPMI 0100h): dpmi = its selector */
#define GK_ALIAS    2   /* a code alias of another block's memory */

#define GRAN 32UL                       /* allocation granularity */
#define ROUND(n) (((n) + GRAN - 1) & ~(GRAN - 1))

/* The block records live in chunks of their own DPMI memory, chained;
   another chunk is added when all are in use. */
typedef struct achunk {
    struct achunk FAR *next;
    garena a[ARENA_CHUNK];
} achunk;

static achunk FAR *chunks;

void global_init(void)
{
    chunks = NULL;
}

static achunk FAR *chunk_new(void)
{
    u32 lin, h;
    u16 sel;
    achunk FAR *c;

    if (!dpmi_alloc_mem(sizeof(achunk), &lin, &h))
        return NULL;
    sel = dpmi_alloc_desc(1);
    if (!sel || !dpmi_set_desc(sel, lin, sizeof(achunk) - 1, 0)) {
        if (sel)
            dpmi_free_desc(sel);
        dpmi_free_mem(h);
        return NULL;
    }
    c = MK_FP(sel, 0);
    kmemset(c, 0, sizeof(achunk));
    return c;
}

static garena FAR *find(u16 h)
{
    u16 sel = h | 1, i;
    achunk FAR *c;
    if (!h)
        return NULL;
    for (c = chunks; c; c = c->next)
        for (i = 0; i < ARENA_CHUNK; i++)
            if (c->a[i].sel == sel)
                return &c->a[i];
    return NULL;
}

static garena FAR *free_record(void)
{
    achunk FAR *c, FAR *last = NULL;
    u16 i;
    for (c = chunks; c; last = c, c = c->next)
        for (i = 0; i < ARENA_CHUNK; i++)
            if (!c->a[i].sel)
                return &c->a[i];
    c = chunk_new();
    if (!c)
        return NULL;
    if (last)
        last->next = c;
    else
        chunks = c;
    return &c->a[0];
}

static u16 handle_of(const garena FAR *a)
{
    /* observed (tests/apps/tk15.c): a task database, which is in DOS
       memory, has GlobalHandle(hTask) == hTask, an odd selector */
    return a->kind == GK_NORMAL ? a->sel & ~1u : a->sel;
}

static int discarded(const garena FAR *a)
{
    return a->size == 0;
}

/* Set the base and limit of every tile of `a` from a->lin and a->size. */
static int set_tiles(garena FAR *a)
{
    u16 i;
    u32 left = a->size;
    if (discarded(a))                   /* keep the selector, no memory */
        return dpmi_set_desc(a->sel, 0, 0, 0);
    /* Observed (tests/apps/tk17.c): each tile's limit reaches the end of
       the block (tile i: size - 64K * i - 1), as a 386 can address
       through it. A host that only takes 64K limits gets those. */
    for (i = 0; i < a->nsel; i++) {
        u16 t = a->sel + i * sel_inc;
        u32 base = a->lin + ((u32)i << 16);
        if (!dpmi_set_desc(t, base, left - 1, 0) &&
            !dpmi_set_desc(t, base, (left > 0x10000UL ? 0x10000UL : left) - 1, 0))
            return 0;
        left -= left > 0x10000UL ? 0x10000UL : left;
    }
    return 1;
}

static void zero_range(garena FAR *a, u32 from)
{
    while (from < a->size) {
        u16 tile = (u16)(from >> 16);
        u16 off = (u16)from;
        u32 end = ((u32)tile + 1) << 16;
        u32 n;
        if (end > a->size)
            end = a->size;
        n = end - from;
        if (n == 0x10000UL) {           /* kmemset takes at most 0xFFFF */
            kmemset(MK_FP(a->sel + tile * sel_inc, 0), 0, 0x8000);
            kmemset(MK_FP(a->sel + tile * sel_inc, 0x8000), 0, 0x8000);
        } else {
            kmemset(MK_FP(a->sel + tile * sel_inc, off), 0, (u16)n);
        }
        from = end;
    }
}

u16 galloc(u16 flags, u32 size, u16 owner, u16 *sel_out)
{
    garena FAR *a = NULL;
    int i;

    /* A zero-size fixed block also succeeds on Windows 3.1; what it
       contains is UNVERIFIED, so it gets the minimum allocation. */
    if (size == 0 && !(flags & GMEM_MOVEABLE))
        size = GRAN;
    if (size > 0x00FF0000UL)
        return 0;
    a = free_record();
    if (!a)
        return 0;

    a->size = ROUND(size);
    a->nsel = size ? (u16)((a->size + 0xFFFFUL) >> 16) : 1;
    a->lin = a->dpmi = 0;
    if (size && !dpmi_alloc_mem(a->size, &a->lin, &a->dpmi))
        return 0;
    a->sel = dpmi_alloc_desc(a->nsel);
    if (!a->sel) {
        if (size)
            dpmi_free_mem(a->dpmi);
        return 0;
    }
    if (!set_tiles(a)) {
        for (i = 0; i < a->nsel; i++)
            dpmi_free_desc(a->sel + i * sel_inc);
        if (size)
            dpmi_free_mem(a->dpmi);
        a->sel = 0;
        return 0;
    }
    /* Always zero new memory: deterministic, and never less compatible
       than whatever happened to be there. */
    zero_range(a, 0);
    a->flags = flags;
    a->lock = 0;
    a->owner = owner;
    a->kind = GK_NORMAL;
    a->pglock = 0;
    if (sel_out)
        *sel_out = a->sel;
    return handle_of(a);
}

static void gfree(garena FAR *a)
{
    u16 i;
    regs16 r;
    if (a->kind == GK_DOS) {
        r.ax = 0x0101;                  /* frees the selector too */
        r.dx = (u16)a->dpmi;
        r.ds = r.es = 0;
        dpmi31(&r);
        a->sel = 0;
        return;
    }
    for (i = 0; i < a->nsel; i++)
        dpmi_free_desc(a->sel + i * sel_inc);
    if (a->kind == GK_NORMAL && !discarded(a))
        dpmi_free_mem(a->dpmi);
    a->sel = 0;
}

/* A zeroed block of `size` bytes in DOS memory (DPMI 0100h), whose handle
   is its selector; *seg is its real-mode segment. A block over 64K has
   tiles as GlobalAlloc's (observed, tests/apps/tk17.c: GlobalDOSAlloc).
   Returns the selector, 0 if out of memory. */
u16 galloc_dos(u32 size, u16 owner, u16 *seg)
{
    garena FAR *a;
    regs16 r;
    if (size == 0 || ROUND(size) > 0xFFFF0UL)
        return 0;
    a = free_record();
    if (!a)
        return 0;
    r.ax = 0x0100;
    r.bx = (u16)((ROUND(size) + 15) >> 4);
    r.ds = r.es = 0;
    if (dpmi31(&r))
        return 0;
    a->sel = r.dx;
    a->dpmi = r.dx;
    a->lin = (u32)r.ax << 4;
    a->size = ROUND(size);
    a->nsel = (u16)((a->size + 0xFFFFUL) >> 16);
    a->flags = GMEM_FIXED;
    a->lock = 0;
    a->owner = owner;
    a->kind = GK_DOS;
    a->pglock = 0;
    *seg = r.ax;
    set_tiles(a);                       /* rights and limits as GlobalAlloc's */
    zero_range(a, 0);
    return a->sel;
}

/* A code selector for the memory of block `sel`, itself a block that
   GlobalHandle and GlobalSize know (freeing it frees only the selector).
   Returns 0 on failure. */
u16 galias_code(u16 sel, u16 owner)
{
    garena FAR *s = find(sel), FAR *a;
    u16 c;
    if (!s || discarded(s) || s->size > 0x10000UL)
        return 0;
    a = free_record();
    if (!a)
        return 0;
    c = dpmi_alloc_desc(1);
    if (!c)
        return 0;
    if (!dpmi_set_desc(c, s->lin, s->size - 1, 1)) {
        dpmi_free_desc(c);
        return 0;
    }
    a->sel = c;
    a->nsel = 1;
    a->lin = s->lin;
    a->size = s->size;
    a->dpmi = 0;
    a->flags = GMEM_FIXED;
    a->lock = 0;
    a->owner = owner;
    a->kind = GK_ALIAS;
    a->pglock = 0;
    return c;
}

/* Free every block `owner` owns (a task's PDB, when the task ends). */
void gfree_owner(u16 owner)
{
    achunk FAR *c;
    u16 i;
    if (!owner)
        return;
    for (c = chunks; c; c = c->next)
        for (i = 0; i < ARENA_CHUNK; i++)
            if (c->a[i].sel && c->a[i].owner == owner)
                gfree(&c->a[i]);
}

u16 gowner(u16 h)
{
    garena FAR *a = find(h);
    return a ? a->owner : 0;
}

int gset_code(u16 sel)
{
    garena FAR *a = find(sel);
    if (!a)
        return 0;
    return dpmi_set_desc(a->sel, a->lin, (a->size > 0x10000UL ? 0x10000UL : a->size) - 1, 1);
}

u32 gsize_of_sel(u16 sel)
{
    garena FAR *a = find(sel);
    return a ? a->size : 0;
}

/* Lock counts for selector.c (GlobalFix, GlobalWire, GlobalPageLock,
   AllocDStoCSAlias): add delta to the block's lock count, which counts
   for every kind of block, and return the new count; -1 if h is not a
   block. The count does not go below 0. */
int glock_adjust(u16 h, int delta)
{
    garena FAR *a = find(h);
    if (!a)
        return -1;
    if (delta > 0 && a->lock < GMEM_LOCKCOUNT)
        a->lock++;
    else if (delta < 0 && a->lock)
        a->lock--;
    return a->lock;
}

/* The same for the page-lock count; delta 0 reads it. */
int gpagelock_adjust(u16 h, int delta)
{
    garena FAR *a = find(h);
    if (!a)
        return -1;
    if (delta > 0)
        a->pglock++;
    else if (delta < 0 && a->pglock)
        a->pglock--;
    return a->pglock;
}

/* ---- API ---- */

/* Observed (tests/apps/tk15.c): a block a task allocates is freed when
   the task ends. As on Windows (Pietrek, "Windows Internals"), the owner
   is the current task's PDB. UNVERIFIED: GMEM_SHARE blocks (owned by no
   task here). */
u16 KAPI GlobalAlloc(u16 flags, u32 size)                     /* KERNEL.15 */
{
    return galloc(flags, size, (flags & GMEM_SHARE) ? 0 : task_pdb(), NULL);
}

/* Give back a selector run that a failed reallocation took. */
static void undo_run(u16 run, u16 n)
{
    u16 i;
    for (i = 0; run && i < n; i++)
        dpmi_free_desc(run + i * sel_inc);
}

u16 KAPI GlobalReAlloc(u16 h, u32 size, u16 flags)            /* KERNEL.16 */
{
    garena FAR *a = find(h);
    u32 old, lin, nh, newsize;
    u16 newn, run = 0;

    if (!a || a->kind != GK_NORMAL)
        return 0;
    if (flags & GMEM_MODIFY) {
        /* only the discardable/moveable attributes can change */
        a->flags = (a->flags & ~GMEM_DISCARDABLE) | (flags & GMEM_DISCARDABLE);
        return handle_of(a);
    }
    if (size == 0) {
        /* GlobalDiscard. Observed on Windows 3.1 (tests/apps/tk12.c): an
           unlocked discardable block becomes discarded (GlobalFlags
           4000h | 0100h, size 0) and keeps its handle, which is returned.
           UNVERIFIED: moveable blocks that are not discardable (discarded
           here too, as LocalReAlloc does), locked and fixed blocks (0). */
        u16 i;
        if (!(flags & GMEM_MOVEABLE) || a->lock ||
            !(a->flags & (GMEM_MOVEABLE | GMEM_DISCARDABLE)))
            return 0;
        if (!discarded(a)) {
            dpmi_free_mem(a->dpmi);
            a->size = 0;
            a->lin = a->dpmi = 0;
            for (i = 0; i < a->nsel; i++)
                dpmi_set_desc(a->sel + i * sel_inc, 0, 0, 0);
        }
        return handle_of(a);
    }
    newsize = ROUND(size);
    newn = (u16)((newsize + 0xFFFFUL) >> 16);
    if (newn > a->nsel) {
        /* More tiles. Observed (tests/apps/tk17.c): the block always moves
           to a new selector run, so the handle changes, for moveable and
           fixed blocks alike (also without GMEM_MOVEABLE). */
        run = dpmi_alloc_desc(newn);
        if (!run)
            return 0;
    }
    old = a->size;
    if (discarded(a)) {
        if (!dpmi_alloc_mem(newsize, &lin, &nh)) {
            undo_run(run, newn);
            return 0;
        }
    } else if (a->nsel == 1 && newn == 1) {
        /* A single-tile block may be the running task's stack (its DGROUP
           grows with its local heap), so it must never be unmapped while
           in use, as DPMI 0503h may do when it moves a block. Growing:
           copy into a new block and switch the selector in one step
           (seg_move). Shrinking: keep the memory. */
        if (newsize > old) {
            u16 tmp;
            if (!dpmi_alloc_mem(newsize, &lin, &nh))
                return 0;
            tmp = dpmi_alloc_desc(1);
            if (!tmp || !dpmi_set_desc(tmp, lin, newsize - 1, 0)) {
                if (tmp)
                    dpmi_free_desc(tmp);
                dpmi_free_mem(nh);
                return 0;
            }
            seg_move(a->sel, tmp, (u16)((old + 1) >> 1), lin);
            dpmi_free_desc(tmp);
            dpmi_free_mem(a->dpmi);
        } else {
            lin = a->lin;
            nh = a->dpmi;
        }
    } else if (!dpmi_resize_mem(a->dpmi, newsize, &lin, &nh)) {
        undo_run(run, newn);
        return 0;
    }
    if (run) {                          /* the block moves to the new run */
        u16 i;
        for (i = 0; i < a->nsel; i++)
            dpmi_free_desc(a->sel + i * sel_inc);
        a->sel = run;
    }
    if (newn < a->nsel) {               /* fewer tiles: free the rest */
        u16 i;
        for (i = newn; i < a->nsel; i++)
            dpmi_free_desc(a->sel + i * sel_inc);
    }
    a->nsel = newn;
    a->lin = lin;
    a->dpmi = nh;
    a->size = newsize;
    set_tiles(a);
    if (newsize > old)
        zero_range(a, old);
    return handle_of(a);
}

u16 KAPI GlobalFree(u16 h)                                    /* KERNEL.17 */
{
    garena FAR *a = find(h);
    if (a)
        gfree(a);
    return 0;
}

void FAR * KAPI GlobalLock(u16 h)                             /* KERNEL.18 */
{
    garena FAR *a = find(h);
    if (!a || discarded(a))
        return NULL;
    if ((a->flags & GMEM_DISCARDABLE) && a->lock < 0xFF)
        a->lock++;
    return MK_FP(a->sel, 0);
}

u16 KAPI GlobalUnlock(u16 h)                                  /* KERNEL.19 */
{
    garena FAR *a = find(h);
    if (!a)
        return 0;
    if (a->lock)
        a->lock--;
    return a->lock != 0;
}

u32 KAPI GlobalSize(u16 h)                                    /* KERNEL.20 */
{
    garena FAR *a = find(h);
    return a ? a->size : 0;
}

/* Returns handle in the low word and selector in the high word. */
u32 KAPI GlobalHandle(u16 sel)                                /* KERNEL.21 */
{
    garena FAR *a = find(sel);
    if (!a)
        return 0;
    return ((u32)a->sel << 16) | handle_of(a);
}

u16 KAPI GlobalFlags(u16 h)                                   /* KERNEL.22 */
{
    garena FAR *a = find(h);
    if (!a)
        return 0;
    return (a->lock & GMEM_LOCKCOUNT) | (a->flags & GMEM_DISCARDABLE) |
           (discarded(a) ? GMEM_DISCARDED : 0);
}

/* Observed (tests/apps/tk17.c): LockSegment and UnlockSegment return the
   handle, and count only for discardable blocks, as GlobalLock does.
   TODO: LockSegment(-1) should lock the caller's data segment; __loadds
   has replaced DS by then, so it needs an asm entry point. */
u16 KAPI LockSegment(u16 seg)                                 /* KERNEL.23 */
{
    garena FAR *a = find(seg);
    if (!a)
        return 0;
    if ((a->flags & GMEM_DISCARDABLE) && a->lock < GMEM_LOCKCOUNT)
        a->lock++;
    return handle_of(a);
}

u16 KAPI UnlockSegment(u16 seg)                               /* KERNEL.24 */
{
    garena FAR *a = find(seg);
    if (!a)
        return 0;
    if ((a->flags & GMEM_DISCARDABLE) && a->lock)
        a->lock--;
    return handle_of(a);
}

/* Largest free block. The DPMI host's figure is the honest answer here. */
u32 KAPI GlobalCompact(u32 min_free)                          /* KERNEL.25 */
{
    (void)min_free;
    return 0x00100000UL;                /* UNVERIFIED: placeholder 1 MB */
}
