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
static void ghost_repaint(HDC hdc, HWND hwnd, int bar, const RECT *rc, BOOL vert);

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
    if ((w->style & WS_VSCROLL) && s) {
        draw_scrollbar(hdc, &v, TRUE, &s->v);
        ghost_repaint(hdc, HW(w), SB_VERT, &v, TRUE);
    }
    if ((w->style & WS_HSCROLL) && s) {
        draw_scrollbar(hdc, &h, FALSE, &s->h);
        ghost_repaint(hdc, HW(w), SB_HORZ, &h, FALSE);
    }
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
        return (SBINFO *)WND_EXTRA(w);
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

/* ---- tracking: the mouse on a bar of window or control hwnd ----

   Dragging the thumb, as on Windows 3.1 (probe US19 with real mouse input,
   tests/boottest.sh sbdrag; screenshots taken mid-drag):
   - the thumb's place is shown by its outline (a rectangle of the thumb's
     size, one pixel wide, corners included) inverted through the 50% gray
     pattern aligned to the screen (pixels where x + y is odd on VGA, so a
     black line goes white at every other pixel and the Scrollbar gray goes
     dark gray), drawn over whatever is there: over the real thumb when the
     press is made, then following the pointer pixel for pixel, its offset
     from the pointer kept, stopped at the ends of the thumb's travel. The
     real thumb stays where it is unless the program moves it (SetScrollPos
     on SB_THUMBTRACK, as Notepad's edit control does): the outline is then
     drawn on the moved thumb. Released, the outline goes and the thumb is
     wherever the program put it;
   - the press itself sends SB_THUMBTRACK with the bar's position; each
     move that changes the position sends SB_THUMBTRACK with it (none when
     it does not change); the position is the thumb's offset in its travel
     scaled to the range, rounded to the nearest (a bar of travel 150 for
     0-100: 1 pixel gave 1, 3 gave 2, 4 gave 3);
   - with the pointer out of the bar's rectangle widened by 4 pixels left
     and right and 1 up and down (measured on a window's vertical bar and a
     horizontal control, 17 pixels thick: the pointer moved a pixel at a
     time), the outline goes back to the thumb's starting place and
     SB_THUMBTRACK sends the position of that place (which can differ by
     one from the bar's: a control 120 high at 50 sent 51); back in, it
     follows again;
   - the release sends SB_THUMBPOSITION with the last position sent (the
     starting place's if released outside), then SB_ENDSCROLL;
   - meanwhile the task's windows repaint (an area only invalidated on
     SB_THUMBTRACK was drawn mid-drag). */
static int hit_part(int at, int len, int arrow, SBINFO *s)
{
    int t = thumb_pos(len, arrow, s);
    if (at < arrow)
        return SB_LINEUP;
    if (at >= len - arrow)
        return SB_LINEDOWN;
    if (s->max > s->min && len > 3 * arrow - 2 && at >= t && at < t + arrow)
        return SB_THUMBTRACK;
    return at < t ? SB_PAGEUP : SB_PAGEDOWN;
}

extern HBRUSH hbrGray;

/* the thumb being dragged: whose bar, and the outline's offset along the
   bar (as thumb_pos's), -1 when none is shown */
static HWND sbt_hwnd;
static int sbt_bar, sbt_off = -1;

/* the outline at offset off of the bar in rc of hdc (inverting: drawn
   again it goes) */
static void ghost(HDC hdc, const RECT *rc, BOOL vert, int off)
{
    int arrow = vert ? sysmet[SM_CYVSCROLL] : sysmet[SM_CXHSCROLL];
    int x, y, cx, cy;
    HBRUSH old;
    if (vert)
        x = rc->left, y = rc->top + off, cx = rc->right - rc->left, cy = arrow;
    else
        x = rc->left + off, y = rc->top, cx = arrow, cy = rc->bottom - rc->top;
    /* the pattern aligned with the screen whatever the DC's origin (as
       GrayString's, rect.c) */
    {
        DWORD org = GetDCOrg(hdc);
        UnrealizeObject(hbrGray);
        SetBrushOrg(hdc, (LOWORD(org) + HIWORD(org)) & 1, 0);
    }
    old = SelectObject(hdc, hbrGray);
    PatBlt(hdc, x, y, cx, 1, PATINVERT);
    PatBlt(hdc, x, y + cy - 1, cx, 1, PATINVERT);
    PatBlt(hdc, x, y + 1, 1, cy - 2, PATINVERT);
    PatBlt(hdc, x + cx - 1, y + 1, 1, cy - 2, PATINVERT);
    SelectObject(hdc, old);
    SetBrushOrg(hdc, 0, 0);
}

/* after a bar of hwnd was drawn in rc of hdc: the outline again on it if
   it is the one being dragged (SetScrollPos from SB_THUMBTRACK paints the
   bar under the outline; the paint and the outline clipped alike) */
static void ghost_repaint(HDC hdc, HWND hwnd, int bar, const RECT *rc, BOOL vert)
{
    if (sbt_off >= 0 && hwnd == sbt_hwnd && bar == sbt_bar)
        ghost(hdc, rc, vert, sbt_off);
}

/* the outline moved to off (-1: taken away); rc the bar in screen
   coordinates */
static void ghost_move(HWND hwnd, const RECT *rc, BOOL vert, int off)
{
    PWND w = valid_wnd(hwnd);
    HDC hdc;
    RECT r = *rc;
    if (off == sbt_off || !w)
        return;
    if ((hdc = GetWindowDC(hwnd)) == 0)
        return;
    OffsetRect(&r, -w->rcWindow.left, -w->rcWindow.top);
    if (sbt_off >= 0)
        ghost(hdc, &r, vert, sbt_off);
    sbt_off = off;
    if (off >= 0)
        ghost(hdc, &r, vert, off);
    ReleaseDC(hwnd, hdc);
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
    int travel = len - 3 * arrow + 2;
    UINT msg = vert ? WM_VSCROLL : WM_HSCROLL;
    LPARAM lp = bar == SB_CTL ? MAKELONG(0, hwnd) : 0;
    MSG m;
    DWORD next = GetTickCount() + 400;
    int off0 = thumb_pos(len, arrow, s), pos0 = s->pos, last = s->pos, min = s->min, max = s->max;
    RECT near_;
    SetCapture(hwnd);
    if (part != SB_THUMBTRACK)
        send_message(notify, msg, part, lp);
    else {
        /* the pointer's room: the bar widened 4 pixels left and right,
           1 up and down (3.1, VGA; UNVERIFIED on other displays) */
        near_ = *rc;
        near_.left -= 4;
        near_.right += 4;
        near_.top -= 1;
        near_.bottom += 1;
        sbt_hwnd = hwnd;
        sbt_bar = bar;
        sbt_off = -1;
        ghost_move(hwnd, rc, vert, off0);
        send_message(notify, msg, SB_THUMBTRACK, MAKELONG(pos0, HIWORD(lp)));
    }
    for (;;) {
        /* again after each message sent: the state is in the object heap */
        if ((s = info(hwnd, bar, &w)) == 0)
            break;
        if (!PeekMessage(&m, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            if (part != SB_THUMBTRACK && (long)(GetTickCount() - next) >= 0) {
                send_message(notify, msg, part, lp);
                next = GetTickCount() + 50;
            }
            /* the task's windows repaint meanwhile (3.1: US19's window,
               which only invalidates on SB_THUMBTRACK, showed each tracked
               position mid-drag; Notepad's text followed the thumb) */
            if (PeekMessage(&m, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) {
                DispatchMessage(&m);
                continue;
            }
            WaitMessage();
            continue;
        }
        if (m.message == WM_MOUSEMOVE && part == SB_THUMBTRACK) {
            int off = off0, pos = pos0;
            if (travel > 0) {
                if (PtInRect(&near_, m.pt)) {
                    off = off0 + (vert ? m.pt.y - pt.y : m.pt.x - pt.x);
                    if (off < arrow - 1)
                        off = arrow - 1;
                    if (off > arrow - 1 + travel)
                        off = arrow - 1 + travel;
                }
                /* rounded to the nearest; out of the room too the position
                   is the starting place's, which need not be the one the
                   press sent (3.1: a control of travel 71 at 50 sent 51) */
                pos = min + (int)(((long)(off - (arrow - 1)) * (max - min) * 2 + travel) /
                                  (2L * travel));
            }
            ghost_move(hwnd, rc, vert, off);
            if (pos != last) {
                last = pos;
                send_message(notify, msg, SB_THUMBTRACK, MAKELONG(pos, HIWORD(lp)));
            }
        }
        if (m.message == WM_LBUTTONUP) {
            if (part == SB_THUMBTRACK) {
                ghost_move(hwnd, rc, vert, -1);
                sbt_hwnd = 0;
                send_message(notify, msg, SB_THUMBPOSITION, MAKELONG(last, HIWORD(lp)));
            }
            break;
        }
    }
    if (part == SB_THUMBTRACK) {
        /* (the window went during the drag) */
        if (sbt_off >= 0 && IsWindow(hwnd))
            ghost_move(hwnd, rc, vert, -1);
        sbt_hwnd = 0;
        sbt_off = -1;
    }
    ReleaseCapture();
    send_message(notify, msg, SB_ENDSCROLL, lp);
}

/* ---- the scroll-bar menu: a w16 extension ----
   Windows 2000 and later give every scroll bar a shortcut menu on a right
   click (Raymond Chen, The Old New Thing, 2026-09-22: "Windows 2000 added
   a right-click menu to the scroll bar"; Scroll Here, Top, Bottom, Page
   Up, Page Down, Scroll Up, Scroll Down; the separators as in the menu
   Nick Gammon's forum transcribes, gammon.com.au/forum thread 8417; the
   horizontal wording, Left Edge, Right Edge, Page Left, Page Right,
   Scroll Left, Scroll Right, as a Trapeze Group application's help page
   lists both bars' right-click menus); the order, wording and separators confirmed by a
   screenshot of the vertical menu from PsyMP3 (the user's own
   ISC-licensed program; supplied 2026-11-13). None of
   those sources shows a mnemonic, so the items have none. Windows 3.1 has no such menu: [w16] ScrollBarMenu=0 in
   SYSTEM.INI (read at start-up; default 1) turns it off, and then nothing
   here runs and USER behaves as 3.1's (ARCHITECTURE.md 10.3).

   Shown on the right button's release (later Windows shows it from
   WM_CONTEXTMENU, which DefWindowProc makes from WM_RBUTTONUP and
   WM_NCRBUTTONUP: Win32 SDK, WM_CONTEXTMENU), at the pointer, by USER's
   own menu loop (menu_track_quiet: the window gets none of the menu's
   messages, only the WM_VSCROLL/WM_HSCROLL it already understands). Not
   for a disabled window or a bar with nothing to scroll (shown disabled,
   or an empty range): what later Windows does there is not documented,
   so the conservative choice. The commands send what a mouse or the
   scroll-bar control's keys send for the same thing, then SB_ENDSCROLL;
   Scroll Here sends SB_THUMBTRACK and SB_THUMBPOSITION with the position
   whose thumb is centred on the click, as a drag of the thumb there
   would (the exact sequence later Windows sends is not documented:
   UNVERIFIED; both, so that programs that act on either one move) */
WORD sb_menu_on = 1;            /* [w16] ScrollBarMenu */

void sbmenu_init(void)
{
    sb_menu_on = GetPrivateProfileInt("w16", "ScrollBarMenu", 1, "SYSTEM.INI") != 0;
}

UINT menu_track_quiet(HMENU hm, int x, int y, HWND h);

#define SBM_HERE 0x100          /* the items' ids: 100h + SB_ code */

/* one command's message; with [w16] userlog=1 USER.LOG gets the code, the
   position sent and the bar's position after it */
static void sbm_send(HWND notify, UINT msg, int code, int pos, WORD ctl, HWND hwnd, int bar)
{
    send_message(notify, msg, code, MAKELONG(pos, ctl));
    ulogx("sbmenu: code ", code);
    ulogx("  pos sent ", pos);
    ulogx("  bar's pos after ", IsWindow(hwnd) ? GetScrollPos(hwnd, bar) : 0xFFFF);
}

/* hwnd's bar (SB_VERT, SB_HORZ, SB_CTL) in rc (screen) right-clicked at pt
   (screen): the menu, and the command chosen sent to notify (the window
   for its own bars, the parent for a control) */
void scrollbar_menu(HWND hwnd, HWND notify, int bar, RECT *rc, POINT pt)
{
    static const char *const vtext[] = { "Scroll Here", "Top", "Bottom", "Page Up", "Page Down",
                                         "Scroll Up", "Scroll Down" };
    static const char *const htext[] = { "Scroll Here", "Left Edge", "Right Edge", "Page Left",
                                         "Page Right", "Scroll Left", "Scroll Right" };
    static const BYTE codes[] = { SB_THUMBPOSITION, SB_TOP, SB_BOTTOM, SB_PAGEUP, SB_PAGEDOWN,
                                  SB_LINEUP, SB_LINEDOWN };
    PWND w;
    SBINFO *s;
    BOOL vert;
    int min, max, len, arrow, at, travel, here, i;
    long v;
    HMENU hm;
    UINT cmd, msg;
    WORD ctl;
    int bar2;
    if (!sb_menu_on || !IsWindowEnabled(hwnd))
        return;
    if ((s = info(hwnd, bar, &w)) == 0 || s->disabled || s->max <= s->min)
        return;
    min = s->min, max = s->max;
    vert = bar == SB_VERT || (bar == SB_CTL && (w->style & SBS_VERT));
    if (bar == SB_CTL && (w->style & SBS_SIZEBOX))
        return;
    /* the position whose thumb is centred on the click */
    len = vert ? rc->bottom - rc->top : rc->right - rc->left;
    arrow = vert ? sysmet[SM_CYVSCROLL] : sysmet[SM_CXHSCROLL];
    if (len < 2 * arrow)
        arrow = len / 2;
    at = (vert ? pt.y - rc->top : pt.x - rc->left) - (arrow - 1) - arrow / 2;
    travel = len - 3 * arrow + 2;
    v = travel > 0 && at > 0 ? min + ((long)at * (max - min) * 2 + travel) / (2L * travel) : min;
    here = v > max ? max : (int)v;
    if ((hm = CreatePopupMenu()) == 0)
        return;
    for (i = 0; i < 7; i++) {
        if (i == 1 || i == 3 || i == 5)
            AppendMenu(hm, MF_SEPARATOR, 0, NULL);
        AppendMenu(hm, MF_STRING, SBM_HERE + codes[i], vert ? vtext[i] : htext[i]);
    }
    cmd = menu_track_quiet(hm, pt.x, pt.y, hwnd);
    DestroyMenu(hm);
    if (!cmd || !IsWindow(notify) || !IsWindow(hwnd))
        return;
    msg = vert ? WM_VSCROLL : WM_HSCROLL;
    ctl = bar == SB_CTL ? (WORD)hwnd : 0;
    bar2 = bar == SB_CTL ? SB_CTL : vert ? SB_VERT : SB_HORZ;
    cmd -= SBM_HERE;
    if (cmd == SB_THUMBPOSITION) {
        sbm_send(notify, msg, SB_THUMBTRACK, here, ctl, hwnd, bar2);
        sbm_send(notify, msg, SB_THUMBPOSITION, here, ctl, hwnd, bar2);
    } else if (cmd == SB_TOP || cmd == SB_BOTTOM) {
        /* On 3.1 SB_TOP and SB_BOTTOM come only from a scroll-bar
           control's Home and End keys, never from a window's own bars, so
           3.x programs may ignore them (Write: Top and Bottom did nothing,
           its bar did not move). They are sent first, as later Windows
           does; if the bar has not moved and is not at that end yet, the
           thumb is "dragged" there as Scroll Here does, which every
           program handles */
        int before = GetScrollPos(hwnd, bar2), after, lo, hi, end;
        sbm_send(notify, msg, cmd, 0, ctl, hwnd, bar2);
        if (IsWindow(notify) && IsWindow(hwnd)) {
            after = GetScrollPos(hwnd, bar2);
            GetScrollRange(hwnd, bar2, &lo, &hi);
            end = cmd == SB_TOP ? lo : hi;
            if (after == before && after != end && hi > lo) {
                ulog("sbmenu: not moved; the thumb dragged there\r\n");
                sbm_send(notify, msg, SB_THUMBTRACK, end, ctl, hwnd, bar2);
                sbm_send(notify, msg, SB_THUMBPOSITION, end, ctl, hwnd, bar2);
            }
        }
    } else
        sbm_send(notify, msg, cmd, 0, ctl, hwnd, bar2);
    if (IsWindow(notify))
        sbm_send(notify, msg, SB_ENDSCROLL, 0, ctl, hwnd, bar2);
}

/* ---- the SCROLLBAR class: its SBINFO in the window extra bytes
   (Windows 3.1's class has 10: US2) ---- */
LRESULT WINAPI __loadds ScrollBarWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w = valid_wnd(h);
    SBINFO *s;
    if (!w)
        return 0;
    s = (SBINFO *)WND_EXTRA(w);
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
        else {
            draw_scrollbar(hdc, &r, (w->style & SBS_VERT) != 0, s);
            ghost_repaint(hdc, h, SB_CTL, &r, (w->style & SBS_VERT) != 0);
        }
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
    case WM_RBUTTONUP:
        if (sb_menu_on) {               /* (w16's extension, above) */
            RECT r;
            POINT pt;
            GetClientRect(h, &r);       /* where the bar is drawn */
            ClientToScreen(h, (POINT *)&r.left);
            ClientToScreen(h, (POINT *)&r.right);
            pt.x = (int)LOWORD(lp);
            pt.y = (int)HIWORD(lp);
            ClientToScreen(h, &pt);
            scrollbar_menu(h, GetParent(h), SB_CTL, &r, pt);
            return 0;
        }
        break;
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
