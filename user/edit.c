/*
 * edit.c - the EDIT class: single-line and (simple) multi-line edit
 * controls.
 *
 * The state (ED) and the text are blocks of USER's object heap (uheap.c),
 * not of DGROUP (3.1's edit control costs DGROUP only its WND: US15), the
 * window's first extra word the state's handle, reached through E() on
 * every use (nothing in the heap is held across a call that allocates in
 * it or sends a message). UNVERIFIED and not yet done
 * the way Windows 3.1 does it: 3.1 keeps the text in the local heap of the
 * hInstance given to CreateWindow (EM_GETHANDLE gives that local handle;
 * Notepad relies on it); undo; word wrap of multi-line controls.
 * Drawing (measured on US4's dialogs; UNVERIFIED in detail): the text
 * starts 1 pixel inside the border, the selection in the Highlight colours.
 *
 * Window extra bytes (3.1's class has 6, US2): word 0 the state block.
 */
#include "user.h"

typedef struct ED {
    WORD   text;                /* local block, NUL-terminated */
    WORD   size;                /* bytes allocated */
    WORD   len;
    WORD   sel0, sel1;          /* selection; sel1 is the caret */
    WORD   first;               /* first visible character (single line) */
    int    topline;             /* first visible line (multi-line) */
    int    xoff;                /* pixels scrolled off at the left (multi-line) */
    WORD   multi;               /* ES_MULTILINE */
    WORD   ntabs;               /* EM_SETTABSTOPS: 0 the default */
    WORD   tabs;                /* local block of ntabs stops, in dialog units */
    WORD   limit;
    HFONT  font;
    WORD   focus;
    WORD   modified;
    char   password;
    HWND   parent;
} ED;
typedef WORD PED;               /* the state block's handle (object heap) */
#define E(e)    ((ED __far *)UH_PTR(&uhObj, (e)))

static PED ed_of(HWND h)
{
    return (PED)GetWindowWord(h, 0);
}

static DWORD estyle(HWND h)
{
    return GetWindowLong(h, GWL_STYLE);
}

static char __far *text(PED e)
{
    return (char __far *)UH_PTR(&uhObj, E(e)->text);
}

static void notify(HWND h, PED e, WORD code)
{
    send_message(E(e)->parent, WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

static BOOL grow(PED e, WORD need)
{
    if (need + 1 <= E(e)->size)
        return TRUE;
    {
        WORD n = need + 32;
        WORD b = uh_realloc(&uhObj, E(e)->text, n);
        if (!b)
            return FALSE;
        E(e)->text = b;
        E(e)->size = n;
    }
    return TRUE;
}

static void set_text(PED e, LPCSTR s)
{
    WORD n = s ? lstr_len(s) : 0;
    if (!grow(e, n))
        return;
    if (n)
        fmemcpy((char __far *)text(e), s, n);
    text(e)[n] = 0;
    E(e)->len = n;
    E(e)->sel0 = E(e)->sel1 = 0;
    E(e)->first = 0;
    E(e)->topline = 0;
}

/* lines of a multi-line control: start offsets */
static WORD line_start(PED e, int line)
{
    WORD i = 0;
    while (line > 0 && i < E(e)->len) {
        if (text(e)[i] == '\r' && text(e)[i + 1] == '\n') {
            i += 2;
            line--;
        } else if (text(e)[i] == '\n') {
            i++;
            line--;
        } else
            i++;
    }
    return i;
}

static WORD line_len(PED e, WORD start)
{
    WORD i = start;
    while (i < E(e)->len && text(e)[i] != '\r' && text(e)[i] != '\n')
        i++;
    return i - start;
}

static int line_of(PED e, WORD pos)
{
    WORD i;
    int n = 0;
    for (i = 0; i < pos && i < E(e)->len; i++)
        if (text(e)[i] == '\n')
            n++;
    return n;
}

#define EM_GETTHUMB (WM_USER + 14)

static int line_count(PED e)
{
    return line_of(e, E(e)->len) + 1;
}

static int line_height(HWND h, PED e)
{
    HDC hdc = GetDC(h);
    TEXTMETRIC tm;
    HFONT old = SelectObject(hdc, E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT));
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    /* UNVERIFIED: Windows 3.1 highlights a selection in MS Sans Serif 8
       (13 high) over 14 rows (US4's Run dialog); the rule is not known */
    return tm.tmHeight;
}

/* where the text starts inside a single-line edit: half the room the
   font leaves, both across and down (US4: 3 pixels in a 24-pixel high
   control with the system font; UNVERIFIED across as a rule) */
static int inset(HWND h, PED e)
{
    RECT r;
    int d;
    if (GetWindowLong(h, GWL_STYLE) & ES_MULTILINE)
        return 0;               /* Notepad's text at its edit's left edge (3.1) */
    GetClientRect(h, &r);
    d = (r.bottom - r.top - line_height(h, e)) / 2;
    return d > 0 ? d : 0;
}

/* A multi-line control expands tabs (3.1 SDK, EM_SETTABSTOPS: stops
   in dialog units, every 32 by default, i.e. 8 average characters of the
   system font): the stops in pixels for GetTabbedTextExtent/
   TabbedTextOut, into buf; returns their number */
static int tab_stops(PED e, int *buf, int max)
{
    int bx = LOWORD(GetDialogBaseUnits()), i, n = 0;
    if (!E(e)->ntabs || !E(e)->tabs) {
        buf[0] = 32 * bx / 4;
        return 1;
    }
    for (i = 0; i < (int)E(e)->ntabs && i < max; i++, n++)
        buf[i] = ((int __far *)UH_PTR(&uhObj, E(e)->tabs))[i] * bx / 4;
    return n;
}

static int line_start_of(PED e, WORD pos)
{
    return line_start(e, line_of(e, pos));
}

/* the width from the start of pos's line to pos, tabs expanded */
static int tabbed_x(HDC hdc, PED e, WORD pos)
{
    int stops[16], n;
    WORD ls = line_start_of(e, pos);
    int *sp = stops;
    if (pos <= ls)
        return 0;
    if (E(e)->ntabs > 16) {
        sp = (int __near *)LocalAlloc(LPTR, E(e)->ntabs * sizeof(int));
        if (!sp)
            sp = stops;
    }
    n = tab_stops(e, sp, sp == stops ? 16 : E(e)->ntabs);
    n = LOWORD(GetTabbedTextExtent(hdc, text(e) + ls, pos - ls, n, sp));
    if (sp != stops)
        LocalFree((HLOCAL)sp);
    return n;
}

/* text at x; tabs expanded from origin (the line's left) in a
   multi-line control */
static void out_text(HDC hdc, HWND h, PED e, int x, int y, WORD start, WORD n, int origin)
{
    if ((estyle(h) & ES_MULTILINE) && !E(e)->password) {
        int stops[16], k;
        int *sp = stops;
        if (E(e)->ntabs > 16) {
            sp = (int __near *)LocalAlloc(LPTR, E(e)->ntabs * sizeof(int));
            if (!sp)
                sp = stops;
        }
        k = tab_stops(e, sp, sp == stops ? 16 : E(e)->ntabs);
        TabbedTextOut(hdc, x, y, text(e) + start, n, k, sp, origin);
        if (sp != stops)
            LocalFree((HLOCAL)sp);
    } else
        TextOut(hdc, x, y, text(e) + start, n);
}

static int text_x(HDC hdc, PED e, WORD start, WORD n)
{
    if (!n)
        return 0;
    if (E(e)->multi && !E(e)->password)
        return tabbed_x(hdc, e, start + n) - tabbed_x(hdc, e, start);
    if (E(e)->password) {
        char buf[64];
        WORD i;
        for (i = 0; i < n && i < sizeof buf; i++)
            buf[i] = E(e)->password;
        return LOWORD(GetTextExtent(hdc, buf, n < sizeof buf ? n : sizeof buf));
    }
    return LOWORD(GetTextExtent(hdc, text(e) + start, n));
}

static void draw_run(HDC hdc, HWND h, PED e, int x, int y, WORD start, WORD n, BOOL sel, int origin)
{
    if (!n)
        return;
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, syscolor[sel ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
    SetTextColor(hdc, syscolor[sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
    if (E(e)->password) {
        char buf[64];
        WORD i;
        for (i = 0; i < n && i < sizeof buf; i++)
            buf[i] = E(e)->password;
        TextOut(hdc, x, y, buf, n < sizeof buf ? n : sizeof buf);
    } else
        out_text(hdc, h, e, x, y, start, n, origin);
}

static void paint(HWND h, PED e, HDC hdc)
{
    RECT r;
    HFONT old = SelectObject(hdc, E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT));
    HBRUSH b = (HBRUSH)(WORD)send_message(E(e)->parent, WM_CTLCOLOR, (WPARAM)hdc,
                                          MAKELONG(h, CTLCOLOR_EDIT));
    WORD s0 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel0 : E(e)->sel1, s1 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel1 : E(e)->sel0;
    BOOL showsel = E(e)->focus || (estyle(h) & ES_NOHIDESEL);
    int lh = line_height(h, e);
    GetClientRect(h, &r);
    FillRect(hdc, &r, b ? b : sysbrush[COLOR_WINDOW]);
    /* a disabled edit control's text is drawn as usual (US4) */
    SetTextColor(hdc, syscolor[COLOR_WINDOWTEXT]);
    {
        int line, nlines = (estyle(h) & ES_MULTILINE) ? line_count(e) : 1;
        for (line = (estyle(h) & ES_MULTILINE) ? E(e)->topline : 0; line < nlines; line++) {
            WORD st = (estyle(h) & ES_MULTILINE) ? line_start(e, line) : E(e)->first;
            WORD n = (estyle(h) & ES_MULTILINE) ? line_len(e, st) : E(e)->len - E(e)->first;
            int y = (estyle(h) & ES_MULTILINE) ? (line - E(e)->topline) * lh : inset(h, e);
            int x = inset(h, e) - ((estyle(h) & ES_MULTILINE) ? E(e)->xoff : 0);
            WORD a = st, z = st + n;
            if (y > r.bottom)
                break;
            /* before, inside, after the selection */
            if (showsel && s1 > s0 && s0 < z && s1 > a) {
                WORD p = s0 > a ? s0 : a, q = s1 < z ? s1 : z;
                int xp = x + text_x(hdc, e, a, p - a), xq = xp + text_x(hdc, e, p, q - p);
                draw_run(hdc, h, e, x, y, a, p - a, FALSE, x);
                draw_run(hdc, h, e, xq, y, q, z - q, FALSE, x);
                /* the selection: its background a pixel wider than the
                   text (US4: "notepad" selected in MS Sans Serif, 47
                   pixels for text 46) */
                {
                    RECT b;
                    SetRect(&b, xp, y, xq + 1, y + lh);
                    FillRect(hdc, &b, sysbrush[COLOR_HIGHLIGHT]);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, syscolor[COLOR_HIGHLIGHTTEXT]);
                    if (E(e)->password) {
                        char buf[64];
                        WORD i;
                        for (i = 0; i < q - p && i < sizeof buf; i++)
                            buf[i] = E(e)->password;
                        TextOut(hdc, xp, y, buf, i);
                    } else
                        out_text(hdc, h, e, xp, y, p, q - p, x);
                }
            } else {
                if (!IsWindowEnabled(h)) {
                    SetBkMode(hdc, TRANSPARENT);
                    if (E(e)->password)
                        draw_run(hdc, h, e, x, y, a, z - a, FALSE, x);
                    else
                        out_text(hdc, h, e, x, y, a, z - a, x);
                } else
                    draw_run(hdc, h, e, x, y, a, z - a, FALSE, x);
            }
        }
    }
    SelectObject(hdc, old);
}

static void place_caret(HWND h, PED e)
{
    HDC hdc;
    HFONT old;
    int x, y = inset(h, e), lh;
    WORD st;
    if (!E(e)->focus)
        return;
    hdc = GetDC(h);
    old = SelectObject(hdc, E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT));
    lh = line_height(h, e);
    if (estyle(h) & ES_MULTILINE) {
        int line = line_of(e, E(e)->sel1);
        st = line_start(e, line);
        y = (line - E(e)->topline) * lh;
    } else
        st = E(e)->first;
    x = inset(h, e) + (E(e)->sel1 > st ? text_x(hdc, e, st, E(e)->sel1 - st) : 0);
    if (estyle(h) & ES_MULTILINE)
        x -= E(e)->xoff;
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    SetCaretPos(x, y);
}

/* keep the caret in view (single line, ES_AUTOHSCROLL; multi-line lines) */
static void scroll_to_caret(HWND h, PED e)
{
    RECT r;
    GetClientRect(h, &r);
    if (estyle(h) & ES_MULTILINE) {
        int line = line_of(e, E(e)->sel1), lh = line_height(h, e), rows = r.bottom / (lh ? lh : 1);
        if (line < E(e)->topline)
            E(e)->topline = line;
        else if (rows > 0 && line >= E(e)->topline + rows)
            E(e)->topline = line - rows + 1;
        return;
    }
    if (E(e)->sel1 < E(e)->first)
        E(e)->first = E(e)->sel1;
    else {
        HDC hdc = GetDC(h);
        HFONT old = SelectObject(hdc, E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT));
        while (E(e)->first < E(e)->sel1 && text_x(hdc, e, E(e)->first, E(e)->sel1 - E(e)->first) > r.right - 2)
            E(e)->first++;
        SelectObject(hdc, old);
        ReleaseDC(h, hdc);
    }
}

/* ---- scrolling a multi-line control (probe US6, Windows 3.1):
   its scroll bars always run 0-100; the vertical one's position is the
   first visible line * 100 / (lines - 1), and the first visible line
   goes down to the last line; WM_VSCROLL's SB_THUMBTRACK and
   SB_THUMBPOSITION take a position on that scale (40 of 51 lines: line
   20); a page is the lines shown less one; EM_LINESCROLL's low word is
   lines, its high word characters. The control notifies EN_VSCROLL
   (and EN_HSCROLL after EM_LINESCROLL) once the position has changed;
   not while the thumb is tracked. UNVERIFIED: the horizontal scale (here
   the widest line's width is 100) and the width of a character step
   (here the font's average width). ---- */
static int char_w(HWND h, PED e)
{
    HDC hdc = GetDC(h);
    TEXTMETRIC tm;
    HFONT old = SelectObject(hdc, E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT));
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    return tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 1;
}

static int widest(HWND h, PED e)
{
    HDC hdc = GetDC(h);
    HFONT old = SelectObject(hdc, E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT));
    int line, n = line_count(e), w = 0;
    for (line = 0; line < n; line++) {
        WORD st = line_start(e, line);
        int x = text_x(hdc, e, st, line_len(e, st));
        if (x > w)
            w = x;
    }
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    return w;
}

static void set_scrollbars(HWND h, PED e)
{
    DWORD s = estyle(h);
    int n;
    if (!(s & ES_MULTILINE))
        return;
    n = line_count(e);
    if (s & WS_VSCROLL) {
        SetScrollRange(h, SB_VERT, 0, 100, FALSE);
        SetScrollPos(h, SB_VERT, n > 1 ? (int)((long)E(e)->topline * 100 / (n - 1)) : 0, TRUE);
    }
    if (s & WS_HSCROLL) {
        int w = widest(h, e);
        SetScrollRange(h, SB_HORZ, 0, 100, FALSE);
        SetScrollPos(h, SB_HORZ, w > 0 ? (int)((long)E(e)->xoff * 100 / w) : 0, TRUE);
    }
}

/* scroll to first line top and left offset xoff; notify as asked (when
   something moved, or always with force) */
static void scroll_to(HWND h, PED e, int top, int xoff, BOOL nv, BOOL nh, BOOL force)
{
    int n = line_count(e);
    if (top > n - 1)
        top = n - 1;
    if (top < 0)
        top = 0;
    if (xoff < 0)
        xoff = 0;
    if (top != E(e)->topline || xoff != E(e)->xoff) {
        E(e)->topline = top;
        E(e)->xoff = xoff;
        HideCaret(h);
        InvalidateRect(h, NULL, TRUE);
        place_caret(h, e);
        ShowCaret(h);
        set_scrollbars(h, e);
    } else if (!force)
        nv = nh = FALSE;
    if (nv)
        notify(h, e, EN_VSCROLL);
    if (nh)
        notify(h, e, EN_HSCROLL);
}

static int visible_lines(HWND h, PED e)
{
    RECT r;
    int lh = line_height(h, e);
    GetClientRect(h, &r);
    /* UNVERIFIED: 3 rows kept out (US6: an 82-row client area of
       16-pixel lines pages by 3, so 4 lines count as shown) */
    return lh > 0 ? (r.bottom - 3) / lh : 1;
}

static void refresh(HWND h, PED e)
{
    /* invalidated, not painted now: a dialog's focus goes to its edit
       control (selecting its text) while the dialog itself is still to be
       painted, and the dialog, without WS_CLIPCHILDREN, would paint over
       it (US4's Run dialog) */
    scroll_to_caret(h, e);
    HideCaret(h);
    InvalidateRect(h, NULL, TRUE);
    place_caret(h, e);
    ShowCaret(h);
    set_scrollbars(h, e);
}

static void replace_sel(HWND h, PED e, LPCSTR s)
{
    WORD s0 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel0 : E(e)->sel1, s1 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel1 : E(e)->sel0;
    WORD n = s ? lstr_len(s) : 0;
    if (E(e)->limit && E(e)->len - (s1 - s0) + n > E(e)->limit) {
        notify(h, e, EN_MAXTEXT);
        return;
    }
    if (!grow(e, E(e)->len - (s1 - s0) + n))
        return;
    fmemcpy((char __far *)(text(e) + s0 + n), (char __far *)(text(e) + s1), E(e)->len - s1 + 1);
    if (n)
        fmemcpy((char __far *)(text(e) + s0), s, n);
    E(e)->len = E(e)->len - (s1 - s0) + n;
    E(e)->sel0 = E(e)->sel1 = s0 + n;
    E(e)->modified = 1;
    refresh(h, e);
    notify(h, e, EN_CHANGE);
}

static WORD pos_from_x(HWND h, PED e, int x, int y)
{
    HDC hdc = GetDC(h);
    HFONT old = SelectObject(hdc, E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT));
    WORD st, n, i;
    if (estyle(h) & ES_MULTILINE) {
        int line = E(e)->topline + y / line_height(h, e);
        if (line >= line_count(e))
            line = line_count(e) - 1;
        st = line_start(e, line);
        n = line_len(e, st);
    } else {
        st = E(e)->first;
        n = E(e)->len - st;
    }
    for (i = 0; i < n; i++)
        if (inset(h, e) - ((estyle(h) & ES_MULTILINE) ? E(e)->xoff : 0) +
            text_x(hdc, e, st, i + 1) - (text_x(hdc, e, st + i, 1) / 2) > x)
            break;
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    return st + i;
}

LRESULT WINAPI __loadds EditWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PED e;
    if (!valid_wnd(h))
        return 0;
    e = ed_of(h);
    if (msg == WM_NCCREATE) {
        WORD t;
        e = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, sizeof(ED));
        if (!e)
            return 0;
        t = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, 32);
        if (!t) {
            uh_free(&uhObj, e);
            return 0;
        }
        E(e)->text = t;
        E(e)->size = 32;
        E(e)->multi = (estyle(h) & ES_MULTILINE) != 0;
        E(e)->parent = GetParent(h);
        if (estyle(h) & ES_PASSWORD)
            E(e)->password = '*';
        SetWindowWord(h, 0, (WORD)e);
        {
            CREATESTRUCT __far *cs = (CREATESTRUCT __far *)lp;
            if (cs->lpszName && !ISATOM(cs->lpszName))
                set_text(e, cs->lpszName);
        }
        return DefWindowProc(h, msg, wp, lp);
    }
    if (!e)
        return DefWindowProc(h, msg, wp, lp);
    switch (msg) {
    case WM_NCDESTROY:
        uh_free(&uhObj, E(e)->tabs);
        uh_free(&uhObj, E(e)->text);
        uh_free(&uhObj, e);
        SetWindowWord(h, 0, 0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint(h, e, ps.hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFONT:
        E(e)->font = (HFONT)wp;
        if (LOWORD(lp))
            InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return (LONG)(WORD)E(e)->font;
    case WM_SETTEXT:
        set_text(e, (LPCSTR)lp);
        E(e)->modified = 0;
        refresh(h, e);
        notify(h, e, EN_CHANGE);
        return 1;
    case WM_GETTEXT:
        if ((int)wp <= 0)
            return 0;
        lstr_cpyn((LPSTR)lp, (LPCSTR)text(e), wp);
        return lstr_len((LPCSTR)lp);
    case WM_GETTEXTLENGTH:
        return E(e)->len;
    case WM_SETFOCUS:
        E(e)->focus = 1;
        /* two pixels wide in Notepad's multi-line control, one in the
           Run dialog's single-line one (3.1; UNVERIFIED which of style
           and font decides) */
        CreateCaret(h, 0, E(e)->multi ? 2 : 1, line_height(h, e));
        place_caret(h, e);
        ShowCaret(h);
        InvalidateRect(h, NULL, TRUE);
        notify(h, e, EN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        E(e)->focus = 0;
        DestroyCaret();
        InvalidateRect(h, NULL, TRUE);
        notify(h, e, EN_KILLFOCUS);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS |
               ((estyle(h) & ES_MULTILINE) && (estyle(h) & ES_WANTRETURN) ? DLGC_WANTALLKEYS : 0);
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(h);
        {
            WORD pos = pos_from_x(h, e, (int)LOWORD(lp), (int)HIWORD(lp));
            E(e)->sel0 = E(e)->sel1 = pos;
        }
        SetCapture(h);
        refresh(h, e);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == h) {
            WORD p = pos_from_x(h, e, (int)LOWORD(lp), (int)HIWORD(lp));
            if (p != E(e)->sel1) {
                E(e)->sel1 = p;
                refresh(h, e);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h)
            ReleaseCapture();
        return 0;
    case WM_CHAR:
        if (estyle(h) & ES_READONLY)
            return 0;
        if (wp == 8) {
            if (E(e)->sel0 == E(e)->sel1 && E(e)->sel1 > 0)
                E(e)->sel0 = E(e)->sel1 - 1;
            replace_sel(h, e, "");
        } else if (wp == '\r') {
            if (estyle(h) & ES_MULTILINE)
                replace_sel(h, e, "\r\n");
        } else if (wp >= ' ' || wp == '\t') {
            char s[2];
            s[0] = (char)wp;
            s[1] = 0;
            if (estyle(h) & ES_UPPERCASE)
                AnsiUpper(s);
            else if (estyle(h) & ES_LOWERCASE)
                AnsiLower(s);
            replace_sel(h, e, s);
        }
        return 0;
    case WM_KEYDOWN: {
        BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        WORD p = E(e)->sel1;
        switch (wp) {
        case VK_LEFT: if (p) p--; break;
        case VK_RIGHT: if (p < E(e)->len) p++; break;
        case VK_HOME:
            p = (estyle(h) & ES_MULTILINE) ? line_start(e, line_of(e, p)) : 0;
            break;
        case VK_END:
            p = (estyle(h) & ES_MULTILINE) ? line_start(e, line_of(e, p)) +
                line_len(e, line_start(e, line_of(e, p))) : E(e)->len;
            break;
        case VK_DELETE:
            if (estyle(h) & ES_READONLY)
                return 0;
            if (E(e)->sel0 == E(e)->sel1 && E(e)->sel1 < E(e)->len)
                E(e)->sel1++;
            replace_sel(h, e, "");
            return 0;
        case VK_UP:
        case VK_DOWN:
            if (estyle(h) & ES_MULTILINE) {
                int line = line_of(e, p) + (wp == VK_UP ? -1 : 1);
                WORD col = p - line_start(e, line_of(e, p));
                if (line >= 0 && line < line_count(e)) {
                    WORD st = line_start(e, line), n = line_len(e, st);
                    p = st + (col < n ? col : n);
                }
                break;
            }
            return 0;
        default:
            return 0;
        }
        E(e)->sel1 = p;
        if (!shift)
            E(e)->sel0 = p;
        refresh(h, e);
        return 0;
    }
    /* ---- EM_ messages ---- */
    case EM_GETSEL:
        return MAKELONG(E(e)->sel0 < E(e)->sel1 ? E(e)->sel0 : E(e)->sel1, E(e)->sel0 < E(e)->sel1 ? E(e)->sel1 : E(e)->sel0);
    case EM_SETSEL: {
        int a = (int)LOWORD(lp), b = (int)HIWORD(lp);
        if (a == -1) {
            E(e)->sel0 = E(e)->sel1;
        } else {
            E(e)->sel0 = (WORD)(a < 0 ? 0 : a > (int)E(e)->len ? E(e)->len : a);
            E(e)->sel1 = (WORD)(b < 0 || b > (int)E(e)->len ? E(e)->len : b);
        }
        refresh(h, e);
        return 1;
    }
    case EM_REPLACESEL:
        replace_sel(h, e, (LPCSTR)lp);
        return 0;
    case EM_LIMITTEXT:
        E(e)->limit = wp;
        return 0;
    case EM_GETLINECOUNT:
        return line_count(e);
    case EM_LINEINDEX: {
        int line = (int)wp == -1 ? line_of(e, E(e)->sel1) : (int)wp;
        if (line >= line_count(e))
            return -1;
        return line_start(e, line);
    }
    case EM_LINELENGTH: {
        WORD at = (int)wp == -1 ? E(e)->sel1 : wp;
        return line_len(e, line_start(e, line_of(e, at)));
    }
    case EM_LINEFROMCHAR:
        return line_of(e, (int)wp == -1 ? E(e)->sel1 : wp);
    case EM_GETLINE: {
        WORD st, n, max = *(WORD __far *)lp;
        if ((int)wp >= line_count(e))
            return 0;
        st = line_start(e, wp);
        n = line_len(e, st);
        if (n > max)
            n = max;
        fmemcpy((void __far *)lp, (char __far *)(text(e) + st), n);
        return n;
    }
    case EM_GETMODIFY:
        return E(e)->modified;
    case EM_SETMODIFY:
        E(e)->modified = wp != 0;
        return 0;
    case EM_SETREADONLY: {
        PWND w = valid_wnd(h);
        if (wp)
            w->style |= ES_READONLY;
        else
            w->style &= ~ES_READONLY;
        return 1;
    }
    case EM_SETPASSWORDCHAR:
        E(e)->password = (char)wp;
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case EM_GETPASSWORDCHAR:
        return (BYTE)E(e)->password;
    case EM_CANUNDO:
    case EM_UNDO:
    case EM_EMPTYUNDOBUFFER:
        return 0;
    case EM_GETFIRSTVISIBLELINE:
        return (estyle(h) & ES_MULTILINE) ? E(e)->topline : E(e)->first;
    case EM_LINESCROLL:
        if (estyle(h) & ES_MULTILINE)
            scroll_to(h, e, E(e)->topline + (int)LOWORD(lp), E(e)->xoff + (int)HIWORD(lp) * char_w(h, e),
                      TRUE, TRUE, FALSE);
        return 1;
    case WM_VSCROLL:
    case WM_HSCROLL: {
        BOOL v = msg == WM_VSCROLL;
        int n = line_count(e), page = visible_lines(h, e) - 1, cw = char_w(h, e), top = E(e)->topline,
            x = E(e)->xoff, pos = (int)LOWORD(lp);
        if (!(estyle(h) & ES_MULTILINE))
            return 0;
        if (page < 1)
            page = 1;
        switch (wp) {
        case SB_LINEUP: if (v) top--; else x -= cw; break;
        case SB_LINEDOWN: if (v) top++; else x += cw; break;
        case SB_PAGEUP: if (v) top -= page; else x -= 8 * cw; break;
        case SB_PAGEDOWN: if (v) top += page; else x += 8 * cw; break;
        case SB_TOP: if (v) top = 0; else x = 0; break;
        case SB_BOTTOM: if (v) top = n - 1; else x = widest(h, e); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            if (v)
                top = (int)((long)pos * (n - 1) / 100);
            else
                x = (int)((long)pos * widest(h, e) / 100);
            break;
        default:
            return 0;
        }
        /* SB_THUMBPOSITION notifies even when the thumb ended where it
           was tracked to (US6) */
        scroll_to(h, e, top, x, v && wp != SB_THUMBTRACK, !v && wp != SB_THUMBTRACK,
                  wp == SB_THUMBPOSITION);
        return 0;
    }
    case EM_SETTABSTOPS:
        /* wParam stops at lParam (dialog units); 0: the default, 1: a
           stop every that many (3.1 SDK) */
        uh_free(&uhObj, E(e)->tabs);
        E(e)->tabs = 0;
        E(e)->ntabs = 0;
        if (!(estyle(h) & ES_MULTILINE))
            return FALSE;
        if (wp && lp) {
            WORD i, n = wp;
            int __far *t;
            WORD b;
            /* one value: a stop every that many units (TabbedTextOut
               repeats a single stop) */
            b = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, n * sizeof(int));
            if (!b)
                return FALSE;
            E(e)->tabs = b;
            t = (int __far *)UH_PTR(&uhObj, b);
            for (i = 0; i < n; i++)
                t[i] = ((int __far *)lp)[i];
            E(e)->ntabs = n;
        }
        InvalidateRect(h, NULL, TRUE);
        return TRUE;
    case EM_GETTHUMB:
        /* WM_USER+14, internal in the 3.1 DDK's WINDOWS.H (Schulman ch.
           7 lists it): probe US6 got 0 from Windows 3.1's edit control
           every time - scrolled, after thumb messages, from inside
           EN_VSCROLL/EN_HSCROLL, and while the thumb was dragged with
           mouse_event input */
        return 0;
    case EM_GETRECT:
        GetClientRect(h, (RECT __far *)lp);
        return 0;
    case WM_CUT:
    case WM_CLEAR:
        replace_sel(h, e, "");
        return 0;
    case WM_COPY:
    case WM_PASTE:
        return 0;               /* UNVERIFIED: the clipboard is not yet */
    }
    return DefWindowProc(h, msg, wp, lp);
}
