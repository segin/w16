/*
 * scroll.c - scroll bars: the SCROLLBAR class and windows' own scroll bars
 * (WS_VSCROLL, WS_HSCROLL), SetScrollPos/Range, GetScrollPos/Range,
 * ShowScrollBar, and the tracking loop.
 *
 * Measured on Windows 3.1 (probe US4): a scroll bar is the display's arrow
 * bitmaps (OBM_LFARROW, OBM_RGARROW, OBM_UPARROW, OBM_DNARROW, 17x17 with
 * their own black edges) at its ends, the shaft between in the Scrollbar
 * colour with black lines along it, and the thumb, an arrow-sized button
 * whose edges overlap the arrows' inner edges at the ends of its travel:
 * left = start + arrow - 1 + (len - thumb) * (pos - min) / (max - min),
 * len being the length less two arrows plus two.
 */
#include "user.h"

typedef struct SBINFO {
    int min, max, pos;
    WORD disabled;              /* shown with nothing to scroll (no thumb,
                                   greyed arrows): sb_show_disabled */
} SBINFO;

typedef struct SBS {            /* a window's two bars (WND.scroll) */
    SBINFO v, h;
} SBS;

static HBITMAP bmArrow[12];     /* up, down, right, left; pressed; greyed */

static void load_arrows(void)
{
    static const WORD ids[12] = { OBM_UPARROW, OBM_DNARROW, OBM_RGARROW, OBM_LFARROW,
                                  OBM_UPARROWD, OBM_DNARROWD, OBM_RGARROWD, OBM_LFARROWD,
                                  32737, 32736, 32735, 32734 };   /* OBM_UPARROWI ... */
    int i;
    if (bmArrow[0])
        return;
    for (i = 0; i < 12; i++)
        bmArrow[i] = LoadBitmap(0, MAKEINTRESOURCE(ids[i]));
}

static void fillc(HDC hdc, int x, int y, int cx, int cy, int c)
{
    HBRUSH old = SelectObject(hdc, sysbrush[c]);
    PatBlt(hdc, x, y, cx, cy, PATCOPY);
    SelectObject(hdc, old);
}

static void blt(HDC hdc, int x, int y, int cx, int cy, HBITMAP b)
{
    HBITMAP old = SelectObject(hdcMem, b);
    BitBlt(hdc, x, y, cx, cy, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, old);
}

/* the thumb: an arrow's frame without the glyph */
static void draw_thumb(HDC hdc, int x, int y, int cx, int cy)
{
    fillc(hdc, x, y, cx, 1, COLOR_WINDOWFRAME);
    fillc(hdc, x, y + cy - 1, cx, 1, COLOR_WINDOWFRAME);
    fillc(hdc, x, y, 1, cy, COLOR_WINDOWFRAME);
    fillc(hdc, x + cx - 1, y, 1, cy, COLOR_WINDOWFRAME);
    fillc(hdc, x + 1, y + 1, cx - 2, cy - 2, COLOR_BTNFACE);
    fillc(hdc, x + 1, y + 1, cx - 3, 1, COLOR_BTNHIGHLIGHT);
    fillc(hdc, x + 1, y + 1, 1, cy - 3, COLOR_BTNHIGHLIGHT);
    /* a two-pixel shadow whose inner line starts a pixel later (US4) */
    fillc(hdc, x + cx - 2, y + 1, 1, cy - 2, COLOR_BTNSHADOW);
    fillc(hdc, x + cx - 3, y + 2, 1, cy - 3, COLOR_BTNSHADOW);
    fillc(hdc, x + 1, y + cy - 2, cx - 2, 1, COLOR_BTNSHADOW);
    fillc(hdc, x + 2, y + cy - 3, cx - 3, 1, COLOR_BTNSHADOW);
}

int thumb_pos(int len, int arrow, SBINFO *s)
{
    int travel = len - 2 * arrow + 2 - arrow;
    if (s->max <= s->min || travel <= 0)
        return arrow - 1;
    /* rounded to the nearest (US4: 45.5 pixels went to 46) */
    return arrow - 1 + (int)(((long)travel * (s->pos - s->min) * 2 + (s->max - s->min)) /
                             (2L * (s->max - s->min)));
}

/* a bar in rc of hdc */
void draw_scrollbar(HDC hdc, RECT *rc, BOOL vert, SBINFO *s)
{
    int len = vert ? rc->bottom - rc->top : rc->right - rc->left;
    int thick = vert ? rc->right - rc->left : rc->bottom - rc->top;
    int arrow = vert ? sysmet[SM_CYVSCROLL] : sysmet[SM_CXHSCROLL];
    int t, ath = thick;         /* the arrows' thickness */
    int g = s->disabled && bmArrow[8] ? 8 : 0;      /* greyed arrows */
    load_arrows();
    g = s->disabled && bmArrow[8] ? 8 : 0;
    if (len < 2 * arrow)
        arrow = len / 2;
    /* a bar thicker than the arrows: they keep their size at the near
       side, black beside them to the far edge; the shaft and the thumb
       take the whole thickness (US4: 18-pixel scroll-bar controls) */
    if (vert && thick > sysmet[SM_CXVSCROLL]) {
        ath = sysmet[SM_CXVSCROLL];
        fillc(hdc, rc->left + ath, rc->top, thick - ath, arrow, COLOR_WINDOWFRAME);
        fillc(hdc, rc->left + ath, rc->bottom - arrow, thick - ath, arrow, COLOR_WINDOWFRAME);
    } else if (!vert && thick > sysmet[SM_CYHSCROLL]) {
        ath = sysmet[SM_CYHSCROLL];
        fillc(hdc, rc->left, rc->top + ath, arrow, thick - ath, COLOR_WINDOWFRAME);
        fillc(hdc, rc->right - arrow, rc->top + ath, arrow, thick - ath, COLOR_WINDOWFRAME);
    }
    if (vert) {
        blt(hdc, rc->left, rc->top, ath, arrow, bmArrow[g + 0]);
        blt(hdc, rc->left, rc->bottom - arrow, ath, arrow, bmArrow[g + 1]);
        /* disabled: a white shaft (3.1, File Manager's tree; UNVERIFIED
           that it is the window colour) */
        fillc(hdc, rc->left, rc->top + arrow, thick, len - 2 * arrow, g ? COLOR_WINDOW : COLOR_SCROLLBAR);
        fillc(hdc, rc->left, rc->top + arrow, 1, len - 2 * arrow, COLOR_WINDOWFRAME);
        fillc(hdc, rc->right - 1, rc->top + arrow, 1, len - 2 * arrow, COLOR_WINDOWFRAME);
        if (s->max > s->min && len > 3 * arrow - 2) {
            t = thumb_pos(len, arrow, s);
            draw_thumb(hdc, rc->left, rc->top + t, thick, arrow);
        }
    } else {
        blt(hdc, rc->left, rc->top, arrow, ath, bmArrow[g + 3]);
        blt(hdc, rc->right - arrow, rc->top, arrow, ath, bmArrow[g + 2]);
        fillc(hdc, rc->left + arrow, rc->top, len - 2 * arrow, thick, g ? COLOR_WINDOW : COLOR_SCROLLBAR);
        fillc(hdc, rc->left + arrow, rc->top, len - 2 * arrow, 1, COLOR_WINDOWFRAME);
        fillc(hdc, rc->left + arrow, rc->bottom - 1, len - 2 * arrow, 1, COLOR_WINDOWFRAME);
        if (s->max > s->min && len > 3 * arrow - 2) {
            t = thumb_pos(len, arrow, s);
            draw_thumb(hdc, rc->left + t, rc->top, arrow, thick);
        }
    }
}

/* ---- a window's bars ----
   WND+34 is the handle of their state, a block of the object heap
   (uheap.c; 3.1's is a 10h-byte DGROUP block made with a window that has
   scroll bars: US15); a pointer to it is not kept across a message */
static SBS __far *sbs_of(PWND w, BOOL create)
{
    if (!w->scroll && create) {
        WORD h = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, sizeof(SBS));
        SBS __far *s;
        if (!h)
            return 0;
        s = (SBS __far *)UH_PTR(&uhObj, h);
        s->v.max = s->h.max = 100;
        w->scroll = h;
    }
    return w->scroll ? (SBS __far *)UH_PTR(&uhObj, w->scroll) : (SBS __far *)0;
}

/* DestroyWindow */
void free_scroll(PWND w)
{
    uh_free(&uhObj, w->scroll);
    w->scroll = 0;
}

void window_scrollbar_rects(PWND w, RECT *v, RECT *h)
{
    /* window coordinates: right of and below the client area, their
       outer lines on the frame's (or menu bar's) black lines, the two
       bars sharing a line (US4) */
    RECT c = w->rcClient;
    OffsetRect(&c, -w->rcWindow.left, -w->rcWindow.top);
    SetRect(v, c.right, c.top - sysmet[SM_CYBORDER],
            c.right + sysmet[SM_CXVSCROLL], c.bottom + sysmet[SM_CYBORDER]);
    SetRect(h, c.left - sysmet[SM_CXBORDER], c.bottom,
            c.right + sysmet[SM_CXBORDER], c.bottom + sysmet[SM_CYHSCROLL]);
}

/* called by nc_paint with the window DC */
void nc_draw_scrollbars(HDC hdc, PWND w)
{
    SBS __far *s = sbs_of(w, TRUE);
    RECT v, h;
    window_scrollbar_rects(w, &v, &h);
    if ((w->style & WS_VSCROLL) && s)
        draw_scrollbar(hdc, &v, TRUE, &s->v);
    if ((w->style & WS_HSCROLL) && s)
        draw_scrollbar(hdc, &h, FALSE, &s->h);
    if ((w->style & WS_VSCROLL) && (w->style & WS_HSCROLL)) {
        /* the corner between them */
        fillc(hdc, v.left + 1, h.top + 1, v.right - v.left - 2, h.bottom - h.top - 2, COLOR_BTNFACE);
    }
}

static SBINFO *info(HWND hwnd, int bar, PWND *pw)
{
    PWND w = valid_wnd(hwnd);
    *pw = w;
    if (!w)
        return 0;
    if (bar == SB_CTL)
        return (SBINFO *)MAKELP(get_ds(), WND_EXTRA(w));
    {
        SBS __far *s = sbs_of(w, TRUE);
        if (!s)
            return 0;
        return bar == SB_VERT ? &s->v : &s->h;
    }
}

static void redraw_bar(HWND hwnd, int bar)
{
    PWND w = valid_wnd(hwnd);
    if (!w || !is_visible(w))
        return;
    if (bar == SB_CTL)
        InvalidateRect(hwnd, NULL, FALSE), UpdateWindow(hwnd);
    else
        send_message(hwnd, WM_NCPAINT, 1, 0);
}

int WINAPI SetScrollPos(HWND hwnd, int bar, int pos, BOOL redraw)
{
    PWND w;
    SBINFO *s = info(hwnd, bar, &w);
    int old;
    if (!s)
        return 0;
    old = s->pos;
    if (pos < s->min)
        pos = s->min;
    if (pos > s->max)
        pos = s->max;
    s->pos = pos;
    if (redraw && old != pos)
        redraw_bar(hwnd, bar);
    return old;
}

int WINAPI GetScrollPos(HWND hwnd, int bar)
{
    PWND w;
    SBINFO *s = info(hwnd, bar, &w);
    return s ? s->pos : 0;
}

void WINAPI SetScrollRange(HWND hwnd, int bar, int min, int max, BOOL redraw)
{
    PWND w;
    SBINFO *s = info(hwnd, bar, &w);
    WORD was;
    if (!s)
        return;
    was = s->disabled;
    s->disabled = 0;
    s->min = min;
    s->max = max;
    if (s->pos < min)
        s->pos = min;
    if (s->pos > max)
        s->pos = max;
    if (bar != SB_CTL) {
        /* an empty range hides the bar (3.1 SDK) */
        DWORD f = bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
        BOOL want = min != max;
        if (((w->style & f) != 0) != want) {
            if (want)
                w->style |= f;
            else
                w->style &= ~f;
            set_window_pos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                           SWP_NOACTIVATE | SWP_FRAMECHANGED);
            return;
        }
    }
    /* a bar that was shown disabled is drawn again either way (File
       Manager's file list, refilled, kept its greyed bar) */
    if (redraw || was)
        redraw_bar(hwnd, bar);
}

/* a window's bar shown with nothing to scroll: no thumb, the arrows
   greyed (3.1: File Manager's tree, LBS_DISABLENOSCROLL, four items in
   room for more, shows its vertical bar so) */
void sb_show_disabled(HWND hwnd, int bar)
{
    PWND w;
    SBINFO *s = info(hwnd, bar, &w);
    DWORD f = bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
    if (!s || bar == SB_CTL)
        return;
    s->min = s->max = s->pos = 0;
    if (s->disabled && (w->style & f))
        return;
    s->disabled = 1;
    if (!(w->style & f)) {
        w->style |= f;
        set_window_pos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else
        redraw_bar(hwnd, bar);
}

void WINAPI GetScrollRange(HWND hwnd, int bar, int __far *min, int __far *max)
{
    PWND w;
    SBINFO *s = info(hwnd, bar, &w);
    *min = s ? s->min : 0;
    *max = s ? s->max : 0;
}

void WINAPI ShowScrollBar(HWND hwnd, int bar, BOOL show)
{
    PWND w = valid_wnd(hwnd);
    DWORD f;
    if (!w)
        return;
    if (bar == SB_CTL) {
        ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
        return;
    }
    f = bar == SB_VERT ? WS_VSCROLL : bar == SB_HORZ ? WS_HSCROLL : WS_VSCROLL | WS_HSCROLL;
    if (show)
        w->style |= f;
    else
        w->style &= ~f;
    set_window_pos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

/* ---- tracking: the mouse on a bar of window or control hwnd ---- */
static int hit_part(int at, int len, int arrow, SBINFO *s)
{
    int t = thumb_pos(len, arrow, s);
    if (at < arrow)
        return SB_LINEUP;
    if (at >= len - arrow)
        return SB_LINEDOWN;
    if (s->max > s->min && at >= t && at < t + arrow)
        return SB_THUMBTRACK;
    return at < t ? SB_PAGEUP : SB_PAGEDOWN;
}

void track_scrollbar(HWND hwnd, HWND notify, int bar, RECT *rc, POINT pt)
{
    PWND w;
    SBINFO *s = info(hwnd, bar, &w);
    BOOL vert = bar == SB_VERT || (bar == SB_CTL && (GetWindowLong(hwnd, GWL_STYLE) & SBS_VERT));
    int len = vert ? rc->bottom - rc->top : rc->right - rc->left;
    int arrow = vert ? sysmet[SM_CYVSCROLL] : sysmet[SM_CXHSCROLL];
    int at = vert ? pt.y - rc->top : pt.x - rc->left;
    int part = hit_part(at, len, arrow, s);
    UINT msg = vert ? WM_VSCROLL : WM_HSCROLL;
    LPARAM lp = bar == SB_CTL ? MAKELONG(0, hwnd) : 0;
    MSG m;
    DWORD next = GetTickCount() + 400;
    int grab = at - thumb_pos(len, arrow, s);
    SetCapture(hwnd);
    if (part != SB_THUMBTRACK)
        send_message(notify, msg, part, lp);
    for (;;) {
        /* again after each message sent: the state is in the object heap */
        if ((s = info(hwnd, bar, &w)) == 0)
            break;
        if (!PeekMessage(&m, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            if (part != SB_THUMBTRACK && (long)(GetTickCount() - next) >= 0) {
                send_message(notify, msg, part, lp);
                next = GetTickCount() + 50;
            }
            WaitMessage();
            continue;
        }
        if (m.message == WM_MOUSEMOVE && part == SB_THUMBTRACK) {
            POINT p = m.pt;
            int a = (vert ? p.y - rc->top : p.x - rc->left) - grab - (arrow - 1);
            int travel = len - 3 * arrow + 2;
            long v = travel > 0 ? s->min + (long)a * (s->max - s->min) / travel : s->min;
            if (bar != SB_CTL) {
                POINT o;
                o.x = o.y = 0;
                (void)o;
            }
            if (v < s->min)
                v = s->min;
            if (v > s->max)
                v = s->max;
            send_message(notify, msg, SB_THUMBTRACK, MAKELONG((int)v, HIWORD(lp)));
        }
        if (m.message == WM_LBUTTONUP) {
            if (part == SB_THUMBTRACK) {
                POINT p = m.pt;
                int a = (vert ? p.y - rc->top : p.x - rc->left) - grab - (arrow - 1);
                int travel = len - 3 * arrow + 2;
                long v = travel > 0 ? s->min + (long)a * (s->max - s->min) / travel : s->min;
                if (v < s->min)
                    v = s->min;
                if (v > s->max)
                    v = s->max;
                send_message(notify, msg, SB_THUMBPOSITION, MAKELONG((int)v, HIWORD(lp)));
            }
            break;
        }
    }
    ReleaseCapture();
    send_message(notify, msg, SB_ENDSCROLL, lp);
}

/* ---- the SCROLLBAR class: its SBINFO in the window extra bytes
   (Windows 3.1's class has 10: US2) ---- */
LRESULT WINAPI __loadds ScrollBarWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w = valid_wnd(h);
    SBINFO *s;
    if (!w)
        return 0;
    s = (SBINFO *)MAKELP(get_ds(), WND_EXTRA(w));
    switch (msg) {
    case WM_CREATE:
        s->min = s->max = s->pos = 0;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        HDC hdc = BeginPaint(h, &ps);
        GetClientRect(h, &r);
        if (w->style & SBS_SIZEBOX)
            FillRect(hdc, &r, sysbrush[COLOR_BTNFACE]);
        else
            draw_scrollbar(hdc, &r, (w->style & SBS_VERT) != 0, s);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        RECT r;
        POINT pt;
        GetWindowRect(h, &r);
        pt.x = (int)LOWORD(lp) + r.left;
        pt.y = (int)HIWORD(lp) + r.top;
        track_scrollbar(h, GetParent(h), SB_CTL, &r, pt);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN: {
        UINT code;
        switch (wp) {
        case VK_UP: case VK_LEFT: code = SB_LINEUP; break;
        case VK_DOWN: case VK_RIGHT: code = SB_LINEDOWN; break;
        case VK_PRIOR: code = SB_PAGEUP; break;
        case VK_NEXT: code = SB_PAGEDOWN; break;
        case VK_HOME: code = SB_TOP; break;
        case VK_END: code = SB_BOTTOM; break;
        default: return 0;
        }
        send_message(GetParent(h), (w->style & SBS_VERT) ? WM_VSCROLL : WM_HSCROLL, code,
                     MAKELONG(0, h));
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}
