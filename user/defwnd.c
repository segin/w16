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
void menu_track_bar(HWND h, char key);
void track_window(HWND h, UINT hit, BOOL keyboard);
LRESULT WINAPI __loadds MDIClientWndProc(HWND, UINT, WPARAM, LPARAM);
void menu_track_point(HWND h, POINT pt);
void menu_track_sys(HWND h);

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

void follow_icon_title(HWND h);
void redraw_icon_title(HWND h);

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
        /* a black line and the dialog frame (US4) */
        *cx = sysmet[SM_CXDLGFRAME] + sysmet[SM_CXBORDER];
        *cy = sysmet[SM_CYDLGFRAME] + sysmet[SM_CYBORDER];
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
    if (has_caption(w) && has_dlgframe(w) && !has_thick(w)) {
        /* a dialog frame: the caption, its top line white and one row
           into the frame (US4: client 24 below the top, 5 inside the
           other edges) */
        InflateRect(r, -fx, -fy);
        r->top += sysmet[SM_CYCAPTION] + sysmet[SM_CYDLGFRAME] - fy;
    } else {
        if (has_caption(w)) {
            /* the caption's upper line is the frame's inner line */
            r->top += sysmet[SM_CYCAPTION] - sysmet[SM_CYBORDER];
            if (!fy)
                r->top += sysmet[SM_CYBORDER];
        }
        InflateRect(r, -fx, -fy);
    }
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

/* AdjustWindowRectEx (USER.454) / AdjustWindowRect (USER.102): the
   window rectangle for a client rectangle, nc_calcsize the other way
   round, with a one-line menu bar when menu is set (3.1 SDK; scroll
   bars not counted). Calculator sizes itself with it: 259 x 230 client,
   WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX and a menu, 261 x 270 on 3.1. */
void WINAPI AdjustWindowRectEx(RECT FAR *r, DWORD style, BOOL menu, DWORD exstyle)
{
    /* nc_calcsize's rules on the style alone (its WND is a near pointer
       into USER's segment) */
    BOOL caption = (style & WS_CAPTION) == WS_CAPTION, thick = (style & WS_THICKFRAME) != 0;
    BOOL dlg = (style & WS_CAPTION) == WS_DLGFRAME || (exstyle & WS_EX_DLGMODALFRAME);
    int fx = 0, fy = 0, top;
    if (thick)
        fx = sysmet[SM_CXFRAME], fy = sysmet[SM_CYFRAME];
    else if (dlg)
        fx = sysmet[SM_CXDLGFRAME] + sysmet[SM_CXBORDER], fy = sysmet[SM_CYDLGFRAME] + sysmet[SM_CYBORDER];
    else if (style & WS_BORDER)
        fx = sysmet[SM_CXBORDER], fy = sysmet[SM_CYBORDER];
    if (caption && dlg && !thick)
        top = sysmet[SM_CYCAPTION] + sysmet[SM_CYDLGFRAME];
    else
        top = fy + (caption ? sysmet[SM_CYCAPTION] - sysmet[SM_CYBORDER] + (fy ? 0 : sysmet[SM_CYBORDER]) : 0);
    r->left -= fx;
    r->right += fx;
    r->bottom += fy;
    r->top -= top;
    if (menu)
        r->top -= sysmet[SM_CYMENU] + sysmet[SM_CYBORDER];
}

void WINAPI AdjustWindowRect(RECT FAR *r, DWORD style, BOOL menu)
{
    AdjustWindowRectEx(r, style, menu, 0);
}

/* caption rectangle, window coordinates (including its two black lines) */
static void caption_rect(PWND w, RECT *r)
{
    int fx, fy;
    frame_size(w, &fx, &fy);
    r->left = fx ? fx - sysmet[SM_CXBORDER] : 0;
    r->top = fy ? fy - sysmet[SM_CYBORDER] : 0;
    if (has_dlgframe(w) && !has_thick(w)) {
        r->left = fx;
        r->right = w->rcWindow.right - w->rcWindow.left - fx;
        r->top = 1 + sysmet[SM_CYDLGFRAME] - sysmet[SM_CYBORDER];
    }
    r->right = w->rcWindow.right - w->rcWindow.left - r->left;
    r->bottom = r->top + sysmet[SM_CYCAPTION];
}

/* the system-menu box, window coordinates: inverted while the system
   menu is open (US4) */
HWND hwndSysMenuOpen;          /* its system menu is open: its box inverted */

void sysbox_rect(PWND w, RECT *r)
{
    RECT c;
    caption_rect(w, &c);
    SetRect(r, c.left + 1, c.top + 1, c.left + 1 + sysmet[SM_CXSIZE], c.bottom - 1);
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
    /* the black lines above and below, and at the ends (white above and
       at the ends in a dialog frame: US4) */
    {
        int edge = (has_dlgframe(w) && !has_thick(w)) ? COLOR_WINDOW : COLOR_WINDOWFRAME;
        fill(hdc, r.left, r.top, r.right - r.left, 1, edge);
        fill(hdc, r.left, r.top, 1, r.bottom - r.top, edge);
        fill(hdc, r.right - 1, r.top, 1, r.bottom - r.top, edge);
        fill(hdc, r.left + (edge == COLOR_WINDOW), r.bottom - 1,
             r.right - r.left - 2 * (edge == COLOR_WINDOW), 1, COLOR_WINDOWFRAME);
    }
    t.left = r.left + 1;
    t.top = r.top + 1;
    t.right = r.right - 1;
    t.bottom = r.bottom - 1;
    if (w->style & WS_SYSMENU) {
        int s = sysmet[SM_CXSIZE];
        blt_bitmap(hdc, bmClose, t.left, t.top, (w->style & WS_CHILD) ? s : 0, s, t.bottom - t.top);
        if ((HWND)(WORD)w == hwndSysMenuOpen) {
            RECT b;
            SetRect(&b, t.left, t.top, t.left + s, t.bottom);
            InvertRect(hdc, &b);
        }
        /* in a dialog frame the white top line runs over it (US4) */
        {
            int d = has_dlgframe(w) && !has_thick(w);
            fill(hdc, t.left + s, r.top + d, 1, r.bottom - r.top - d, COLOR_WINDOWFRAME);
        }
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
        /* dialog frame (US4, a message box): the active caption colour
           (UNVERIFIED for an inactive one), 3 rows at the top, 4 pixels
           elsewhere */
        int b = active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION;
        int d = sysmet[SM_CXDLGFRAME];
        fill(hdc, 1, 1, cx - 2, d - 1, b);
        fill(hdc, 1, cy - 1 - d, cx - 2, d, b);
        fill(hdc, 1, 1, d, cy - 2, b);
        fill(hdc, cx - 1 - d, 1, d, cy - 2, b);

    }
}

void nc_draw_scrollbars(HDC hdc, PWND w);
void window_scrollbar_rects(PWND w, RECT *v, RECT *h);
void track_scrollbar(HWND hwnd, HWND notify, int bar, RECT *rc, POINT pt);

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
        /* as wide as the frame's inside, over the vertical scroll bar
           too (US4) */
        int fx, fy, top, mw;
        frame_size(w, &fx, &fy);
        mw = w->rcWindow.right - w->rcWindow.left - 2 * fx;
        top = cr.top - menu_bar_height(w, mw);
        menu_draw_bar(hdc, w, cr.left, top, mw);
    }
    if (w->style & (WS_VSCROLL | WS_HSCROLL))
        nc_draw_scrollbars(hdc, w);
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

/* the next visible, enabled top-level window after cur, activated */
void switch_window(HWND cur, BOOL tobottom)
{
    HWND t, first = 0;
    for (t = PW(hwndDesktop)->hwndChild; t; t = PW(t)->hwndNext) {
        PWND tw = PW(t);
        if (t == cur || tw->hwndOwner || !(tw->style & WS_VISIBLE) || (tw->style & WS_DISABLED))
            continue;
        if (!first)
            first = t;
    }
    if (!first)
        return;
    if (tobottom && valid_wnd(cur))
        set_window_pos(cur, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    /* Alt+Tab takes the window under the current one: the bottom of the
       others goes on top when cur stays put (UNVERIFIED order) */
    activate_window(first, FALSE);
    /* Alt+Tab restores an icon; Alt+Esc only activates it (3.1: Alt+Esc
       from Notepad's icon to Calculator's leaves both minimised) */
    if (IsIconic(first) && !tobottom)
        ShowWindow(first, SW_RESTORE);
}

/* ---- DefWindowProc ---- */
extern WORD lastkey;            /* the last key pressed (input.c) */
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
    case SC_MOVE:
        track_window(h, HTCAPTION, (cmd & 0xF) == 0);
        break;
    case SC_SIZE:
        track_window(h, (cmd & 0xF) ? HTLEFT + (cmd & 0xF) - 1 : HTBOTTOMRIGHT, (cmd & 0xF) == 0);
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
    case SC_KEYMENU:
        if ((w->style & WS_CHILD) && w->hwndParent) {
            send_message(top_level(h), WM_SYSCOMMAND, cmd, lp);
            break;
        }
        menu_track_bar(h, (char)LOWORD(lp));
        break;
    case SC_MOUSEMENU: {
        POINT pt;
        pt.x = (int)LOWORD(lp);
        pt.y = (int)HIWORD(lp);
        if ((cmd & 0xF) == HTSYSMENU)
            menu_track_sys(h);
        else
            menu_track_point(h, pt);
        break;
    }
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
        /* a minimised window's title under the icon: its colours follow
           (it was repainted only where something had covered it, so
           old highlighting stayed) */
        if (w->style & WS_MINIMIZE)
            redraw_icon_title(h);
        return 1;
    case WM_NCDESTROY:
        return 0;
    case WM_NCLBUTTONDOWN:
        switch (wp) {
        case HTCAPTION:
            if (!(w->style & WS_CHILD) && h != hwndActive)
                activate_window(h, TRUE);
            if ((w = valid_wnd(h)) != 0 && !(w->style & WS_MAXIMIZE))
                send_message(h, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, lp);
            break;
        case HTLEFT:
        case HTRIGHT:
        case HTTOP:
        case HTTOPLEFT:
        case HTTOPRIGHT:
        case HTBOTTOM:
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
            if (!(w->style & WS_CHILD) && h != hwndActive)
                activate_window(h, TRUE);
            if ((w = valid_wnd(h)) != 0)
                send_message(h, WM_SYSCOMMAND, SC_SIZE | (wp - HTLEFT + 1), lp);
            break;
        case HTCLIENT:
            if (!(w->style & WS_CHILD) && h != hwndActive)
                activate_window(h, TRUE);
            break;
        case HTSYSMENU:
            if (!(w->style & WS_CHILD) && h != hwndActive)
                activate_window(h, TRUE);
            if (w->style & WS_SYSMENU)
                send_message(h, WM_SYSCOMMAND, SC_MOUSEMENU | HTSYSMENU, lp);
            break;
        case HTMENU:
            if (h != hwndActive)
                activate_window(h, TRUE);
            send_message(h, WM_SYSCOMMAND, SC_MOUSEMENU, lp);
            break;
        case HTVSCROLL:
        case HTHSCROLL: {
            RECT v, hz;
            POINT pt;
            if (!(w->style & WS_CHILD) && h != hwndActive)
                activate_window(h, TRUE);
            if (!(w = valid_wnd(h)))
                return 0;
            window_scrollbar_rects(w, &v, &hz);
            OffsetRect(&v, w->rcWindow.left, w->rcWindow.top);
            OffsetRect(&hz, w->rcWindow.left, w->rcWindow.top);
            pt.x = (int)LOWORD(lp);
            pt.y = (int)HIWORD(lp);
            track_scrollbar(h, h, wp == HTVSCROLL ? SB_VERT : SB_HORZ, wp == HTVSCROLL ? &v : &hz, pt);
            break;
        }
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
            DrawIcon(ps.hdc, 2, 2, ((PCLS)w->cls)->hIcon);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PAINTICON: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (((PCLS)w->cls)->hIcon)
            DrawIcon(ps.hdc, 2, 2, ((PCLS)w->cls)->hIcon);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ICONERASEBKGND: {
        /* the parent's background behind the icon */
        PWND p = PW(w->hwndParent);
        HBRUSH b = p && p->hwndParent ? ((PCLS)p->cls)->hbrBackground :
                   (HBRUSH)(COLOR_BACKGROUND + 1);
        RECT r;
        GetClientRect(h, &r);
        b = sys_brush(b);
        if (b) {
            UnrealizeObject(b);
            FillRect((HDC)wp, &r, b);
        }
        return 1;
    }
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
        if (w->style & WS_MINIMIZE) {
            follow_icon_title(h);       /* sized to the new text */
            redraw_icon_title(h);
        }
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
        sys_command(h, w, wp, lp);
        return 0;
    case WM_KEYDOWN:
        /* Ctrl+Esc: the Task List (the system menu's Switch To...) */
        if (wp == VK_ESCAPE && (GetKeyState(VK_CONTROL) & 0x8000))
            send_message(top_level(h), WM_SYSCOMMAND, SC_TASKLIST, 0);
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_F4 && (HIWORD(lp) & KF_ALTDOWN))
            send_message(top_level(h), WM_SYSCOMMAND, SC_CLOSE, 0);
        /* Alt+Tab, Alt+Esc: the next top-level window (Alt+Esc sends the
           current one to the bottom). UNVERIFIED: Windows 3.1 shows the
           window's title while Alt+Tab is held; here it switches at once */
        else if ((wp == VK_TAB || wp == VK_ESCAPE) && (HIWORD(lp) & KF_ALTDOWN))
            switch_window(top_level(h), wp == VK_ESCAPE);
        return 0;
    case WM_SYSKEYUP:
        /* Alt or F10 pressed and released alone (Alt+F4 must not then
           enter the menu: it took the next keys) */
        if ((wp == VK_MENU || wp == VK_F10) && wp == lastkey && !(w->style & WS_CHILD))
            send_message(h, WM_SYSCOMMAND, SC_KEYMENU, 0);
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
    case 0x022A:                /* WM_DROPOBJECT ... WM_DRAGMOVE (drag.c) */
    case 0x022B:
    case 0x022C:
    case 0x022D:
    case 0x022E:
    case 0x022F: {
        extern LRESULT def_drag(PWND w, UINT msg);
        return def_drag(w, msg);
    }
    case WM_DRAWITEM:
        return 0;
    case WM_GETFONT:
    case WM_SETFONT:
        return 0;
    case WM_SYSCHAR:
        /* Alt+Space: the top-level window's system menu, from any of
           its children (the reference opens Program Manager's with the
           focus in a group); Alt+-: the MDI child's around the focus */
        if (lp & 0x20000000L) {
            if ((BYTE)wp == '-') {
                HWND c;
                for (c = h; c && valid_wnd(c); c = PW(c)->hwndParent)
                    if (PW(c)->hwndParent && valid_wnd(PW(c)->hwndParent) &&
                        ((PCLS)PW(PW(c)->hwndParent)->cls)->lpfnWndProc ==
                        (WNDPROC)MDIClientWndProc) {
                        menu_track_bar(c, ' ');
                        return 0;
                    }
            }
            send_message(top_level(h), WM_SYSCOMMAND, SC_KEYMENU, (BYTE)wp);
        }
        return 0;
    }
    return 0;
}

/* ---- the desktop window (class #32769) ---- */
LRESULT WINAPI __loadds DesktopWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDBLCLK:
        /* a double-click on the desktop: the Task List (3.1 User's Guide) */
        WinExec("taskman.exe", SW_SHOWNORMAL);
        return 0;
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
