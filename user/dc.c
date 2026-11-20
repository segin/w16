/*
 * dc.c - display contexts for windows.
 *
 * USER makes its DCs at boot with CreateDC("DISPLAY") (US1 saw five cache
 * DCs and a screen DC, each made private to USER with MakeObjectPrivate)
 * and gives one out per GetDC/BeginPaint: it selects the window's visible
 * region into it (GDI's SelectVisRgn), sets its origin to the window's
 * client or window corner in screen coordinates (SetDCOrg) and, when the
 * DC is released, resets it to the default state saved at boot
 * (GetDCState/SetDCState). A window of a CS_OWNDC class keeps a DC of its
 * own. ZOI: the cache grows when all its DCs are out. The DCE records are
 * blocks of the object heap (uheap.c; 3.1 has them in DGROUP, 10h bytes
 * for a CS_OWNDC window: US15), reached through DC_() by handle.
 */
#include "user.h"

typedef struct DCE {
    WORD   next;
    HDC    hdc;
    HWND   hwnd;                /* 0: free */
    WORD   flags;               /* DCE_* */
    HRGN   clip;                /* extra clipping (BeginPaint's update region) */
} DCE;
typedef DCE __far *PDCE;
#define DC_(h)  ((PDCE)UH_PTR(&uhObj, (h)))
#define DCE_WINDOW  1           /* GetWindowDC: the whole window */
#define DCE_OWN     2           /* a CS_OWNDC window's */
#define DCE_INUSE   4
#define DCE_CLIPCHILDREN 8
#define DCE_CLASS   0x10        /* with DCE_OWN: a CS_CLASSDC class's (its
                                   hwnd: the window that had it last) */
#define DCE_PARENT  0x20        /* a CS_PARENTDC window's: the parent's
                                   visible region */

static WORD dces;
/* the driver name GDI gets; a buffer of its own (UNVERIFIED: GDI appears to
   write into it, see ARCHITECTURE.md 10) */
static char szDisplay[16] = "DISPLAY";
HDC hdcScreen, hdcMem, hdcState;
HRGN hrgnTmp1, hrgnTmp2;

/* a new DC in the cache: its DCE's handle, 0 if none */
static WORD new_dce(void)
{
    HDC hdc = CreateDC(szDisplay, NULL, NULL, NULL);
    WORD h;
    PDCE d;
    if (!hdc)
        return 0;
    h = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, sizeof(DCE));
    if (!h) {
        DeleteDC(hdc);
        return 0;
    }
    G_MAKEOBJECTPRIVATE(hdc, TRUE);
    d = DC_(h);
    d->hdc = hdc;
    d->next = dces;
    dces = h;
    return h;
}

void dc_init(void)
{
    int i;
    hdcScreen = CreateDC(szDisplay, NULL, NULL, NULL);
    G_MAKEOBJECTPRIVATE(hdcScreen, TRUE);
    for (i = 0; i < 5; i++)
        new_dce();
    hdcState = G_GETDCSTATE(hdcScreen);
    hdcMem = CreateCompatibleDC(hdcScreen);
    G_MAKEOBJECTPRIVATE(hdcMem, TRUE);
    hrgnTmp1 = CreateRectRgn(0, 0, 0, 0);
    hrgnTmp2 = CreateRectRgn(0, 0, 0, 0);
}

/* The visible region of a window, in screen coordinates: its window or
   client rectangle, cut to its ancestors' client areas, less the
   siblings above it (and above each ancestor) where clipping applies,
   less its children with clipchildren. */
HRGN visible_rgn(PWND w, BOOL client, BOOL clipchildren)
{
    HRGN r = CreateRectRgnIndirect(client ? &w->rcClient : &w->rcWindow);
    PWND p = w;
    HWND s;
    if (!is_visible(w)) {
        SetRectRgn(r, 0, 0, 0, 0);
        return r;
    }
    while (p->hwndParent) {
        PWND parent = PW(p->hwndParent);
        BOOL clipsib = (p->style & WS_CLIPSIBLINGS) || !(p->style & WS_CHILD);
        /* inside a minimised window: nothing (its children are hidden
           with it; IsWindowVisible still says TRUE) */
        if (parent->style & WS_MINIMIZE) {
            SetRectRgn(r, 0, 0, 0, 0);
            return r;
        }
        /* cut to the parent's client area; for a top-level window the
           desktop's, the screen (Windows 3.1's USER does: the GDI agent
           saw its visible regions end at the screen's edges) */
        SetRectRgn(hrgnTmp1, parent->rcClient.left, parent->rcClient.top,
                   parent->rcClient.right, parent->rcClient.bottom);
        CombineRgn(r, r, hrgnTmp1, RGN_AND);
        if (clipsib) {
            for (s = parent->hwndChild; s && s != HW(p); s = PW(s)->hwndNext) {
                PWND sw = PW(s);
                if (!(sw->style & WS_VISIBLE))
                    continue;
                SetRectRgn(hrgnTmp1, sw->rcWindow.left, sw->rcWindow.top,
                           sw->rcWindow.right, sw->rcWindow.bottom);
                CombineRgn(r, r, hrgnTmp1, RGN_DIFF);
            }
        }
        p = parent;
    }
    if (clipchildren && !(w->style & WS_MINIMIZE)) {
        for (s = w->hwndChild; s; s = PW(s)->hwndNext) {
            PWND sw = PW(s);
            RECT c;
            if (!(sw->style & WS_VISIBLE))
                continue;
            /* a child shows only inside its parent's client area: one
               placed partly outside (w16's Program Manager puts its MDI
               client at -1,-1) must not hide the parent's frame */
            if (!IntersectRect(&c, &sw->rcWindow, &w->rcClient))
                continue;
            SetRectRgn(hrgnTmp1, c.left, c.top, c.right, c.bottom);
            CombineRgn(r, r, hrgnTmp1, RGN_DIFF);
        }
    }
    return r;
}

static void setup_dc(PDCE d, PWND w)
{
    BOOL window = (d->flags & DCE_WINDOW) != 0;
    BOOL clipch = (w->style & WS_CLIPCHILDREN) != 0;
    HRGN r;
    /* the desktop's window DC (GetDC(NULL), GetWindowDC of the desktop or
       of NULL, GetDCEx(desktop, DCX_WINDOW)) is the whole screen, the
       top-level windows too: a simple region, and GetPixel reads what
       they show (US33 gdc on 3.1; GetDC of the desktop window itself
       leaves them out, and so does the desktop's own painting) */
    if (window && w == PW(hwndDesktop))
        clipch = FALSE;
    if (d->flags & DCE_CLIPCHILDREN)
        clipch = TRUE;
    if ((d->flags & DCE_PARENT) && w->hwndParent)
        r = visible_rgn(PW(w->hwndParent), TRUE, FALSE);
    else
        r = visible_rgn(w, !window, clipch);
    if (d->clip)
        CombineRgn(r, r, d->clip, RGN_AND);
    G_SELECTVISRGN(d->hdc, r);
    DeleteObject(r);
    if (window)
        G_SETDCORG(d->hdc, w->rcWindow.left, w->rcWindow.top);
    else
        G_SETDCORG(d->hdc, w->rcClient.left, w->rcClient.top);
}

HDC get_dc(HWND h, BOOL window, HRGN clip, WORD flags)
{
    PWND w = h ? valid_wnd(h) : PW(hwndDesktop);
    PDCE d;
    WORD dh;
    PCLS c;
    if (!w)
        return 0;
    if (!h) {
        h = hwndDesktop;
        window = TRUE;          /* the screen (setup_dc) */
    }
    c = (PCLS)w->cls;
    if ((c->style & CS_CLASSDC) && !(c->style & CS_OWNDC) && !window) {
        /* CS_CLASSDC: one DC for all the class's windows, set up for the
           one asking, its state kept between them (US29 dc on 3.1: two
           windows' GetDCs give the same DC, a mode set for one is there for
           the other, its origin the asking window's; not the cache's, and
           GetWindowDC gives a cache DC). w16 gave cache DCs (USER audit,
           item 14). */
        dh = c->hdce;
        if (!dh) {
            dh = new_dce();
            if (!dh)
                return 0;
            c = (PCLS)w->cls;
            c->hdce = dh;
            DC_(dh)->flags = DCE_OWN | DCE_CLASS;
        }
        DC_(dh)->hwnd = h;
    } else if ((c->style & CS_OWNDC) && !window) {
        for (dh = dces; dh; dh = DC_(dh)->next)
            if ((DC_(dh)->flags & DCE_OWN) && DC_(dh)->hwnd == h)
                break;
        if (!dh) {
            dh = new_dce();
            if (!dh)
                return 0;
            DC_(dh)->flags = DCE_OWN;
            DC_(dh)->hwnd = h;
        }
    } else {
        for (dh = dces; dh; dh = DC_(dh)->next)
            if (!(DC_(dh)->flags & (DCE_INUSE | DCE_OWN)))
                break;
        if (!dh && !(dh = new_dce()))
            return 0;
        DC_(dh)->flags = DCE_INUSE | (window ? DCE_WINDOW : 0);
        /* CS_PARENTDC: a cache DC clipped to the parent's client area, its
           origin the child's (US29 dc on 3.1: GetClipBox -10 -10 388 269
           for a child at 10,10 of a 398 x 279 client area). The system
           controls' classes have it too, and 3.1's GetDC gives them the
           parent's area as well (US32: a dialog's list box, -18 -16 245
           114, the dialog's client area); USER's own drawing in them asks
           for the control's area (DCX_OWNCLIENT: ctl_dc), as a
           superclass of them, of the application's module, took the
           parent's and drew outside itself. BeginPaint clips to the update
           region, inside the window either way. */
        if ((c->style & CS_PARENTDC) && !(flags & DCX_OWNCLIENT) && !window &&
            (w->style & WS_CHILD) && w->hwndParent)
            DC_(dh)->flags |= DCE_PARENT;
        DC_(dh)->hwnd = h;
    }
    d = DC_(dh);        /* (nothing below allocates in the object heap) */
    if (flags & 1)
        d->flags |= DCE_CLIPCHILDREN;
    if (d->clip)
        DeleteObject(d->clip);
    d->clip = 0;
    if (clip) {
        d->clip = CreateRectRgn(0, 0, 0, 0);
        CombineRgn(d->clip, clip, 0, RGN_COPY);
    }
    setup_dc(d, w);
    return d->hdc;
}

int release_dc(HWND h, HDC hdc)
{
    PDCE d;
    WORD dh;
    for (dh = dces; dh; dh = d->next)
        if ((d = DC_(dh))->hdc == hdc) {
            if (d->clip) {
                DeleteObject(d->clip);
                d->clip = 0;
            }
            if (d->flags & DCE_OWN) {
                d->flags &= ~DCE_CLIPCHILDREN;
                return 1;
            }
            G_SETDCSTATE(hdc, hdcState);
            d->flags = 0;
            d->hwnd = 0;
            return 1;
        }
    (void)h;
    return 0;
}

/* a window going away gives back its own DC */
void free_window_dcs(HWND h)
{
    PDCE d;
    WORD dh;
    for (dh = dces; dh; dh = d->next)
        if ((d = DC_(dh))->hwnd == h) {
            /* a class's DC stays the class's, with its state */
            if (d->flags & DCE_CLASS) {
                d->hwnd = 0;
                continue;
            }
            /* a CS_OWNDC window's DC joins the cache; one out of the
               cache comes back as release_dc gives it back: reset */
            if (d->clip) {
                DeleteObject(d->clip);
                d->clip = 0;
            }
            G_SETDCSTATE(d->hdc, hdcState);
            d->hwnd = 0;
            d->flags = 0;
        }
}

/* a CS_CLASSDC class goes: its DC joins the cache */
void free_class_dc(PCLS c)
{
    PDCE d = DC_(c->hdce);
    if (d->clip) {
        DeleteObject(d->clip);
        d->clip = 0;
    }
    G_SETDCSTATE(d->hdc, hdcState);
    d->hwnd = 0;
    d->flags = 0;
    c->hdce = 0;
}

/* windows moved: DCs out for them see the new place */
void refresh_dcs(void)
{
    PDCE d;
    WORD dh;
    for (dh = dces; dh; dh = d->next)
        if ((d = DC_(dh))->hwnd && valid_wnd(d->hwnd) && (d->flags & (DCE_INUSE | DCE_OWN)))
            setup_dc(d, PW(d->hwnd));
}

HDC WINAPI GetDC(HWND h)
{
    return get_dc(h, FALSE, 0, 0);
}

HDC WINAPI GetWindowDC(HWND h)
{
    return get_dc(h, TRUE, 0, 0);
}

int WINAPI ReleaseDC(HWND h, HDC hdc)
{
    return release_dc(h, hdc);
}

HDC WINAPI U_GETDCEX(HWND h, HRGN clip, DWORD flags)
{
    /* DCX_WINDOW 1, DCX_CLIPCHILDREN 8, DCX_INTERSECTRGN 80h (3.1 SDK;
       w16 took DCX_CACHE, 2, for DCX_CLIPCHILDREN: US33 gdc) */
    return get_dc(h, (flags & 1) != 0, (flags & 0x80) ? clip : 0, (flags & 8) ? 1 : 0);
}

/* the 3.1 SDK's LockWindowUpdate: drawing to the window is suppressed;
   w16 only remembers the window (UNVERIFIED: Windows clips its DCs away) */
static HWND hwndLocked;
BOOL WINAPI LockWindowUpdate(HWND h)
{
    if (h && hwndLocked)
        return FALSE;
    hwndLocked = h;
    if (!h)
        redraw_screen();
    return TRUE;
}
