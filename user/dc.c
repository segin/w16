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
 * own. ZOI: the cache grows when all its DCs are out.
 */
#include "user.h"

typedef struct DCE {
    WORD   next;
    HDC    hdc;
    HWND   hwnd;                /* 0: free */
    WORD   flags;               /* DCE_* */
    HRGN   clip;                /* extra clipping (BeginPaint's update region) */
} DCE;
typedef DCE __near *PDCE;
#define DCE_WINDOW  1           /* GetWindowDC: the whole window */
#define DCE_OWN     2           /* a CS_OWNDC window's */
#define DCE_INUSE   4
#define DCE_CLIPCHILDREN 8

static WORD dces;
/* the driver name GDI gets; a buffer of its own (UNVERIFIED: GDI appears to
   write into it, see ARCHITECTURE.md 10) */
static char szDisplay[16] = "DISPLAY";
HDC hdcScreen, hdcMem, hdcState;
HRGN hrgnTmp1, hrgnTmp2;

static PDCE new_dce(void)
{
    PDCE d = (PDCE)LocalAlloc(LPTR, sizeof(DCE));
    if (!d)
        return 0;
    d->hdc = CreateDC(szDisplay, NULL, NULL, NULL);
    if (!d->hdc) {
        LocalFree((HLOCAL)d);
        return 0;
    }
    G_MAKEOBJECTPRIVATE(d->hdc, TRUE);
    d->next = dces;
    dces = (WORD)d;
    return d;
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
            for (s = parent->hwndChild; s && s != (HWND)(WORD)p; s = PW(s)->hwndNext) {
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
    if (d->flags & DCE_CLIPCHILDREN)
        clipch = TRUE;
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
    PCLS c;
    if (!w)
        return 0;
    if (!h)
        h = hwndDesktop;
    c = (PCLS)w->cls;
    if ((c->style & CS_OWNDC) && !window) {
        for (d = (PDCE)dces; d; d = (PDCE)d->next)
            if ((d->flags & DCE_OWN) && d->hwnd == h)
                break;
        if (!d) {
            d = new_dce();
            if (!d)
                return 0;
            d->flags = DCE_OWN;
            d->hwnd = h;
        }
    } else {
        for (d = (PDCE)dces; d; d = (PDCE)d->next)
            if (!(d->flags & (DCE_INUSE | DCE_OWN)))
                break;
        if (!d && !(d = new_dce()))
            return 0;
        d->flags = DCE_INUSE | (window ? DCE_WINDOW : 0);
        d->hwnd = h;
    }
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
    for (d = (PDCE)dces; d; d = (PDCE)d->next)
        if (d->hdc == hdc) {
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
    for (d = (PDCE)dces; d; d = (PDCE)d->next)
        if (d->hwnd == h) {
            if (d->flags & DCE_OWN) {
                d->flags = 0;
                G_SETDCSTATE(d->hdc, hdcState);
            }
            d->hwnd = 0;
            d->flags &= ~DCE_INUSE;
        }
}

/* windows moved: DCs out for them see the new place */
void refresh_dcs(void)
{
    PDCE d;
    for (d = (PDCE)dces; d; d = (PDCE)d->next)
        if (d->hwnd && valid_wnd(d->hwnd) && (d->flags & (DCE_INUSE | DCE_OWN)))
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
    return get_dc(h, (flags & 1) != 0, (flags & 0x80) ? clip : 0, (flags & 2) ? 1 : 0);
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
