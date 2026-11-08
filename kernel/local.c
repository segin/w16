/*
 * local.c - local heap (LocalInit, LocalAlloc and friends).
 *
 * A local heap lives inside one segment (normally a task's or DLL's
 * automatic data segment) and is addressed with near pointers. The Local*
 * APIs work on the heap in the caller's DS; kentry.asm's LOCALAPI wrappers
 * pass that DS to the C functions here as their first argument.
 *
 * Everything below was observed black-box on Windows 3.1 (Standard and
 * 386 Enhanced mode agree) with tests/apps/tk3.c, by looking at the
 * program's own data segment after the calls. Field names follow published
 * descriptions of the heap (e.g. Pietrek, "Windows Internals"); offsets and
 * values are what was observed. ARCHITECTURE.md 9.2 lists the findings.
 *
 * Instance data: word 06h of the segment points to the heap information
 * block (pHeap).
 *
 * Arenas: the heap is a doubly linked list of 4-aligned arenas, from a
 * first to a last sentinel. Every arena starts with
 *     +0  prev arena | flags (bit 0 busy, bit 1 moveable)
 *     +2  next arena (the size of an arena is next - this)
 * A free arena continues with
 *     +4  size, +6 previous free arena, +8 next free arena
 * and the free list runs in address order from the first sentinel to the
 * last. The sentinels are busy fixed arenas of size 0Ah whose free-list
 * links close the list (first: prev free = itself, last: next free =
 * itself). Fixed blocks start at arena+4, so their handle is their near
 * pointer (always a multiple of 4). Moveable blocks keep their handle at
 * arena+4 and start at arena+6.
 *
 * Arena sizes: max(0Ch, round4(n + 4)) for fixed and max(0Ch,
 * round4(n + 6)) for moveable blocks; LocalSize returns the arena size
 * less 4 or 6. A free arena is split only if at least 14h bytes remain;
 * otherwise the block gets all of it. Fixed blocks are taken from the
 * lowest free arena that fits, moveable blocks from the top of the highest
 * one. Freed arenas merge with free neighbours; freed memory keeps its
 * contents except for the free-arena fields.
 *
 * The information block is itself the data of a busy fixed arena of size
 * 30h right after the first sentinel (see LI_* below).
 *
 * Moveable handles point into handle tables: fixed blocks of a count word
 * (hdelta = 20h entries), the 4-byte entries, and a word linking to the
 * previously made table (htable is the newest). Handles are therefore
 * 2 mod 4. An entry is
 *     +0  near address of the data (0 when discarded)
 *     +2  flags byte (LMEM_DISCARDABLE >> 8 bits, 40h discarded),
 *     +3  lock count
 * A free entry holds the next free entry and FFFFh; a new table's entries
 * are chained in ascending order, freed handles are pushed on the front.
 * The handle is taken (and a table made) before the block is allocated.
 *
 * Growing: when a request does not fit, the segment is reallocated to
 * round32(GlobalSize + arena size + extra + 18h) and the new space joins
 * the heap end. (That 18h is the observed slack; any value in 15h..18h
 * gives the same sizes, since arenas are multiples of 4.)
 */
#include "kernel.h"

/* ---- heap information block (offsets from pHeap) ---- */
#define LI_CHECK        0x00
#define LI_FREEZE       0x02
#define LI_ITEMS        0x04    /* number of arenas */
#define LI_FIRST        0x06    /* first sentinel */
#define LI_LAST         0x0A    /* last sentinel */
#define LI_NCOMPACT     0x0E    /* byte */
#define LI_DISLEVEL     0x0F    /* byte */
#define LI_DISTOTAL     0x10    /* dword */
#define LI_HTABLE       0x14    /* newest handle table */
#define LI_HFREE        0x16    /* first free handle */
#define LI_HDELTA       0x18    /* entries per new handle table */
#define LI_EXPAND       0x1A
#define LI_PSTATS       0x1C
#define LI_NOTIFY       0x1E    /* far pointer, LocalNotify procedure */
#define LI_LOCK         0x22
#define LI_EXTRA        0x24    /* extra bytes when growing */
#define LI_MINSIZE      0x26    /* last - first + 0Ah */
#define LI_MAGIC        0x28
#define LI_SIZE         0x2A

#define LH_MAGIC        0x484C  /* "LH" */
#define LH_HDELTA       0x0020
#define LH_EXTRA        0x0200

/* ---- arenas ---- */
#define LA_BUSY         0x0001
#define LA_MOVEABLE     0x0002
#define LA_FLAGS        0x0003
#define LA_PREV         0
#define LA_NEXT         2
#define LA_SIZE         4       /* free arenas */
#define LA_FREEPREV     6
#define LA_FREENEXT     8
#define LA_HANDLE       4       /* moveable arenas */

#define SENTINEL_SIZE   0x000A
#define MIN_ARENA       0x000C
#define SPLIT_MIN       0x0014  /* smallest remainder that is split off */
#define GROW_SLOP       0x0018

/* ---- handle table entries ---- */
#define HE_ADDR         0
#define HE_FLAGS        2       /* byte */
#define HE_LOCK         3       /* byte */
#define HE_FREE         0xFFFF  /* word at +2 of a free entry */
#define HF_DISCARDABLE  0x0F
#define HF_DISCARDED    0x40

#define LMEM_FIXED      0x0000
#define LMEM_MOVEABLE   0x0002
#define LMEM_NOCOMPACT  0x0010
#define LMEM_NODISCARD  0x0020
#define LMEM_ZEROINIT   0x0040
#define LMEM_MODIFY     0x0080
#define LMEM_DISCARDABLE 0x0F00

/* Segments hold at most 64K: offsets are 16-bit (format bound). */
#define SEG_MAX         0x10000UL

typedef struct lheap {
    u16 seg;
    u16 info;                   /* pHeap */
} lheap;

#define W(s, o)  (*(u16 FAR *)MK_FP((s), (o)))
#define B(s, o)  (*(u8 FAR *)MK_FP((s), (o)))
#define I(hp, f) W((hp)->seg, (hp)->info + (f))

/* limit of a selector (LSL); returns 0 if it is not accessible */
static u16 sel_limit(u16 sel);
#pragma aux sel_limit = \
    "xor ax, ax" \
    "lsl ax, dx" \
    parm [dx] value [ax] modify exact [ax];

static void kmemmove(u16 seg, u16 to, u16 from, u16 n)
{
    u8 FAR *d = MK_FP(seg, to);
    const u8 FAR *s = MK_FP(seg, from);
    if (to < from) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
}

static int heap_of(u16 seg, lheap *hp)
{
    u16 info;
    if (!seg || sel_limit(seg) < 0x0F)
        return 0;
    info = W(seg, 6);
    if (!info || (u32)info + LI_SIZE > (u32)sel_limit(seg) + 1)
        return 0;
    hp->seg = seg;
    hp->info = info;
    return I(hp, LI_MAGIC) == LH_MAGIC;
}

/* ---- arena helpers ---- */

static u16 a_prev(const lheap *hp, u16 a) { return W(hp->seg, a + LA_PREV) & ~LA_FLAGS; }
static u16 a_next(const lheap *hp, u16 a) { return W(hp->seg, a + LA_NEXT); }
static u16 a_flags(const lheap *hp, u16 a) { return W(hp->seg, a + LA_PREV) & LA_FLAGS; }
static u16 a_size(const lheap *hp, u16 a) { return a_next(hp, a) - a; }
static int a_free(const lheap *hp, u16 a) { return !(a_flags(hp, a) & LA_BUSY); }

static void set_prev(const lheap *hp, u16 a, u16 prev, u16 flags)
{
    W(hp->seg, a + LA_PREV) = prev | flags;
}

/* Is `a` the start of an arena of this heap (structurally consistent)? */
static int is_arena(const lheap *hp, u16 a)
{
    u16 first = I(hp, LI_FIRST), last = I(hp, LI_LAST), n, p;
    if (a & 3 || a <= first || a >= last)
        return 0;
    n = a_next(hp, a);
    p = a_prev(hp, a);
    if (n <= a || n > last || p >= a || p < first)
        return 0;
    return a_next(hp, p) == a && a_prev(hp, n) == a;
}

static u16 arena_need(u16 n, int moveable)
{
    u32 s = ((u32)n + (moveable ? 6 : 4) + 3) & ~3UL;
    if (s < MIN_ARENA)
        s = MIN_ARENA;
    return s > 0xFFF0UL ? 0 : (u16)s;
}

/* free list, address ordered, closed by the sentinels */
static void fl_unlink(const lheap *hp, u16 a)
{
    u16 p = W(hp->seg, a + LA_FREEPREV), n = W(hp->seg, a + LA_FREENEXT);
    W(hp->seg, p + LA_FREENEXT) = n;
    W(hp->seg, n + LA_FREEPREV) = p;
}

static void fl_link_after(const lheap *hp, u16 p, u16 a)
{
    u16 n = W(hp->seg, p + LA_FREENEXT);
    W(hp->seg, a + LA_FREEPREV) = p;
    W(hp->seg, a + LA_FREENEXT) = n;
    W(hp->seg, p + LA_FREENEXT) = a;
    W(hp->seg, n + LA_FREEPREV) = a;
}

/* the nearest free arena below `a`, or the first sentinel */
static u16 free_below(const lheap *hp, u16 a)
{
    u16 first = I(hp, LI_FIRST);
    do
        a = a_prev(hp, a);
    while (a != first && !a_free(hp, a));
    return a;
}

/* Turn busy arena `a` into a free one and merge it with free neighbours.
   Returns the resulting free arena. */
static u16 release(const lheap *hp, u16 a)
{
    u16 n = a_next(hp, a), p = a_prev(hp, a);

    set_prev(hp, a, p, 0);
    fl_link_after(hp, free_below(hp, a), a);
    if (a_free(hp, n) && n != I(hp, LI_LAST)) {
        fl_unlink(hp, n);
        W(hp->seg, a + LA_NEXT) = a_next(hp, n);
        set_prev(hp, a_next(hp, n), a, a_flags(hp, a_next(hp, n)));
        I(hp, LI_ITEMS)--;
    }
    if (a_free(hp, p) && p != I(hp, LI_FIRST)) {
        fl_unlink(hp, a);
        W(hp->seg, p + LA_NEXT) = a_next(hp, a);
        set_prev(hp, a_next(hp, a), p, a_flags(hp, a_next(hp, a)));
        I(hp, LI_ITEMS)--;
        a = p;
    }
    W(hp->seg, a + LA_SIZE) = a_size(hp, a);
    return a;
}

/* Split busy arena `a` so that it keeps `keep` bytes, if enough remains. */
static void trim(const lheap *hp, u16 a, u16 keep)
{
    u16 rest, n;
    if (a_size(hp, a) - keep < SPLIT_MIN)
        return;
    n = a_next(hp, a);
    rest = a + keep;
    W(hp->seg, rest + LA_NEXT) = n;
    set_prev(hp, rest, a, LA_BUSY);
    set_prev(hp, n, rest, a_flags(hp, n));
    W(hp->seg, a + LA_NEXT) = rest;
    I(hp, LI_ITEMS)++;
    release(hp, rest);
}

/* Take `need` bytes from free arena `f`: fixed from its bottom, moveable
   from its top. Returns the new busy arena. Observed in the data of new
   blocks: the free arena is first split in two (the lower part's size
   becomes its new size, the upper part is linked into the free list after
   it), then the part wanted is unlinked; its free-arena fields stay. */
static u16 take(const lheap *hp, u16 f, u16 need, int moveable)
{
    u16 size = a_size(hp, f), at, n, a;
    u16 fl = LA_BUSY | (moveable ? LA_MOVEABLE : 0);

    if (size - need >= SPLIT_MIN) {
        at = moveable ? f + size - need : f + need;
        n = a_next(hp, f);
        set_prev(hp, at, f, 0);
        W(hp->seg, at + LA_NEXT) = n;
        set_prev(hp, n, at, a_flags(hp, n));
        W(hp->seg, f + LA_NEXT) = at;
        W(hp->seg, f + LA_SIZE) = at - f;
        W(hp->seg, at + LA_SIZE) = n - at;
        fl_link_after(hp, f, at);
        I(hp, LI_ITEMS)++;
        a = moveable ? at : f;
    } else {
        a = f;
    }
    fl_unlink(hp, a);
    set_prev(hp, a, a_prev(hp, a), fl);
    return a;
}

/* find a free arena of at least `need` bytes */
static u16 fit(const lheap *hp, u16 need, int moveable)
{
    u16 first = I(hp, LI_FIRST), last = I(hp, LI_LAST), f;
    if (!moveable) {
        for (f = W(hp->seg, first + LA_FREENEXT); f != last; f = W(hp->seg, f + LA_FREENEXT))
            if (a_size(hp, f) >= need)
                return f;
    } else {
        for (f = W(hp->seg, last + LA_FREEPREV); f != first; f = W(hp->seg, f + LA_FREEPREV))
            if (a_size(hp, f) >= need)
                return f;
    }
    return 0;
}

static u16 largest_free(const lheap *hp)
{
    u16 first = I(hp, LI_FIRST), last = I(hp, LI_LAST), f, m = 0;
    for (f = W(hp->seg, first + LA_FREENEXT); f != last; f = W(hp->seg, f + LA_FREENEXT))
        if (a_size(hp, f) > m)
            m = a_size(hp, f);
    return m;
}

static u16 h_lock(const lheap *hp, u16 h) { return B(hp->seg, h + HE_LOCK); }
static u16 h_flags(const lheap *hp, u16 h) { return B(hp->seg, h + HE_FLAGS); }

/* Move unlocked moveable blocks up into free space directly above them
   (observed: LocalCompact moves a block into the hole above it).
   UNVERIFIED: the order in which Windows visits blocks. */
static void compact(const lheap *hp)
{
    u16 first = I(hp, LI_FIRST), a, f, s, fs, n, p, h;
    int moved;

    do {
        moved = 0;
        for (a = a_prev(hp, I(hp, LI_LAST)); a != first; a = a_prev(hp, a)) {
            if (a_flags(hp, a) != (LA_BUSY | LA_MOVEABLE))
                continue;
            f = a_next(hp, a);
            h = W(hp->seg, a + LA_HANDLE);
            if (!a_free(hp, f) || h_lock(hp, h))
                continue;
            s = a_size(hp, a);
            fs = a_size(hp, f);
            n = a_next(hp, f);
            p = a_prev(hp, a);
            fl_unlink(hp, f);
            kmemmove(hp->seg, a + fs, a, s);
            /* block now at a + fs, a free arena of fs bytes below it */
            set_prev(hp, a + fs, a, LA_BUSY | LA_MOVEABLE);
            W(hp->seg, a + fs + LA_NEXT) = n;
            set_prev(hp, n, a + fs, a_flags(hp, n));
            W(hp->seg, h + HE_ADDR) = a + fs + 6;
            set_prev(hp, a, p, LA_BUSY);
            W(hp->seg, a + LA_NEXT) = a + fs;
            release(hp, a);
            moved = 1;
            a = a + fs;
        }
    } while (moved);
}

static void discard_block(const lheap *hp, u16 h)
{
    u16 addr = W(hp->seg, h + HE_ADDR);
    if (addr)
        release(hp, addr - 6);
    W(hp->seg, h + HE_ADDR) = 0;
    B(hp->seg, h + HE_FLAGS) |= HF_DISCARDED;
}

/* discard every unlocked discardable block; returns whether any was.
   UNVERIFIED: whether Windows stops once the request would fit. */
static int discard_all(const lheap *hp)
{
    u16 first = I(hp, LI_FIRST), a, h;
    int any = 0, again;
    do {
        again = 0;
        for (a = a_prev(hp, I(hp, LI_LAST)); a != first; a = a_prev(hp, a)) {
            if (a_flags(hp, a) != (LA_BUSY | LA_MOVEABLE))
                continue;
            h = W(hp->seg, a + LA_HANDLE);
            if ((h_flags(hp, h) & HF_DISCARDABLE) && !h_lock(hp, h)) {
                discard_block(hp, h);   /* arenas merge: start over */
                any = again = 1;
                break;
            }
        }
    } while (again);
    return any;
}

/* Grow the heap's segment so that `need` more bytes fit (see top). */
static int grow(const lheap *hp, u16 need)
{
    u16 h = (u16)GlobalHandle(hp->seg);
    u32 old = GlobalSize(h), size;
    u16 last = I(hp, LI_LAST), nl, p;

    if (!h || !old)
        return 0;
    size = (old + need + I(hp, LI_EXTRA) + GROW_SLOP + 31) & ~31UL;
    /* UNVERIFIED: what Windows does when this passes 64K */
    if (size > SEG_MAX)
        size = SEG_MAX;
    nl = (u16)((size - 1 - SENTINEL_SIZE) & ~3UL);
    if (size <= old || nl <= last || nl - last < need || !GlobalReAlloc(h, size, 0))
        return 0;
    /* the old last sentinel becomes an arena; a new one ends the heap */
    p = W(hp->seg, last + LA_FREEPREV);
    set_prev(hp, nl, last, LA_BUSY);
    W(hp->seg, nl + LA_NEXT) = nl;
    W(hp->seg, nl + LA_SIZE) = SENTINEL_SIZE;
    W(hp->seg, nl + LA_FREEPREV) = p;
    W(hp->seg, nl + LA_FREENEXT) = nl;
    W(hp->seg, p + LA_FREENEXT) = nl;
    W(hp->seg, last + LA_NEXT) = nl;
    I(hp, LI_LAST) = nl;
    I(hp, LI_ITEMS)++;
    set_prev(hp, last, a_prev(hp, last), LA_BUSY);
    release(hp, last);
    return 1;
}

/* Allocate an arena of `need` bytes, compacting, discarding and growing
   as needed. UNVERIFIED: Windows' exact order of these steps; growing
   (verified) was observed only where nothing could be compacted. */
static u16 arena_alloc(const lheap *hp, u16 need, int moveable, u16 flags)
{
    u16 f = fit(hp, need, moveable);
    if (!f && !(flags & LMEM_NOCOMPACT)) {
        compact(hp);
        f = fit(hp, need, moveable);
    }
    if (!f && !(flags & LMEM_NODISCARD) && discard_all(hp)) {
        compact(hp);
        f = fit(hp, need, moveable);
    }
    if (!f && grow(hp, need))
        f = fit(hp, need, moveable);
    return f ? take(hp, f, need, moveable) : 0;
}

/* ---- handles ---- */

static int valid_handle(const lheap *hp, u16 h)
{
    u16 addr;
    if ((h & 3) != 2 || h <= I(hp, LI_FIRST) || h >= I(hp, LI_LAST))
        return 0;
    if (W(hp->seg, h + HE_FLAGS) == HE_FREE)
        return 0;
    addr = W(hp->seg, h + HE_ADDR);
    if (!addr)
        return (h_flags(hp, h) & HF_DISCARDED) != 0;
    return (addr & 3) == 2 && is_arena(hp, addr - 6) &&
           a_flags(hp, addr - 6) == (LA_BUSY | LA_MOVEABLE) &&
           W(hp->seg, addr - 6 + LA_HANDLE) == h;
}

static int valid_fixed(const lheap *hp, u16 p)
{
    return !(p & 3) && is_arena(hp, p - 4) && a_flags(hp, p - 4) == LA_BUSY;
}

static u16 new_handle(const lheap *hp, u16 flags)
{
    u16 h = I(hp, LI_HFREE);
    if (!h) {
        u16 n = I(hp, LI_HDELTA), t, i;
        u16 need = arena_need(2 + 4 * n + 2, 0);
        if (!n || !need || !(t = arena_alloc(hp, need, 0, flags)))
            return 0;
        t += 4;
        W(hp->seg, t) = n;
        for (i = 0; i < n; i++) {
            W(hp->seg, t + 2 + 4 * i + HE_ADDR) = i + 1 < n ? t + 2 + 4 * (i + 1) : 0;
            W(hp->seg, t + 2 + 4 * i + HE_FLAGS) = HE_FREE;
        }
        W(hp->seg, t + 2 + 4 * n) = I(hp, LI_HTABLE);
        I(hp, LI_HTABLE) = t;
        h = t + 2;
    }
    I(hp, LI_HFREE) = W(hp->seg, h + HE_ADDR);
    return h;
}

static void free_handle(const lheap *hp, u16 h)
{
    W(hp->seg, h + HE_ADDR) = I(hp, LI_HFREE);
    W(hp->seg, h + HE_FLAGS) = HE_FREE;
    I(hp, LI_HFREE) = h;
}

/* ---- LocalInit ---- */

/* Default LocalNotify procedure, stored in new heaps' information block.
   UNVERIFIED: what the Windows one does; w16 never calls it yet. */
static u16 __far __pascal notify_default(u16 msg, u16 h, u16 arg)
{
    (void)msg;
    (void)h;
    (void)arg;
    return 0;
}

/* Observed: start is rounded up to 4; the last sentinel goes at
   (end - 0Ah) & ~3; with start 0 the heap is the last `end` bytes of the
   segment. The first sentinel (0Ch bytes), the information arena (30h)
   and one free arena follow each other. */
static u16 local_init(u16 seg, u16 start, u16 end)
{
    lheap hp;
    u16 first, last, ia, fr;
    u32 segsize = (u32)sel_limit(seg) + 1;

    if (!seg || segsize < 0x10)
        return 0;
    if (start == 0) {
        if (end > segsize)
            return 0;                   /* UNVERIFIED */
        start = (u16)(segsize - end);
        end = (u16)(segsize - 1);
    }
    first = (start + 3) & ~3;
    if (end >= segsize || end < SENTINEL_SIZE || first < start)
        return 0;
    last = (end - SENTINEL_SIZE) & ~3;
    ia = first + MIN_ARENA;
    fr = ia + 4 + ((LI_SIZE + 3) & ~3);
    /* UNVERIFIED: Windows 3.1 faulted on a heap this small instead */
    if (last < first || last - first < (fr - first) + MIN_ARENA)
        return 0;

    hp.seg = seg;
    hp.info = ia + 4;
    kmemset(MK_FP(seg, first), 0, fr + LA_FREENEXT + 2 - first);
    /* first sentinel */
    set_prev(&hp, first, first, LA_BUSY);
    W(seg, first + LA_NEXT) = ia;
    W(seg, first + LA_SIZE) = SENTINEL_SIZE;
    W(seg, first + LA_FREEPREV) = first;
    W(seg, first + LA_FREENEXT) = fr;
    /* information block */
    set_prev(&hp, ia, first, LA_BUSY);
    W(seg, ia + LA_NEXT) = fr;
    I(&hp, LI_ITEMS) = 4;
    I(&hp, LI_FIRST) = first;
    I(&hp, LI_LAST) = last;
    I(&hp, LI_HDELTA) = LH_HDELTA;
    *(u32 FAR *)MK_FP(seg, hp.info + LI_NOTIFY) = (u32)notify_default;
    I(&hp, LI_EXTRA) = LH_EXTRA;
    I(&hp, LI_MINSIZE) = last - first + SENTINEL_SIZE;
    I(&hp, LI_MAGIC) = LH_MAGIC;
    /* the free arena */
    set_prev(&hp, fr, ia, 0);
    W(seg, fr + LA_NEXT) = last;
    W(seg, fr + LA_SIZE) = last - fr;
    W(seg, fr + LA_FREEPREV) = first;
    W(seg, fr + LA_FREENEXT) = last;
    /* last sentinel */
    set_prev(&hp, last, fr, LA_BUSY);
    W(seg, last + LA_NEXT) = last;
    W(seg, last + LA_SIZE) = SENTINEL_SIZE;
    W(seg, last + LA_FREEPREV) = fr;
    W(seg, last + LA_FREENEXT) = last;

    W(seg, 6) = hp.info;
    return 1;
}

/* InitTask: the task's heap is the last `heap` bytes of its data segment
   (observed: the same layout as LocalInit(DS, 0, heap)). */
int local_init_task(u16 ds, u16 heap)
{
    return local_init(ds, 0, heap);
}

/* ---- API (called through LOCALAPI in kentry.asm; seg = caller's DS) ---- */

u16 KAPI LInit(u16 ds, u16 seg, u16 start, u16 end)          /* KERNEL.4 */
{
    return local_init(seg ? seg : ds, start, end);
}

u16 KAPI LAlloc(u16 ds, u16 flags, u16 n)                     /* KERNEL.5 */
{
    lheap hp;
    u16 need, a, h;
    int moveable = (flags & LMEM_MOVEABLE) != 0;

    if (!heap_of(ds, &hp))
        return 0;
    need = arena_need(n, moveable);
    if (!need)
        return 0;
    if (!moveable) {
        if (!n)                         /* observed: fails */
            return 0;
        a = arena_alloc(&hp, need, 0, flags);
        if (!a)
            return 0;
        if (flags & LMEM_ZEROINIT)
            kmemset(MK_FP(ds, a + 4), 0, a_size(&hp, a) - 4);
        return a + 4;
    }
    h = new_handle(&hp, flags);
    if (!h)
        return 0;
    W(ds, h + HE_ADDR) = 0;
    B(ds, h + HE_FLAGS) = (u8)((flags & LMEM_DISCARDABLE) >> 8);
    B(ds, h + HE_LOCK) = 0;
    if (!n) {                           /* observed: a discarded block */
        B(ds, h + HE_FLAGS) |= HF_DISCARDED;
        return h;
    }
    a = arena_alloc(&hp, need, 1, flags);
    if (!a) {
        free_handle(&hp, h);
        return 0;
    }
    W(ds, a + LA_HANDLE) = h;
    W(ds, h + HE_ADDR) = a + 6;
    if (flags & LMEM_ZEROINIT)
        kmemset(MK_FP(ds, a + 6), 0, a_size(&hp, a) - 6);
    return h;
}

/* Resize busy arena `a` in place if possible; returns whether it is now
   at least `need` bytes. */
static int resize_in_place(const lheap *hp, u16 a, u16 need)
{
    u16 n = a_next(hp, a);
    if (a_size(hp, a) >= need) {
        trim(hp, a, need);
        return 1;
    }
    if (!a_free(hp, n) || n == I(hp, LI_LAST) || a_size(hp, a) + a_size(hp, n) < need)
        return 0;
    fl_unlink(hp, n);
    W(hp->seg, a + LA_NEXT) = a_next(hp, n);
    set_prev(hp, a_next(hp, n), a, a_flags(hp, a_next(hp, n)));
    I(hp, LI_ITEMS)--;
    trim(hp, a, need);
    return 1;
}

u16 KAPI LReAlloc(u16 ds, u16 h, u16 n, u16 flags)            /* KERNEL.6 */
{
    lheap hp;
    u16 a, b, need, old;

    if (!heap_of(ds, &hp))
        return 0;
    if (h & 2) {
        if (!valid_handle(&hp, h))
            return 0;
        if (flags & LMEM_MODIFY) {
            /* UNVERIFIED: whether MODIFY without DISCARDABLE clears it */
            B(ds, h + HE_FLAGS) = (u8)((h_flags(&hp, h) & ~HF_DISCARDABLE) |
                                       ((flags & LMEM_DISCARDABLE) >> 8));
            return h;
        }
        if (!n) {
            /* observed: discards any unlocked moveable block, but only
               when LMEM_MOVEABLE is given */
            if (!(flags & LMEM_MOVEABLE) || h_lock(&hp, h))
                return 0;
            discard_block(&hp, h);
            return h;
        }
        need = arena_need(n, 1);
        if (!need)
            return 0;
        a = W(ds, h + HE_ADDR);
        if (!a) {                       /* discarded: allocate again */
            b = arena_alloc(&hp, need, 1, flags);
            if (!b)
                return 0;
            W(ds, b + LA_HANDLE) = h;
            W(ds, h + HE_ADDR) = b + 6;
            B(ds, h + HE_FLAGS) &= ~HF_DISCARDED;
            if (flags & LMEM_ZEROINIT)  /* UNVERIFIED */
                kmemset(MK_FP(ds, b + 6), 0, a_size(&hp, b) - 6);
            return h;
        }
        a -= 6;
        old = a_size(&hp, a);
        /* observed: a moveable block moves when it cannot grow in place,
           even while locked and without LMEM_MOVEABLE. The new place is
           found while the old block still exists. */
        if (!resize_in_place(&hp, a, need)) {
            /* UNVERIFIED: compaction during this allocation may move
               the block being resized; re-read its address afterwards */
            b = arena_alloc(&hp, need, 1, flags);
            if (!b)
                return 0;
            a = W(ds, h + HE_ADDR) - 6;
            kmemcpy(MK_FP(ds, b + 6), MK_FP(ds, a + 6), old - 6);
            W(ds, b + LA_HANDLE) = h;
            W(ds, h + HE_ADDR) = b + 6;
            release(&hp, a);
            a = b;
        }
        if ((flags & LMEM_ZEROINIT) && a_size(&hp, a) > old)
            kmemset(MK_FP(ds, a + old), 0, a_size(&hp, a) - old);
        return h;
    }

    if (!valid_fixed(&hp, h))
        return 0;
    if (flags & LMEM_MODIFY)            /* observed: no effect on fixed */
        return h;
    a = h - 4;
    if (!n) {
        if (flags & LMEM_MOVEABLE)      /* observed: frees the block */
            release(&hp, a);
        return 0;
    }
    need = arena_need(n, 0);
    if (!need)
        return 0;
    old = a_size(&hp, a);
    if (!resize_in_place(&hp, a, need)) {
        if (!(flags & LMEM_MOVEABLE))   /* observed: fails */
            return 0;
        b = arena_alloc(&hp, need, 0, flags);
        if (!b)
            return 0;
        kmemcpy(MK_FP(ds, b + 4), MK_FP(ds, a + 4), old - 4);
        release(&hp, a);
        a = b;
    }
    if ((flags & LMEM_ZEROINIT) && a_size(&hp, a) > old)
        kmemset(MK_FP(ds, a + old), 0, a_size(&hp, a) - old);
    return a + 4;
}

/* Observed: returns 0 whether or not the handle was valid, and frees
   locked blocks too. */
u16 KAPI LFree(u16 ds, u16 h)                                 /* KERNEL.7 */
{
    lheap hp;
    if (!heap_of(ds, &hp))
        return 0;
    if (h & 2) {
        if (valid_handle(&hp, h)) {
            if (W(ds, h + HE_ADDR))
                release(&hp, W(ds, h + HE_ADDR) - 6);
            free_handle(&hp, h);
        }
    } else if (valid_fixed(&hp, h)) {
        release(&hp, h - 4);
    }
    return 0;
}

/* Observed: the lock count stops at FFh and then stays there. */
u16 KAPI LLock(u16 ds, u16 h)                                 /* KERNEL.8 */
{
    lheap hp;
    if (!heap_of(ds, &hp))
        return 0;
    if (h & 2) {
        if (!valid_handle(&hp, h) || !W(ds, h + HE_ADDR))
            return 0;
        if (h_lock(&hp, h) < 0xFF)
            B(ds, h + HE_LOCK)++;
        return W(ds, h + HE_ADDR);
    }
    return valid_fixed(&hp, h) ? h : 0;
}

/* Observed: returns the new lock count (0 for fixed blocks). */
u16 KAPI LUnlock(u16 ds, u16 h)                               /* KERNEL.9 */
{
    lheap hp;
    u16 l;
    if (!heap_of(ds, &hp) || !(h & 2) || !valid_handle(&hp, h))
        return 0;
    l = h_lock(&hp, h);
    if (l && l < 0xFF)
        B(ds, h + HE_LOCK) = (u8)--l;
    return l;
}

u16 KAPI LSize(u16 ds, u16 h)                                 /* KERNEL.10 */
{
    lheap hp;
    if (!heap_of(ds, &hp))
        return 0;
    if (h & 2) {
        if (!valid_handle(&hp, h) || !W(ds, h + HE_ADDR))
            return 0;
        return a_size(&hp, W(ds, h + HE_ADDR) - 6) - 6;
    }
    return valid_fixed(&hp, h) ? a_size(&hp, h - 4) - 4 : 0;
}

/* Observed: the handle for a block's address; 0 for anything else
   (including a moveable handle itself). */
u16 KAPI LHandle(u16 ds, u16 p)                               /* KERNEL.11 */
{
    lheap hp;
    u16 h;
    if (!heap_of(ds, &hp))
        return 0;
    if (p & 2) {
        if ((p & 3) != 2 || p < 2)
            return 0;
        h = W(ds, p - 2);
        return valid_handle(&hp, h) && W(ds, h + HE_ADDR) == p ? h : 0;
    }
    return valid_fixed(&hp, p) ? p : 0;
}

/* Observed: flags byte in the high byte, lock count in the low one;
   0 for fixed blocks. */
u16 KAPI LFlags(u16 ds, u16 h)                                /* KERNEL.12 */
{
    lheap hp;
    if (!heap_of(ds, &hp) || !(h & 2) || !valid_handle(&hp, h))
        return 0;
    return ((u16)h_flags(&hp, h) << 8) | h_lock(&hp, h);
}

/* Observed: moves unlocked moveable blocks up; discards unlocked
   discardable blocks only if the largest free arena is still smaller
   than requested; returns the largest free arena less 6. The bytes at
   +0Eh/+0Fh read 00h/10h after a call whose request was met without
   discarding and FFh/00h after one that was not (even with nothing to
   discard). UNVERIFIED: what they mean. */
u16 KAPI LCompact(u16 ds, u16 want)                           /* KERNEL.13 */
{
    lheap hp;
    u16 m;
    int discarding;

    if (!heap_of(ds, &hp))
        return 0;
    compact(&hp);
    m = largest_free(&hp);
    discarding = (m < 6 ? 0 : m - 6) < want;
    if (discarding && discard_all(&hp)) {
        compact(&hp);
        m = largest_free(&hp);
    }
    B(ds, hp.info + LI_NCOMPACT) = discarding ? 0xFF : 0x00;
    B(ds, hp.info + LI_DISLEVEL) = discarding ? 0x00 : 0x10;
    return m < 6 ? 0 : m - 6;
}
