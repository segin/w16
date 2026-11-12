/*
 * global.c - the global heap, in the layout of Windows 3.1's KRNL286
 * (Standard mode).
 *
 * Observed black-box on Windows 3.1 with tests/apps/tk19.c (KRNL286 in
 * Standard mode; see ARCHITECTURE.md 9.2 for how). The structures are the
 * ones published in Schulman et al., "Undocumented Windows" (1992), ch. 5
 * ("Global Arena", "Global Heap", "Selector Table", "THHOOK") and Pietrek,
 * "Windows Internals" (1993), ch. 2; every field used here was checked on
 * Windows 3.1 by the probe. Facts not checked are marked UNVERIFIED.
 *
 * Layout:
 *  - Every block is preceded in linear memory by its arena, a paragraph
 *    with a selector of its own (limit 0Fh):
 *      00 byte  'M' (4Dh) for fixed and free blocks and the sentinels but
 *               the last ('Z'); the lock count of a moveable block
 *               (GlobalLock of a discardable block, GlobalFix, GlobalWire)
 *      01 word  owner: a PDB, a module, the block itself (a task database),
 *               0 free, FFFFh sentinel, FFFDh Burgermaster, FFFCh "not
 *               there" (the ends of a piece of linear memory)
 *      03 word  size of the data in paragraphs; arena and data together
 *               are an even number of paragraphs, so GlobalSize is an odd
 *               multiple of 10h (GlobalAlloc(1): 10h, (100): 70h)
 *      05 byte  flags: 04h DGROUP, 08h discardable code, 40h GMEM_NOTIFY
 *      06 word  previous arena, 08 word next arena (address order; the
 *               first sentinel's previous and the last's next are itself)
 *      0A word  handle (0 when free)
 *      0C word  previous, 0E word next: a free block's neighbours in the
 *               list of free blocks (address order, from the first
 *               sentinel's 0Eh to the last's 0Ch; the sentinels' other
 *               link is FFFFh); a discardable block's neighbours in the
 *               LRU ring (0 for other blocks)
 *  - The Burgermaster (GlobalMasterHandle) is the first block after the
 *    first sentinel, a fixed block owned by FFFDh: the header below, then
 *    the selector table at 72h (THHOOK 16h/18h give its length and
 *    offset): one word per LDT entry (index = selector >> 3), the arena of
 *    the block whose (first) selector that is, the owner of a discarded
 *    block, else 0.
 *  - The heap is made of pieces of linear memory ("chunks"), each bounded
 *    by two "not there" arenas of size 0 (the first chunk starts with the
 *    first sentinel and the last ends with the last sentinel, both of size
 *    1). 3.1's DOSX gave KRNL286 conventional memory and 1 MB pieces of
 *    extended memory at start-up; w16 asks DPMI for chunks as it needs
 *    them (below).
 *  - Handles: a fixed block's handle is its selector (odd); a moveable
 *    block's is its selector with bit 0 clear. Blocks over 64 KB have one
 *    selector per 64 KB tile, __AHINCR apart; tile i's limit is the size
 *    less i * 64 KB, less 1 (observed; the first tile covers the whole
 *    block), and only the first is in the selector table.
 *  - A discarded block keeps its handle: its descriptor is not present
 *    (access 73h for a ring-3 data segment) with the owner as its limit,
 *    and the selector table holds the owner. It has no arena. (w16 keeps
 *    its flags and the number of tiles in the descriptor's base, which is
 *    not observable through the APIs.)
 *
 * Behaviour observed (tk19.c):
 *  - GlobalAlloc(GMEM_MOVEABLE, 0) gives a discarded block (GlobalFlags
 *    4000h); GlobalAlloc(GMEM_FIXED, 0) fails.
 *  - GMEM_SHARE blocks are owned by the calling module, others by the
 *    current task's PDB (Pietrek; observed for a program).
 *  - GlobalFlags = flags << 8 (DGROUP 0400h, discardable code 0800h; not
 *    GMEM_NOTIFY) | 0100h if discardable | 4000h if discarded | the lock
 *    count of a moveable block. Discardable blocks are the ones in the LRU
 *    ring, fixed ones included (GMEM_FIXED | GMEM_DISCARDABLE), though
 *    only moveable ones are discarded.
 *  - GlobalLock counts only for discardable blocks; GlobalUnlock likewise,
 *    returning the new count. GlobalFix/GlobalUnfix count for every
 *    moveable block, not for fixed ones. GlobalWire = GlobalFix and moving
 *    the block to the lowest free place; GlobalUnwire = GlobalUnfix,
 *    returning FFFFh when the count reaches 0, else 0.
 *  - GlobalFree returns 0, also for locked blocks (freed), for handles
 *    already freed and for non-handles.
 *  - GlobalReAlloc: MODIFY puts a block into the LRU ring (at the newest
 *    end) or takes it out; size 0 with GMEM_MOVEABLE discards an unlocked
 *    discardable block (returns the handle) and returns 0 for others; a
 *    discarded block gets memory again under its handle (still
 *    discardable); a block grows in place into following free space, else
 *    a moveable block moves, and a fixed one only with GMEM_MOVEABLE (its
 *    selector stays); otherwise 0. A moveable block in the way of a
 *    growing block is moved instead of it.
 *  - New discardable blocks go to the newest end of the LRU ring (its head,
 *    Burgermaster 1Ah); GlobalLRUNewest/Oldest move a block to either end.
 *  - Discarding calls the GlobalNotify procedure of the owner's task for a
 *    GMEM_NOTIFY block, with the handle and DS = ES = SS; if it returns 0
 *    the block stays (also for GlobalReAlloc(h, 0, GMEM_MOVEABLE), which
 *    then returns 0). Blocks without GMEM_NOTIFY are discarded silently;
 *    a discarded block that is reallocated has lost GMEM_NOTIFY. The
 *    procedure is kept at TDB 2Eh (a pointer with a bad selector is
 *    ignored).
 *  - GlobalCompact(n) moves moveable blocks down into free space and
 *    returns the size of the largest free block less 30h; GlobalCompact(-1)
 *    discards every unlocked discardable block.
 *  - When an allocation does not fit, unlocked discardable blocks are
 *    discarded, oldest first (3.1 discarded all three of the probe's at
 *    once; w16 discards one at a time until the block fits).
 *  - TOOLHELP reads these structures: GlobalInfo.wcItems is the
 *    Burgermaster's count + 1; GlobalFirst/Next report the arena's address,
 *    GlobalSize, arena byte 0 as wcLock, the flags (| GF_PDB_OWNER for a
 *    PDB's block) and the next arena's selector.
 *  - Fixed and moveable blocks are allocated from the lowest free space
 *    that fits (3.1 also moves moveable blocks up out of a fixed block's
 *    way, and puts discardable code at the top of the heap above a fence:
 *    not done by w16, UNVERIFIED in detail).
 *
 * Where w16 differs on purpose: the heap grows on demand by DPMI (0501h)
 * chunks before it compacts or discards (as Pietrek describes for 386
 * Enhanced mode), and GlobalCompact/GetFreeSpace count what DPMI can still
 * give. Memory below 1 MB (GlobalDOSAlloc-style requests, task databases)
 * comes from separate DOS-memory chunks (DPMI 0100h), which other blocks
 * do not use while there is extended memory.
 */
#include "kernel.h"

#pragma pack(push, 1)
typedef struct arena {
    u8  count;          /* 00 */
    u16 owner;          /* 01 */
    u16 size;           /* 03 paragraphs */
    u8  flags;          /* 05 GA_* */
    u16 prev;           /* 06 */
    u16 next;           /* 08 */
    u16 handle;         /* 0A */
    u16 lprev;          /* 0C */
    u16 lnext;          /* 0E */
} arena;

/* the Burgermaster's header (Schulman, "Global Heap"; values observed) */
typedef struct bmhdr {
    u16 check;          /* 00 0 */
    u16 freeze;         /* 02 0 */
    u16 count;          /* 04 arenas in the list (observed) */
    u16 first;          /* 06 first sentinel */
    u16 last;           /* 08 last sentinel */
    u8  ncompact;       /* 0A */
    u8  dislevel;       /* 0B */
    u16 distotal;       /* 0C */
    u16 htable;         /* 0E 0 */
    u16 hfree;          /* 10 0 */
    u16 hdelta;         /* 12 20h (observed) */
    u16 hexpand;        /* 14 0 */
    u16 pstats;         /* 16 0 */
    u16 lrulock;        /* 18 0 */
    u16 lruchain;       /* 1A newest discardable block (observed) */
    u16 lrucount;       /* 1C blocks in the LRU ring (observed) */
    u16 reserve;        /* 1E paragraphs kept for discardable code */
    u16 disfence;       /* 20 */
    u16 freecount;      /* 22 free blocks (observed: TOOLHELP's
                           GlobalInfo.wcItemsFree) */
    u16 alt[8];         /* 24 FFFFh FFFFh, then 0 (observed; UNVERIFIED
                           meaning) */
} bmhdr;

/* what w16 keeps in the paragraph after a chunk's first arena */
typedef struct chunkinfo {
    u16 magic;          /* CHUNK_MAGIC */
    u16 kind;           /* CK_* */
    u32 dpmi;           /* DPMI memory block handle, or DOS selector */
    u32 size;           /* bytes from the first arena to the end */
    u32 lin;            /* linear address of the first arena */
} chunkinfo;
#pragma pack(pop)

W16_STATIC_ASSERT(arena_size, sizeof(arena) == 16);
W16_STATIC_ASSERT(bm_size, sizeof(bmhdr) == 0x34);

#define SIG_M           0x4D
#define SIG_Z           0x5A
#define OWN_SENTINEL    0xFFFF
#define OWN_BURGER      0xFFFD
#define OWN_NOTTHERE    0xFFFC
#define GA_DGROUP       0x04
#define GA_DISCCODE     0x08
#define GA_NOTIFY       0x40
#define GA_SHOWN        0x0C            /* flags GlobalFlags shows (<< 8) */

#define SELTAB_OFF      0x72            /* observed (THHOOK 18h) */
#define SELTAB_LEN      0x4000          /* one word per LDT entry (3.1:
                                           3FFCh) */
#define CHUNK_MAGIC     0x4843          /* "CH" */
#define CK_EXT          1
#define CK_DOS          2
#define CHUNK_GROW      0x100000UL      /* growth step: the size of the
                                           pieces 3.1's DOSX hands out */
#define DOS_GROW        0x1000UL        /* growth step below 1 MB */
#define ONE_MB          0x100000UL

/* the flags a discarded block keeps in its descriptor's base */
#define DB_DISC         0x01
#define DB_NOTIFY       0x02
#define DB_DGROUP       0x04
#define DB_DISCCODE     0x08

#define AR(a)           ((arena FAR *)MK_FP((a), 0))
#define TOTAL(a)        ((u16)(AR(a)->size + 1))        /* paragraphs */

static u16 bm;                          /* Burgermaster selector = handle */
static bmhdr boot_hdr;                  /* the header until bm exists */
static bmhdr FAR *hp = &boot_hdr;
static u16 tmp_a, tmp_b;                /* scratch selectors */
static u16 alias_sel;                   /* block of alias records */
static u16 kernel_owner;                /* owner of KERNEL's own blocks */

/* GlobalNotify: the procedure's slot in the task database (Pietrek,
   Table 3-2, 2Eh) */
#define TDB_GNOTIFY     0x2E

/* ---- selector helpers ---- */

static u16 lar(u16 s);
#pragma aux lar = "lar ax, ax" "jz l1" "xor ax, ax" "l1:" parm [ax] value [ax];

static u16 lsl(u16 s);
#pragma aux lsl = "lsl ax, ax" "jz l2" "xor ax, ax" "l2:" parm [ax] value [ax];

/* DPMI 0009h: access rights byte of a data descriptor (ring cpl) */
static void set_access(u16 s, u8 rights)
{
    regs16 r;
    r.ax = 0x0009;
    r.bx = s;
    r.cx = rights | (cpl << 5);
    r.ds = r.es = 0;
    dpmi31(&r);
}

static int present(u16 s)
{
    return (lar(s) & 0x8000) != 0;
}

static u32 lin_of(u16 s)
{
    return dpmi_get_base(s);
}

static u16 sel_new(u32 lin, u32 limit)
{
    u16 s = dpmi_alloc_desc(1);
    if (s && !dpmi_set_desc(s, lin, limit, 0)) {
        dpmi_free_desc(s);
        s = 0;
    }
    return s;
}

static u16 FAR *seltab(u16 s)
{
    return (u16 FAR *)MK_FP(bm, SELTAB_OFF + ((s >> 3) << 1));
}

static u16 tmp_at(u16 t, u32 lin, u32 limit)
{
    dpmi_set_desc(t, lin, limit, 0);
    return t;
}

static int is_free(u16 a)
{
    return AR(a)->handle == 0 && AR(a)->owner == 0;
}

/* ---- the list of arenas ---- */

static void link_after(u16 p, u16 a)
{
    u16 n = AR(p)->next;
    AR(a)->prev = p;
    AR(a)->next = n;
    AR(n)->prev = a;
    AR(p)->next = a;
    hp->count++;
}

static void unlink(u16 a)
{
    u16 p = AR(a)->prev, n = AR(a)->next;
    AR(p)->next = n;
    AR(n)->prev = p;
    hp->count--;
}

/* ---- the free list (address order, between the sentinels) ---- */

static void free_link(u16 a)
{
    u16 p = AR(a)->prev, n;
    while (p != hp->first && !is_free(p))
        p = AR(p)->prev;
    n = AR(p)->lnext;
    AR(a)->lprev = p;
    AR(a)->lnext = n;
    AR(p)->lnext = a;
    AR(n)->lprev = a;
    hp->freecount++;
}

static void free_unlink(u16 a)
{
    u16 p = AR(a)->lprev, n = AR(a)->lnext;
    AR(p)->lnext = n;
    AR(n)->lprev = p;
    AR(a)->lprev = AR(a)->lnext = 0;
    hp->freecount--;
}

/* b takes a's place in the free list */
static void free_replace(u16 a, u16 b)
{
    u16 p = AR(a)->lprev, n = AR(a)->lnext;
    AR(b)->lprev = p;
    AR(b)->lnext = n;
    AR(p)->lnext = b;
    AR(n)->lprev = b;
    AR(a)->lprev = AR(a)->lnext = 0;
}

/* ---- the LRU ring of discardable blocks (head: newest) ---- */

static int discardable(u16 a)
{
    return AR(a)->handle && AR(a)->lnext;
}

static void lru_add(u16 a, int newest)
{
    u16 h = hp->lruchain, t;
    if (!h) {
        AR(a)->lprev = AR(a)->lnext = a;
        hp->lruchain = a;
    } else {
        t = AR(h)->lprev;
        AR(a)->lnext = h;
        AR(a)->lprev = t;
        AR(t)->lnext = a;
        AR(h)->lprev = a;
        if (newest)
            hp->lruchain = a;
    }
    hp->lrucount++;
}

static void lru_remove(u16 a)
{
    u16 p = AR(a)->lprev, n = AR(a)->lnext;
    if (n == a) {
        hp->lruchain = 0;
    } else {
        AR(p)->lnext = n;
        AR(n)->lprev = p;
        if (hp->lruchain == a)
            hp->lruchain = n;
    }
    AR(a)->lprev = AR(a)->lnext = 0;
    hp->lrucount--;
}

/* ---- arenas ---- */

static void set_arena(u16 a, u8 count, u16 owner, u16 size, u8 flags, u16 handle)
{
    arena FAR *p = AR(a);
    p->count = count;
    p->owner = owner;
    p->size = size;
    p->flags = flags;
    p->handle = handle;
}

static void mark_free(u16 a)
{
    set_arena(a, SIG_M, 0, AR(a)->size, 0, 0);
    AR(a)->lprev = AR(a)->lnext = 0;
}

/* merge free arena b (right after a) into free arena a */
static void merge_next(u16 a)
{
    u16 b = AR(a)->next;
    free_unlink(b);
    AR(a)->size += TOTAL(b);
    unlink(b);
    dpmi_free_desc(b);
}

static void chunk_check_empty(u16 a);

/* a (taken out of the free list or busy and unlinked from the LRU)
   becomes free, merged with free neighbours */
static void release(u16 a)
{
    u16 p;
    mark_free(a);
    free_link(a);
    if (is_free(AR(a)->next))
        merge_next(a);
    p = AR(a)->prev;
    if (p != a && is_free(p)) {
        merge_next(p);
        a = p;
    }
    chunk_check_empty(a);
}

/* Take `tot` paragraphs (arena included) of free arena f, at its start
   (or its end if `high`). Returns the arena of the taken part, out of the
   free list; the rest stays free. */
static u16 carve(u16 f, u16 tot, int high)
{
    u16 t = TOTAL(f), r;
    if (t - tot >= 2) {
        if (!high) {
            r = sel_new(lin_of(f) + ((u32)tot << 4), 0xF);
            if (r) {
                mark_free(r);
                AR(r)->size = t - tot - 1;
                link_after(f, r);
                free_replace(f, r);
                AR(f)->size = tot - 1;
                return f;
            }
        } else {
            r = sel_new(lin_of(f) + ((u32)(t - tot) << 4), 0xF);
            if (r) {
                mark_free(r);
                AR(r)->size = tot - 1;
                AR(f)->size = t - tot - 1;
                link_after(f, r);
                return r;
            }
        }
    }
    free_unlink(f);
    return f;
}

/* ---- chunks ---- */

static void chunk_info_write(u32 lin, chunkinfo *ci)
{
    kmemcpy(MK_FP(tmp_at(tmp_a, lin + 0x10, 0xF), 0), ci, sizeof *ci);
}

static int chunk_info_read(u16 head, chunkinfo *ci)
{
    kmemcpy(ci, MK_FP(tmp_at(tmp_a, lin_of(head) + 0x10, 0xF), 0), sizeof *ci);
    return ci->magic == CHUNK_MAGIC;
}

static void sentinel(u16 a, int last)
{
    set_arena(a, last ? SIG_Z : SIG_M, OWN_SENTINEL, 1, 0, 0);
}

static void notthere(u16 a)
{
    set_arena(a, SIG_M, OWN_NOTTHERE, 0, 0, 0);
    AR(a)->lprev = AR(a)->lnext = 0;
}

/* Add the memory [lin, lin + size) to the heap. Returns its free arena,
   or 0. */
static u16 chunk_add(u32 lin, u32 size, u16 kind, u32 handle)
{
    u32 base = (lin + 31) & ~31UL, len;
    u16 h, f, t, x;
    chunkinfo ci;


    len = (lin + size - base) & ~31UL;
    if (lin + size < base || len < 0x60)
        return 0;
    h = sel_new(base, 0xF);
    f = sel_new(base + 0x20, 0xF);
    t = sel_new(base + len - 0x20, 0xF);
    if (!h || !f || !t) {
        if (h)
            dpmi_free_desc(h);
        if (f)
            dpmi_free_desc(f);
        if (t)
            dpmi_free_desc(t);
        return 0;
    }
    ci.magic = CHUNK_MAGIC;
    ci.kind = kind;
    ci.dpmi = handle;
    ci.size = len;
    ci.lin = base;
    chunk_info_write(base, &ci);
    kmemset(MK_FP(tmp_at(tmp_a, base + len - 0x10, 0xF), 0), 0, 16);
    mark_free(f);
    AR(f)->size = (u16)((len - 0x40) >> 4) - 1;

    if (!hp->first) {
        /* the first chunk: the sentinels */
        sentinel(h, 0);
        sentinel(t, 1);
        AR(h)->prev = h;
        AR(h)->next = f;
        AR(f)->prev = h;
        AR(f)->next = t;
        AR(t)->prev = f;
        AR(t)->next = t;
        AR(h)->lprev = 0xFFFF;
        AR(h)->lnext = f;
        AR(f)->lprev = h;
        AR(f)->lnext = t;
        AR(t)->lprev = f;
        AR(t)->lnext = 0xFFFF;
        hp->first = h;
        hp->last = t;
        hp->count = 3;
        hp->freecount = 1;
        return f;
    }
    notthere(h);
    notthere(t);
    if (base > lin_of(hp->last)) {
        /* above every chunk: it gets the last sentinel; the old one
           becomes the end of its chunk */
        u16 old = hp->last, lastfree = AR(old)->lprev;
        notthere(old);
        AR(old)->next = h;
        AR(h)->prev = old;
        AR(h)->next = f;
        AR(f)->prev = h;
        AR(f)->next = t;
        AR(t)->prev = f;
        AR(t)->next = t;
        sentinel(t, 1);
        AR(t)->lprev = lastfree;
        AR(t)->lnext = 0xFFFF;
        AR(lastfree)->lnext = t;
        hp->last = t;
        hp->count += 3;
    } else if (base + len <= lin_of(hp->first)) {
        /* below every chunk: it gets the first sentinel */
        u16 old = hp->first, firstfree = AR(old)->lnext;
        notthere(old);
        sentinel(h, 0);
        AR(h)->prev = h;
        AR(h)->next = f;
        AR(f)->prev = h;
        AR(f)->next = t;
        AR(t)->prev = f;
        AR(t)->next = old;
        AR(old)->prev = t;
        AR(h)->lprev = 0xFFFF;
        AR(h)->lnext = firstfree;
        AR(firstfree)->lprev = h;
        hp->first = h;
        hp->count += 3;
    } else {
        /* between two chunks: before the first arena above it */
        for (x = AR(hp->first)->next; x != hp->last && lin_of(x) < base; x = AR(x)->next)
            ;
        x = AR(x)->prev;                /* the lower chunk's last arena */
        link_after(x, h);
        link_after(h, f);
        link_after(f, t);
        AR(f)->lprev = AR(f)->lnext = 0;
        free_link(f);
        return f;
    }
    AR(f)->lprev = AR(f)->lnext = 0;
    free_link(f);
    return f;
}

/* The chunk around free arena a, if a is all of it: give it back. Not
   the chunk of the Burgermaster, and not one holding a sentinel unless
   the neighbouring chunk can take it over. */
static void chunk_check_empty(u16 a)
{
    u16 h = AR(a)->prev, t = AR(a)->next;
    chunkinfo ci;
    regs16 r;

    if (!bm || AR(h)->size > 1 || AR(t)->size > 1 ||
        (AR(h)->owner != OWN_NOTTHERE && AR(h)->owner != OWN_SENTINEL) ||
        (AR(t)->owner != OWN_NOTTHERE && AR(t)->owner != OWN_SENTINEL))
        return;
    if (h == hp->first && t == hp->last)
        return;                         /* the only chunk */
    if (!chunk_info_read(h, &ci))
        return;
    if (h == hp->first) {
        /* the next chunk's first arena becomes the first sentinel */
        u16 n = AR(t)->next;
        sentinel(n, 0);
        AR(n)->prev = n;
        AR(n)->lprev = 0xFFFF;
        AR(n)->lnext = AR(a)->lnext;
        AR(AR(n)->lnext)->lprev = n;
        hp->first = n;
        hp->count -= 3;
        hp->freecount--;
    } else if (t == hp->last) {
        u16 p = AR(h)->prev;
        sentinel(p, 1);
        AR(p)->next = p;
        AR(p)->lnext = 0xFFFF;
        AR(p)->lprev = AR(a)->lprev;
        AR(AR(p)->lprev)->lnext = p;
        hp->last = p;
        hp->count -= 3;
        hp->freecount--;
    } else {
        free_unlink(a);
        unlink(t);
        unlink(a);
        unlink(h);
    }
    dpmi_free_desc(h);
    dpmi_free_desc(a);
    dpmi_free_desc(t);
    if (ci.kind == CK_DOS) {
        r.ax = 0x0101;
        r.dx = (u16)ci.dpmi;
        r.ds = r.es = 0;
        dpmi31(&r);
    } else {
        dpmi_free_mem(ci.dpmi);
    }
}

/* The largest block DPMI can still give (0500h), in bytes. */
static u32 dpmi_largest(void)
{
    static u32 info[12];
    regs16 r;
    r.ax = 0x0500;
    r.di = FP_OFF((void FAR *)info);
    r.es = FP_SEG((void FAR *)info);
    r.ds = 0;
    if (dpmi31(&r))
        return 0;
    return info[0];
}

/* Grow the heap by a chunk with room for a block of `tot` paragraphs. */
static int grow(u16 tot, int dos)
{
    u32 need = ((u32)tot << 4) + 0x60, size, lin, h;
    regs16 r;

    if (dos) {
        size = need > DOS_GROW ? need : DOS_GROW;
        r.ax = 0x0100;
        r.bx = (u16)((size + 15) >> 4);
        r.ds = r.es = 0;
        if (dpmi31(&r)) {
            if (r.bx < (u16)((need + 15) >> 4))
                return 0;
            size = (u32)r.bx << 4;
            r.ax = 0x0100;
            r.ds = r.es = 0;
            if (dpmi31(&r))
                return 0;
        }
        if (!chunk_add((u32)r.ax << 4, size, CK_DOS, r.dx)) {
            r.ax = 0x0101;
            r.ds = r.es = 0;
            dpmi31(&r);
            return 0;
        }
        return 1;
    }
    size = need > CHUNK_GROW ? need : CHUNK_GROW;
    size = (size + 0xFFF) & ~0xFFFUL;
    if (!dpmi_alloc_mem(size, &lin, &h)) {
        size = dpmi_largest() & ~0xFFFUL;
        if (size < need || !dpmi_alloc_mem(size, &lin, &h))
            return 0;
    }
    if (!chunk_add(lin, size, CK_EXT, h)) {
        dpmi_free_mem(h);
        return 0;
    }
    return 1;
}

/* ---- finding space ---- */

#define WHERE_EXT   0           /* extended memory only */
#define WHERE_DOS   1           /* below 1 MB only */
#define WHERE_ANY   2

static int where_ok(u16 f, u16 tot, int where)
{
    u32 l = lin_of(f);
    if (where == WHERE_DOS)
        return l + ((u32)tot << 4) <= ONE_MB;
    if (where == WHERE_EXT)
        return l >= ONE_MB;
    return 1;
}

/* the lowest (or highest) free arena of at least tot paragraphs */
static u16 find_free(u16 tot, int where, int high)
{
    u16 f;
    if (!high) {
        for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext)
            if (TOTAL(f) >= tot && where_ok(f, tot, where))
                return f;
    } else {
        for (f = AR(hp->last)->lprev; f != hp->first; f = AR(f)->lprev)
            if (TOTAL(f) >= tot && where_ok(f, tot, where))
                return f;
    }
    return 0;
}

/* ---- moving ---- */

/* copy n bytes from linear src to linear dst (dst below src or apart) */
static void lin_copy(u32 dst, u32 src, u32 n)
{
    while (n) {
        u16 k = n > 0x8000UL ? 0x8000 : (u16)n;
        tmp_at(tmp_a, src, 0xFFFF);
        tmp_at(tmp_b, dst, 0xFFFF);
        kcopyw(tmp_b, tmp_a, k >> 1);
        src += k;
        dst += k;
        n -= k;
    }
}

static void lin_zero(u32 dst, u32 n)
{
    while (n) {
        u16 k = n > 0x8000UL ? 0x8000 : (u16)n;
        kzerow(tmp_at(tmp_b, dst, 0xFFFF), k >> 1);
        dst += k;
        n -= k;
    }
}

static u16 tiles(u32 size)
{
    return size ? (u16)((size + 0xFFFFUL) >> 16) : 1;
}

/* Point the tiles of block selector s at [lin, lin + size). */
static int set_tiles(u16 s, u16 n, u32 lin, u32 size, int code)
{
    u16 i;
    for (i = 0; i < n; i++) {
        u32 off = (u32)i << 16;
        u32 lim = size > off ? size - off - 1 : 0;
        if (!dpmi_set_desc(s + i * sel_inc, lin + off, lim, code))
            return 0;
    }
    return 1;
}

static int is_code(u16 s)
{
    return (lar(s) & 0x0800) != 0;
}

/* Move block a's data (`bytes` of it) to the arena slot at linear `to`
   (the data at to + 10h), where nothing else is: its selectors follow. */
static void move_data(u16 a, u32 to, u32 bytes)
{
    u16 s = AR(a)->handle | 1;
    u32 from = lin_of(a) + 0x10, head = bytes > 0x10000UL ? 0x10000UL : bytes;
    /* The first 64 KB may be the stack we run on (a task's DGROUP): copy
       it and switch its base in one step. The block moves down or to a
       place apart, so the rest is still in place afterwards. */
    tmp_at(tmp_b, to + 0x10, head ? head - 1 : 0);
    seg_move(s, tmp_b, (u16)((head + 1) >> 1), to + 0x10);
    if (bytes > head) {
        lin_copy(to + 0x10 + head, from + head, bytes - head);
        set_tiles(s, tiles(bytes), to + 0x10, bytes, is_code(s));
    }
}

/* nw takes old's place in the LRU ring (sv: old's arena as it was) */
static void lru_retarget(const arena *sv, u16 old, u16 nw)
{
    if (sv->lnext == old) {
        AR(nw)->lprev = AR(nw)->lnext = nw;
    } else {
        AR(nw)->lprev = sv->lprev;
        AR(nw)->lnext = sv->lnext;
        AR(sv->lprev)->lnext = nw;
        AR(sv->lnext)->lprev = nw;
    }
    if (hp->lruchain == old)
        hp->lruchain = nw;
}

/* Block a moves into arena f (taken out of the free list, at least its
   size, apart from it): f becomes the block, a's place becomes free. */
static void transplant(u16 a, u16 f)
{
    arena save = *AR(a);
    move_data(a, lin_of(f), (u32)save.size << 4);
    AR(f)->count = save.count;
    AR(f)->owner = save.owner;
    AR(f)->flags = save.flags;
    AR(f)->handle = save.handle;
    AR(f)->lprev = AR(f)->lnext = 0;
    if (save.lnext)
        lru_retarget(&save, a, f);
    *seltab(save.handle | 1) = f;
    AR(a)->lprev = AR(a)->lnext = 0;
    release(a);
}

static int movable(u16 a)
{
    arena FAR *p = AR(a);
    return p->handle && !(p->handle & 1) && p->count == 0;
}

/* Free arena p lies right below block a: slide a down into it. p
   becomes the block, a's selector the free space above it. Returns p. */
static u16 slide_down(u16 a)
{
    u16 p = AR(a)->prev, ptot = TOTAL(p), next = AR(a)->next;
    u16 pfl = AR(p)->lprev, pfn = AR(p)->lnext;
    arena save = *AR(a);
    u32 to = lin_of(p);

    move_data(a, to, (u32)save.size << 4);
    set_arena(p, save.count, save.owner, save.size, save.flags, save.handle);
    AR(p)->lprev = AR(p)->lnext = 0;
    if (save.lnext)
        lru_retarget(&save, a, p);
    dpmi_set_desc(a, to + ((u32)(save.size + 1) << 4), 0xF, 0);
    set_arena(a, SIG_M, 0, ptot - 1, 0, 0);
    AR(a)->prev = p;
    AR(a)->next = next;
    AR(a)->lprev = pfl;
    AR(a)->lnext = pfn;
    AR(pfl)->lnext = a;
    AR(pfn)->lprev = a;
    *seltab(save.handle | 1) = p;
    if (is_free(next))
        merge_next(a);
    return p;
}

/* Move moveable blocks down into free space. Returns 1 if any moved. */
static int compact(void)
{
    u16 a, f, guard = hp->count + 8;
    int moved = 0;
again:
    for (a = AR(hp->first)->next; a != hp->last; a = AR(a)->next) {
        if (!movable(a))
            continue;
        if (is_free(AR(a)->prev)) {
            a = slide_down(a);
            moved = 1;
            continue;
        }
        f = find_free(TOTAL(a), lin_of(a) >= ONE_MB ? WHERE_EXT : WHERE_DOS, 0);
        if (f && lin_of(f) < lin_of(a)) {
            transplant(a, carve(f, TOTAL(a), 0));
            moved = 1;
            if (--guard)
                goto again;     /* a's neighbours may be gone */
            break;
        }
    }
    return moved;
}

/* ---- discarding ---- */

static u16 task_of_pdb(u16 pdb)
{
    u16 t;
    for (t = (u16)(GetCurrentTask() >> 16); t; t = *(u16 FAR *)MK_FP(t, 0))
        if (*(u16 FAR *)MK_FP(t, 0x60) == pdb)
            return t;
    return 0;
}

/* Ask the GlobalNotify procedure of the owner's task whether a may go. */
static int notify_ok(u16 a)
{
    u16 t;
    u32 proc;
    if (!(AR(a)->flags & GA_NOTIFY))
        return 1;
    t = task_of_pdb(AR(a)->owner);
    if (!t)
        return 1;
    proc = *(u32 FAR *)MK_FP(t, TDB_GNOTIFY);
    if (!proc)
        return 1;
    return notify_call(proc, AR(a)->handle) != 0;
}

/* Discard block a (discardable, unlocked). Returns 1 if discarded. */
static int discard(u16 a)
{
    arena save = *AR(a);
    u16 s = save.handle | 1, n, i;
    u8 db = DB_DISC;

    /* observed: a fixed discardable block (in the LRU ring) stays */
    if (save.count || (save.handle & 1))
        return 0;
    if (!notify_ok(a))
        return 0;
    if (save.flags & GA_NOTIFY)
        db |= DB_NOTIFY;
    if (save.flags & GA_DGROUP)
        db |= DB_DGROUP;
    if (save.flags & GA_DISCCODE)
        db |= DB_DISCCODE;
    n = tiles((u32)save.size << 4);
    lru_remove(a);
    for (i = 0; i < n; i++)
        dpmi_set_desc(s + i * sel_inc, 0, 0, 0);
    dpmi_set_desc(s, ((u32)n << 16) | db, save.owner, 0);
    set_access(s, 0x13);           /* not present (observed 73h) */
    for (i = 1; i < n; i++)
        set_access(s + i * sel_inc, 0x13);
    *seltab(s) = save.owner;
    release(a);
    return 1;
}

/* the oldest unlocked discardable block that may go, discarded */
static int discard_oldest(void)
{
    u16 a, n, h = hp->lruchain;
    if (!h)
        return 0;
    for (a = AR(h)->lprev, n = hp->lrucount; n; n--, a = AR(a)->lprev)
        if (discard(a))
            return 1;
    return 0;
}

/* ---- allocating ---- */

/* Free space for a block of tot paragraphs: grow, compact, discard. */
static u16 alloc_space(u16 tot, int where, u16 flags)
{
    int high = (flags & GA_REQ_DISCCODE) != 0;
    u16 f, w = where == WHERE_DOS ? WHERE_DOS : WHERE_EXT;
    int tries;

    for (tries = 0;; tries++) {
        f = find_free(tot, w, high);
        if (!f && where == WHERE_ANY && tries)
            f = find_free(tot, WHERE_ANY, high);
        if (f)
            return carve(f, tot, high);
        if (tries == 0) {
            grow(tot, where == WHERE_DOS);
            continue;
        }
        if (flags & GMEM_NOCOMPACT)
            return 0;
        if (tries == 1 || tries == 2) {
            if (compact())
                continue;
        }
        if ((flags & GMEM_NODISCARD) || !discard_oldest())
            return 0;
        compact();
    }
}

static u16 owner_or_kernel(u16 owner)
{
    if (owner)
        return owner;
    if (!kernel_owner && module_first)
        kernel_owner = module_first->ne_sel;
    return kernel_owner ? kernel_owner : OWN_BURGER;
}

/* A discarded block of no memory (GlobalAlloc(GMEM_MOVEABLE, 0)). */
static u16 alloc_discarded(u16 flags, u16 owner)
{
    u16 s = dpmi_alloc_desc(1);
    u8 db = 0;
    if (!s)
        return 0;
    if (flags & GMEM_DISCARDABLE)
        db |= DB_DISC;
    if (flags & GMEM_NOTIFY)
        db |= DB_NOTIFY;
    dpmi_set_desc(s, (1UL << 16) | db, owner, 0);
    set_access(s, 0x13);
    *seltab(s) = owner;
    return s & ~1u;
}

/* Paragraphs (arena included) of a block of n bytes, 0 if too big. */
static u16 paras_for(u32 n)
{
    u32 d = (n + 15) >> 4;
    if (d > 0xFFFFUL)
        return 0;
    return (u16)((d + 2) & ~1UL);   /* 0 for FFFFh: d + 2 = 10001h */
}

static u16 make_block(u16 a, u16 flags, u16 owner, u16 *sel_out)
{
    u32 bytes = (u32)AR(a)->size << 4;
    u16 n = tiles(bytes), s;
    u8 gf = 0;

    s = dpmi_alloc_desc(n);
    if (!s || !set_tiles(s, n, lin_of(a) + 0x10, bytes, 0)) {
        if (s) {
            u16 i;
            for (i = 0; i < n; i++)
                dpmi_free_desc(s + i * sel_inc);
        }
        release(a);
        return 0;
    }
    lin_zero(lin_of(a) + 0x10, bytes);
    if (flags & GMEM_NOTIFY)
        gf |= GA_NOTIFY;
    if (flags & GA_REQ_DGROUP)
        gf |= GA_DGROUP;
    if (flags & GA_REQ_DISCCODE)
        gf |= GA_DISCCODE;
    set_arena(a, (flags & GMEM_MOVEABLE) ? 0 : SIG_M, owner, AR(a)->size, gf,
              (flags & GMEM_MOVEABLE) ? s & ~1u : s);
    AR(a)->lprev = AR(a)->lnext = 0;
    if (flags & GMEM_DISCARDABLE)
        lru_add(a, 1);
    *seltab(s) = a;
    if (sel_out)
        *sel_out = s;
    return AR(a)->handle;
}

static u16 galloc_where(u16 flags, u32 size, u16 owner, u16 *sel_out, int where)
{
    u16 tot, a;

    owner = owner_or_kernel(owner);
    if (size == 0) {
        if (!(flags & GMEM_MOVEABLE))
            return 0;                   /* observed: fixed size 0 fails */
        a = alloc_discarded(flags, owner);
        if (sel_out)
            *sel_out = a | 1;
        return a;
    }
    tot = paras_for(size);
    if (!tot)
        return 0;
    a = alloc_space(tot, where, flags);
    if (!a)
        return 0;
    return make_block(a, flags, owner, sel_out);
}

u16 galloc(u16 flags, u32 size, u16 owner, u16 *sel_out)
{
    return galloc_where(flags, size, owner, sel_out, WHERE_ANY);
}

/* A fixed, zeroed block below 1 MB (a task database, GlobalDOSAlloc;
   over 64K it has tiles like any block); *seg is its real-mode segment.
   Returns the selector (= handle), 0 if out of memory. */
u16 galloc_dos(u32 size, u16 owner, u16 *seg)
{
    u16 s;
    if (!galloc_where(GMEM_FIXED, size, owner, &s, WHERE_DOS))
        return 0;
    *seg = (u16)(lin_of(s) >> 4);
    return s;
}

/* ---- finding blocks ---- */

#define FB_NONE     0
#define FB_BLOCK    1           /* *a = its arena */
#define FB_DISC     2           /* a discarded block */

/* What handle or selector h is. */
static int find(u16 h, u16 *a)
{
    u16 s = h | 1, e;
    if (!bm || !h || (s >> 3) >= SELTAB_LEN / 2)
        return FB_NONE;
    e = *seltab(s);
    if (!e)
        return FB_NONE;
    if (!present(s)) {
        if (lar(s) == 0)
            return FB_NONE;
        *a = e;                         /* the owner */
        return FB_DISC;
    }
    if (lsl(e) != 0xF || (AR(e)->handle | 1) != s)
        return FB_NONE;
    *a = e;
    return FB_BLOCK;
}

static u8 disc_bits(u16 s)
{
    return (u8)(lin_of(s) & 0xFF);
}

static u16 disc_tiles(u16 s)
{
    return (u16)(lin_of(s) >> 16);
}

/* ---- aliases (galias_code): code selectors for a block's memory ---- */

typedef struct alias {
    u16 sel, owner, src;
} alias;

static alias FAR *alias_find(u16 s, u16 *n)
{
    alias FAR *p;
    u16 i, cnt;
    if (!alias_sel)
        return NULL;
    p = MK_FP(alias_sel, 0);
    cnt = (u16)gsize_of_sel(alias_sel) / sizeof(alias);
    for (i = 0; i < cnt; i++)
        if (p[i].sel && (p[i].sel | 3) == (s | 3)) {
            if (n)
                *n = i;
            return &p[i];
        }
    return NULL;
}

static void alias_free(alias FAR *p)
{
    dpmi_free_desc(p->sel);
    p->sel = 0;
}

/* A code selector for the memory of block `sel`; GlobalHandle and
   GlobalSize know it, and GlobalFree frees only the selector. Returns 0
   on failure. */
u16 galias_code(u16 sel, u16 owner)
{
    u16 a, c, i, cnt;
    alias FAR *p;
    if (find(sel, &a) != FB_BLOCK || AR(a)->size > 0x1000)
        return 0;
    if (!alias_sel && !galloc(GMEM_FIXED, 16 * sizeof(alias), 0, &alias_sel))
        return 0;
    p = MK_FP(alias_sel, 0);
    cnt = (u16)gsize_of_sel(alias_sel) / sizeof(alias);
    for (i = 0; i < cnt && p[i].sel; i++)
        ;
    if (i == cnt) {
        if (!GlobalReAlloc(alias_sel, (u16)((cnt + 16) * sizeof(alias)), GMEM_MOVEABLE))
            return 0;
        p = MK_FP(alias_sel, 0);
    }
    c = dpmi_alloc_desc(1);
    if (!c)
        return 0;
    if (!dpmi_set_desc(c, lin_of(a) + 0x10, ((u32)AR(a)->size << 4) - 1, 1)) {
        dpmi_free_desc(c);
        return 0;
    }
    p[i].sel = c;
    p[i].owner = owner;
    p[i].src = AR(a)->handle | 1;
    return c;
}

/* ---- freeing ---- */

static void free_block(u16 a)
{
    u16 s = AR(a)->handle | 1, n = tiles((u32)AR(a)->size << 4), i;
    if (discardable(a))
        lru_remove(a);
    *seltab(s) = 0;
    for (i = 0; i < n; i++)
        dpmi_free_desc(s + i * sel_inc);
    release(a);
}

static void free_discarded(u16 h)
{
    u16 s = h | 1, n = disc_tiles(s), i;
    *seltab(s) = 0;
    for (i = 0; i < n; i++)
        dpmi_free_desc(s + i * sel_inc);
}

/* Free every block `owner` owns (a task's PDB, when the task ends). */
void gfree_owner(u16 owner)
{
    u16 a, n, i, cnt;
    alias FAR *p;
    if (!owner || !bm)
        return;
    if (alias_sel) {
        p = MK_FP(alias_sel, 0);
        cnt = (u16)gsize_of_sel(alias_sel) / sizeof(alias);
        for (i = 0; i < cnt; i++)
            if (p[i].sel && p[i].owner == owner)
                alias_free(&p[i]);
    }
    a = AR(hp->first)->next;
    while (a != hp->last) {
        n = AR(a)->next;
        if (AR(a)->handle && AR(a)->owner == owner) {
            n = AR(a)->prev;            /* still there after the merge, */
            if (AR(n)->owner == OWN_NOTTHERE || n == hp->first)
                n = 0;                  /* unless its chunk goes */
            free_block(a);
            a = AR(n ? n : hp->first)->next;
            continue;
        }
        a = n;
    }
    for (i = 1; i < SELTAB_LEN / 2; i++) {
        u16 s = (i << 3) | 4 | cpl;
        if (((u16 FAR *)MK_FP(bm, SELTAB_OFF))[i] == owner && !present(s) && lar(s))
            free_discarded(s);
    }
}

/* ---- initialisation ---- */

void global_init(void)
{
    u16 f, a;
    u16 tot = paras_for(SELTAB_OFF + SELTAB_LEN);

    tmp_a = sel_new(0, 0xF);
    tmp_b = sel_new(0, 0xF);
    /* the first chunk in conventional memory, as on 3.1 (there DOSX gives
       KERNEL all of it): the Burgermaster comes right after the first
       sentinel, and DOS-memory chunks made later lie above it */
    if (!tmp_a || !tmp_b || (!grow(tot, 1) && !grow(tot, 0))) {
        kputs("w16: no memory for the global heap\r\n");
        kexit_to_dos(0xFF);
    }
    /* the Burgermaster: the first block, fixed, owned by FFFDh */
    f = AR(hp->first)->lnext;
    a = carve(f, tot, 0);
    bm = sel_new(lin_of(a) + 0x10, ((u32)AR(a)->size << 4) - 1);
    if (!bm) {
        kputs("w16: no memory for the global heap\r\n");
        kexit_to_dos(0xFF);
    }
    lin_zero(lin_of(a) + 0x10, (u32)AR(a)->size << 4);
    set_arena(a, 0, OWN_BURGER, AR(a)->size, 0, bm);    /* observed: 0 */
    AR(a)->lprev = AR(a)->lnext = 0;
    boot_hdr.hdelta = 0x20;
    boot_hdr.alt[0] = boot_hdr.alt[1] = 0xFFFF;
    kmemcpy(MK_FP(bm, 0), &boot_hdr, sizeof boot_hdr);
    hp = (bmhdr FAR *)MK_FP(bm, 0);
    *seltab(bm) = a;
}

/* ---- helpers for the rest of KERNEL ---- */

u16 gowner(u16 h)
{
    u16 a;
    switch (find(h, &a)) {
    case FB_BLOCK:
        return AR(a)->owner;
    case FB_DISC:
        return a;
    }
    return 0;
}

int gset_code(u16 sel)
{
    u16 a;
    if (find(sel, &a) != FB_BLOCK)
        return 0;
    return set_tiles(AR(a)->handle | 1, tiles((u32)AR(a)->size << 4),
                     lin_of(a) + 0x10, (u32)AR(a)->size << 4, 1);
}

u32 gsize_of_sel(u16 sel)
{
    u16 a;
    alias FAR *p;
    if (find(sel, &a) == FB_BLOCK)
        return (u32)AR(a)->size << 4;
    p = alias_find(sel, NULL);
    return p ? gsize_of_sel(p->src) : 0;
}


/* ---- [w16] heapcheck=1 (winboot.c): the global heap and every local
   heap in it checked whole at each Global/Local allocation call and task
   switch; the first inconsistency is reported (where it was found, the
   arena, its neighbours) and Windows stopped, so that the code that wrote
   past its block is caught close to the act (a debugging aid) ---- */
u16 heap_check_on;
u32 api_caller;            /* kentry.asm: the last heap API caller */
static u16 heap_check_busy;

const char *local_check(u16 seg, u16 *at);      /* local.c */

static const char *global_check(u16 *bad)
{
    u16 a, n, items = 0, f, pf;
    *bad = 0;
    if (!bm)
        return 0;
    for (a = hp->first; ; a = n) {
        *bad = a;
        if (!present(a) || lsl(a) < 0x0F)
            return "global: arena selector not usable";
        n = AR(a)->next;
        if (a == hp->last)
            break;
        if (!n || !present(n))
            return "global: bad next arena";
        if (AR(n)->prev != a)
            return "global: next arena's prev is not this one";
        if (AR(a)->owner != OWN_NOTTHERE && AR(n)->owner != OWN_NOTTHERE &&
            lin_of(a) + ((u32)TOTAL(a) << 4) != lin_of(n))
            return "global: arena size does not reach the next arena";
        if (!is_free(a) && AR(a)->owner != OWN_SENTINEL && AR(a)->owner != OWN_NOTTHERE &&
            AR(a)->handle) {
            u16 s = AR(a)->handle | 1;
            if (*seltab(s) != a)
                return "global: selector table entry is not this arena";
            if (!present(s) || lin_of(s) != lin_of(a) + 0x10)
                return "global: block's selector does not start after its arena";
            {   /* every tile: where set_tiles put it (the low word of
                   its limit; a 286 host gives at most FFFFh) */
                u32 bytes = (u32)AR(a)->size << 4, want;
                u16 i, t, l, nt = tiles(bytes);
                if ((AR(a)->flags & GA_DGROUP) && nt > 1)
                    return "global: a DGROUP is more than one selector";
                for (i = 0; i < nt; i++) {
                    t = s + i * sel_inc;
                    want = bytes - ((u32)i << 16) - 1;
                    l = lsl(t);
                    if (!present(t) || lin_of(t) != lin_of(a) + 0x10 + ((u32)i << 16) ||
                        (l != (u16)want && !(want > 0xFFFFUL && l == 0xFFFF)))
                        return "global: a tile's base or limit is not the block's";
                }
            }
        }
        if (++items == 0)
            return "global: no end";
    }
    /* free blocks, in address order */
    pf = hp->first;
    for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext) {
        *bad = f;
        if (!f || !present(f) || !is_free(f) || AR(f)->lprev != pf || lin_of(f) <= lin_of(pf))
            return "global: bad free list";
        pf = f;
    }
    *bad = 0;
    return 0;
}

static void hc_arena(const char *what, u16 a)
{
    u16 i;
    kputs(what);
    kputhex(a);
    if (a && present(a)) {
        kputs(":");
        for (i = 0; i < 16; i += 2) {
            kputs(" ");
            kputhex(*(u16 FAR *)MK_FP(a, i));
        }
    }
    kputs("\r\n");
}

/* where: the API or event, caller: its return address (0:0 if not known) */
void heap_check(const char *where, u32 caller)
{
    u16 bad;
    const char *why;
    if (!heap_check_on || heap_check_busy)
        return;
    heap_check_busy = 1;
    why = global_check(&bad);
    heap_check_busy = 0;
    if (why)
        heap_fail(why, where, caller, bad, 0, 0);
}

/* the local heap in segment ds (at each Local* call on it) */
void local_heap_check(const char *where, u16 ds)
{
    u16 lbad;
    const char *why;
    if (!heap_check_on || heap_check_busy)
        return;
    why = local_check(ds, &lbad);
    if (why)
        heap_fail(why, where, api_caller, 0, ds, lbad);
}

void heap_fail(const char *why, const char *where, u32 caller, u16 bad, u16 lseg, u16 lbad)
{
    heap_check_on = 0;
    kputs("w16 heapcheck: ");
    kputs(why);
    kputs(" at ");
    kputs(where);
    kputs(" from ");
    kputhex((u16)(caller >> 16));
    kputs(":");
    kputhex((u16)caller);
    kputs("\r\n");
    if (bad) {
        hc_arena("  arena ", bad);
        if (present(bad)) {
            hc_arena("  prev  ", AR(bad)->prev);
            hc_arena("  next  ", AR(bad)->next);
        }
    }
    if (lseg) {
        u16 i, o = lbad > 0x10 ? lbad - 0x10 : 0;
        kputs("  local heap in ");
        kputhex(lseg);
        kputs(", arena ");
        kputhex(lbad);
        kputs(":");
        for (i = 0; i < 0x30 && (u32)o + i + 1 <= (u32)lsl(lseg); i += 2) {
            if (!(i & 0x0F)) {
                kputs("\r\n    ");
                kputhex(o + i);
                kputs(":");
            }
            kputs(" ");
            kputhex(*(u16 FAR *)MK_FP(lseg, o + i));
        }
        kputs("\r\n");
    }
    kexit_to_dos(0xFE);
}

/* ---- API ---- */

/* Observed (tests/apps/tk15.c): a block a task allocates is freed when
   the task ends: the owner is the current task's PDB. GMEM_SHARE blocks
   belong to the calling module (Pietrek); w16 takes the current task's
   module (UNVERIFIED for a DLL calling). */
u16 KAPI GlobalAlloc(u16 flags, u32 size)                     /* KERNEL.15 */
{
    u16 owner = task_pdb();
    heap_check("GlobalAlloc", 0);
    if (flags & GMEM_SHARE) {
        module FAR *m = task_module();
        owner = m ? m->ne_sel : 0;
    }
    return galloc(flags & ~(GA_REQ_DGROUP | GA_REQ_DISCCODE), size, owner, NULL);
}

/* GlobalReAlloc of a discarded block: memory again under its handle */
static u16 realloc_discarded(u16 h, u32 size, u16 flags)
{
    u16 s = h | 1, owner = lsl(s), n = disc_tiles(s);
    u16 tot = paras_for(size), a, need, i, gf = 0;
    u8 db = disc_bits(s);
    u32 bytes;

    if (flags & GMEM_MODIFY) {
        db = (u8)((db & ~DB_DISC) | ((flags & GMEM_DISCARDABLE) ? DB_DISC : 0));
        dpmi_set_desc(s, ((u32)n << 16) | db, owner, 0);
        set_access(s, 0x13);
        return h;
    }
    if (!size)
        return h;
    if (!tot)
        return 0;
    need = tiles(((u32)tot - 1) << 4);
    if (need > n)
        return 0;                       /* TODO: a longer run of tiles */
    a = alloc_space(tot, WHERE_ANY, flags);
    if (!a)
        return 0;
    bytes = (u32)AR(a)->size << 4;
    for (i = need; i < n; i++)
        dpmi_free_desc(s + i * sel_inc);
    set_tiles(s, need, lin_of(a) + 0x10, bytes, (db & DB_DISCCODE) != 0);
    lin_zero(lin_of(a) + 0x10, bytes);
    /* observed: no GMEM_NOTIFY any more (no call when it is discarded
       again); UNVERIFIED: the DGROUP and code flags kept */
    if (db & DB_DGROUP)
        gf |= GA_DGROUP;
    if (db & DB_DISCCODE)
        gf |= GA_DISCCODE;
    set_arena(a, 0, owner, AR(a)->size, (u8)gf, h);
    AR(a)->lprev = AR(a)->lnext = 0;
    if (db & DB_DISC)
        lru_add(a, 1);
    *seltab(s) = a;
    return h;
}

/* Block a (handle h) grows to tot paragraphs, more 64K tiles than it has.
   Observed (tests/apps/tk17.c, TK17A, KRNL286 and KRNL386): the block
   always moves to a new selector run, so its handle changes (the data is
   kept); a fixed block only with GMEM_MOVEABLE (KRNL286). Returns the new
   handle, 0 if there is no room or no selectors. */
static u16 grow_tiles(u16 a, u16 h, u16 tot, u16 flags)
{
    u16 os = h | 1, on = tiles((u32)AR(a)->size << 4), ns, nn, f, i;
    u32 old = (u32)AR(a)->size << 4, bytes;
    int code = is_code(os);

    if ((h & 1) && !(flags & GMEM_MOVEABLE))
        return 0;
    if (AR(a)->count && !(h & 1))
        return 0;                       /* UNVERIFIED: a locked block */
    nn = tiles(((u32)tot - 1) << 4);
    ns = dpmi_alloc_desc(nn);
    if (!ns)
        return 0;
    AR(a)->count++;                     /* hold it while making room */
    f = alloc_space(tot, lin_of(a) >= ONE_MB ? WHERE_ANY : WHERE_DOS, flags);
    AR(a)->count--;
    if (!f) {
        for (i = 0; i < nn; i++)
            dpmi_free_desc(ns + i * sel_inc);
        return 0;
    }
    transplant(a, f);                   /* the old selectors follow it */
    a = f;
    *seltab(os) = 0;
    for (i = 0; i < on; i++)
        dpmi_free_desc(os + i * sel_inc);
    h = (h & 1) ? ns : ns & ~1u;
    AR(a)->handle = h;
    *seltab(ns) = a;
    bytes = (u32)AR(a)->size << 4;
    set_tiles(ns, nn, lin_of(a) + 0x10, bytes, code);
    if (bytes > old)
        lin_zero(lin_of(a) + 0x10 + old, bytes - old);
    return h;
}

/* Block a is to grow to tot paragraphs: move the moveable block after
   it out of the way if that frees enough room right after it (observed:
   a moveable block grown in front of another moveable block stays put). */
static int make_room_after(u16 a, u16 tot)
{
    u16 nx = AR(a)->next, after, room, f, t, i;
    int w;
    if (!movable(nx))
        return 0;
    after = AR(nx)->next;
    room = TOTAL(a) + TOTAL(nx) + (is_free(after) ? TOTAL(after) : 0);
    if (room < tot)
        return 0;
    t = TOTAL(nx);
    w = lin_of(nx) >= ONE_MB ? WHERE_EXT : WHERE_DOS;
    for (i = 0; i < 2; i++) {
        for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext)
            if (f != after && TOTAL(f) >= t && where_ok(f, t, w))
                break;
        if (f != hp->last || i || !grow(t, w == WHERE_DOS))
            break;
    }
    if (f == hp->last)
        return 0;
    transplant(nx, carve(f, t, 0));
    return is_free(AR(a)->next) && TOTAL(a) + TOTAL(AR(a)->next) >= tot;
}

u16 KAPI GlobalReAlloc(u16 h, u32 size, u16 flags)            /* KERNEL.16 */
{
    u16 a, tot, cur, s, n;
    u32 old, bytes;

    heap_check("GlobalReAlloc", api_caller);
    switch (find(h, &a)) {
    case FB_DISC:
        return realloc_discarded(h, size, flags);
    case FB_BLOCK:
        break;
    default:
        return 0;
    }
    h = AR(a)->handle;
    s = h | 1;
    if (flags & GMEM_MODIFY) {
        if ((flags & GMEM_DISCARDABLE) && !discardable(a))
            lru_add(a, 1);
        else if (!(flags & GMEM_DISCARDABLE) && discardable(a))
            lru_remove(a);
        return h;
    }
    if (size == 0) {
        /* GlobalDiscard */
        if (!(flags & GMEM_MOVEABLE) || !discardable(a))
            return 0;
        return discard(a) ? h : 0;
    }
    tot = paras_for(size);
    if (!tot)
        return 0;
    cur = TOTAL(a);
    n = tiles(((u32)cur - 1) << 4);
    if (tiles(((u32)tot - 1) << 4) > n)
        return grow_tiles(a, h, tot, flags);
    old = (u32)AR(a)->size << 4;
    if (tot < cur) {
        /* shrink in place; the rest becomes free (UNVERIFIED: 3.1 was
           seen to move a shrunk moveable block down) */
        u16 r = sel_new(lin_of(a) + ((u32)tot << 4), 0xF);
        if (r) {
            mark_free(r);
            AR(r)->size = cur - tot - 1;
            link_after(a, r);
            AR(a)->size = tot - 1;
            release(r);
        }
    } else if (tot > cur) {
        u16 nx = AR(a)->next;
        if (is_free(nx) && cur + TOTAL(nx) >= tot) {
            /* grow in place into the free block above */
            u16 part = carve(nx, tot - cur, 0);
            AR(a)->size += TOTAL(part);
            unlink(part);
            dpmi_free_desc(part);
        } else if (make_room_after(a, tot)) {
            u16 part, nx2 = AR(a)->next;
            part = carve(nx2, tot - cur, 0);
            AR(a)->size += TOTAL(part);
            unlink(part);
            dpmi_free_desc(part);
        } else if (!(h & 1) || (flags & GMEM_MOVEABLE)) {
            u16 f;
            if (AR(a)->count && !(h & 1))
                return 0;               /* fixed with GlobalFix */
            AR(a)->count++;             /* hold it while making room */
            f = alloc_space(tot, lin_of(a) >= ONE_MB ? WHERE_ANY : WHERE_DOS, flags);
            AR(a)->count--;
            if (!f)
                return 0;
            transplant(a, f);
            a = f;
        } else {
            return 0;
        }
    }
    bytes = (u32)AR(a)->size << 4;
    set_tiles(s, tiles(bytes), lin_of(a) + 0x10, bytes, is_code(s));
    /* observed (tk17.c): shrinking to fewer tiles frees the rest */
    for (cur = tiles(bytes); cur < n; cur++)
        dpmi_free_desc(s + cur * sel_inc);
    if (bytes > old)
        lin_zero(lin_of(a) + 0x10 + old, bytes - old);
    return h;
}

u16 KAPI GlobalFree(u16 h)                                    /* KERNEL.17 */
{
    u16 a, i;
    alias FAR *p;
    heap_check("GlobalFree", api_caller);
    switch (find(h, &a)) {
    case FB_BLOCK:
        if (AR(a)->owner == OWN_BURGER)
            return h;
        free_block(a);
        return 0;
    case FB_DISC:
        free_discarded(h);
        return 0;
    }
    p = alias_find(h, &i);
    if (p)
        alias_free(p);
    return 0;
}

void FAR * KAPI GlobalLock(u16 h)                             /* KERNEL.18 */
{
    u16 a;
    alias FAR *p;
    heap_check("GlobalLock", api_caller);
    if (find(h, &a) != FB_BLOCK) {
        p = alias_find(h, NULL);
        return p ? MK_FP(p->sel, 0) : NULL;
    }
    if (discardable(a) && !(AR(a)->handle & 1) && AR(a)->count < 0xFF)
        AR(a)->count++;
    return MK_FP(AR(a)->handle | 1, 0);
}

u16 KAPI GlobalUnlock(u16 h)                                  /* KERNEL.19 */
{
    u16 a;
    if (find(h, &a) != FB_BLOCK || !discardable(a) || (AR(a)->handle & 1))
        return 0;
    if (AR(a)->count)
        AR(a)->count--;
    return AR(a)->count;
}

u32 KAPI GlobalSize(u16 h)                                    /* KERNEL.20 */
{
    return gsize_of_sel(h);
}

/* Returns the handle in the low word and the selector in the high word;
   0 for a discarded block (observed). */
u32 KAPI_ES GlobalHandle(u16 sel)                                /* KERNEL.21 */
{
    u16 a;
    alias FAR *p;
    if (find(sel, &a) == FB_BLOCK)
        return ((u32)(AR(a)->handle | 1) << 16) | AR(a)->handle;
    p = alias_find(sel, NULL);
    if (p)
        return ((u32)p->sel << 16) | p->sel;
    return 0;
}

u16 KAPI GlobalFlags(u16 h)                                   /* KERNEL.22 */
{
    u16 a, f;
    u8 db;
    switch (find(h, &a)) {
    case FB_BLOCK:
        f = (AR(a)->flags & GA_SHOWN) << 8;
        if (discardable(a))
            f |= GMEM_DISCARDABLE;
        if (!(AR(a)->handle & 1))
            f |= AR(a)->count;
        return f;
    case FB_DISC:
        db = disc_bits(h | 1);
        f = GMEM_DISCARDED;
        if (db & DB_DISC)
            f |= GMEM_DISCARDABLE;
        if (db & DB_DGROUP)
            f |= GA_DGROUP << 8;
        if (db & DB_DISCCODE)
            f |= GA_DISCCODE << 8;
        return f;
    }
    return 0;
}

/* Observed (tests/apps/tk17.c, KRNL286 and KRNL386): LockSegment and
   UnlockSegment count only for discardable blocks (GlobalFlags of a
   moveable block stays 0), as GlobalLock; both return the handle.
   UNVERIFIED: LockSegment(-1) (the caller's DS: __loadds has replaced DS
   by then). */
u16 KAPI LockSegment(u16 seg)                                 /* KERNEL.23 */
{
    u16 a;
    if (find(seg, &a) != FB_BLOCK)
        return 0;
    if (!(AR(a)->handle & 1) && discardable(a) && AR(a)->count < 0xFF)
        AR(a)->count++;
    return AR(a)->handle;
}

u16 KAPI UnlockSegment(u16 seg)                               /* KERNEL.24 */
{
    u16 a;
    if (find(seg, &a) != FB_BLOCK)
        return 0;
    if (!(AR(a)->handle & 1) && discardable(a) && AR(a)->count)
        AR(a)->count--;
    return AR(a)->handle;
}

static u32 largest_free(void)
{
    u16 f, best = 0;
    for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext)
        if (AR(f)->size > best)
            best = AR(f)->size;
    return (u32)best << 4;
}

/* Observed: compacts; GlobalCompact(-1) discards every unlocked
   discardable block; returns the largest free block less 30h. w16 also
   counts what DPMI can still give (the heap grows on demand). UNVERIFIED:
   for other values, discarding (oldest first) only until a free block of
   min_free bytes exists. */
u32 KAPI GlobalCompact(u32 min_free)                          /* KERNEL.25 */
{
    u32 best, d;
    compact();
    if (min_free == 0xFFFFFFFFUL) {
        while (discard_oldest())
            ;
        compact();
    } else {
        while (largest_free() < min_free && discard_oldest())
            compact();
    }
    best = largest_free();
    d = dpmi_largest();
    d = d > 0x60 ? d - 0x60 : 0;
    if (d > 0xFFFF0UL)
        d = 0xFFFF0UL;
    if (d > best)
        best = d;
    return best > 0x30 ? best - 0x30 : 0;
}

/* Frees the blocks of a PDB (0: the current task's) (Schulman). */
void KAPI GlobalFreeAll(u16 pdb)                              /* KERNEL.26 */
{
    gfree_owner(pdb ? pdb : task_pdb());
}

/* DX = the Burgermaster's selector, AX = its handle (the same: observed) */
u32 KAPI GlobalMasterHandle(void)                             /* KERNEL.28 */
{
    return ((u32)bm << 16) | bm;
}

/* GlobalFix and moving the block to the lowest free place (observed). */
void FAR * KAPI GlobalWire(u16 h)                             /* KERNEL.111 */
{
    u16 a, f;
    if (find(h, &a) != FB_BLOCK)
        return NULL;
    if (!(AR(a)->handle & 1)) {
        f = find_free(TOTAL(a), lin_of(a) >= ONE_MB ? WHERE_EXT : WHERE_DOS, 0);
        if (f && lin_of(f) < lin_of(a)) {
            f = carve(f, TOTAL(a), 0);
            transplant(a, f);
            a = f;
        }
        if (AR(a)->count < 0xFF)
            AR(a)->count++;
    }
    return MK_FP(AR(a)->handle | 1, 0);
}

/* GlobalUnfix; returns FFFFh when the count reaches 0, else 0
   (observed). */
u16 KAPI GlobalUnwire(u16 h)                                  /* KERNEL.112 */
{
    u16 a;
    if (find(h, &a) != FB_BLOCK || (AR(a)->handle & 1) || !AR(a)->count)
        return 0;
    return --AR(a)->count ? 0 : 0xFFFF;
}

/* The task's GlobalNotify procedure, in its TDB (Pietrek; observed at
   2Eh). Observed: a pointer with a bad selector is not stored.
   UNVERIFIED: which selectors count as bad (here: LAR fails). */
void KAPI GlobalNotify(u32 proc)                              /* KERNEL.154 */
{
    u16 t = (u16)GetCurrentTask();
    if (t && (!proc || lar((u16)(proc >> 16))))
        *(u32 FAR *)MK_FP(t, TDB_GNOTIFY) = proc;
}

u32 KAPI GlobalHandleNoRIP(u16 sel)                           /* KERNEL.159 */
{
    return GlobalHandle(sel);
}

/* UNVERIFIED: the return value (the handle here). */
u16 KAPI GlobalLRUOldest(u16 h)                               /* KERNEL.163 */
{
    u16 a;
    if (find(h, &a) != FB_BLOCK || !discardable(a))
        return 0;
    lru_remove(a);
    lru_add(a, 0);
    return AR(a)->handle;
}

u16 KAPI GlobalLRUNewest(u16 h)                               /* KERNEL.164 */
{
    u16 a;
    if (find(h, &a) != FB_BLOCK || !discardable(a))
        return 0;
    lru_remove(a);
    lru_add(a, 1);
    return AR(a)->handle;
}

/* Free and unlocked discardable blocks, arenas included, and what DPMI
   can still give. Observed on 3.1: the free and discardable blocks less
   the paragraphs reserved for discardable code (none in w16).
   UNVERIFIED: the flag (discardable blocks counted only for 0). */
u32 KAPI GetFreeSpace(u16 flags)                              /* KERNEL.169 */
{
    u16 a;
    u32 t = dpmi_largest();
    for (a = hp->first; ; a = AR(a)->next) {
        if (is_free(a) || (!flags && discardable(a) &&
                           (AR(a)->count == 0 || (AR(a)->handle & 1))))
            t += (u32)TOTAL(a) << 4;
        if (a == hp->last)
            break;
    }
    return t;
}

/* GlobalFix: moveable blocks only (observed). GlobalFix and GlobalUnfix
   are void; observed (tests/apps/tk17.c): AX is the handle passed
   afterwards, for both. */
u16 KAPI GlobalFix(u16 h)                                     /* KERNEL.197 */
{
    u16 a;
    if (find(h, &a) != FB_BLOCK)
        return h;
    if (!(AR(a)->handle & 1) && AR(a)->count < 0xFF)
        AR(a)->count++;
    return h;
}

u16 KAPI GlobalUnfix(u16 h)                                   /* KERNEL.198 */
{
    u16 a;
    if (find(h, &a) == FB_BLOCK && !(AR(a)->handle & 1) && AR(a)->count)
        AR(a)->count--;
    return h;
}

/* The owner word of a block's arena (Schulman). */
void KAPI FarSetOwner(u16 h, u16 owner)                       /* KERNEL.403 */
{
    u16 a;
    if (find(h, &a) == FB_BLOCK)
        AR(a)->owner = owner;
}

u16 KAPI FarGetOwner(u16 h)                                   /* KERNEL.404 */
{
    return gowner(h);
}
