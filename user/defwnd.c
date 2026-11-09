/*
 * defwnd.c - DefWindowProc, the window frame (non-client area), the
 * desktop window.
 *
 * The frame is drawn the way Windows 3.1 draws it on VGA, measured from
 * screenshots of the reference system (ARCHITECTURE.md 10.4): a sizing
 * frame of SM_CXFRAME pixels is a black line, ActiveBorder colour, and a
 * black line, with notches where the caption and the caption buttons
 * meet it; the caption (SM_CYCAPTION including one black line above and
 * one below, the frame's inner line being the upper one) holds the system
 * menu box (the left half of the display's OBM_CLOSE, then a black line)
 * and the minimise/maximise buttons (OBM_REDUCE, OBM_ZOOM, OBM_RESTORE,
 * each with its own black line on the left); the title is centred between
 * them in the system font. The menu bar (SM_CYMENU) follows, with a black
 * line under it.
 */
#include "user.h"

void def_settext(PWND w, LPCSTR s);
LONG def_gettext(PWND w, int n, LPSTR buf);
LONG def_gettextlength(PWND w);
HBRUSH sys_brush(HBRUSH b);
void invalidate_screen(const RECT *rc);

static HBITMAP bmClose, bmReduce, bmZoom, bmRestore, bmReduceD, bmZoomD, bmRestoreD;

static void load_frame_bitmaps(void)
{
    if (bmClose)
        return;
    bmClose = LoadBitmap(0, MAKEINTRESOURCE(OBM_CLOSE));
    bmReduce = LoadBitmap(0, MAKEINTRESOURCE(OBM_REDUCE));
    bmZoom = LoadBitmap(0, MAKEINTRESOURCE(OBM_ZOOM));
    bmRestore = LoadBitmap(0, MAKEINTRESOURCE(OBM_RESTORE));
    bmReduceD = LoadBitmap(0, MAKEINTRESOURCE(OBM_REDUCED));
    bmZoomD = LoadBitmap(0, MAKEINTRESOURCE(OBM_ZOOMD));
    bmRestoreD = LoadBitmap(0, MAKEINTRESOURCE(OBM_RESTORED));
}

/* ---- frame geometry ---- */
static BOOL has_thick(PWND w)   { return (w->style & WS_THICKFRAME) && !(w->style & WS_MINIMIZE); }
static BOOL has_dlgframe(PWND w)
{
    return !(w->style & WS_MINIMIZE) &&
           (((w->style & WS_CAPTION) == WS_DLGFRAME) || (w->exstyle & WS_EX_DLGMODALFRAME));
}
static BOOL has_caption(PWND w) { return (w->style & WS_CAPTION) == WS_CAPTION && !(w->style & WS_MINIMIZE); }
static BOOL has_menu(PWND w)
{
    return !(w->style & WS_CHILD) && w->hMenu && !(w->style & WS_MINIMIZE);
}

/* the width of the frame outside the caption on each side */
static void frame_size(PWND w, int *cx, int *cy)
{
    if (w->style & WS_MINIMIZE) {
        *cx = *cy = 0;
    } else if (has_thick(w)) {
        *cx = sysmet[SM_CXFRAME];
        *cy = sysmet[SM_CYFRAME];
    } else if (has_dlgframe(w)) {
        *cx = sysmet[SM_CXDLGFRAME];
        *cy = sysmet[SM_CYDLGFRAME];
    } else if (w->style & WS_BORDER) {
        *cx = sysmet[SM_CXBORDER];
        *cy = sysmet[SM_CYBORDER];
    } else
        *cx = *cy = 0;
}

void nc_calcsize(PWND w, RECT *r)
{
    int fx, fy;
    if (w->style & WS_MINIMIZE)
        return;
    frame_size(w, &fx, &fy);
    if (has_caption(w)) {
        /* the caption's upper line is the frame's inner line */
        r->top += sysmet[SM_CYCAPTION] - sysmet[SM_CYBORDER];
        if (!fy)
            r->top += sysmet[SM_CYBORDER];
    }
    InflateRect(r, -fx, -fy);
    if (has_menu(w))
        r->top += menu_bar_height(w, r->right - r->left) ;
    if (w->style & WS_VSCROLL)
        r->right -= sysmet[SM_CXVSCROLL] - sysmet[SM_CXBORDER];
    if (w->style & WS_HSCROLL)
        r->bottom -= sysmet[SM_CYHSCROLL] - sysmet[SM_CYBORDER];
    if (r->bottom < r->top)
        r->bottom = r->top;
    if (r->right < r->left)
        r->right = r->left;
}

/* caption rectangle, window coordinates (including its two black lines) */
static void caption_rect(PWND w, RECT *r)
{
    int fx, fy;
    frame_size(w, &fx, &fy);
    r->left = fx ? fx - sysmet[SM_CXBORDER] : 0;
    r->top = fy ? fy - sysmet[SM_CYBORDER] : 0;
    r->right = w->rcWindow.right - w->rcWindow.left - r->left;
    r->bottom = r->top + sysmet[SM_CYCAPTION];
}

static void blt_bitmap(HDC hdc, HBITMAP bm, int x, int y, int sx, int cx, int cy)
{
    HBITMAP old = SelectObject(hdcMem, bm);
    BitBlt(hdc, x, y, cx, cy, hdcMem, sx, 0, SRCCOPY);
    SelectObject(hdcMem, old);
}

static void fill(HDC hdc, int x, int y, int cx, int cy, int color)
{
    HBRUSH old = SelectObject(hdc, sysbrush[color]);
    PatBlt(hdc, x, y, cx, cy, PATCOPY);
    SelectObject(hdc, old);
}

static void draw_caption(HDC hdc, PWND w, BOOL active)
{
    RECT r, t;
    int bx;
    char title[128];
    caption_rect(w, &r);
    load_frame_bitmaps();
    /* the black lines above and below, and at the ends */
    fill(hdc, r.left, r.top, r.right - r.left, 1, COLOR_WINDOWFRAME);
    fill(hdc, r.left, r.bottom - 1, r.right - r.left, 1, COLOR_WINDOWFRAME);
    fill(hdc, r.left, r.top, 1, r.bottom - r.top, COLOR_WINDOWFRAME);
    fill(hdc, r.right - 1, r.top, 1, r.bottom - r.top, COLOR_WINDOWFRAME);
    t.left = r.left + 1;
    t.top = r.top + 1;
    t.right = r.right - 1;
    t.bottom = r.bottom - 1;
    if (w->style & WS_SYSMENU) {
        int s = sysmet[SM_CXSIZE];
        blt_bitmap(hdc, bmClose, t.left, t.top, (w->style & WS_CHILD) ? s : 0, s, t.bottom - t.top);
        fill(hdc, t.left + s, r.top, 1, r.bottom - r.top, COLOR_WINDOWFRAME);
        t.left += s + 1;
    }
    bx = t.right;
    if (w->style & WS_MAXIMIZEBOX) {
        bx -= sysmet[SM_CXSIZE] + 1;
        blt_bitmap(hdc, (w->style & WS_MAXIMIZE) ? bmRestore : bmZoom, bx, t.top, 0,
                   sysmet[SM_CXSIZE] + 1, t.bottom - t.top);
    }
    if (w->style & WS_MINIMIZEBOX) {
        bx -= sysmet[SM_CXSIZE] + 1;
        blt_bitmap(hdc, bmReduce, bx, t.top, 0, sysmet[SM_CXSIZE] + 1, t.bottom - t.top);
    }
    t.right = bx;
    fill(hdc, t.left, t.top, t.right - t.left, t.bottom - t.top,
         active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
    def_gettext(w, sizeof title, title);
    if (title[0]) {
        int n = lstr_len(title), tw, x, y;
        HFONT old = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
        int mode = SetBkMode(hdc, TRANSPARENT);
        COLORREF c = SetTextColor(hdc, syscolor[active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT]);
        tw = LOWORD(GetTextExtent(hdc, title, n));
        x = t.left + (t.right - t.left - tw) / 2;
        if (x < t.left)
            x = t.left;
        y = t.top + (t.bottom - t.top - cySysChar) / 2;
        SaveDC(hdc);
        IntersectClipRect(hdc, t.left, t.top, t.right, t.bottom);
        TextOut(hdc, x, y, title, n);
        RestoreDC(hdc, -1);
        SetTextColor(hdc, c);
        SetBkMode(hdc, mode);
        SelectObject(hdc, old);
    }
}

static void draw_frame(HDC hdc, PWND w, BOOL active)
{
    int cx = w->rcWindow.right - w->rcWindow.left;
    int cy = w->rcWindow.bottom - w->rcWindow.top;
    int fx, fy;
    frame_size(w, &fx, &fy);
    if (!fx && !fy)
        return;
    /* outer black line */
    fill(hdc, 0, 0, cx, 1, COLOR_WINDOWFRAME);
    fill(hdc, 0, cy - 1, cx, 1, COLOR_WINDOWFRAME);
    fill(hdc, 0, 0, 1, cy, COLOR_WINDOWFRAME);
    fill(hdc, cx - 1, 0, 1, cy, COLOR_WINDOWFRAME);
    if (fx <= 1)
        return;
    if (has_thick(w)) {
        int b = active ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER;
        int n = sysmet[SM_CXSIZE] + fx;         /* notch distance from the edge */
        int m = sysmet[SM_CYSIZE] + fy;
        fill(hdc, 1, 1, cx - 2, fy - 2, b);
        fill(hdc, 1, cy - fy + 1, cx - 2, fy - 2, b);
        fill(hdc, 1, 1, fx - 2, cy - 2, b);
        fill(hdc, cx - fx + 1, 1, fx - 2, cy - 2, b);
        /* inner black line */
        fill(hdc, fx - 1, fy - 1, cx - 2 * fx + 2, 1, COLOR_WINDOWFRAME);
        fill(hdc, fx - 1, cy - fy, cx - 2 * fx + 2, 1, COLOR_WINDOWFRAME);
        fill(hdc, fx - 1, fy - 1, 1, cy - 2 * fy + 2, COLOR_WINDOWFRAME);
        fill(hdc, cx - fx, fy - 1, 1, cy - 2 * fy + 2, COLOR_WINDOWFRAME);
        /* the notches: the sizing corners */
        fill(hdc, n, 1, 1, fy - 1, COLOR_WINDOWFRAME);
        fill(hdc, cx - n - 1, 1, 1, fy - 1, COLOR_WINDOWFRAME);
        fill(hdc, n, cy - fy, 1, fy - 1, COLOR_WINDOWFRAME);
        fill(hdc, cx - n - 1, cy - fy, 1, fy - 1, COLOR_WINDOWFRAME);
        fill(hdc, 1, m, fx - 1, 1, COLOR_WINDOWFRAME);
        fill(hdc, cx - fx, m, fx - 1, 1, COLOR_WINDOWFRAME);
        fill(hdc, 1, cy - m - 1, fx - 1, 1, COLOR_WINDOWFRAME);
        fill(hdc, cx - fx, cy - m - 1, fx - 1, 1, COLOR_WINDOWFRAME);
    } else {
        /* dialog frame: UNVERIFIED colours (to be measured) */
        int b = active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION;
        fill(hdc, 1, 1, cx - 2, fy - 1, b);
        fill(hdc, 1, cy - fy, cx - 2, fy - 1, b);
        fill(hdc, 1, 1, fx - 1, cy - 2, b);
        fill(hdc, cx - fx, 1, fx - 1, cy - 2, b);
    }
}

void nc_paint(HWND h, HRGN rgn, BOOL active)
{
    PWND w = valid_wnd(h);
    HDC hdc;
    RECT cr;
    if (!w || !is_visible(w))
        return;
    if (w->style & WS_MINIMIZE)
        return;
    hdc = get_dc(h, TRUE, 0, 0);
    /* leave the client area alone */
    cr = w->rcClient;
    OffsetRect(&cr, -w->rcWindow.left, -w->rcWindow.top);
    ExcludeClipRect(hdc, cr.left, cr.top, cr.right, cr.bottom);
    draw_frame(hdc, w, active);
    if (has_caption(w))
        draw_caption(hdc, w, active);
    if (has_menu(w)) {
        int fx, fy, top;
        frame_size(w, &fx, &fy);
        top = cr.top - menu_bar_height(w, cr.right - cr.left);
        menu_draw_bar(hdc, w, cr.left, top, cr.right - cr.left);
    }
    release_dc(h, hdc);
    (void)rgn;
}

/* ---- hit testing ---- */
LONG nc_hittest(PWND w, POINT pt)
{
    RECT r = w->rcWindow, c;
    int fx, fy, x, y, cx, cy;
    if (!PtInRect(&r, pt))
        return HTNOWHERE;
    if (w->style & WS_MINIMIZE)
        return HTCAPTION;
    if (PtInRect(&w->rcClient, pt))
        return HTCLIENT;
    frame_size(w, &fx, &fy);
    x = pt.x - r.left;
    y = pt.y - r.top;
    cx = r.right - r.left;
    cy = r.bottom - r.top;
    if (has_thick(w) && (x < fx || y < fy || x >= cx - fx || y >= cy - fy)) {
        int n = sysmet[SM_CXSIZE] + fx, m = sysmet[SM_CYSIZE] + fy;
        if (y < m) {
            if (x < n) return HTTOPLEFT;
            if (x >= cx - n) return HTTOPRIGHT;
        }
        if (y >= cy - m) {
            if (x < n) return HTBOTTOMLEFT;
            if (x >= cx - n) return HTBOTTOMRIGHT;
        }
        if (x < fx) return HTLEFT;
        if (x >= cx - fx) return HTRIGHT;
        if (y < fy) return HTTOP;
        return HTBOTTOM;
    }
    if (x < fx || y < fy || x >= cx - fx || y >= cy - fy)
        return HTBORDER;
    if (has_caption(w)) {
        caption_rect(w, &c);
        if (y < c.bottom) {
            if ((w->style & WS_SYSMENU) && x < c.left + 1 + sysmet[SM_CXSIZE])
                return HTSYSMENU;
            {
                int bx = c.right - 1;
                if (w->style & WS_MAXIMIZEBOX) {
                    bx -= sysmet[SM_CXSIZE] + 1;
                    if (x >= bx)
                        return HTMAXBUTTON;
                }
                if (w->style & WS_MINIMIZEBOX) {
                    bx -= sysmet[SM_CXSIZE] + 1;
                    if (x >= bx)
                        return HTMINBUTTON;
                }
            }
            return HTCAPTION;
        }
    }
    if (has_menu(w) && pt.y < w->rcClient.top)
        return HTMENU;
    if ((w->style & WS_VSCROLL) && pt.x >= w->rcClient.right)
        return HTVSCROLL;
    if ((w->style & WS_HSCROLL) && pt.y >= w->rcClient.bottom)
        return HTHSCROLL;
    return HTBORDER;
}

/* ---- DefWindowProc ---- */
static LONG def_ctlcolor(HDC hdc, int type)
{
    if (type == CTLCOLOR_SCROLLBAR) {
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, RGB(0, 0, 0));
        UnrealizeObject(sysbrush[COLOR_SCROLLBAR]);
        return (LONG)(WORD)sysbrush[COLOR_SCROLLBAR];
    }
    if (type == CTLCOLOR_MSGBOX || type == CTLCOLOR_DLG || type == CTLCOLOR_STATIC ||
        type == CTLCOLOR_BTN) {
        SetBkColor(hdc, syscolor[COLOR_WINDOW]);
        SetTextColor(hdc, syscolor[COLOR_WINDOWTEXT]);
        return (LONG)(WORD)sysbrush[COLOR_WINDOW];
    }
    SetBkColor(hdc, syscolor[COLOR_WINDOW]);
    SetTextColor(hdc, syscolor[COLOR_WINDOWTEXT]);
    return (LONG)(WORD)sysbrush[COLOR_WINDOW];
}

static void sys_command(HWND h, PWND w, WPARAM cmd, LPARAM lp)
{
    switch (cmd & 0xFFF0) {
    case SC_CLOSE:
        send_message(h, WM_CLOSE, 0, 0);
        break;
    case SC_MINIMIZE:
        ShowWindow(h, SW_MINIMIZE);
        break;
    case SC_MAXIMIZE:
        ShowWindow(h, SW_SHOWMAXIMIZED);
        break;
    case SC_RESTORE:
        ShowWindow(h, SW_RESTORE);
        break;
    case SC_TASKLIST:
        WinExec("taskman.exe", SW_SHOWNORMAL);
        break;
    }
    (void)w, (void)lp;
}

LRESULT WINAPI DefWindowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w = valid_wnd(h);
    if (!w)
        return 0;
    switch (msg) {
    case WM_NCCREATE:
        if (lp) {
            CREATESTRUCT __far *cs = (CREATESTRUCT __far *)lp;
            if (cs->lpszName && !ISATOM(cs->lpszName))
                def_settext(w, cs->lpszName);
        }
        return 1;
    case WM_NCCALCSIZE: {
        RECT r = ((RECT __far *)lp)[0];
        nc_calcsize(w, &r);
        ((RECT __far *)lp)[0] = r;
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt;
        pt.x = (int)LOWORD(lp);
        pt.y = (int)HIWORD(lp);
        return nc_hittest(w, pt);
    }
    case WM_NCPAINT:
        nc_paint(h, (HRGN)wp, (w->flags & WF_ACTIVEFRAME) != 0);
        return 0;
    case WM_NCACTIVATE:
        if (wp)
            w->flags |= WF_ACTIVEFRAME;
        else
            w->flags &= ~WF_ACTIVEFRAME;
        if (is_visible(w))
            nc_paint(h, 0, wp != 0);
        return 1;
    case WM_NCDESTROY:
        return 0;
    case WM_NCLBUTTONDOWN:
        switch (wp) {
        case HTCAPTION:
        case HTSYSMENU:
        case HTCLIENT:
            if (!(w->style & WS_CHILD) && h != hwndActive)
                activate_window(h, TRUE);
            break;
        case HTMINBUTTON:
            send_message(h, WM_SYSCOMMAND, SC_MINIMIZE, lp);
            break;
        case HTMAXBUTTON:
            send_message(h, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE, lp);
            break;
        }
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTSYSMENU)
            send_message(h, WM_SYSCOMMAND, SC_CLOSE, lp);
        else if (wp == HTCAPTION && (w->style & WS_MAXIMIZEBOX))
            send_message(h, WM_SYSCOMMAND, (w->style & (WS_MAXIMIZE | WS_MINIMIZE)) ? SC_RESTORE :
                         SC_MAXIMIZE, lp);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if ((w->style & WS_MINIMIZE) && ((PCLS)w->cls)->hIcon) {
            send_message(h, WM_ICONERASEBKGND, (WPARAM)ps.hdc, 0);
            DrawIcon(ps.hdc, 0, 0, ((PCLS)w->cls)->hIcon);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PAINTICON:
        return 0;
    case WM_ICONERASEBKGND:
    case WM_ERASEBKGND: {
        HBRUSH b = ((PCLS)w->cls)->hbrBackground;
        RECT r;
        if (!b)
            return 0;
        b = sys_brush(b);
        GetClientRect(h, &r);
        UnrealizeObject(b);
        SetBrushOrg((HDC)wp, 0, 0);
        FillRect((HDC)wp, &r, b);
        return 1;
    }
    case WM_SETTEXT:
        def_settext(w, (LPCSTR)lp);
        if (has_caption(w) && is_visible(w))
            nc_paint(h, 0, (w->flags & WF_ACTIVEFRAME) != 0);
        return 1;
    case WM_GETTEXT:
        return def_gettext(w, wp, (LPSTR)lp);
    case WM_GETTEXTLENGTH:
        return def_gettextlength(w);
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION:
    case WM_QUERYOPEN:
        return 1;
    case WM_SYSCOMMAND:
        ulogx("WM_SYSCOMMAND ", wp);
        sys_command(h, w, wp, lp);
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_F4 && (HIWORD(lp) & KF_ALTDOWN))
            send_message(top_level(h), WM_SYSCOMMAND, SC_CLOSE, 0);
        return 0;
    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS __far *p = (WINDOWPOS __far *)lp;
        if (!(p->flags & SWP_NOMOVE)) {
            int x = w->rcClient.left, y = w->rcClient.top;
            if ((w->style & WS_CHILD) && w->hwndParent) {
                x -= PW(w->hwndParent)->rcClient.left;
                y -= PW(w->hwndParent)->rcClient.top;
            }
            send_message(h, WM_MOVE, 0, MAKELONG(x, y));
        }
        if (!(p->flags & SWP_NOSIZE) && (w = valid_wnd(h)) != 0)
            send_message(h, WM_SIZE, (w->style & WS_MINIMIZE) ? SIZE_MINIMIZED :
                         (w->style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : SIZE_RESTORED,
                         MAKELONG(w->rcClient.right - w->rcClient.left,
                                  w->rcClient.bottom - w->rcClient.top));
        return 0;
    }
    case WM_SETCURSOR:
        if ((w->style & WS_CHILD) && w->hwndParent &&
            send_message(w->hwndParent, WM_SETCURSOR, wp, lp))
            return 1;
        if (LOWORD(lp) == HTCLIENT) {
            if (((PCLS)w->cls)->hCursor)
                SetCursor(((PCLS)w->cls)->hCursor);
            return 0;
        }
        if (LOWORD(lp) == HTERROR && (HIWORD(lp) == WM_LBUTTONDOWN))
            MessageBeep(0);
        SetCursor(load_icon_cursor(0, IDC_ARROW, TRUE));
        return 0;
    case WM_MOUSEACTIVATE:
        if ((w->style & WS_CHILD) && w->hwndParent) {
            LONG r = send_message(w->hwndParent, WM_MOUSEACTIVATE, wp, lp);
            if (r)
                return r;
        }
        return LOWORD(lp) == HTCAPTION ? MA_ACTIVATE : MA_ACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && !HIWORD(lp))
            SetFocus(h);
        return 0;
    case WM_SHOWWINDOW:
        return 0;
    case WM_CTLCOLOR:
        return def_ctlcolor((HDC)wp, HIWORD(lp));
    case WM_GETMINMAXINFO:
    case WM_CANCELMODE:
        return 0;
    case WM_DRAWITEM:
        return 0;
    case WM_GETFONT:
    case WM_SETFONT:
        return 0;
    case WM_SYSCHAR:
        return 0;
    }
    return 0;
}

/* ---- the desktop window (class #32769) ---- */
LRESULT WINAPI DesktopWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r;
        HBRUSH old;
        GetClientRect(h, &r);
        UnrealizeObject(sysbrush[COLOR_BACKGROUND]);
        old = SelectObject((HDC)wp, sysbrush[COLOR_BACKGROUND]);
        PatBlt((HDC)wp, r.left, r.top, r.right - r.left, r.bottom - r.top, PATCOPY);
        SelectObject((HDC)wp, old);
        return 1;
    }
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCPAINT:
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}
