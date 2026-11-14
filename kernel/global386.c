/*
 * global386.c - the global heap of KRNL386 (built with -DKRNL386 only;
 * KRNL286.EXE has kernel/global.c, KRNL286's layout).
 *
 * Observed black-box on Windows 3.1 with tests/apps/tk58.c (KRNL386 in
 * Standard and 386 Enhanced mode, tests/refrun.sh), tk17.c, tk3.c, tk12.c
 * (the KRNL386 values of their tables) and tk51.c (THHOOK). The
 * structures are the ones Pietrek, "Windows Internals" (1993), ch. 2
 * (Tables 2-2 to 2-4) and Schulman et al., "Undocumented Windows" (1992),
 * ch. 5 publish for KRNL386; TK58 checked every field used here. Facts
 * not checked are marked UNVERIFIED.
 *
 * Layout:
 *  - The Burgermaster (GlobalMasterHandle: AX = DX, a fixed block owned
 *    by FFFDh, more than 64 KB, its arena's address its base) holds the
 *    header below (80h bytes), the arenas, and at the end the selector
 *    table (THHOOK 18h: its offset, a dword; 16h: its length, 8000h, one
 *    dword per LDT entry: the offset of the arena of the block whose
 *    first selector that is, the owner of a discarded block, else 0). w16
 *    reaches offsets past 64 KB through the Burgermaster's tiles.
 *  - An arena is 20h bytes at a 32-bit offset in the Burgermaster, apart
 *    from its block: 00 next, 04 previous (address order; the first
 *    sentinel's previous and the last's next are itself), 08 the block's
 *    linear address, 0C its size in bytes (a multiple of 20h: GlobalSize),
 *    10 handle (0 free), 12 owner (0 free, FFFFh sentinel, FFFDh
 *    Burgermaster, FFFCh "not there", else a PDB or module), 14 lock count
 *    (GlobalFix, GlobalPageLock, GlobalLock of a discardable block; 'M'
 *    for a GlobalDOSAlloc block and the first sentinel, 'Z' for the last),
 *    15 page-lock count, 16 flags (04h DGROUP, 08h discardable code), 17
 *    the number of selectors (64 KB tiles), 18/1C previous/next in the
 *    free list (free arenas, address order, from the first sentinel's 1Ch
 *    to the last sentinel) or the LRU ring (discardable blocks; BM 20h is
 *    the newest), else 0.
 *  - The first arena (the first sentinel) is at 80h. Blocks are
 *    contiguous in linear memory within a piece of it; a piece other than
 *    the first is bounded by two "not there" arenas of size 0.
 *  - Handles: the selector with bit 0 clear, fixed and moveable blocks
 *    alike (the arena's flags do not tell them apart; w16 keeps which are
 *    fixed in a bitmap of its own); a GlobalDOSAlloc block's handle is its
 *    selector. Blocks over 64 KB have one selector per 64 KB tile; only
 *    the first is in the selector table.
 *  - A discarded block has no arena; its selector table entry is the
 *    owner and its descriptor is not present (w16 keeps its flags and
 *    tiles in the descriptor's base, as in global.c).
 *
 * Behaviour observed (tk58.c, tk17.c) where it differs from KRNL286's:
 *  - GlobalSize rounds the request up to 20h (GlobalAlloc(1): 20h, (100):
 *    80h); GlobalAlloc(GMEM_FIXED, 0) gives a discarded block as
 *    GlobalAlloc(GMEM_MOVEABLE, 0) does (GlobalFlags 4000h).
 *  - GlobalFix counts for fixed blocks too, and GlobalFlags shows the
 *    count of any block; GlobalPageLock adds to both the lock and the
 *    page-lock count and returns the page-lock count (GlobalPageUnlock
 *    the same, down).
 *  - A fixed block grows past its tiles without GMEM_MOVEABLE too.
 *
 * Where w16 differs on purpose (as global.c): the heap grows on demand by
 * DPMI chunks, GlobalCompact/GetFreeSpace count what DPMI can still give.
 * The arenas' room grows when it is used up (Pietrek: in 8 KB steps;
 * w16 moves the Burgermaster to a larger block then, the selector table
 * to its new end).
 */
#include "kernel.h"

#ifdef KRNL386

#pragma pack(push, 1)
typedef struct arena {
    u32 next;           /* 00 */
    u32 prev;           /* 04 */
    u32 addr;           /* 08 linear address */
    u32 size;           /* 0C bytes */
    u16 handle;         /* 10 */
    u16 owner;          /* 12 */
    u8  count;          /* 14 */
    u8  pglock;         /* 15 */
    u8  flags;          /* 16 */
    u8  selcount;       /* 17 */
    u32 lprev;          /* 18 */
    u32 lnext;          /* 1C */
} arena;

/* the Burgermaster's header (Pietrek, Tables 2-2 and 2-3; values
   observed with tk58.c) */
typedef struct bmhdr {
    u16 check;          /* 00 0 */
    u16 freeze;         /* 02 0 */
    u16 count;          /* 04 arenas in the list */
    u32 first;          /* 06 first sentinel */
    u32 last;           /* 0A last sentinel */
    u8  ncompact;       /* 0E 0 */
    u8  dislevel;       /* 0F 40h (observed) */
    u32 distotal;       /* 10 0 */
    u16 htable;         /* 14 0 */
    u16 hfree;          /* 16 0 */
    u16 hdelta;         /* 18 0 (observed) */
    u16 hexpand;        /* 1A 0 */
    u16 pstats;         /* 1C 0 */
    u16 lrulock;        /* 1E 0 */
    u32 lruchain;       /* 20 newest discardable block */
    u16 lrucount;       /* 24 */
    u32 reserve;        /* 26 bytes kept for discardable code (w16: 0) */
    u32 disfence;       /* 2A */
    u16 freecount;      /* 2E free blocks */
    u16 alt[9];         /* 30 FFFF FFFF 0 0 0 0 0 0 FFFF (observed) */
    u16 disfence_hi;    /* 42 */
    u16 gflags;         /* 44 0 */
    u8  pad[0x80 - 0x46];
} bmhdr;
#pragma pack(pop)

W16_STATIC_ASSERT(arena386_size, sizeof(arena) == 0x20);
W16_STATIC_ASSERT(bm386_size, sizeof(bmhdr) == 0x80);

typedef u32 aid;                        /* an arena: its offset in the BM */

#define SIG_M           0x4D
#define SIG_Z           0x5A
#define OWN_SENTINEL    0xFFFF
#define OWN_BURGER      0xFFFD
#define OWN_NOTTHERE    0xFFFC
#define GA_DGROUP       0x04
#define GA_DISCCODE     0x08
#define GA_NOTIFY       0x40            /* UNVERIFIED: KRNL386's bit */
#define GA_SHOWN        0x0C            /* flags GlobalFlags shows (<< 8) */
#define NOLINK          0xFFFFFFFFUL    /* the sentinels' other free link
                                           (UNVERIFIED) */

#define FIRST_ARENA     0x80UL          /* observed */
#define SELTAB_LEN      0x8000UL        /* observed (THHOOK 16h): 8192 LDT
                                           entries, the LDT's bound */
#define SLOTS_STEP      256             /* arenas added at a time (8 KB,
                                           Pietrek) */
#define SLOTS_FIRST     (8 * SLOTS_STEP)
#define CHUNK_GROW      0x100000UL
#define DOS_GROW        0x1000UL
#define ONE_MB          0x100000UL

#define DB_DISC         0x01
#define DB_NOTIFY       0x02
#define DB_DGROUP       0x04
#define DB_DISCCODE     0x08
#define DB_FIXED        0x10

static u16 bm;                          /* Burgermaster selector = handle */
static u16 bm_tiles;                    /* selectors reserved for it */
static u32 bm_size;                     /* its size */
static u32 bm_dpmi;                     /* its DPMI memory block */
static u32 seltab_off;                  /* THHOOK 18h */
static aid slot_free;                   /* unused arena slots */
static u32 slot_end;                    /* end of the arenas' room */
static u16 tmp_a, tmp_b;
static u16 alias_sel;
static u16 kernel_owner;
static u8 fixmap[(SELTAB_LEN / 4 + 7) / 8];    /* fixed blocks, by LDT index */

#define TDB_GNOTIFY     0x2E

/* ---- the Burgermaster's memory ---- */

static void FAR *bmp(u32 off)
{
    return MK_FP(bm + (u16)(off >> 16) * sel_inc, (u16)off);
}

#define AR(a)   ((arena FAR *)bmp(a))
#define hp      ((bmhdr FAR *)MK_FP(bm, 0))

static u32 FAR *seltab(u16 s)
{
    return (u32 FAR *)bmp(seltab_off + ((u32)(s >> 3) << 2));
}

/* ---- selector helpers ---- */

static u16 lar(u16 s);
#pragma aux lar = "lar ax, ax" "jz l1" "xor ax, ax" "l1:" parm [ax] value [ax];

static u16 lsl(u16 s);
#pragma aux lsl = "lsl ax, ax" "jz l2" "xor ax, ax" "l2:" parm [ax] value [ax];

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

static u16 tmp_at(u16 t, u32 lin, u32 limit)
{
    dpmi_set_desc(t, lin, limit, 0);
    return t;
}

static int is_fixed(u16 s)
{
    u16 i = s >> 3;
    return (fixmap[i >> 3] >> (i & 7)) & 1;
}

static void set_fixed(u16 s, int on)
{
    u16 i = s >> 3;
    if (on)
        fixmap[i >> 3] |= (u8)(1 << (i & 7));
    else
        fixmap[i >> 3] &= (u8)~(1 << (i & 7));
}

static u32 round32(u32 n)
{
    return (n + 0x1F) & ~0x1FUL;
}

static u16 tiles(u32 size)
{
    return size ? (u16)((size + 0xFFFFUL) >> 16) : 1;
}

static int set_tiles(u16 s, u16 n, u32 lin, u32 size, int code)
{
    u16 i;
    for (i = 0; i < n; i++) {
        u32 off = (u32)i << 16;
        u32 lim = size > off ? size - off - 1 : 0;
        if (!dpmi_set_desc(s + i * sel_inc, lin + off, lim, code) &&
            (lim <= 0xFFFFUL || !dpmi_set_desc(s + i * sel_inc, lin + off, 0xFFFF, code)))
            return 0;
    }
    return 1;
}

static int is_code(u16 s)
{
    return (lar(s) & 0x0800) != 0;
}

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

static int bm_grow(void);
static int is_free(u32 a);
static void free_link(u32 a);
static void link_after(u32 p, u32 a);
static void unlink(u32 a);
static void free_unlink(u32 a);

/* ---- arena slots ---- */

static aid slot_get(void)
{
    aid a;
    if (!slot_free && !bm_grow())
        return 0;
    a = slot_free;
    slot_free = AR(a)->next;
    kmemset(AR(a), 0, sizeof(arena));
    return a;
}

static void slot_put(aid a)
{
    kmemset(AR(a), 0, sizeof(arena));
    AR(a)->next = slot_free;
    slot_free = a;
}

static void slots_add(u32 from, u32 to)
{
    u32 a;
    for (a = to; a > from; ) {
        a -= sizeof(arena);
        slot_put(a);
    }
}

/* the Burgermaster's selectors at [lin, lin + size) */
static void bm_point(u32 lin, u32 size)
{
    u16 i;
    for (i = 0; i < bm_tiles; i++) {
        u32 off = (u32)i << 16;
        dpmi_set_desc(bm + i * sel_inc, off < size ? lin + off : lin,
                      off < size ? size - off - 1 : 0, 0);
    }
}

static aid bm_arena;                    /* the Burgermaster's own arena */

/* ---- arena helpers ---- */

static int is_free(aid a)
{
    return AR(a)->handle == 0 && AR(a)->owner == 0;
}

static void link_after(aid p, aid a)
{
    aid n = AR(p)->next;
    AR(a)->prev = p;
    AR(a)->next = n;
    AR(n)->prev = a;
    AR(p)->next = a;
    hp->count++;
}

static void unlink(aid a)
{
    aid p = AR(a)->prev, n = AR(a)->next;
    AR(p)->next = n;
    AR(n)->prev = p;
    hp->count--;
}

static void free_link(aid a)
{
    aid p = AR(a)->prev, n;
    while (p != hp->first && !is_free(p))
        p = AR(p)->prev;
    n = AR(p)->lnext;
    AR(a)->lprev = p;
    AR(a)->lnext = n;
    AR(p)->lnext = a;
    AR(n)->lprev = a;
    hp->freecount++;
}

static void free_unlink(aid a)
{
    aid p = AR(a)->lprev, n = AR(a)->lnext;
    AR(p)->lnext = n;
    AR(n)->lprev = p;
    AR(a)->lprev = AR(a)->lnext = 0;
    hp->freecount--;
}

static void free_replace(aid a, aid b)
{
    aid p = AR(a)->lprev, n = AR(a)->lnext;
    AR(b)->lprev = p;
    AR(b)->lnext = n;
    AR(p)->lnext = b;
    AR(n)->lprev = b;
    AR(a)->lprev = AR(a)->lnext = 0;
}

static int discardable(aid a)
{
    return AR(a)->handle && AR(a)->lnext;
}

static void lru_add(aid a, int newest)
{
    aid h = hp->lruchain, t;
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

static void lru_remove(aid a)
{
    aid p = AR(a)->lprev, n = AR(a)->lnext;
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

static void mark_free(aid a)
{
    arena FAR *p = AR(a);
    p->handle = p->owner = 0;
    p->count = p->pglock = p->flags = p->selcount = 0;
    p->lprev = p->lnext = 0;
}

/* merge free arena b (right after a) into free arena a */
static void merge_next(aid a)
{
    aid b = AR(a)->next;
    free_unlink(b);
    AR(a)->size += AR(b)->size;
    unlink(b);
    slot_put(b);
}

static void chunk_check_empty(aid a);

static void release(aid a)
{
    aid p;
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

/* Take `bytes` of free arena f, at its start (or its end if `high`).
   Returns the arena of the taken part, out of the free list; the rest
   stays free. 0 when no arena slot is left for the rest. */
static aid carve(aid f, u32 bytes, int high)
{
    u32 t = AR(f)->size;
    aid r;
    if (t > bytes) {
        r = slot_get();
        if (!r)
            return 0;
        if (!high) {
            AR(r)->addr = AR(f)->addr + bytes;
            AR(r)->size = t - bytes;
            AR(f)->size = bytes;
            link_after(f, r);
            free_replace(f, r);
            return f;
        }
        AR(r)->addr = AR(f)->addr + t - bytes;
        AR(r)->size = bytes;
        AR(f)->size = t - bytes;
        link_after(f, r);
        return r;
    }
    free_unlink(f);
    return f;
}

/* ---- chunks: pieces of linear memory ---- */

#define CK_EXT  1
#define CK_DOS  2

/* what w16 keeps of each chunk, in a block of its own (the arenas have no
   room for it; chunks made before that block exists are never given
   back) */
typedef struct chunk {
    u32 lin, size, dpmi;
    u16 kind;
} chunk;
static u16 chunk_sel;
static u16 chunk_cap;

static aid new_arena(u32 addr, u32 size, u16 owner, u8 count)
{
    aid a = slot_get();
    if (a) {
        AR(a)->addr = addr;
        AR(a)->size = size;
        AR(a)->owner = owner;
        AR(a)->count = count;
    }
    return a;
}

static void as_sentinel(aid a, int last)
{
    AR(a)->owner = OWN_SENTINEL;
    AR(a)->count = last ? SIG_Z : SIG_M;
    AR(a)->size = 0;
}

static void as_notthere(aid a)
{
    AR(a)->owner = OWN_NOTTHERE;
    AR(a)->count = 0;
    AR(a)->size = 0;
    AR(a)->lprev = AR(a)->lnext = 0;
}

/* Link a chunk into the list in address order: h and t (size 0, at its
   start and end) around m (a free arena, which joins the free list, or
   a block). The ends of the heap are the sentinels; the other ends of
   chunks are "not there" arenas (observed: 2 of them with one piece
   besides the first). */
static void chunk_insert(aid h, aid m, aid t)
{
    u32 base = AR(h)->addr, end = AR(t)->addr;
    aid x;
    AR(h)->lprev = AR(h)->lnext = AR(t)->lprev = AR(t)->lnext = 0;
    if (!hp->first) {
        as_sentinel(h, 0);
        as_sentinel(t, 1);
        AR(h)->prev = h;
        AR(h)->next = m;
        AR(m)->prev = h;
        AR(m)->next = t;
        AR(t)->prev = m;
        AR(t)->next = t;
        AR(h)->lprev = NOLINK;
        AR(h)->lnext = t;
        AR(t)->lprev = h;
        AR(t)->lnext = NOLINK;
        hp->first = h;
        hp->last = t;
        hp->count = 3;
    } else if (base >= AR(hp->last)->addr) {
        /* above every chunk: it takes the last sentinel */
        aid old = hp->last, lastfree = AR(old)->lprev;
        as_notthere(old);
        as_notthere(h);
        as_sentinel(t, 1);
        AR(old)->next = h;
        AR(h)->prev = old;
        AR(h)->next = m;
        AR(m)->prev = h;
        AR(m)->next = t;
        AR(t)->prev = m;
        AR(t)->next = t;
        AR(t)->lprev = lastfree;
        AR(t)->lnext = NOLINK;
        AR(lastfree)->lnext = t;
        hp->last = t;
        hp->count += 3;
    } else if (end <= AR(hp->first)->addr) {
        /* below every chunk: it takes the first sentinel */
        aid old = hp->first, firstfree = AR(old)->lnext;
        as_notthere(old);
        as_sentinel(h, 0);
        as_notthere(t);
        AR(h)->prev = h;
        AR(h)->next = m;
        AR(m)->prev = h;
        AR(m)->next = t;
        AR(t)->prev = m;
        AR(t)->next = old;
        AR(old)->prev = t;
        AR(h)->lprev = NOLINK;
        AR(h)->lnext = firstfree;
        AR(firstfree)->lprev = h;
        hp->first = h;
        hp->count += 3;
    } else {
        /* between two chunks: after the lower one's end */
        as_notthere(h);
        as_notthere(t);
        for (x = AR(hp->first)->next; x != hp->last && AR(x)->addr < end; x = AR(x)->next)
            ;
        x = AR(x)->prev;
        link_after(x, h);
        link_after(h, m);
        link_after(m, t);
    }
    if (is_free(m))
        free_link(m);
}

/* Take a chunk (h, m, t as chunk_insert left them; m out of the free
   list) out of the list; a sentinel among its ends goes to the
   neighbouring chunk's end. Not the only chunk. */
static void chunk_remove(aid h, aid m, aid t)
{
    if (h == hp->first) {
        aid n = AR(t)->next;
        as_sentinel(n, 0);
        AR(n)->prev = n;
        AR(n)->lprev = NOLINK;
        AR(n)->lnext = AR(h)->lnext;
        AR(AR(n)->lnext)->lprev = n;
        hp->first = n;
        hp->count -= 3;
    } else if (t == hp->last) {
        aid p = AR(h)->prev;
        as_sentinel(p, 1);
        AR(p)->next = p;
        AR(p)->lnext = NOLINK;
        AR(p)->lprev = AR(t)->lprev;
        AR(AR(p)->lprev)->lnext = p;
        hp->last = p;
        hp->count -= 3;
    } else {
        unlink(t);
        unlink(m);
        unlink(h);
    }
}

/* Add [lin, lin + size) to the heap. Returns its free arena, or 0. */
static aid chunk_add(u32 lin, u32 size, u16 kind, u32 handle)
{
    u32 base = round32(lin), len;
    aid h, f, t;
    len = (lin + size - base) & ~0x1FUL;
    if (lin + size < base || len < 0x40)
        return 0;
    h = new_arena(base, 0, 0, 0);
    f = h ? new_arena(base, len, 0, 0) : 0;
    t = f ? new_arena(base + len, 0, 0, 0) : 0;
    if (!t) {
        if (h)
            slot_put(h);
        if (f)
            slot_put(f);
        return 0;
    }
    chunk_insert(h, f, t);
    if (chunk_sel) {
        chunk FAR *c = MK_FP(chunk_sel, 0);
        u16 i;
        for (i = 0; i < chunk_cap && c[i].size; i++)
            ;
        if (i == chunk_cap &&
            GlobalReAlloc(chunk_sel & ~1u, (chunk_cap + 16) * sizeof(chunk), GMEM_MOVEABLE)) {
            chunk_cap += 16;
            c = MK_FP(chunk_sel, 0);
        }
        if (i < chunk_cap) {
            c[i].lin = base;
            c[i].size = len;
            c[i].dpmi = handle;
            c[i].kind = kind;
        }
    }
    return f;
}

/* The chunk around free arena a, if a is all of it, a chunk w16 recorded
   and not the only one: given back. */
static void chunk_check_empty(aid a)
{
    aid h = AR(a)->prev, t = AR(a)->next;
    chunk FAR *c = NULL;
    regs16 r;
    u16 i;
    if (!chunk_sel || AR(h)->size || AR(t)->size || AR(h)->addr != AR(a)->addr ||
        (AR(h)->owner != OWN_NOTTHERE && AR(h)->owner != OWN_SENTINEL) ||
        (AR(t)->owner != OWN_NOTTHERE && AR(t)->owner != OWN_SENTINEL) ||
        (h == hp->first && t == hp->last))
        return;
    for (i = 0; i < chunk_cap; i++) {
        c = (chunk FAR *)MK_FP(chunk_sel, 0) + i;
        if (c->size && c->lin == AR(a)->addr && c->size == AR(a)->size)
            break;
    }
    if (i == chunk_cap)
        return;
    free_unlink(a);
    chunk_remove(h, a, t);
    slot_put(h);
    slot_put(a);
    slot_put(t);
    if (c->kind == CK_DOS) {
        r.ax = 0x0101;
        r.dx = (u16)c->dpmi;
        r.ds = r.es = 0;
        dpmi31(&r);
    } else {
        dpmi_free_mem(c->dpmi);
    }
    c->size = 0;
}

/* More room for arenas: the Burgermaster moves to a larger DPMI block
   (SLOTS_STEP more arenas), the selector table to its new end (THHOOK
   18h follows); its selectors stay, its piece of the heap moves in the
   list to its new place. 0 if there is no memory. */
static int bm_grow(void)
{
    u32 nsize = bm_size + (u32)SLOTS_STEP * sizeof(arena), lin, h, olin;
    u32 nseltab = slot_end + (u32)SLOTS_STEP * sizeof(arena);
    aid lo, hi;
    if (tiles(nsize) > bm_tiles || !dpmi_alloc_mem(nsize, &lin, &h))
        return 0;
    olin = lin_of(bm);
    lin_copy(lin, olin, slot_end);
    lin_zero(lin + slot_end, nseltab - slot_end);
    lin_copy(lin + nseltab, olin + seltab_off, SELTAB_LEN);
    bm_point(lin, nsize);
    dpmi_free_mem(bm_dpmi);
    bm_dpmi = h;
    bm_size = nsize;
    seltab_off = nseltab;
    slots_add(slot_end, nseltab);
    slot_end = nseltab;
    thhook_seltab(seltab_off, (u16)SELTAB_LEN);
    if (bm_arena) {
        lo = AR(bm_arena)->prev;
        hi = AR(bm_arena)->next;
        chunk_remove(lo, bm_arena, hi);
        AR(lo)->addr = lin;
        AR(bm_arena)->addr = lin;
        AR(bm_arena)->size = nsize;
        AR(bm_arena)->selcount = (u8)tiles(nsize);
        AR(hi)->addr = lin + nsize;
        chunk_insert(lo, bm_arena, hi);
    }
    return 1;
}

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

static int grow(u32 bytes, int dos)
{
    u32 need = bytes + 0x40, size, lin, h;
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

#define WHERE_EXT   0
#define WHERE_DOS   1
#define WHERE_ANY   2

static int where_ok(aid f, u32 bytes, int where)
{
    u32 l = AR(f)->addr;
    if (where == WHERE_DOS)
        return l + bytes <= ONE_MB;
    if (where == WHERE_EXT)
        return l >= ONE_MB;
    return 1;
}

static aid find_free(u32 bytes, int where, int high)
{
    aid f;
    if (!high) {
        for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext)
            if (AR(f)->size >= bytes && where_ok(f, bytes, where))
                return f;
    } else {
        for (f = AR(hp->last)->lprev; f != hp->first; f = AR(f)->lprev)
            if (AR(f)->size >= bytes && where_ok(f, bytes, where))
                return f;
    }
    return 0;
}

/* ---- moving ---- */

/* block a's data (`bytes` of it) to linear `to`, where nothing else is:
   its selectors follow */
static void move_data(aid a, u32 to, u32 bytes)
{
    u16 s = AR(a)->handle | 1;
    u32 from = AR(a)->addr, head = bytes > 0x10000UL ? 0x10000UL : bytes;
    tmp_at(tmp_b, to, head ? head - 1 : 0);
    seg_move(s, tmp_b, (u16)((head + 1) >> 1), to);
    if (bytes > head) {
        lin_copy(to + head, from + head, bytes - head);
        set_tiles(s, tiles(bytes), to, bytes, is_code(s));
    }
}

static void lru_retarget(const arena *sv, aid old, aid nw)
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

/* block a moves into arena f (out of the free list, its size): f becomes
   the block, a's place free */
static void transplant(aid a, aid f)
{
    arena save;
    kmemcpy(&save, AR(a), sizeof save);
    move_data(a, AR(f)->addr, save.size);
    AR(f)->count = save.count;
    AR(f)->pglock = save.pglock;
    AR(f)->owner = save.owner;
    AR(f)->flags = save.flags;
    AR(f)->selcount = save.selcount;
    AR(f)->handle = save.handle;
    AR(f)->lprev = AR(f)->lnext = 0;
    if (save.lnext)
        lru_retarget(&save, a, f);
    *seltab(save.handle | 1) = f;
    AR(a)->lprev = AR(a)->lnext = 0;
    release(a);
}

static int movable(aid a)
{
    arena FAR *p = AR(a);
    return p->handle && p->count == 0 && p->owner != OWN_BURGER &&
           !is_fixed(p->handle | 1) && !(p->handle & 1);
}

/* free arena p lies right below block a: a slides down into it; the
   records swap roles (p the block, a the free space above it) */
static aid slide_down(aid a)
{
    aid p = AR(a)->prev, next = AR(a)->next;
    u32 psize = AR(p)->size, to = AR(p)->addr;
    aid pfl = AR(p)->lprev, pfn = AR(p)->lnext;
    arena save;
    kmemcpy(&save, AR(a), sizeof save);
    move_data(a, to, save.size);
    AR(p)->handle = save.handle;
    AR(p)->owner = save.owner;
    AR(p)->count = save.count;
    AR(p)->pglock = save.pglock;
    AR(p)->flags = save.flags;
    AR(p)->selcount = save.selcount;
    AR(p)->size = save.size;
    AR(p)->lprev = AR(p)->lnext = 0;
    if (save.lnext)
        lru_retarget(&save, a, p);
    mark_free(a);
    AR(a)->addr = to + save.size;
    AR(a)->size = psize;
    AR(a)->lprev = pfl;
    AR(a)->lnext = pfn;
    AR(pfl)->lnext = a;
    AR(pfn)->lprev = a;
    (void)next;
    *seltab(save.handle | 1) = p;
    if (is_free(AR(a)->next))
        merge_next(a);
    return p;
}

static int compact(void)
{
    aid a, f;
    u16 guard = hp->count + 8;
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
        f = find_free(AR(a)->size, AR(a)->addr >= ONE_MB ? WHERE_EXT : WHERE_DOS, 0);
        if (f && AR(f)->addr < AR(a)->addr) {
            f = carve(f, AR(a)->size, 0);
            if (!f)
                break;
            transplant(a, f);
            moved = 1;
            if (--guard)
                goto again;
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

static int notify_ok(aid a)
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

static void disc_desc(u16 s, u16 n, u8 db, u16 owner)
{
    u16 i;
    for (i = 0; i < n; i++)
        dpmi_set_desc(s + i * sel_inc, 0, 0, 0);
    dpmi_set_desc(s, ((u32)n << 16) | db, owner, 0);
    for (i = 0; i < n; i++)
        set_access(s + i * sel_inc, 0x13);
    *seltab(s) = owner;
}

static int discard(aid a)
{
    arena save;
    u16 s, n;
    u8 db = DB_DISC;
    kmemcpy(&save, AR(a), sizeof save);
    s = save.handle | 1;
    if (save.count || is_fixed(s) || (save.handle & 1))
        return 0;
    if (!notify_ok(a))
        return 0;
    if (save.flags & GA_NOTIFY)
        db |= DB_NOTIFY;
    if (save.flags & GA_DGROUP)
        db |= DB_DGROUP;
    if (save.flags & GA_DISCCODE)
        db |= DB_DISCCODE;
    n = tiles(save.size);
    lru_remove(a);
    disc_desc(s, n, db, save.owner);
    release(a);
    return 1;
}

static int discard_oldest(void)
{
    aid a, h = hp->lruchain;
    u16 n;
    if (!h)
        return 0;
    for (a = AR(h)->lprev, n = hp->lrucount; n; n--, a = AR(a)->lprev)
        if (discard(a))
            return 1;
    return 0;
}

/* ---- allocating ---- */

static aid alloc_space(u32 bytes, int where, u16 flags)
{
    int high = (flags & GA_REQ_DISCCODE) != 0;
    int w = where == WHERE_DOS ? WHERE_DOS : WHERE_EXT;
    aid f;
    int tries;
    for (tries = 0;; tries++) {
        f = find_free(bytes, w, high);
        if (!f && where == WHERE_ANY && tries)
            f = find_free(bytes, WHERE_ANY, high);
        if (f)
            return carve(f, bytes, high);
        if (tries == 0) {
            grow(bytes, where == WHERE_DOS);
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

/* a discarded block of no memory: GlobalAlloc(GMEM_MOVEABLE, 0), and
   under KRNL386 GlobalAlloc(GMEM_FIXED, 0) too (tk58.c: GlobalFlags
   4000h, size 0, GlobalHandle of its selector 0) */
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
    set_fixed(s, 0);
    disc_desc(s, 1, db, owner);
    return s & ~1u;
}

/* the bytes of a block of n bytes: n rounded up to 20h (observed) */
static u32 bytes_for(u32 n)
{
    return round32(n);
}

static u16 make_block(aid a, u16 flags, u16 owner, u16 *sel_out, int dosblk)
{
    u32 bytes = AR(a)->size;
    u16 n = tiles(bytes), s;
    u8 gf = 0;
    s = dpmi_alloc_desc(n);
    if (!s || !set_tiles(s, n, AR(a)->addr, bytes, 0)) {
        if (s) {
            u16 i;
            for (i = 0; i < n; i++)
                dpmi_free_desc(s + i * sel_inc);
        }
        release(a);
        return 0;
    }
    lin_zero(AR(a)->addr, bytes);
    if (flags & GMEM_NOTIFY)
        gf |= GA_NOTIFY;
    if (flags & GA_REQ_DGROUP)
        gf |= GA_DGROUP;
    if (flags & GA_REQ_DISCCODE)
        gf |= GA_DISCCODE;
    AR(a)->owner = owner;
    AR(a)->flags = gf;
    AR(a)->selcount = (u8)n;
    /* observed: a GlobalDOSAlloc block's handle is its selector, its
       count 'M', its page-lock count 1 */
    AR(a)->count = dosblk ? SIG_M : 0;
    AR(a)->pglock = dosblk ? 1 : 0;
    AR(a)->handle = dosblk ? s : s & ~1u;
    AR(a)->lprev = AR(a)->lnext = 0;
    set_fixed(s, !(flags & GMEM_MOVEABLE));
    if (flags & GMEM_DISCARDABLE)
        lru_add(a, 1);
    *seltab(s) = a;
    if (sel_out)
        *sel_out = s;
    return AR(a)->handle;
}

static u16 galloc_where(u16 flags, u32 size, u16 owner, u16 *sel_out, int where, int dosblk)
{
    u32 bytes;
    aid a;
    owner = owner_or_kernel(owner);
    if (size == 0) {
        if (dosblk)
            return 0;                   /* GlobalDOSAlloc(0): observed */
        a = alloc_discarded(flags, owner);
        if (sel_out)
            *sel_out = (u16)a | 1;
        return (u16)a;
    }
    bytes = bytes_for(size);
    if (bytes < size || (u32)tiles(bytes) > 0xFFUL)
        return 0;
    a = alloc_space(bytes, where, flags);
    if (!a)
        return 0;
    return make_block(a, flags, owner, sel_out, dosblk);
}

u16 galloc(u16 flags, u32 size, u16 owner, u16 *sel_out)
{
    return galloc_where(flags, size, owner, sel_out, WHERE_ANY, 0);
}

u16 galloc_dos(u32 size, u16 owner, u16 *seg)
{
    u16 s;
    if (!galloc_where(GMEM_FIXED, size, owner, &s, WHERE_DOS, 1))
        return 0;
    *seg = (u16)(lin_of(s) >> 4);
    return s;
}

/* ---- finding blocks ---- */

#define FB_NONE     0
#define FB_BLOCK    1
#define FB_DISC     2

static int find(u16 h, aid *a)
{
    u16 s = h | 1;
    u32 e;
    if (!bm || !h || (u32)(s >> 3) >= SELTAB_LEN / 4)
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
    if (e < FIRST_ARENA || e >= slot_end || (e & 0x1F) ||
        (AR(e)->handle | 1) != s)
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

/* ---- aliases ---- */

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

u16 galias_code(u16 sel, u16 owner)
{
    aid a;
    u16 c, i, cnt;
    alias FAR *p;
    if (find(sel, &a) != FB_BLOCK || AR(a)->size > 0x10000UL)
        return 0;
    if (!alias_sel && !galloc(GMEM_FIXED, 16 * sizeof(alias), 0, &alias_sel))
        return 0;
    p = MK_FP(alias_sel, 0);
    cnt = (u16)gsize_of_sel(alias_sel) / sizeof(alias);
    for (i = 0; i < cnt && p[i].sel; i++)
        ;
    if (i == cnt) {
        if (!GlobalReAlloc(alias_sel & ~1u, (u16)((cnt + 16) * sizeof(alias)), GMEM_MOVEABLE))
            return 0;
        p = MK_FP(alias_sel, 0);
    }
    c = dpmi_alloc_desc(1);
    if (!c)
        return 0;
    if (!dpmi_set_desc(c, AR(a)->addr, AR(a)->size - 1, 1)) {
        dpmi_free_desc(c);
        return 0;
    }
    p[i].sel = c;
    p[i].owner = owner;
    p[i].src = AR(a)->handle | 1;
    return c;
}

/* ---- freeing ---- */

static void free_block(aid a)
{
    u16 s = AR(a)->handle | 1, n = tiles(AR(a)->size), i;
    if (discardable(a))
        lru_remove(a);
    *seltab(s) = 0;
    set_fixed(s, 0);
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

void gfree_owner(u16 owner)
{
    aid a, n;
    u16 i, cnt;
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
    for (i = 1; i < SELTAB_LEN / 4; i++) {
        u16 s = (i << 3) | 4 | cpl;
        if (*seltab(s) == owner && !present(s) && lar(s))
            free_discarded(s);
    }
}

/* ---- initialisation ---- */

void global_init(void)
{
    u32 lin, h, size;
    aid lo, hi;

    tmp_a = dpmi_alloc_desc(1);
    tmp_b = dpmi_alloc_desc(1);
    /* the Burgermaster: a DPMI block of its own holding the header, room
       for SLOTS_FIRST arenas and the selector table; selectors for the
       most it can grow to (two arenas per LDT entry, and a step more:
       free arenas lie between blocks, and every block has a selector) */
    slot_end = FIRST_ARENA + (u32)SLOTS_FIRST * sizeof(arena);
    size = slot_end + SELTAB_LEN;
    bm_tiles = tiles(FIRST_ARENA + 2 * (SELTAB_LEN / 4) * sizeof(arena) +
                     (u32)SLOTS_STEP * sizeof(arena) + SELTAB_LEN);
    bm = dpmi_alloc_desc(bm_tiles);
    if (!tmp_a || !tmp_b || !bm || !dpmi_alloc_mem(size, &lin, &h))
        goto nomem;
    bm_dpmi = h;
    bm_size = size;
    seltab_off = slot_end;
    bm_point(lin, size);
    lin_zero(lin, size);
    hp->dislevel = 0x40;                /* observed */
    hp->alt[0] = hp->alt[1] = hp->alt[8] = 0xFFFF;
    slots_add(FIRST_ARENA, slot_end);
    thhook_seltab(seltab_off, (u16)SELTAB_LEN);
    /* a first piece in conventional memory, as global.c does (there
       DOSX gives KERNEL all of it): its first arena is the first
       sentinel, at 80h (observed) */
    if (!grow(0x1000, 1) && !grow(0x1000, 0))
        goto nomem;
    /* the Burgermaster's piece of the heap: its block alone */
    lo = new_arena(lin, 0, 0, 0);
    bm_arena = new_arena(lin, size, OWN_BURGER, 0);
    hi = new_arena(lin + size, 0, 0, 0);
    if (!lo || !bm_arena || !hi)
        goto nomem;
    AR(bm_arena)->handle = bm;
    AR(bm_arena)->selcount = (u8)tiles(size);
    chunk_insert(lo, bm_arena, hi);
    set_fixed(bm, 1);
    *seltab(bm) = bm_arena;
    /* the chunk records: chunks added from now on can be given back */
    chunk_cap = 16;
    if (!galloc(GMEM_FIXED, chunk_cap * sizeof(chunk), 0, &chunk_sel))
        goto nomem;
    return;
nomem:
    kputs("w16: no memory for the global heap\r\n");
    kexit_to_dos(0xFF);
}

/* ---- helpers for the rest of KERNEL ---- */

u16 gowner(u16 h)
{
    aid a;
    switch (find(h, &a)) {
    case FB_BLOCK:
        return AR(a)->owner;
    case FB_DISC:
        return (u16)a;
    }
    return 0;
}

int gset_code(u16 sel)
{
    aid a;
    if (find(sel, &a) != FB_BLOCK)
        return 0;
    return set_tiles(AR(a)->handle | 1, tiles(AR(a)->size), AR(a)->addr, AR(a)->size, 1);
}

u32 gsize_of_sel(u16 sel)
{
    aid a;
    alias FAR *p;
    if (find(sel, &a) == FB_BLOCK)
        return AR(a)->size;
    p = alias_find(sel, NULL);
    return p ? gsize_of_sel(p->src) : 0;
}

/* Under WIN386 the block's pages are locked in memory too (DPMI 0600h,
   unlocked with 0601h when the count is 0 again); DOSX does not page. */
static void page_lock(aid a, u16 fn)
{
    regs16 r;
    if (!k386_enhanced || !AR(a)->size)
        return;
    r.ax = fn;
    r.bx = (u16)(AR(a)->addr >> 16);
    r.cx = (u16)AR(a)->addr;
    r.si = (u16)(AR(a)->size >> 16);
    r.di = (u16)AR(a)->size;
    r.ds = r.es = 0;
    dpmi31(&r);
}

/* GlobalPageLock/GlobalPageUnlock (selector.c): observed (tk58.c,
   tk17.c) both counts move together, and the page-lock count is
   returned. UNVERIFIED: limits (the counts stop at FFh here). */
u16 gpage_lock(u16 h, int up)
{
    aid a;
    if (find(h, &a) != FB_BLOCK)
        return 0;
    if (up) {
        if (AR(a)->pglock == 0xFF || AR(a)->count == 0xFF)
            return AR(a)->pglock;
        if (!AR(a)->pglock)
            page_lock(a, 0x0600);
        AR(a)->pglock++;
        AR(a)->count++;
    } else {
        if (!AR(a)->pglock)
            return 0;
        AR(a)->pglock--;
        if (AR(a)->count)
            AR(a)->count--;
        if (!AR(a)->pglock)
            page_lock(a, 0x0601);
    }
    return AR(a)->pglock;
}

/* ---- [w16] heapcheck=1 (as global.c) ---- */
u16 heap_check_on;
u32 api_caller;
static u16 heap_check_busy;

const char *local_check(u16 seg, u16 *at);      /* local.c */

static const char *global_check(u32 *bad)
{
    aid a, n, f, pf;
    u16 items = 0;
    *bad = 0;
    if (!bm)
        return 0;
    for (a = hp->first; ; a = n) {
        *bad = a;
        if (a < FIRST_ARENA || a >= slot_end || (a & 0x1F))
            return "global: arena outside the arenas' room";
        n = AR(a)->next;
        if (a == hp->last)
            break;
        if (AR(n)->prev != a)
            return "global: next arena's prev is not this one";
        if (AR(a)->owner != OWN_NOTTHERE && AR(n)->owner != OWN_NOTTHERE &&
            AR(a)->addr + AR(a)->size != AR(n)->addr)
            return "global: arena size does not reach the next arena";
        if (!is_free(a) && AR(a)->owner != OWN_SENTINEL && AR(a)->owner != OWN_NOTTHERE &&
            AR(a)->handle) {
            u16 s = AR(a)->handle | 1;
            if (*seltab(s) != a)
                return "global: selector table entry is not this arena";
            if (!present(s) || lin_of(s) != AR(a)->addr)
                return "global: block's selector is not at the block";
            {
                u32 bytes = AR(a)->size, want;
                u16 i, t, l, nt = tiles(bytes);
                if ((AR(a)->flags & GA_DGROUP) && nt > 1)
                    return "global: a DGROUP is more than one selector";
                for (i = 0; i < nt && a != bm_arena; i++) {
                    t = s + i * sel_inc;
                    want = bytes - ((u32)i << 16) - 1;
                    l = lsl(t);
                    if (!present(t) || lin_of(t) != AR(a)->addr + ((u32)i << 16) ||
                        (l != (u16)want && !(want > 0xFFFFUL && l == 0xFFFF)))
                        return "global: a tile's base or limit is not the block's";
                }
            }
        }
        if (++items == 0)
            return "global: no end";
    }
    pf = hp->first;
    for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext) {
        *bad = f;
        if (!f || !is_free(f) || AR(f)->lprev != pf || AR(f)->addr <= AR(pf)->addr)
            return "global: bad free list";
        pf = f;
    }
    *bad = 0;
    return 0;
}

static void hc_arena(const char *what, aid a)
{
    u16 i;
    kputs(what);
    kputhex((u16)(a >> 16));
    kputhex((u16)a);
    if (a >= FIRST_ARENA && a < slot_end) {
        kputs(":");
        for (i = 0; i < 0x20; i += 2) {
            kputs(" ");
            kputhex(*(u16 FAR *)bmp(a + i));
        }
    }
    kputs("\r\n");
}

void heap_check(const char *where, u32 caller)
{
    u32 bad;
    const char *why;
    if (!heap_check_on || heap_check_busy)
        return;
    heap_check_busy = 1;
    why = global_check(&bad);
    heap_check_busy = 0;
    if (why) {
        text_mode();
        heap_check_on = 0;
        kputs("w16 heapcheck: ");
        kputs(why);
        kputs(" at ");
        kputs(where);
        kputs("\r\n");
        hc_arena("  arena ", bad);
        if (bad >= FIRST_ARENA && bad < slot_end) {
            hc_arena("  prev  ", AR(bad)->prev);
            hc_arena("  next  ", AR(bad)->next);
        }
        heap_fail(why, where, caller, 0, 0, 0);
    }
}

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
    text_mode();
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
    (void)bad;
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

static u16 realloc_discarded(u16 h, u32 size, u16 flags)
{
    u16 s = h | 1, owner = lsl(s), n = disc_tiles(s), need, i, gf = 0;
    u32 bytes = bytes_for(size);
    u8 db = disc_bits(s);
    aid a;

    if (flags & GMEM_MODIFY) {
        db = (u8)((db & ~DB_DISC) | ((flags & GMEM_DISCARDABLE) ? DB_DISC : 0));
        dpmi_set_desc(s, ((u32)n << 16) | db, owner, 0);
        set_access(s, 0x13);
        return h;
    }
    if (!size)
        return h;
    if (bytes < size)
        return 0;
    need = tiles(bytes);
    a = alloc_space(bytes, WHERE_ANY, flags);
    if (!a)
        return 0;
    if (need > n) {
        u16 ns = dpmi_alloc_desc(need);
        if (!ns) {
            release(a);
            return 0;
        }
        *seltab(s) = 0;
        for (i = 0; i < n; i++)
            dpmi_free_desc(s + i * sel_inc);
        s = ns;
        h = (h & 1) ? ns : ns & ~1u;
        n = need;
    }
    for (i = need; i < n; i++)
        dpmi_free_desc(s + i * sel_inc);
    set_tiles(s, need, AR(a)->addr, bytes, (db & DB_DISCCODE) != 0);
    lin_zero(AR(a)->addr, bytes);
    if (db & DB_DGROUP)
        gf |= GA_DGROUP;
    if (db & DB_DISCCODE)
        gf |= GA_DISCCODE;
    AR(a)->owner = owner;
    AR(a)->flags = (u8)gf;
    AR(a)->selcount = (u8)need;
    AR(a)->count = AR(a)->pglock = 0;
    AR(a)->handle = h & ~1u;
    AR(a)->lprev = AR(a)->lnext = 0;
    set_fixed(s, 0);
    if (db & DB_DISC)
        lru_add(a, 1);
    *seltab(s) = a;
    return h & ~1u;
}

/* more 64K tiles than the block has: a new selector run (observed,
   tk17.c TK17A: the handle changes, the data is kept); under KRNL386 a
   fixed block too without GMEM_MOVEABLE (observed) */
static u16 grow_tiles(aid a, u16 h, u32 bytes, u16 flags)
{
    u16 os = h | 1, on = tiles(AR(a)->size), ns, nn, i;
    u8 hold;
    u32 old = AR(a)->size;
    int code = is_code(os), fixed = is_fixed(os);
    aid f;

    if (AR(a)->count && !fixed)
        return 0;                       /* UNVERIFIED: a locked block */
    nn = tiles(bytes);
    ns = dpmi_alloc_desc(nn);
    if (!ns)
        return 0;
    hold = AR(a)->count;
    if (hold != 0xFF)
        AR(a)->count++;
    f = alloc_space(bytes, AR(a)->addr >= ONE_MB ? WHERE_ANY : WHERE_DOS, flags);
    AR(a)->count = hold;
    if (!f) {
        for (i = 0; i < nn; i++)
            dpmi_free_desc(ns + i * sel_inc);
        return 0;
    }
    transplant(a, f);
    a = f;
    *seltab(os) = 0;
    set_fixed(os, 0);
    for (i = 0; i < on; i++)
        dpmi_free_desc(os + i * sel_inc);
    h = (h & 1) ? ns : ns & ~1u;
    AR(a)->handle = h;
    AR(a)->selcount = (u8)nn;
    set_fixed(ns, fixed);
    *seltab(ns) = a;
    set_tiles(ns, nn, AR(a)->addr, AR(a)->size, code);
    if (AR(a)->size > old)
        lin_zero(AR(a)->addr + old, AR(a)->size - old);
    return h;
}

static int make_room_after(aid a, u32 bytes)
{
    aid nx = AR(a)->next, after, f;
    u32 room, t;
    int w, i;
    if (!movable(nx))
        return 0;
    after = AR(nx)->next;
    room = AR(a)->size + AR(nx)->size + (is_free(after) ? AR(after)->size : 0);
    if (room < bytes)
        return 0;
    t = AR(nx)->size;
    w = AR(nx)->addr >= ONE_MB ? WHERE_EXT : WHERE_DOS;
    for (i = 0; i < 2; i++) {
        for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext)
            if (f != after && AR(f)->size >= t && where_ok(f, t, w))
                break;
        if (f != hp->last || i || !grow(t, w == WHERE_DOS))
            break;
    }
    if (f == hp->last)
        return 0;
    f = carve(f, t, 0);
    if (!f)
        return 0;
    transplant(nx, f);
    return is_free(AR(a)->next) && AR(a)->size + AR(AR(a)->next)->size >= bytes;
}

/* the free arena right after a gives `extra` bytes to a */
static void take_next(aid a, u32 extra)
{
    aid part = carve(AR(a)->next, extra, 0);
    if (!part)
        return;
    AR(a)->size += AR(part)->size;
    unlink(part);
    slot_put(part);
}

u16 KAPI GlobalReAlloc(u16 h, u32 size, u16 flags)            /* KERNEL.16 */
{
    aid a;
    u16 s, n, cur;
    u32 old, bytes, want;
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
        if (!(flags & GMEM_MOVEABLE) || !discardable(a))
            return 0;
        return discard(a) ? h : 0;
    }
    want = bytes_for(size);
    if (want < size)
        return 0;
    old = AR(a)->size;
    n = tiles(old);
    if (tiles(want) > n)
        return grow_tiles(a, h, want, flags);
    if (want < old) {
        aid r = slot_get();
        if (r) {
            AR(r)->addr = AR(a)->addr + want;
            AR(r)->size = old - want;
            link_after(a, r);
            AR(a)->size = want;
            release(r);
        }
    } else if (want > old) {
        aid nx = AR(a)->next;
        if (is_free(nx) && old + AR(nx)->size >= want) {
            take_next(a, want - old);
        } else if (make_room_after(a, want)) {
            take_next(a, want - old);
        } else if (!is_fixed(s) || (flags & GMEM_MOVEABLE)) {
            aid f;
            u8 hold;
            if (AR(a)->count && !is_fixed(s))
                return 0;
            hold = AR(a)->count;
            if (hold != 0xFF)
                AR(a)->count++;
            f = alloc_space(want, AR(a)->addr >= ONE_MB ? WHERE_ANY : WHERE_DOS, flags);
            AR(a)->count = hold;
            if (!f)
                return 0;
            transplant(a, f);
            a = f;
        } else {
            return 0;
        }
    }
    bytes = AR(a)->size;
    AR(a)->selcount = (u8)tiles(bytes);
    set_tiles(s, tiles(bytes), AR(a)->addr, bytes, is_code(s));
    for (cur = tiles(bytes); cur < n; cur++)
        dpmi_free_desc(s + cur * sel_inc);
    if (bytes > old)
        lin_zero(AR(a)->addr + old, bytes - old);
    return h;
}

u16 KAPI GlobalFree(u16 h)                                    /* KERNEL.17 */
{
    aid a;
    u16 i;
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
    aid a;
    alias FAR *p;
    heap_check("GlobalLock", api_caller);
    if (find(h, &a) != FB_BLOCK) {
        p = alias_find(h, NULL);
        return p ? MK_FP(p->sel, 0) : NULL;
    }
    if (discardable(a) && !is_fixed(AR(a)->handle | 1) && AR(a)->count < 0xFF)
        AR(a)->count++;
    return MK_FP(AR(a)->handle | 1, 0);
}

u16 KAPI GlobalUnlock(u16 h)                                  /* KERNEL.19 */
{
    aid a;
    if (find(h, &a) != FB_BLOCK || !discardable(a) || is_fixed(AR(a)->handle | 1))
        return 0;
    if (AR(a)->count)
        AR(a)->count--;
    return AR(a)->count;
}

u32 KAPI GlobalSize(u16 h)                                    /* KERNEL.20 */
{
    return gsize_of_sel(h);
}

u32 KAPI_ES GlobalHandle(u16 sel)                             /* KERNEL.21 */
{
    aid a;
    alias FAR *p;
    if (find(sel, &a) == FB_BLOCK)
        return ((u32)(AR(a)->handle | 1) << 16) | AR(a)->handle;
    p = alias_find(sel, NULL);
    if (p)
        return ((u32)p->sel << 16) | p->sel;
    return 0;
}

/* observed (tk58.c): the lock count shows for every block, fixed ones
   too (GlobalFix) */
u16 KAPI GlobalFlags(u16 h)                                   /* KERNEL.22 */
{
    aid a;
    u16 f;
    u8 db;
    switch (find(h, &a)) {
    case FB_BLOCK:
        f = (AR(a)->flags & GA_SHOWN) << 8;
        if (discardable(a))
            f |= GMEM_DISCARDABLE;
        if (AR(a)->count != SIG_M || !(AR(a)->handle & 1))
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

/* observed (tk17.c, KRNL386): LockSegment of a fixed block returns its
   handle (even), not h | 1 */
u16 KAPI LockSegment(u16 seg)                                 /* KERNEL.23 */
{
    aid a;
    if (find(seg, &a) != FB_BLOCK)
        return 0;
    if (!is_fixed(AR(a)->handle | 1) && discardable(a) && AR(a)->count < 0xFF)
        AR(a)->count++;
    return AR(a)->handle;
}

u16 KAPI UnlockSegment(u16 seg)                               /* KERNEL.24 */
{
    aid a;
    if (find(seg, &a) != FB_BLOCK)
        return 0;
    if (!is_fixed(AR(a)->handle | 1) && discardable(a) && AR(a)->count)
        AR(a)->count--;
    return AR(a)->handle;
}

static u32 largest_free(void)
{
    aid f;
    u32 best = 0;
    for (f = AR(hp->first)->lnext; f != hp->last; f = AR(f)->lnext)
        if (AR(f)->size > best)
            best = AR(f)->size;
    return best;
}

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
    d = d > 0x40 ? d - 0x40 : 0;
    if (d > best)
        best = d;
    return best > 0x20 ? best - 0x20 : 0;      /* UNVERIFIED for KRNL386 */
}

void KAPI GlobalFreeAll(u16 pdb)                              /* KERNEL.26 */
{
    gfree_owner(pdb ? pdb : task_pdb());
}

u32 KAPI GlobalMasterHandle(void)                             /* KERNEL.28 */
{
    return ((u32)bm << 16) | bm;
}

void FAR * KAPI GlobalWire(u16 h)                             /* KERNEL.111 */
{
    aid a, f;
    if (find(h, &a) != FB_BLOCK)
        return NULL;
    if (movable(a)) {
        f = find_free(AR(a)->size, AR(a)->addr >= ONE_MB ? WHERE_EXT : WHERE_DOS, 0);
        if (f && AR(f)->addr < AR(a)->addr) {
            f = carve(f, AR(a)->size, 0);
            if (f) {
                transplant(a, f);
                a = f;
            }
        }
    }
    if (AR(a)->count < 0xFF)
        AR(a)->count++;
    return MK_FP(AR(a)->handle | 1, 0);
}

u16 KAPI GlobalUnwire(u16 h)                                  /* KERNEL.112 */
{
    aid a;
    if (find(h, &a) != FB_BLOCK || !AR(a)->count)
        return 0;
    return --AR(a)->count ? 0 : 0xFFFF;
}

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

u16 KAPI GlobalLRUOldest(u16 h)                               /* KERNEL.163 */
{
    aid a;
    if (find(h, &a) != FB_BLOCK || !discardable(a))
        return 0;
    lru_remove(a);
    lru_add(a, 0);
    return AR(a)->handle;
}

u16 KAPI GlobalLRUNewest(u16 h)                               /* KERNEL.164 */
{
    aid a;
    if (find(h, &a) != FB_BLOCK || !discardable(a))
        return 0;
    lru_remove(a);
    lru_add(a, 1);
    return AR(a)->handle;
}

u32 KAPI GetFreeSpace(u16 flags)                              /* KERNEL.169 */
{
    aid a;
    u32 t = dpmi_largest();
    for (a = hp->first; ; a = AR(a)->next) {
        if (is_free(a) || (!flags && discardable(a) && AR(a)->count == 0))
            t += AR(a)->size;
        if (a == hp->last)
            break;
    }
    return t;
}

/* observed (tk58.c, tk17.c): under KRNL386 GlobalFix counts for fixed
   blocks too; AX is the handle passed */
u16 KAPI GlobalFix(u16 h)                                     /* KERNEL.197 */
{
    aid a;
    if (find(h, &a) == FB_BLOCK && AR(a)->count < 0xFF && AR(a)->owner != OWN_BURGER)
        AR(a)->count++;
    return h;
}

u16 KAPI GlobalUnfix(u16 h)                                   /* KERNEL.198 */
{
    aid a;
    if (find(h, &a) == FB_BLOCK && AR(a)->count && AR(a)->owner != OWN_BURGER)
        AR(a)->count--;
    return h;
}

void KAPI FarSetOwner(u16 h, u16 owner)                       /* KERNEL.403 */
{
    aid a;
    if (find(h, &a) == FB_BLOCK)
        AR(a)->owner = owner;
}

u16 KAPI FarGetOwner(u16 h)                                   /* KERNEL.404 */
{
    return gowner(h);
}

#endif /* KRNL386 */
