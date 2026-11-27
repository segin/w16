/*
 * global.c - w16's TOOLHELP.DLL: the global and local heaps and memory
 * (GlobalInfo, GlobalFirst, GlobalNext, GlobalEntryHandle,
 * GlobalEntryModule, LocalInfo, LocalFirst, LocalNext, MemoryRead,
 * MemoryWrite; ARCHITECTURE.md 22).
 *
 * The global heap is read in the layout of the KERNEL that runs, which is
 * Windows 3.1's for both of w16's KERNELs (kernel/global.c: KRNL286,
 * global386.c: KRNL386; Schulman ch. 5, Pietrek ch. 2). What a
 * GLOBALENTRY holds, as observed with tests/toolhelp/thp.c on Windows
 * 3.1 (Microsoft's TOOLHELP):
 *  - KRNL286: dwAddress the arena's own linear address (the block's less
 *    10h), dwBlockSize its size field in bytes, wcLock its byte 00h ('M'
 *    4Dh for a fixed block, 'Z' for the last sentinel), wcPageLock 0;
 *    dwNext the next arena's selector.
 *  - KRNL386: dwAddress the block's address (arena 08h), dwBlockSize
 *    arena 0Ch, wcLock 14h, wcPageLock 15h; dwNext the next arena's
 *    offset in the Burgermaster.
 *  - Both: hBlock the arena's handle, hOwner its owner, wFlags its flags
 *    byte (04h DGROUP, 08h discardable code) and GF_PDB_OWNER when the
 *    owner is a PDB; dwNext 0 at the last sentinel (whose next is
 *    itself); dwNextAlt 0 in a walk of the whole heap and after
 *    GlobalEntryHandle, else the next in the LRU ring (newest first, 0
 *    once round) or the free list (KRNL286: 0 at the last sentinel;
 *    KRNL386: the last sentinel is the list's last entry).
 *  - wType: free (owner 0), SENTINEL (FFFFh), INTERNAL (the
 *    Burgermaster's FFFDh and "not there" FFFCh: never BURGERMASTER), a
 *    module's table MODULE, its segments CODE, DATA or DGROUP (the
 *    automatic data segment) with wData the segment number, its
 *    resources RESOURCE with wData the type, a task database TASK, all
 *    else UNKNOWN.
 *  - GlobalInfo: wcItems the Burgermaster's arena count + 1 (a walk
 *    visits one less), wcItemsFree and wcItemsLRU its counts.
 */
#include "th.h"

/* ---- the heap's layout ---- */
static BOOL k386;               /* KRNL386's arenas */
static WORD bmsel;              /* the Burgermaster */

/* an arena, read into one form for both layouts */
typedef struct {
    DWORD id;                   /* KRNL286: its selector; KRNL386: its offset */
    DWORD addr, size, next, lprev, lnext;
    WORD handle, owner, lock, plock, flags;
} ARENA;

static void heap_init(void)
{
    DWORD m = kGlobalMasterHandle();
    bmsel = HIWORD(m);
    /* KRNL386's header has the first arena's offset (80h) as a dword at
       06h; KRNL286's the first sentinel's selector (always odd) */
    k386 = PW(bmsel, 6) == 0x80 && PW(bmsel, 8) == 0;
}

/* The Burgermaster's linear address, asked each time: when its arenas
   fill up, KRNL386 moves it to a larger block and frees the old one, its
   selector staying (w16's kernel/global386.c, bm_grow). The address kept
   from the first walk then pointed at freed pages: the VM Task Manager,
   which walks the heap every 5 s, ended Windows with a page fault in
   GlobalNext once enough blocks had been allocated (the user's report,
   2026-11-27; tests/vmtasksoak.sh). */
static DWORD bm_base(void)
{
    if (!bmsel)
        heap_init();
    return GetSelectorBase(bmsel);
}

static BOOL arena(DWORD id, ARENA *a)
{
    if (!bmsel)
        heap_init();
    a->id = id;
    if (k386) {
        BYTE r[0x20];
        if (!id || !lread(bm_base() + id, r, 0x20))
            return FALSE;
        a->next = *(DWORD *)&r[0x00];
        a->addr = *(DWORD *)&r[0x08];
        a->size = *(DWORD *)&r[0x0C];
        a->handle = *(WORD *)&r[0x10];
        a->owner = *(WORD *)&r[0x12];
        a->lock = r[0x14];
        a->plock = r[0x15];
        a->flags = r[0x16];
        a->lprev = *(DWORD *)&r[0x18];
        a->lnext = *(DWORD *)&r[0x1C];
    } else {
        WORD s = (WORD)id;
        if (!readable(s, 0, 0x10))
            return FALSE;
        a->lock = PB(s, 0x00);
        a->owner = PW(s, 0x01);
        a->size = (DWORD)PW(s, 0x03) << 4;
        a->flags = PB(s, 0x05);
        a->next = PW(s, 0x08);
        a->handle = PW(s, 0x0A);
        a->lprev = PW(s, 0x0C);
        a->lnext = PW(s, 0x0E);
        a->addr = GetSelectorBase(s);
        a->plock = 0;
    }
    return TRUE;
}

static DWORD hdr(WORD o286, WORD o386, BOOL dw)
{
    if (!bmsel)
        heap_init();
    if (k386)
        return dw ? *(DWORD FAR *)MAKELP(bmsel, o386) : PW(bmsel, o386);
    return PW(bmsel, o286);
}
#define H_COUNT()       (WORD)hdr(0x04, 0x04, 0)
#define H_FIRST()       hdr(0x06, 0x06, 1)
#define H_LRU()         hdr(0x1A, 0x20, 1)
#define H_LRUCOUNT()    (WORD)hdr(0x1C, 0x24, 0)
#define H_FREECOUNT()   (WORD)hdr(0x22, 0x2E, 0)

static BOOL is_pdb(WORD s)
{
    return s && readable(s | 1, 0, 2) && PW(s | 1, 0) == 0x20CD;      /* INT 20h */
}

/* a module's resource holding handle h: its type, else 0 */
static BOOL find_resource(WORD m, WORD h, WORD *type)
{
    WORD t = PW(m, 0x24), end = PW(m, 0x26), n, i;
    if (!t || t >= end)
        return FALSE;
    t += 2;                             /* the alignment shift */
    while (t + 8 <= end && PW(m, t)) {
        n = PW(m, t + 2);
        for (i = 0; i < n; i++) {
            WORD rh = PW(m, t + 8 + i * 12 + 8);
            if (rh && (rh | 1) == (h | 1)) {
                *type = PW(m, t) & 0x7FFF;
                if (!(PW(m, t) & 0x8000))
                    *type = 0;          /* a type with a name */
                return TRUE;
            }
        }
        t += 8 + n * 12;
    }
    return FALSE;
}

static void classify(GLOBALENTRY FAR *g)
{
    WORD o = g->hOwner, h = g->hBlock, m, n, i, st, type;
    g->wType = GT_UNKNOWN;
    g->wData = 0;
    if (!o) {
        g->wType = GT_FREE;
        return;
    }
    if (o == 0xFFFF) {
        g->wType = GT_SENTINEL;
        return;
    }
    if (o >= 0xFFFC) {
        g->wType = GT_INTERNAL;
        return;
    }
    if (!h)
        return;
    if (is_module(o)) {
        m = o | 1;
        if ((h | 1) == m) {
            g->wType = GT_MODULE;
            return;
        }
        n = PW(m, 0x1C);
        st = PW(m, 0x22);
        for (i = 0; i < n; i++)
            if ((PW(m, st + i * 10 + 8) | 1) == (h | 1) && PW(m, st + i * 10 + 8)) {
                /* data by the arena's DGROUP flag (observed: a data
                   segment whose arena lacks it, as 3.1 KERNEL's segment
                   4, is CODE; one with it that is not the automatic data
                   segment, DATA) */
                g->wData = i + 1;
                if (!(g->wFlags & 0x04))
                    g->wType = GT_CODE;
                else
                    g->wType = i + 1 == PW(m, 0x0E) ? GT_DGROUP : GT_DATA;
                return;
            }
        /* a resource; not in a module without segments (observed: a
           font file's fonts are UNKNOWN) */
        if (n && find_resource(m, h, &type)) {
            g->wType = GT_RESOURCE;
            g->wData = type;
        }
        return;
    }
    if ((h | 1) == (o | 1) && is_task(o))
        g->wType = GT_TASK;
}

static WORD heap_of(WORD h)
{
    WORD s = h | 1, p, off = k386 ? 0x28 : 0x22;
    if (!readable(s, 0, 0x10))
        return 0;
    p = PW(s, 6);
    return readable(s, p + off, 2) && PW(s, p + off) == 0x484C;        /* "LH" */
}

/* the entry for arena a; how: GLOBAL_* of the walk, 0xFFFF for none */
static BOOL fill(GLOBALENTRY FAR *g, DWORD id, WORD how)
{
    ARENA a;
    if (!arena(id, &a))
        return FALSE;
    g->dwAddress = a.addr;
    g->dwBlockSize = a.size;
    g->hBlock = a.handle;
    g->wcLock = a.lock;
    g->wcPageLock = a.plock;
    g->hOwner = a.owner;
    g->wFlags = a.flags;
    if (is_pdb(a.owner)) {
        /* a PDB's block: owned by the task whose PDB it is (observed) */
        WORD t = thhook[7], k = 0;
        g->wFlags |= GF_PDB_OWNER;
        for (; t && k < 1000 && is_task(t); t = PW(t | 1, 0), k++)
            if ((PW(t | 1, 0x60) | 1) == (a.owner | 1)) {
                g->hOwner = t;
                break;
            }
    }
    g->wHeapPresent = a.handle && a.owner && a.owner < 0xFFFC ? heap_of(a.handle) : 0;
    g->dwNext = a.next == id ? 0 : a.next;
    g->dwNextAlt = 0;
    if (how == GLOBAL_LRU)
        g->dwNextAlt = a.lnext == H_LRU() ? 0 : a.lnext;
    else if (how == GLOBAL_FREE) {
        ARENA n;
        g->dwNextAlt = a.lnext;
        if (a.lnext == id || a.lnext == 0xFFFF || a.lnext == 0xFFFFFFFFUL)
            g->dwNextAlt = 0;
        else if (!k386 && arena(a.lnext, &n) && n.owner == 0xFFFF)
            g->dwNextAlt = 0;           /* KRNL286: the walk stops before */
    }
    classify(g);
    return TRUE;
}

static WORD how_of(WORD flags)
{
    return flags == GLOBAL_LRU || flags == GLOBAL_FREE ? flags : GLOBAL_ALL;
}

BOOL THAPI GlobalInfo(GLOBALINFO FAR *gi)
{
    if (!good(gi, sizeof *gi))
        return FALSE;
    gi->wcItems = H_COUNT() + 1;
    gi->wcItemsFree = H_FREECOUNT();
    gi->wcItemsLRU = H_LRUCOUNT();
    return TRUE;
}

BOOL THAPI GlobalFirst(GLOBALENTRY FAR *g, WORD flags)
{
    DWORD id;
    ARENA a;
    if (!good(g, sizeof *g))
        return FALSE;
    flags = how_of(flags);
    if (flags == GLOBAL_LRU)
        id = H_LRU();
    else if (flags == GLOBAL_FREE) {
        if (!arena(H_FIRST(), &a))
            return FALSE;
        id = a.lnext;
    } else
        id = H_FIRST();
    if (!id || id == 0xFFFF)
        return FALSE;
    return fill(g, id, flags);
}

BOOL THAPI GlobalNext(GLOBALENTRY FAR *g, WORD flags)
{
    DWORD id;
    if (!good(g, sizeof *g))
        return FALSE;
    flags = how_of(flags);
    id = flags == GLOBAL_ALL ? g->dwNext : g->dwNextAlt;
    if (!id)
        return FALSE;
    return fill(g, id, flags);
}

/* the arena of a block, through KERNEL's selector table (THHOOK 16h its
   length, 18h its offset in the Burgermaster) */
static DWORD arena_of(WORD h)
{
    DWORD off = *(DWORD FAR *)&thhook[0x0C], id;
    WORD len = thhook[0x0B], idx = (h | 1) >> 3;
    ARENA a;
    if (!h || (WORD)(idx * (k386 ? 4 : 2)) >= len)
        return 0;
    if (!bmsel)
        heap_init();
    if (k386) {
        if (!lread(bm_base() + off + (DWORD)idx * 4, &id, 4))
            return 0;
    } else
        id = PW(bmsel, (WORD)off + idx * 2);
    if (!id || !arena(id, &a) || !a.handle || (a.handle | 1) != (h | 1))
        return 0;
    return id;
}

BOOL THAPI GlobalEntryHandle(GLOBALENTRY FAR *g, WORD h)
{
    DWORD id;
    if (!good(g, sizeof *g) || !(id = arena_of(h)))
        return FALSE;
    return fill(g, id, 0xFFFF);
}

BOOL THAPI GlobalEntryModule(GLOBALENTRY FAR *g, WORD hmod, WORD seg)
{
    WORD m = hmod | 1, h;
    if (!good(g, sizeof *g) || !is_module(hmod) || !seg || seg > PW(m, 0x1C))
        return FALSE;
    h = PW(m, PW(m, 0x22) + (seg - 1) * 10 + 8);
    return GlobalEntryHandle(g, h);
}

/* ===== local heaps: a segment's word 06h points to the heap's
   information block (Schulman ch. 5; KRNL286's has "LH" at 22h, the
   arena count at 04h, the first arena at 06h; KRNL386's "LH" at 28h).
   An arena: 00h the previous arena | 1 busy | 2 moveable, 02h the next
   (kernel/local.c). Observed for a LOCALENTRY: wAddress the arena + 4
   (fixed, free) or + 6 (moveable, whose handle is at the arena + 4),
   wSize the next arena less wAddress (KRNL286: less 2 more), wNext the next arena, hHeap
   the selector; the walk ends before the last sentinel, so LocalInfo's
   count is one more than it visits. ===== */
static WORD lheap(WORD h, WORD *info)
{
    WORD s = h | 1, p, off;
    if (!h || !readable(s, 0, 0x10))
        return 0;
    if (!bmsel)
        heap_init();
    off = k386 ? 0x28 : 0x22;
    p = PW(s, 6);
    if (!readable(s, p, off + 2) || PW(s, p + off) != 0x484C)
        return 0;
    *info = p;
    return s;
}

static WORD gdi_type(WORD s, WORD addr)
{
    WORD m;
    if (!readable(s, addr, 4))
        return 0;
    m = PW(s, addr + 2) & 0x5FFF;       /* 'GO' + type - 1; flags 2000h, 8000h */
    return m >= 0x4F47 && m <= 0x4F50 ? m - 0x4F46 : 0;
}

static BOOL lfill(LOCALENTRY FAR *e, WORD s, WORD a)
{
    WORD w, next, ds;
    if (!readable(s, a, 6))
        return FALSE;
    w = PW(s, a);
    next = PW(s, a + 2);
    e->wType = 0;
    if (!(w & 1)) {
        e->hHandle = 0;
        e->wAddress = a + 4;
        e->wFlags = LF_FREE;
        e->wcLock = 0;
        e->wType = LT_FREE;
    } else if (w & 2) {
        e->hHandle = PW(s, a + 4);
        e->wAddress = a + 6;
        e->wFlags = LF_MOVEABLE;
        e->wcLock = readable(s, e->hHandle, 4) ? PB(s, e->hHandle + 3) : 0;
    } else {
        e->hHandle = a + 4;
        e->wAddress = a + 4;
        e->wFlags = LF_FIXED;
        e->wcLock = 0;
    }
    e->wSize = next - e->wAddress - (k386 ? 0 : 2);  /* observed */
    e->hHeap = s;
    ds = dgroup_of(hu) | 1;
    e->wHeapType = s == ds ? USER_HEAP : s == (dgroup_of(hg) | 1) ? GDI_HEAP : NORMAL_HEAP;
    if (e->wHeapType == GDI_HEAP && e->wFlags != LF_FREE)
        e->wType = gdi_type(s, e->wAddress);
    e->wNext = next;
    return TRUE;
}

BOOL THAPI LocalInfo(LOCALINFO FAR *li, WORD h)
{
    WORD s = h | 1, p;
    if (!good(li, sizeof *li) || !h || !readable(s, 0, 8))
        return FALSE;
    p = PW(s, 6);
    li->wcItems = readable(s, p + 4, 2) ? PW(s, p + 4) : 0;
    return TRUE;
}

BOOL THAPI LocalFirst(LOCALENTRY FAR *e, WORD h)
{
    WORD s, info;
    if (!good(e, sizeof *e) || !(s = lheap(h, &info)))
        return FALSE;
    return lfill(e, s, PW(s, info + 6));
}

BOOL THAPI LocalNext(LOCALENTRY FAR *e)
{
    WORD s, n;
    if (!good(e, sizeof *e))
        return FALSE;
    s = e->hHeap;
    n = e->wNext;
    if (!readable(s, n, 6) || !lfill(e, s, n))
        return FALSE;
    if (PW(s, n + 2) == n) {
        /* the last sentinel (its next is itself): its entry, wNext 0,
           and FALSE (observed) */
        e->wNext = 0;
        return FALSE;
    }
    return TRUE;
}

/* ===== MemoryRead, MemoryWrite: any selector there is (observed: an
   alias too), up to its limit (a request past it is cut short; one that
   starts past it gives 0), through a selector of our own at the
   segment's base, so that code and blocks over 64 KB are reached. ===== */
static DWORD mem(WORD sel, DWORD off, BYTE FAR *buf, DWORD n, BOOL write)
{
    DWORD lim, base, done = 0;
    if (!sel || !n || !(lar(sel) & 0x8000) || !buf)
        return 0;
    lim = GetSelectorLimit(sel);
    if (off > lim)
        return 0;
    if (n > lim - off + 1)
        n = lim - off + 1;
    base = GetSelectorBase(sel) + off;
    while (done < n) {
        WORD k = n - done > 0x8000L ? 0x8000 : (WORD)(n - done);
        SetSelectorBase(scratch, base + done);
        SetSelectorLimit(scratch, k - 1);
        if (write)
            copy(MAKELP(scratch, 0), buf, k);
        else
            copy(buf, MAKELP(scratch, 0), k);
        buf += k;
        done += k;
    }
    return n;
}

DWORD THAPI MemoryRead(WORD sel, DWORD off, void FAR *buf, DWORD n)
{
    return mem(sel, off, buf, n, FALSE);
}

DWORD THAPI MemoryWrite(WORD sel, DWORD off, void FAR *buf, DWORD n)
{
    return mem(sel, off, buf, n, TRUE);
}
