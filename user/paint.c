/*
 * paint.c - update regions, WM_PAINT, BeginPaint/EndPaint.
 *
 * A window's update region (WND.hrgnUpdate) is kept in screen coordinates
 * and covers the whole window, so it also says when the frame needs
 * painting (WF_UPDATENC). A window with something to paint counts once in
 * its queue's paint count, which makes GetMessage produce WM_PAINT when it
 * has nothing else (Pietrek, Windows Internals, ch. 7).
 */
#include "user.h"

static void paint_desktop(HRGN r)
{
    PWND d = PW(hwndDesktop);
    HDC hdc = get_dc(hwndDesktop, FALSE, r, 0);
    call_wnd(d, hwndDesktop, WM_ERASEBKGND, (WPARAM)hdc, 0);
    release_dc(hwndDesktop, hdc);
}

static void need_paint(PWND w)
{
    if (!(w->flags & WF_INPAINT)) {
        w->flags |= WF_INPAINT;
        inc_paint(w->hq);
    }
}

static void no_paint(PWND w)
{
    if (w->flags & WF_INPAINT) {
        w->flags &= ~WF_INPAINT;
        dec_paint(w->hq);
    }
}

/* add rgn (screen coordinates, or NULL for the whole window) to the
   window's update region; with children, also to every visible child it
   touches */
static void add_update(PWND w, HRGN rgn, BOOL erase, BOOL children)
{
    HRGN r;
    HWND c;
    if (!(w->style & WS_VISIBLE))
        return;
    r = CreateRectRgnIndirect(&w->rcWindow);
    if (rgn)
        CombineRgn(r, r, rgn, RGN_AND);
    {
        RECT box;
        if (GetRgnBox(r, &box) == NULLREGION)
            goto kids;
        if (!w->hwndParent) {
            paint_desktop(r);
            goto kids;
        }
        if (!w->hrgnUpdate) {
            w->hrgnUpdate = CreateRectRgn(0, 0, 0, 0);
            G_MAKEOBJECTPRIVATE(w->hrgnUpdate, TRUE);
        }
        CombineRgn(w->hrgnUpdate, w->hrgnUpdate, r, RGN_OR);
        if (box.left < w->rcClient.left || box.top < w->rcClient.top ||
            box.right > w->rcClient.right || box.bottom > w->rcClient.bottom)
            w->flags |= WF_UPDATENC;
        if (erase)
            w->flags |= WF_ERASEBKGND;
        need_paint(w);
    }
kids:
    if (children)
        for (c = w->hwndChild; c; c = PW(c)->hwndNext) {
            PWND cw = PW(c);
            RECT rc;
            if (!(cw->style & WS_VISIBLE))
                continue;
            if (rgn) {
                SetRectRgn(hrgnTmp2, cw->rcWindow.left, cw->rcWindow.top,
                           cw->rcWindow.right, cw->rcWindow.bottom);
                if (CombineRgn(hrgnTmp2, hrgnTmp2, r, RGN_AND) == NULLREGION)
                    continue;
            } else if (!IntersectRect(&rc, &cw->rcWindow, &w->rcWindow))
                continue;
            add_update(cw, r, erase, TRUE);
        }
    DeleteObject(r);
}

/* rgn/rc in client coordinates of h (both NULL: the client area);
   nc: include the frame */
void invalidate(HWND h, HRGN rgn, const RECT *rc, BOOL erase, BOOL children, BOOL nc)
{
    PWND w = h ? valid_wnd(h) : PW(hwndDesktop);
    HRGN r;
    if (!w)
        return;
    if (rgn) {
        r = CreateRectRgn(0, 0, 0, 0);
        CombineRgn(r, rgn, 0, RGN_COPY);
        OffsetRgn(r, w->rcClient.left, w->rcClient.top);
    } else if (rc) {
        r = CreateRectRgn(rc->left + w->rcClient.left, rc->top + w->rcClient.top,
                          rc->right + w->rcClient.left, rc->bottom + w->rcClient.top);
    } else
        r = CreateRectRgnIndirect(nc ? &w->rcWindow : &w->rcClient);
    if (!nc) {
        SetRectRgn(hrgnTmp1, w->rcClient.left, w->rcClient.top, w->rcClient.right,
                   w->rcClient.bottom);
        CombineRgn(r, r, hrgnTmp1, RGN_AND);
    }
    add_update(w, r, erase, children);
    DeleteObject(r);
}

/* everything on screen within rc (screen coordinates), frames too */
void invalidate_screen(const RECT *rc)
{
    HRGN r = CreateRectRgnIndirect(rc);
    add_update(PW(hwndDesktop), r, TRUE, TRUE);
    DeleteObject(r);
}

void redraw_screen(void)
{
    invalidate_screen(&PW(hwndDesktop)->rcWindow);
}

void validate_all(PWND w)
{
    if (w->hrgnUpdate) {
        DeleteObject(w->hrgnUpdate);
        w->hrgnUpdate = 0;
    }
    w->flags &= ~(WF_UPDATENC | WF_ERASEBKGND | WF_INTERNALPAINT);
    no_paint(w);
}

static BOOL children_of(HWND parent)
{
    return !(PW(parent)->style & WS_CLIPCHILDREN);
}

void WINAPI InvalidateRect(HWND h, const RECT __far *rc, BOOL erase)
{
    RECT r;
    if (!h) {
        redraw_screen();
        return;
    }
    if (!valid_wnd(h))
        return;
    if (rc)
        r = *rc;
    invalidate(h, 0, rc ? &r : 0, erase, children_of(h), FALSE);
}

void WINAPI InvalidateRgn(HWND h, HRGN rgn, BOOL erase)
{
    if (!valid_wnd(h))
        return;
    invalidate(h, rgn, 0, erase, children_of(h), FALSE);
}

static void validate(HWND h, HRGN rgn, const RECT *rc)
{
    PWND w = valid_wnd(h);
    HRGN r;
    if (!w || !w->hrgnUpdate)
        return;
    if (!rgn && !rc) {
        /* the client area */
        r = CreateRectRgnIndirect(&w->rcClient);
    } else if (rgn) {
        r = CreateRectRgn(0, 0, 0, 0);
        CombineRgn(r, rgn, 0, RGN_COPY);
        OffsetRgn(r, w->rcClient.left, w->rcClient.top);
    } else
        r = CreateRectRgn(rc->left + w->rcClient.left, rc->top + w->rcClient.top,
                          rc->right + w->rcClient.left, rc->bottom + w->rcClient.top);
    if (CombineRgn(w->hrgnUpdate, w->hrgnUpdate, r, RGN_DIFF) == NULLREGION) {
        DeleteObject(w->hrgnUpdate);
        w->hrgnUpdate = 0;
        w->flags &= ~(WF_UPDATENC | WF_ERASEBKGND);
        if (!(w->flags & WF_INTERNALPAINT))
            no_paint(w);
    }
    DeleteObject(r);
}

void WINAPI ValidateRect(HWND h, const RECT __far *rc)
{
    RECT r;
    if (rc)
        r = *rc;
    validate(h, 0, rc ? &r : 0);
}

void WINAPI ValidateRgn(HWND h, HRGN rgn)
{
    validate(h, rgn, 0);
}

BOOL WINAPI GetUpdateRect(HWND h, RECT __far *rc, BOOL erase)
{
    PWND w = valid_wnd(h);
    RECT r;
    if (!w)
        return FALSE;
    if (!w->hrgnUpdate) {
        if (rc)
            SetRectEmpty(rc);
        return FALSE;
    }
    SetRectRgn(hrgnTmp1, w->rcClient.left, w->rcClient.top, w->rcClient.right, w->rcClient.bottom);
    CombineRgn(hrgnTmp1, hrgnTmp1, w->hrgnUpdate, RGN_AND);
    GetRgnBox(hrgnTmp1, &r);
    OffsetRect(&r, -w->rcClient.left, -w->rcClient.top);
    if (rc)
        *rc = r;
    if (erase && (w->flags & WF_ERASEBKGND)) {
        HDC hdc = get_dc(h, FALSE, w->hrgnUpdate, 0);
        w->flags &= ~WF_ERASEBKGND;
        send_message(h, WM_ERASEBKGND, hdc, 0);
        release_dc(h, hdc);
    }
    return !IsRectEmpty(&r);
}

int WINAPI GetUpdateRgn(HWND h, HRGN rgn, BOOL erase)
{
    PWND w = valid_wnd(h);
    int r;
    if (!w)
        return ERROR;
    if (!w->hrgnUpdate) {
        SetRectRgn(rgn, 0, 0, 0, 0);
        return NULLREGION;
    }
    SetRectRgn(rgn, w->rcClient.left, w->rcClient.top, w->rcClient.right, w->rcClient.bottom);
    r = CombineRgn(rgn, rgn, w->hrgnUpdate, RGN_AND);
    OffsetRgn(rgn, -w->rcClient.left, -w->rcClient.top);
    (void)erase;
    return r;
}

/* the next window of this queue with something to paint, parents first */
static HWND find_paint(HWND h, HQUEUE hq)
{
    HWND c, r;
    PWND w = PW(h);
    if (!(w->style & WS_VISIBLE))
        return 0;
    if (w->hq == hq && (w->flags & WF_INPAINT))
        return h;
    for (c = w->hwndChild; c; c = PW(c)->hwndNext)
        if ((r = find_paint(c, hq)) != 0)
            return r;
    return 0;
}

BOOL paint_message(HQUEUE hq, HWND hf, MSG __far *m)
{
    HWND h;
    QUEUE __far *q = q_of(hq);
    if (!q->npaint)
        return FALSE;
    h = find_paint(hf ? hf : hwndDesktop, hq);
    if (!h) {
        if (!hf) {
            /* nothing visible to paint: the count is stale */
            q->npaint = 0;
            q->wakebits &= ~QS_PAINT;
        }
        return FALSE;
    }
    m->hwnd = h;
    /* a minimised window of a class with an icon gets WM_PAINTICON, which
       DefWindowProc answers by drawing the icon (3.1 SDK) */
    m->message = ((PW(h)->style & WS_MINIMIZE) && ((PCLS)PW(h)->cls)->hIcon) ? WM_PAINTICON : WM_PAINT;
    m->wParam = 0;
    m->lParam = 0;
    m->time = GetTickCount();
    m->pt = ptCursor;
    return TRUE;
}

HDC WINAPI BeginPaint(HWND h, PAINTSTRUCT __far *ps)
{
    PWND w = valid_wnd(h);
    HRGN upd;
    BOOL erase;
    if (!w)
        return 0;
    HideCaret(h);
    if (w->flags & WF_UPDATENC) {
        w->flags &= ~WF_UPDATENC;
        if (w->hrgnUpdate) {
            HRGN nc = CreateRectRgn(0, 0, 0, 0);
            CombineRgn(nc, w->hrgnUpdate, 0, RGN_COPY);
            send_message(h, WM_NCPAINT, (WPARAM)nc, 0);
            DeleteObject(nc);
        } else
            send_message(h, WM_NCPAINT, 1, 0);
        if (!(w = valid_wnd(h)))
            return 0;
    }
    upd = CreateRectRgnIndirect(&w->rcClient);
    if (w->hrgnUpdate)
        CombineRgn(upd, upd, w->hrgnUpdate, RGN_AND);
    else if (!(w->flags & WF_INTERNALPAINT))
        SetRectRgn(upd, 0, 0, 0, 0);
    erase = (w->flags & WF_ERASEBKGND) != 0;
    validate_all(w);
    ps->hdc = get_dc(h, FALSE, upd, 0);
    GetRgnBox(upd, &ps->rcPaint);
    OffsetRect(&ps->rcPaint, -w->rcClient.left, -w->rcClient.top);
    DeleteObject(upd);
    ps->fErase = TRUE;
    if (erase)
        ps->fErase = !send_message(h, ((w->style & WS_MINIMIZE) && ((PCLS)w->cls)->hIcon) ?
                                   WM_ICONERASEBKGND : WM_ERASEBKGND, (WPARAM)ps->hdc, 0);
    ps->fRestore = 0;
    ps->fIncUpdate = 0;
    return ps->hdc;
}

void WINAPI EndPaint(HWND h, const PAINTSTRUCT __far *ps)
{
    release_dc(h, ps->hdc);
    ShowCaret(h);
}

/* paint now: send WM_PAINT to the window (and children) if it has an
   update region */
void update_window(HWND h, BOOL children)
{
    PWND w = valid_wnd(h);
    HWND c;
    if (!w || !(w->style & WS_VISIBLE))
        return;
    ulogx("update_window flags ", (WORD)w->flags);
    if (w->flags & WF_INPAINT) {
        if (w->hq == cur_hq() || !booted)
            send_message(h, WM_PAINT, 0, 0);
        if ((w = valid_wnd(h)) != 0 && (w->flags & WF_INPAINT) && (w->hq == cur_hq() || !booted)) {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
        }
    }
    if (children && (w = valid_wnd(h)) != 0)
        for (c = w->hwndChild; c; c = PW(c)->hwndNext)
            update_window(c, TRUE);
}

void WINAPI UpdateWindow(HWND h)
{
    update_window(h, TRUE);
}

BOOL WINAPI RedrawWindow(HWND h, const RECT __far *rc, HRGN rgn, UINT flags)
{
    RECT r;
    if (!h)
        h = hwndDesktop;
    if (!valid_wnd(h))
        return FALSE;
    if (rc)
        r = *rc;
    if (flags & RDW_INVALIDATE)
        invalidate(h, rgn, rc ? &r : 0, (flags & RDW_ERASE) != 0,
                   (flags & RDW_ALLCHILDREN) || (!(flags & RDW_NOCHILDREN) && children_of(h)),
                   (flags & RDW_FRAME) != 0);
    if (flags & RDW_INTERNALPAINT) {
        PW(h)->flags |= WF_INTERNALPAINT;
        need_paint(PW(h));
    }
    if (flags & RDW_VALIDATE)
        validate(h, rgn, rc ? &r : 0);
    if (flags & (RDW_UPDATENOW | RDW_ERASENOW))
        update_window(h, !(flags & RDW_NOCHILDREN));
    return TRUE;
}

/* ScrollWindow: w16 repaints instead of copying bits (UNVERIFIED: the
   result on screen equals Windows' when the window paints the same) */
void WINAPI ScrollWindow(HWND h, int dx, int dy, const RECT __far *rc, const RECT __far *clip)
{
    PWND w = valid_wnd(h);
    HWND c;
    if (!w)
        return;
    if (!rc)
        for (c = w->hwndChild; c; c = PW(c)->hwndNext) {
            OffsetRect(&PW(c)->rcWindow, dx, dy);
            OffsetRect(&PW(c)->rcClient, dx, dy);
        }
    (void)clip;
    InvalidateRect(h, rc, TRUE);
}

/* UNVERIFIED: w16 does not yet exclude the update region from hdc */
int WINAPI ExcludeUpdateRgn(HDC hdc, HWND h)
{
    (void)hdc, (void)h;
    return SIMPLEREGION;
}
