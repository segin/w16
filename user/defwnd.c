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

BOOL def_settext(PWND w, LPCSTR s);
LONG def_gettext(PWND w, WORD n, LPSTR buf);
LONG def_gettextlength(PWND w);
HBRUSH sys_brush(HBRUSH b);
void invalidate_screen(const RECT *rc);
void menu_track_bar(HWND h, char key);
BOOL track_window(HWND h, UINT hit, BOOL keyboard);
LRESULT WINAPI __loadds MDIClientWndProc(HWND, UINT, WPARAM, LPARAM);
void menu_track_point(HWND h, POINT pt);
void menu_track_sys(HWND h);
void menu_track_icon(HWND h);

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

/* the frame bitmaps for others (the MDI controls in a frame's menu bar,
   mdi.c and menu.c): 0 OBM_CLOSE, 1 OBM_RESTORE, 2 OBM_RESTORED */
HBITMAP frame_bitmap(int which)
{
    load_frame_bitmaps();
    return which == 0 ? bmClose : which == 1 ? bmRestore : bmRestoreD;
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
static void adjust_rect(RECT *r, DWORD style, BOOL menu, DWORD exstyle);

/* a bad RECT: left as it was; the fields of one at a segment's edge that
   can be read are changed (3.1, US27) */
void WINAPI AdjustWindowRectEx(RECT FAR *rp, DWORD style, BOOL menu, DWORD exstyle)
{
    RECT a;
    WORD n = gp_copy(&a, rp, sizeof a) & ~1;
    adjust_rect(&a, style, menu, exstyle);
    gp_copy(rp, &a, n);
}

static void adjust_rect(RECT *r, DWORD style, BOOL menu, DWORD exstyle)
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

/* the caption button held down (track_caption_button): drawn pressed
   while the pointer is on it */
static HWND btn_hwnd;
static int btn_hit;
static BOOL btn_down;

static BOOL btn_pressed(PWND w, int hit)
{
    return btn_down && btn_hit == hit && HW(w) == btn_hwnd;
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
        if (HW(w) == hwndSysMenuOpen) {
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
        blt_bitmap(hdc, (w->style & WS_MAXIMIZE) ? (btn_pressed(w, HTMAXBUTTON) ? bmRestoreD : bmRestore) :
                   btn_pressed(w, HTMAXBUTTON) ? bmZoomD : bmZoom, bx, t.top, 0,
                   sysmet[SM_CXSIZE] + 1, t.bottom - t.top);
    }
    if (w->style & WS_MINIMIZEBOX) {
        bx -= sysmet[SM_CXSIZE] + 1;
        blt_bitmap(hdc, btn_pressed(w, HTMINBUTTON) ? bmReduceD : bmReduce, bx, t.top, 0,
                   sysmet[SM_CXSIZE] + 1, t.bottom - t.top);
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
        /* a title too long for the caption starts a pixel in (3.1, US43's
           window B: its first letter one pixel right of w16's) */
        if (x < t.left)
            x = t.left + 1;
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
void scrollbar_menu(HWND hwnd, HWND notify, int bar, RECT *rc, POINT pt);
extern WORD sb_menu_on;

void nc_paint(HWND h, HRGN rgn, BOOL active)
{
    PWND w = valid_wnd(h);
    HDC hdc;
    if (!w || !is_visible(w))
        return;
    if (w->style & WS_MINIMIZE)
        return;
    hdc = get_dc(h, TRUE, 0, 0);
    nc_draw(hdc, w, active);
    release_dc(h, hdc);
    (void)rgn;
}

/* the frame, caption, menu bar and scroll bars on HDC, whose origin is the
   window's top left (also the Alt+Tab switch window's drawing of a
   window that is not on top: switch.c) */
void nc_draw(HDC hdc, PWND w, BOOL active)
{
    RECT cr;
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
}

/* WM_NCACTIVATE's painting. Observed on 3.1 (US41): the caption is
   repainted, the menu bar and a thick frame of the default colours are
   not (Win32s's Freecell writes "Cards Left" into its menu bar, which
   w16's whole non-client repaint wiped when a message box took the
   activation). The frame is repainted here when its colour changes with
   the activation (UNVERIFIED: 3.1's default colours give it none). */
static void nc_activate_paint(HWND h, BOOL active)
{
    PWND w = valid_wnd(h);
    HDC hdc;
    RECT cr;
    int frame;
    if (!w || !is_visible(w) || (w->style & WS_MINIMIZE))
        return;
    hdc = get_dc(h, TRUE, 0, 0);
    cr = w->rcClient;
    OffsetRect(&cr, -w->rcWindow.left, -w->rcWindow.top);
    ExcludeClipRect(hdc, cr.left, cr.top, cr.right, cr.bottom);
    frame = has_thick(w) ? GetSysColor(COLOR_ACTIVEBORDER) != GetSysColor(COLOR_INACTIVEBORDER)
                         : GetSysColor(COLOR_ACTIVECAPTION) != GetSysColor(COLOR_INACTIVECAPTION);
    if (frame)
        draw_frame(hdc, w, active);
    if (has_caption(w))
        draw_caption(hdc, w, active);
    release_dc(h, hdc);
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

/* The Task List: SYSTEM.INI [boot] taskman.exe= (default taskman.exe;
   Pietrek, Windows Internals ch. 2: USER reads it at start-up), run with
   WinExec, SW_SHOWNORMAL, and the command line "X Y": the point of a
   double-click on the desktop, "0 0" for Ctrl+Esc and the system menu's
   Switch To (probe TM1 as taskman.exe= on 3.1, ARCHITECTURE.md 15.5) */
static char taskman_name[0x82];

void load_taskman_name(void)
{
    GetPrivateProfileString("boot", "taskman.exe", "taskman.exe", taskman_name, sizeof taskman_name,
                            "SYSTEM.INI");
}

void start_taskman(int x, int y)
{
    char cmd[sizeof taskman_name + 16];
    if (!taskman_name[0])
        load_taskman_name();
    wsprintf(cmd, "%s %d %d", (LPSTR)taskman_name, x, y);
    WinExec(cmd, SW_SHOWNORMAL);
}

/* USER.172 SwitchToThisWindow(hwnd, fRestore) (Schulman, Undocumented
   Windows: the Task List's Switch To; fRestore restores an icon). On 3.1
   (probe TM2 under the Task List, ARCHITECTURE.md 15.5) the window is
   activated and brought to the top, then an icon gets WM_SYSCOMMAND
   SC_RESTORE (it answers WM_QUERYOPEN and is restored by DefWindowProc).
   UNVERIFIED: fRestore FALSE (only activated here) */
void WINAPI SwitchToThisWindow(HWND h, BOOL restore)
{
    if (!valid_wnd(h))
        return;
    activate_window(h, FALSE);
    if (restore && IsIconic(h))
        send_message(h, WM_SYSCOMMAND, SC_RESTORE, 0);
}

/* USER.274 GetNextQueueWindow(hwnd, nWhichWay): the top-level window a
   task switch from hwnd's would go to. US11 on 3.1 (six top-level windows,
   one hidden, one disabled, one owned): nWhichWay 1 goes up the Z order
   (GW_HWNDPREV), 0 down, round the end; a hidden or disabled window is
   passed over, and so is one whose last active popup is another window
   (its owned popup stands for it: the owner of an active popup never came
   back); 4 bytes of arguments. Microsoft's WINOLDAP calls it as it starts
   a DOS program; as a stub that popped nothing it put WINOLDAP's stack
   out and its C run-time stopped with R6000. UNVERIFIED: the case with no
   other window (0 here) */
HWND WINAPI U_GETNEXTQUEUEWINDOW(HWND h, int way)
{
    HWND t;
    PWND w = valid_wnd(h);
    if (!w)
        return 0;
    while (w->hwndParent && w->hwndParent != hwndDesktop)
        w = PW(h = w->hwndParent);
    for (t = h;;) {
        t = GetWindow(t, way ? GW_HWNDPREV : GW_HWNDNEXT);
        if (!t)
            t = GetWindow(h, way ? GW_HWNDLAST : GW_HWNDFIRST);
        if (!t || t == h)
            return 0;
        /* (an icon's title is passed over: US24 on 3.1, the ring from an
           icon went main window, icon. w16 gave the title, and Microsoft's
           WINOLDAP entered it as the Windows application to come back
           to: after Alt+Esc its icon stayed active) */
        if ((PW(t)->style & (WS_VISIBLE | WS_DISABLED)) == WS_VISIBLE && GetLastActivePopup(t) == t &&
            GetClassWord(t, GCW_ATOM) != 32772)
            return t;
    }
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

/* a caption button's rectangle (window coordinates), as draw_caption
   places it */
static void caption_button_rect(PWND w, int hit, RECT *b)
{
    RECT r;
    int bx;
    caption_rect(w, &r);
    bx = r.right - 1;
    if (w->style & WS_MAXIMIZEBOX) {
        bx -= sysmet[SM_CXSIZE] + 1;
        if (hit == HTMAXBUTTON) {
            SetRect(b, bx, r.top + 1, bx + sysmet[SM_CXSIZE] + 1, r.bottom - 1);
            return;
        }
    }
    bx -= sysmet[SM_CXSIZE] + 1;
    SetRect(b, bx, r.top + 1, bx + sysmet[SM_CXSIZE] + 1, r.bottom - 1);
}

static void draw_caption_button(HWND h, int hit)
{
    PWND w = valid_wnd(h);
    HDC hdc;
    RECT b;
    BOOL down;
    HBITMAP bm;
    if (!w || !is_visible(w) || (w->style & WS_MINIMIZE) || !has_caption(w))
        return;
    load_frame_bitmaps();
    caption_button_rect(w, hit, &b);
    down = btn_pressed(w, hit);
    if (hit == HTMINBUTTON)
        bm = down ? bmReduceD : bmReduce;
    else if (w->style & WS_MAXIMIZE)
        bm = down ? bmRestoreD : bmRestore;
    else
        bm = down ? bmZoomD : bmZoom;
    if ((hdc = GetWindowDC(h)) == 0)
        return;
    blt_bitmap(hdc, bm, b.left, b.top, 0, b.right - b.left, b.bottom - b.top);
    ReleaseDC(h, hdc);
}

/* the minimize or maximize (restore) button pressed: drawn pressed while
   the pointer is on it, raised when it leaves, the command sent on a
   release on it, nothing on a release elsewhere (3.1, US26: SC_MAXIMIZE
   came after the release; a release off the minimize button sent
   nothing; the "D" bitmaps while held) */
static void track_caption_button(HWND h, int hit)
{
    PWND w = valid_wnd(h);
    RECT b;
    MSG m;
    BOOL was;
    if (!w)
        return;
    caption_button_rect(w, hit, &b);
    OffsetRect(&b, w->rcWindow.left, w->rcWindow.top);
    btn_hwnd = h;
    btn_hit = hit;
    btn_down = TRUE;
    draw_caption_button(h, hit);
    SetCapture(h);
    for (;;) {
        if (!PeekMessage(&m, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            if (PeekMessage(&m, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) {
                DispatchMessage(&m);
                continue;
            }
            if (!valid_wnd(h) || GetCapture() != h)
                break;
            WaitMessage();
            continue;
        }
        if (!valid_wnd(h))
            break;
        if (m.message == WM_MOUSEMOVE || m.message == WM_LBUTTONUP) {
            BOOL in = PtInRect(&b, m.pt);
            if (in != btn_down) {
                btn_down = in;
                draw_caption_button(h, hit);
            }
        }
        if (m.message == WM_LBUTTONUP)
            break;
    }
    was = btn_down;
    btn_down = FALSE;
    btn_hwnd = 0;
    if (GetCapture() == h)
        ReleaseCapture();
    if (!(w = valid_wnd(h)))
        return;
    draw_caption_button(h, hit);
    if (!was)
        return;
    if (hit == HTMINBUTTON)
        send_message(h, WM_SYSCOMMAND, SC_MINIMIZE, MAKELONG(m.pt.x, m.pt.y));
    else
        send_message(h, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE,
                     MAKELONG(m.pt.x, m.pt.y));
}

static void sys_command(HWND h, PWND w, WPARAM cmd, LPARAM lp)
{
    switch (cmd & 0xFFF0) {
    case SC_CLOSE:
        send_message(h, WM_CLOSE, 0, 0);
        break;
    case SC_MOVE:
        /* an icon clicked, not dragged: its system menu (3.1: a click on
           a Program Manager group's icon, or on Program Manager's own on
           the desktop, put the menu up on the release, Restore selected;
           a press held still showed nothing) */
        if (track_window(h, HTCAPTION, (cmd & 0xF) == 0) && (w = valid_wnd(h)) != 0 &&
            (w->style & WS_MINIMIZE) && (w->style & WS_SYSMENU))
            menu_track_icon(h);
        break;
    case SC_SIZE:
        track_window(h, (cmd & 0xF) ? HTLEFT + (cmd & 0xF) - 1 : HTBOTTOMRIGHT, (cmd & 0xF) == 0);
        break;
    case SC_VSCROLL:
    case SC_HSCROLL: {
        /* a press on a window's scroll bar comes here (3.1, US26:
           WM_NCLBUTTONDOWN with HTVSCROLL sent WM_SYSCOMMAND SC_VSCROLL
           before any WM_VSCROLL. UNVERIFIED: its low four bits) */
        RECT v, hz;
        POINT pt;
        BOOL vert = (cmd & 0xFFF0) == SC_VSCROLL;
        window_scrollbar_rects(w, &v, &hz);
        OffsetRect(&v, w->rcWindow.left, w->rcWindow.top);
        OffsetRect(&hz, w->rcWindow.left, w->rcWindow.top);
        pt.x = (int)LOWORD(lp);
        pt.y = (int)HIWORD(lp);
        track_scrollbar(h, h, vert ? SB_VERT : SB_HORZ, vert ? &v : &hz, pt);
        break;
    }
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
        start_taskman(0, 0);
        break;
    case SC_NEXTWINDOW:
    case SC_PREVWINDOW:
        /* (an MDI child's are DefMDIChildProc's) */
        if (!(w->style & WS_CHILD))
            switch_windows(h, cmd, lp);
        break;
    case SC_SCREENSAVE:                 /* SYSTEM.INI's saver (3.1 SDK) */
        screensave_run();
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
            /* no room for the text: CreateWindow fails (3.1: US15) */
            if (cs->lpszName && !ISATOM(cs->lpszName) && !def_settext(w, cs->lpszName))
                return 0;
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
            nc_activate_paint(h, wp != 0);
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
            if (!(w = valid_wnd(h)))        /* (USER audit, sweep) */
                break;
            if (w->style & WS_SYSMENU)
                send_message(h, WM_SYSCOMMAND, SC_MOUSEMENU | HTSYSMENU, lp);
            break;
        case HTMENU:
            if (h != hwndActive)
                activate_window(h, TRUE);
            send_message(h, WM_SYSCOMMAND, SC_MOUSEMENU, lp);
            break;
        case HTVSCROLL:
        case HTHSCROLL:
            if (!(w->style & WS_CHILD) && h != hwndActive)
                activate_window(h, TRUE);
            if (!(w = valid_wnd(h)))
                return 0;
            send_message(h, WM_SYSCOMMAND, wp == HTVSCROLL ? SC_VSCROLL : SC_HSCROLL, lp);
            break;
        case HTMINBUTTON:
        case HTMAXBUTTON:
            track_caption_button(h, wp);
            break;
        }
        return 0;
    case WM_NCRBUTTONUP:
        /* w16's scroll-bar menu (scroll.c); with [w16] ScrollBarMenu=0
           nothing, as on 3.1 */
        if (sb_menu_on && (wp == HTVSCROLL || wp == HTHSCROLL)) {
            RECT v, hz;
            POINT pt;
            window_scrollbar_rects(w, &v, &hz);
            OffsetRect(&v, w->rcWindow.left, w->rcWindow.top);
            OffsetRect(&hz, w->rcWindow.left, w->rcWindow.top);
            pt.x = (int)LOWORD(lp);
            pt.y = (int)HIWORD(lp);
            scrollbar_menu(h, h, wp == HTVSCROLL ? SB_VERT : SB_HORZ, wp == HTVSCROLL ? &v : &hz, pt);
        }
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTSYSMENU)
            send_message(h, WM_SYSCOMMAND, SC_CLOSE, lp);
        /* an icon double-clicked opens, maximise box or not (3.1: the
           MS-DOS Prompt's icon, WINOLDAP's window has only a minimise
           box, w16's WINOLDAP on Microsoft's USER as well; w16 did
           nothing, and the DOS program could not be restored) */
        else if (wp == HTCAPTION && (w->style & WS_MINIMIZE))
            send_message(h, WM_SYSCOMMAND, SC_RESTORE, lp);
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
        DWORD org;
        if (!b)
            return 0;
        b = sys_brush(b);
        GetClientRect(h, &r);
        UnrealizeObject(b);
        /* the DC's brush origin back afterwards: it is the window's corner
           (USER sets it with the DC's origin, dc.c), and what the program
           draws next in this BeginPaint uses it (StarChart's dithered bars
           were three rows out of phase with 3.1's) */
        org = SetBrushOrg((HDC)wp, 0, 0);
        FillRect((HDC)wp, &r, b);
        SetBrushOrg((HDC)wp, LOWORD(org), HIWORD(org));
        return 1;
    }
    case WM_SETTEXT: {
        /* 3.1 answers 0, and a text of 32768 characters or more leaves
           the window none, the old one gone (US36: a window's, a
           static's, a button's; 32767 kept) */
        LPCSTR s = (LPCSTR)lp;
        if (s && !GP_BAD(s) && lstr_len(s) > 0x7FFF)
            s = "";
        if (!def_settext(w, s))
            return 0;
        if (has_caption(w) && is_visible(w))
            nc_paint(h, 0, (w->flags & WF_ACTIVEFRAME) != 0);
        if (w->style & WS_MINIMIZE) {
            follow_icon_title(h);       /* sized to the new text */
            redraw_icon_title(h);
        }
        return 0;
    }
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
        /* WH_CBT first; nonzero: nothing (US30 cbt on 3.1: C8 with the
           SC_ value, a refused SC_MAXIMIZE left the window as it was) */
        if (cbt_hook(HCBT_SYSCOMMAND, wp, lp) || !(w = valid_wnd(h)))
            return 0;
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
        /* Alt+Tab, Alt+Esc: the top-level window gets WM_SYSCOMMAND
           SC_PREVWINDOW, SC_NEXTWINDOW with Shift, lParam the key (3.1,
           US43: F050h 0009h; Alt+Shift+Esc F040h 001Bh), whose
           DefWindowProc switches (switch.c) */
        else if ((wp == VK_TAB || wp == VK_ESCAPE) && (HIWORD(lp) & KF_ALTDOWN) &&
                 GetKeyState(VK_CONTROL) >= 0 && !GetSysModalWindow())
            send_message(top_level(h), WM_SYSCOMMAND,
                         GetKeyState(VK_SHIFT) < 0 ? SC_NEXTWINDOW : SC_PREVWINDOW, wp);
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
        {
            /* the sizing borders' arrows (3.1 SDK, DefWindowProc; 3.1:
               US22, the left border's left-right arrow, the bottom right
               corner's diagonal one) */
            LPCSTR c = IDC_ARROW;
            switch (LOWORD(lp)) {
            case HTLEFT: case HTRIGHT: c = IDC_SIZEWE; break;
            case HTTOP: case HTBOTTOM: c = IDC_SIZENS; break;
            case HTTOPLEFT: case HTBOTTOMRIGHT: c = IDC_SIZENWSE; break;
            case HTTOPRIGHT: case HTBOTTOMLEFT: c = IDC_SIZENESW; break;
            }
            SetCursor(load_icon_cursor(0, c, TRUE));
        }
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
    case WM_SETREDRAW:
        /* WS_VISIBLE taken away or given back, nothing drawn or
           invalidated either way (US33 rd on 3.1: off, IsWindowVisible
           is FALSE, InvalidateRect and SetScrollPos leave the pixels and
           no update region; on, the window visible again with its old
           pixels) */
        if (wp)
            w->style |= WS_VISIBLE;
        else
            w->style &= ~WS_VISIBLE;
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
                        /* maximised, its system menu is in the frame's
                           bar and opens there (mdi.c) */
                        extern BOOL mdi_sysmenu_key(HWND c);
                        if (!mdi_sysmenu_key(c))
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
        start_taskman((int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_ERASEBKGND: {
        RECT r;
        HBRUSH old;
        DWORD org;
        GetClientRect(h, &r);
        /* the pattern aligned with the screen: a cache DC can come with
           the brush origin another user left (the gray caret's sets it
           to 1); 3.1's dithered desktop had one phase everywhere, w16's
           changed from one repaint to the next (boot-test cpclose) */
        UnrealizeObject(sysbrush[COLOR_BACKGROUND]);
        org = SetBrushOrg((HDC)wp, 0, 0);
        old = SelectObject((HDC)wp, sysbrush[COLOR_BACKGROUND]);
        PatBlt((HDC)wp, r.left, r.top, r.right - r.left, r.bottom - r.top, PATCOPY);
        SelectObject((HDC)wp, old);
        SetBrushOrg((HDC)wp, LOWORD(org), HIWORD(org));
        deskver_paint((HDC)wp, &r);     /* w16's stamp; off: nothing */
        return 1;
    }
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCPAINT:
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}
