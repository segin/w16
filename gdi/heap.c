/*
 * heap.c - GDI's own heaps: local heaps in global segments of GDI's, for
 * what programs never reach through a GDI handle (gdi.h, "GDI's own
 * heaps"; ARCHITECTURE.md 11.7).
 *
 * 3.1 keeps nearly all of GDI in its DGROUP local heap (Schulman ch. 8;
 * GD4: every object's block, the DCs' regions, realized pens and brushes,
 * saved DCs; the palette entries only were not in DGROUP), and that 64K
 * heap is what GetFreeSystemResources' GDI percentage measures. w16 keeps
 * the objects' blocks there, where TOOLHELP and programs expect them, and
 * everything else here, so more objects fit in DGROUP.
 *
 * A heap is made as USER's menu heaps are (Pietrek, Windows Internals ch.
 * 1, LoadWindows: a moveable, shared global block made a local heap with
 * LocalInit), and grown by KERNEL's LocalAlloc up to FFF0h bytes. Blocks
 * are LMEM_FIXED (the handle is the offset). More heaps are made as they
 * fill; a heap that refused a size is skipped for that size and larger
 * ones until a block in it is freed; a heap other than the first that
 * becomes empty is given back to KERNEL.
 */
#include "gdi.h"

#define XH_MAX 200                      /* x 64K: more than any machine has */
#define XH_FIRST 0x0800                 /* a new heap's segment */

static HGLOBAL xh[XH_MAX];              /* 0: a free slot */
static WORD xsel[XH_MAX];
static WORD xfail[XH_MAX];              /* smallest size refused since the last free */
static WORD xcount[XH_MAX];             /* blocks in use */
static WORD nxh, xcur;
static WORD toobig = 0xFFFF;            /* smallest size even an empty heap refused */

static WORD heap_sel(HGLOBAL h)
{
    return h ? HIWORD(GlobalHandle(h)) : 0;
}

static int new_heap(void)
{
    HGLOBAL h;
    WORD i;
    for (i = 0; i < nxh && xh[i]; i++)
        ;
    if (i >= XH_MAX)
        return -1;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT, XH_FIRST);
    if (!h)
        return -1;
    if (!LocalInit(heap_sel(h), 0x12, XH_FIRST - 1)) {
        GlobalFree(h);
        return -1;
    }
    xh[i] = h;
    xsel[i] = heap_sel(h);
    xfail[i] = 0xFFFF;
    xcount[i] = 0;
    if (i == nxh)
        nxh++;
    return i;
}

void xheap_init(void)
{
    if (!nxh)
        new_heap();
}

static XP try_heap(int i, WORD size)
{
    WORD off;
    if (!xh[i] || size >= xfail[i])
        return 0;
    off = lh_alloc(xsel[i], LMEM_FIXED | LMEM_ZEROINIT, size);
    xsel[i] = heap_sel(xh[i]);          /* growing may have moved it */
    if (!off) {
        xfail[i] = size;
        return 0;
    }
    xcur = i;
    xcount[i]++;
    return MAKELONG(off, i + 1);
}

/* the heap of p, or -1 */
static int heap_of(XP p)
{
    WORD i = HIWORD(p);
    return i && i <= nxh && xh[i - 1] ? i - 1 : -1;
}

XP xalloc(WORD size)
{
    XP p;
    WORD i;
    int n;
    if (!size)
        size = 2;
    if (size >= toobig)
        return 0;
    if (nxh && (p = try_heap(xcur, size)) != 0)
        return p;
    for (i = 0; i < nxh; i++)
        if (i != xcur && (p = try_heap(i, size)) != 0)
            return p;
    n = new_heap();
    if (n < 0)
        return 0;
    p = try_heap(n, size);
    if (!p) {
        /* not even in an empty heap: the heap goes, and a size that
           cannot fit in 64K is not tried again (a smaller one failed for
           want of global memory, which may come back) */
        if (size > 0xF000)
            toobig = size;
        if (n) {
            GlobalFree(xh[n]);
            xh[n] = 0;
        }
    }
    return p;
}

void __far *xptr(XP p)
{
    int i = heap_of(p);
    return i < 0 ? (void __far *)0 : MAKELP(xsel[i], LOWORD(p));
}

WORD xsize(XP p)
{
    int i = heap_of(p);
    return i < 0 ? 0 : lh_size(xsel[i], LOWORD(p));
}

void xfree(XP p)
{
    int i = heap_of(p);
    if (i < 0)
        return;
    lh_free(xsel[i], LOWORD(p));
    xfail[i] = 0xFFFF;
    if (!--xcount[i] && i) {            /* an empty heap goes back */
        GlobalFree(xh[i]);
        xh[i] = 0;
        if (xcur == i)
            xcur = 0;
    }
}

XP xrealloc(XP p, WORD size)
{
    int i = heap_of(p);
    WORD off, old;
    XP q;
    if (!p)
        return xalloc(size);
    if (i < 0 || size > 0xFF00)
        return 0;
    off = lh_realloc(xsel[i], LOWORD(p), size, LMEM_MOVEABLE);
    xsel[i] = heap_sel(xh[i]);
    if (off)
        return MAKELONG(off, i + 1);
    /* no room in that heap: elsewhere */
    q = xalloc(size);
    if (!q)
        return 0;
    old = xsize(p);
    mem_copy(xptr(q), xptr(p), old < size ? old : size);
    xfree(p);
    return q;
}

/* for the log and tests: heaps, kilobytes of segments they have */
DWORD xheap_stats(void)
{
    DWORD used = 0;
    WORD i, n = 0;
    for (i = 0; i < nxh; i++)
        if (xh[i]) {
            used += GlobalSize(xh[i]);
            n++;
        }
    return MAKELONG((WORD)(used >> 10), n);
}
