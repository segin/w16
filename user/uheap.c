/*
 * uheap.c - USER's heaps besides DGROUP and the menu heaps (ARCHITECTURE.md
 * 10.3, "USER's other heaps").
 *
 * Windows 3.1 keeps little in USER's DGROUP beyond what tools read there:
 * probe US15 found window text and the global atom table (which holds the
 * properties' string names) in a heap of their own, a 100h-byte local heap
 * at start (US14's "unknown" heap), apart from DGROUP and the two menu
 * heaps, and GetFreeSystemResources does not count it (US15: with that heap
 * full of window titles, GFSR_USERRESOURCES still said 44%). w16 goes
 * further, with three such heaps, each a moveable global block of its own
 * made a local heap with LocalInit (as menu.c's):
 *   - the global atom heap (GlobalAddAtom & co., properties' names), as
 *     3.1's; its atom table is made with InitAtomTable in it;
 *   - the string heap: window text, class menu names;
 *   - the object heap: what applications never reach directly - property
 *     lists, hooks, the DC cache, scroll-bar state, window placements, the
 *     list box items and strings.
 * None of them is counted by GetFreeSystemResources, which keeps 3.1's
 * rule over the heaps 3.1 has (DGROUP and the two menu heaps; init.c).
 *
 * Blocks are fixed (LMEM_FIXED: a handle is the block's offset) and
 * reached as MK_FP(heap selector, handle) through UH_PTR; the Local*
 * calls go through uentry.asm's lh_* and the atom calls through at_*.
 * A heap's selector is taken again after each allocation in it (the
 * segment grows and could get a new one), and no far pointer into a heap
 * is kept across a call that allocates in it or sends a message. Nothing
 * here may be touched at interrupt time.
 */
#include "user.h"

UHEAP uhAtoms, uhStr, uhObj;
WORD ext_user_heap;             /* [w16] ExtendedUserHeap (win.c) */

static WORD sel_of(HGLOBAL h)
{
    return h ? HIWORD(GlobalHandle((UINT)h)) : 0;
}

static BOOL make(UHEAP *hp, WORD size)
{
    HGLOBAL h = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (!h)
        return FALSE;
    if (!LocalInit(sel_of(h), 0x12, size - 1)) {
        GlobalFree(h);
        return FALSE;
    }
    hp->h = h;
    hp->sel = sel_of(h);
    return TRUE;
}

/* user_init, before anything is allocated in them */
void uheaps_init(void)
{
    /* 3.1's: 100h bytes, its atom table using 98h (US14, US15) */
    if (make(&uhAtoms, 0x100))
        at_init(uhAtoms.sel, 37);
    uhAtoms.sel = sel_of(uhAtoms.h);
    make(&uhStr, 0x418);
    make(&uhObj, 0x418);
    ext_user_heap = GetPrivateProfileInt("w16", "ExtendedUserHeap", 0, "SYSTEM.INI") != 0;
}

WORD uh_alloc(UHEAP *hp, UINT flags, UINT n)
{
    WORD r;
    if (!hp->sel)
        return 0;
    r = lh_alloc(hp->sel, flags & ~LMEM_MOVEABLE, n);
    hp->sel = sel_of(hp->h);
    return r;
}

WORD uh_realloc(UHEAP *hp, WORD h, UINT n)
{
    WORD r;
    if (!h)
        return uh_alloc(hp, LMEM_FIXED | LMEM_ZEROINIT, n);
    /* LMEM_MOVEABLE: a fixed block may move to grow (its handle with it) */
    r = lh_realloc(hp->sel, h, n, LMEM_MOVEABLE | LMEM_ZEROINIT);
    hp->sel = sel_of(hp->h);
    return r;
}

void uh_free(UHEAP *hp, WORD h)
{
    if (h && hp->sel)
        lh_free(hp->sel, h);
}

UINT uh_size(UHEAP *hp, WORD h)
{
    return h && hp->sel ? lh_size(hp->sel, h) : 0;
}

/* a copy of s in heap hp (0 for NULL, or no room); s not in hp */
WORD uh_strdup_in(UHEAP *hp, LPCSTR s)
{
    WORD n, r;
    if (!s)
        return 0;
    n = lstr_len(s) + 1;
    r = uh_alloc(hp, LMEM_FIXED, n);
    if (r)
        fmemcpy(UH_PTR(hp, r), s, n);
    return r;
}

/* in the string heap */
WORD uh_strdup(LPCSTR s)
{
    return uh_strdup_in(&uhStr, s);
}

/* the global atom table (GlobalAddAtom & co., USER.268-271; properties'
   names) */
ATOM ga_add(LPCSTR s)
{
    ATOM a;
    if (!uhAtoms.sel)
        return 0;
    a = at_add(uhAtoms.sel, s);
    uhAtoms.sel = sel_of(uhAtoms.h);
    return a;
}

ATOM ga_find(LPCSTR s)
{
    return uhAtoms.sel ? at_find(uhAtoms.sel, s) : 0;
}

ATOM ga_delete(ATOM a)
{
    return uhAtoms.sel ? at_delete(uhAtoms.sel, a) : a;
}

UINT ga_name(ATOM a, LPSTR buf, int n)
{
    return uhAtoms.sel ? at_name(uhAtoms.sel, a, buf, n) : 0;
}
