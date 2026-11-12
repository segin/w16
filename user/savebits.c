/*
 * savebits.c - CS_SAVEBITS: the screen under a window of such a class
 * (USER's menus, #32768) is kept while it shows and put back when it goes,
 * instead of the windows under it painting again.
 *
 * 3.1 SDK: CS_SAVEBITS "saves the portion of the screen image obscured by
 * a window"; when the window is removed the image is restored, and the
 * windows under it get no WM_PAINT. Observed on 3.1: Windows Write's ruler
 * kept its highlighted buttons after File > Exit (the File menu had been
 * over the ruler); on w16, which repainted under the menu, Write's ruler
 * painted its buttons plain (its WM_PAINT does not redraw the highlight).
 *
 * The saved image is dropped (and the area painted as before) when
 * anything under it is invalidated meanwhile, or the window moves. Any
 * number of windows can have one (the list grows). UNVERIFIED: whether
 * 3.1 also drops it on drawing under the window that invalidates nothing
 * (w16 does not see such drawing); the caret, which blinks under menus,
 * is left out of the image and shown again after it if on.
 */
#include "user.h"

extern HDC hdcScreen;
BOOL caret_screen_rect(RECT *r);        /* misc.c */
void caret_toggle(void);
void caret_invert_in(const RECT *screen);

typedef struct {
    HWND    h;
    RECT    rc;                 /* screen coordinates */
    HBITMAP bm;
} SAVED;

static SAVED __near *saved;
static WORD nsaved, capsaved;

static void drop(WORD i)
{
    if (saved[i].bm)
        DeleteObject(saved[i].bm);
    saved[i] = saved[--nsaved];
}

static int find(HWND h)
{
    WORD i;
    for (i = 0; i < nsaved; i++)
        if (saved[i].h == h)
            return i;
    return -1;
}

void savebits_forget(HWND h)
{
    int i = find(h);
    if (i >= 0)
        drop((WORD)i);
}

/* every saved image (the screen came back after a switch to a DOS
   program, EnableOEMLayer: what was saved is no longer what the screen
   showed, and everything paints again) */
void savebits_forget_all(void)
{
    while (nsaved)
        drop(nsaved - 1);
}

/* the window is about to show at w->rcWindow */
void savebits_show(HWND h)
{
    PWND w = valid_wnd(h);
    RECT rc, scr, cr;
    HDC mem;
    HBITMAP bm, old;
    BOOL caret;
    int cx, cy;
    if (!w)
        return;
    savebits_forget(h);
    SetRect(&scr, 0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN]);
    if (!IntersectRect(&rc, &w->rcWindow, &scr))
        return;
    cx = rc.right - rc.left;
    cy = rc.bottom - rc.top;
    if (nsaved == capsaved) {
        WORD cap = capsaved ? capsaved * 2 : 4;
        SAVED __near *n = saved
            ? (SAVED __near *)LocalReAlloc((HLOCAL)saved, cap * sizeof(SAVED), LMEM_MOVEABLE)
            : (SAVED __near *)LocalAlloc(LMEM_FIXED, cap * sizeof(SAVED));
        if (!n)
            return;
        saved = n;
        capsaved = cap;
    }
    bm = CreateCompatibleBitmap(hdcScreen, cx, cy);
    if (!bm)
        return;
    mem = CreateCompatibleDC(hdcScreen);
    if (!mem) {
        DeleteObject(bm);
        return;
    }
    old = (HBITMAP)SelectObject(mem, bm);
    /* the image without the caret */
    caret = caret_screen_rect(&cr) && IntersectRect(&cr, &cr, &rc);
    if (caret)
        caret_toggle();
    BitBlt(mem, 0, 0, cx, cy, hdcScreen, rc.left, rc.top, SRCCOPY);
    if (caret)
        caret_toggle();
    SelectObject(mem, old);
    DeleteDC(mem);
    G_MAKEOBJECTPRIVATE(bm, TRUE);
    saved[nsaved].h = h;
    saved[nsaved].rc = rc;
    saved[nsaved].bm = bm;
    nsaved++;
}

/* the window has just been hidden from oldwin: put the image back where no
   other window now shows. FALSE: nothing kept (the caller invalidates) */
BOOL savebits_restore(HWND h, const RECT *oldwin)
{
    int i = find(h);
    RECT scr, rc, cr;
    HRGN vis, t;
    HDC mem;
    HBITMAP old;
    HWND s;
    if (i < 0)
        return FALSE;
    SetRect(&scr, 0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN]);
    IntersectRect(&rc, oldwin, &scr);
    if (!EqualRect(&rc, &saved[i].rc)) {
        drop((WORD)i);
        return FALSE;
    }
    /* less the top-level windows above it that show */
    vis = CreateRectRgnIndirect(&rc);
    for (s = PW(hwndDesktop)->hwndChild; s && s != h; s = PW(s)->hwndNext)
        if (PW(s)->style & WS_VISIBLE) {
            t = CreateRectRgnIndirect(&PW(s)->rcWindow);
            CombineRgn(vis, vis, t, RGN_DIFF);
            DeleteObject(t);
        }
    mem = CreateCompatibleDC(hdcScreen);
    if (!mem) {
        DeleteObject(vis);
        drop((WORD)i);
        return FALSE;
    }
    old = (HBITMAP)SelectObject(mem, saved[i].bm);
    SelectClipRgn(hdcScreen, vis);
    BitBlt(hdcScreen, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
           mem, 0, 0, SRCCOPY);
    SelectClipRgn(hdcScreen, 0);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(vis);
    drop((WORD)i);
    /* the caret, if on, again over the image (which has none) */
    if (caret_screen_rect(&cr) && IntersectRect(&cr, &cr, &rc))
        caret_invert_in(&rc);
    return TRUE;
}

/* is top-level window a above top-level window b? */
static BOOL above(HWND a, HWND b)
{
    HWND s;
    for (s = PW(hwndDesktop)->hwndChild; s; s = PW(s)->hwndNext) {
        if (s == b)
            return FALSE;
        if (s == a)
            return TRUE;
    }
    return FALSE;
}

/* rgn (screen coordinates) invalidated in window by: an image under it
   would be stale. The window itself, its children and windows above it
   (a cascading menu over a menu) do not count */
void savebits_invalidated(HWND by, HRGN rgn)
{
    WORD i;
    HRGN t;
    HWND top = by;
    if (!nsaved)
        return;
    while (top && top != hwndDesktop && PW(top)->hwndParent != hwndDesktop)
        top = PW(top)->hwndParent;
    t = CreateRectRgn(0, 0, 0, 0);
    for (i = 0; i < nsaved; ) {
        HWND h = saved[i].h;
        if (by != h && !IsChild(h, by) && !(top != hwndDesktop && above(top, h))) {
            SetRectRgn(t, saved[i].rc.left, saved[i].rc.top, saved[i].rc.right, saved[i].rc.bottom);
            if (!rgn || CombineRgn(t, t, rgn, RGN_AND) != NULLREGION) {
                drop(i);
                continue;
            }
        }
        i++;
    }
    DeleteObject(t);
}
