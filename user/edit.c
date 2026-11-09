/*
 * edit.c - the EDIT class: single-line and (simple) multi-line edit
 * controls.
 *
 * The text is a block in USER's local heap. UNVERIFIED and not yet done
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
    WORD   limit;
    HFONT  font;
    WORD   focus;
    WORD   modified;
    char   password;
    HWND   parent;
} ED;
typedef ED __near *PED;

static PED ed_of(HWND h)
{
    return (PED)GetWindowWord(h, 0);
}

static DWORD estyle(HWND h)
{
    return GetWindowLong(h, GWL_STYLE);
}

static char __near *text(PED e)
{
    return (char __near *)e->text;
}

static void notify(HWND h, PED e, WORD code)
{
    send_message(e->parent, WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

static BOOL grow(PED e, WORD need)
{
    if (need + 1 <= e->size)
        return TRUE;
    {
        WORD n = need + 32;
        WORD b = (WORD)LocalReAlloc((HLOCAL)e->text, n, LMEM_MOVEABLE);
        if (!b)
            return FALSE;
        e->text = b;
        e->size = n;
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
    e->len = n;
    e->sel0 = e->sel1 = 0;
    e->first = 0;
    e->topline = 0;
}

/* lines of a multi-line control: start offsets */
static WORD line_start(PED e, int line)
{
    WORD i = 0;
    while (line > 0 && i < e->len) {
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
    while (i < e->len && text(e)[i] != '\r' && text(e)[i] != '\n')
        i++;
    return i - start;
}

static int line_of(PED e, WORD pos)
{
    WORD i;
    int n = 0;
    for (i = 0; i < pos && i < e->len; i++)
        if (text(e)[i] == '\n')
            n++;
    return n;
}

static int line_count(PED e)
{
    return line_of(e, e->len) + 1;
}

static int line_height(HWND h, PED e)
{
    HDC hdc = GetDC(h);
    TEXTMETRIC tm;
    HFONT old = SelectObject(hdc, e->font ? e->font : GetStockObject(SYSTEM_FONT));
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
        return 1;
    GetClientRect(h, &r);
    d = (r.bottom - r.top - line_height(h, e)) / 2;
    return d > 0 ? d : 0;
}

static int text_x(HDC hdc, PED e, WORD start, WORD n)
{
    if (!n)
        return 0;
    if (e->password) {
        char buf[64];
        WORD i;
        for (i = 0; i < n && i < sizeof buf; i++)
            buf[i] = e->password;
        return LOWORD(GetTextExtent(hdc, buf, n < sizeof buf ? n : sizeof buf));
    }
    return LOWORD(GetTextExtent(hdc, text(e) + start, n));
}

static void draw_run(HDC hdc, PED e, int x, int y, WORD start, WORD n, BOOL sel)
{
    if (!n)
        return;
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, syscolor[sel ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
    SetTextColor(hdc, syscolor[sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
    if (e->password) {
        char buf[64];
        WORD i;
        for (i = 0; i < n && i < sizeof buf; i++)
            buf[i] = e->password;
        TextOut(hdc, x, y, buf, n < sizeof buf ? n : sizeof buf);
    } else
        TextOut(hdc, x, y, text(e) + start, n);
}

static void paint(HWND h, PED e, HDC hdc)
{
    RECT r;
    HFONT old = SelectObject(hdc, e->font ? e->font : GetStockObject(SYSTEM_FONT));
    HBRUSH b = (HBRUSH)(WORD)send_message(e->parent, WM_CTLCOLOR, (WPARAM)hdc,
                                          MAKELONG(h, CTLCOLOR_EDIT));
    WORD s0 = e->sel0 < e->sel1 ? e->sel0 : e->sel1, s1 = e->sel0 < e->sel1 ? e->sel1 : e->sel0;
    BOOL showsel = e->focus || (estyle(h) & ES_NOHIDESEL);
    int lh = line_height(h, e);
    GetClientRect(h, &r);
    FillRect(hdc, &r, b ? b : sysbrush[COLOR_WINDOW]);
    /* a disabled edit control's text is drawn as usual (US4) */
    SetTextColor(hdc, syscolor[COLOR_WINDOWTEXT]);
    {
        int line, nlines = (estyle(h) & ES_MULTILINE) ? line_count(e) : 1;
        for (line = (estyle(h) & ES_MULTILINE) ? e->topline : 0; line < nlines; line++) {
            WORD st = (estyle(h) & ES_MULTILINE) ? line_start(e, line) : e->first;
            WORD n = (estyle(h) & ES_MULTILINE) ? line_len(e, st) : e->len - e->first;
            int y = (estyle(h) & ES_MULTILINE) ? (line - e->topline) * lh : inset(h, e);
            int x = inset(h, e);
            WORD a = st, z = st + n;
            if (y > r.bottom)
                break;
            /* before, inside, after the selection */
            if (showsel && s1 > s0 && s0 < z && s1 > a) {
                WORD p = s0 > a ? s0 : a, q = s1 < z ? s1 : z;
                int xp = x + text_x(hdc, e, a, p - a), xq = xp + text_x(hdc, e, p, q - p);
                draw_run(hdc, e, x, y, a, p - a, FALSE);
                draw_run(hdc, e, xq, y, q, z - q, FALSE);
                /* the selection: its background a pixel wider than the
                   text (US4: "notepad" selected in MS Sans Serif, 47
                   pixels for text 46) */
                {
                    RECT b;
                    SetRect(&b, xp, y, xq + 1, y + lh);
                    FillRect(hdc, &b, sysbrush[COLOR_HIGHLIGHT]);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, syscolor[COLOR_HIGHLIGHTTEXT]);
                    if (e->password) {
                        char buf[64];
                        WORD i;
                        for (i = 0; i < q - p && i < sizeof buf; i++)
                            buf[i] = e->password;
                        TextOut(hdc, xp, y, buf, i);
                    } else
                        TextOut(hdc, xp, y, text(e) + p, q - p);
                }
            } else {
                if (!IsWindowEnabled(h)) {
                    SetBkMode(hdc, TRANSPARENT);
                    if (e->password)
                        draw_run(hdc, e, x, y, a, z - a, FALSE);
                    else
                        TextOut(hdc, x, y, text(e) + a, z - a);
                } else
                    draw_run(hdc, e, x, y, a, z - a, FALSE);
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
    if (!e->focus)
        return;
    hdc = GetDC(h);
    old = SelectObject(hdc, e->font ? e->font : GetStockObject(SYSTEM_FONT));
    lh = line_height(h, e);
    if (estyle(h) & ES_MULTILINE) {
        int line = line_of(e, e->sel1);
        st = line_start(e, line);
        y = (line - e->topline) * lh;
    } else
        st = e->first;
    x = inset(h, e) + (e->sel1 > st ? text_x(hdc, e, st, e->sel1 - st) : 0);
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
        int line = line_of(e, e->sel1), lh = line_height(h, e), rows = r.bottom / (lh ? lh : 1);
        if (line < e->topline)
            e->topline = line;
        else if (rows > 0 && line >= e->topline + rows)
            e->topline = line - rows + 1;
        return;
    }
    if (e->sel1 < e->first)
        e->first = e->sel1;
    else {
        HDC hdc = GetDC(h);
        HFONT old = SelectObject(hdc, e->font ? e->font : GetStockObject(SYSTEM_FONT));
        while (e->first < e->sel1 && text_x(hdc, e, e->first, e->sel1 - e->first) > r.right - 2)
            e->first++;
        SelectObject(hdc, old);
        ReleaseDC(h, hdc);
    }
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
}

static void replace_sel(HWND h, PED e, LPCSTR s)
{
    WORD s0 = e->sel0 < e->sel1 ? e->sel0 : e->sel1, s1 = e->sel0 < e->sel1 ? e->sel1 : e->sel0;
    WORD n = s ? lstr_len(s) : 0;
    if (e->limit && e->len - (s1 - s0) + n > e->limit) {
        notify(h, e, EN_MAXTEXT);
        return;
    }
    if (!grow(e, e->len - (s1 - s0) + n))
        return;
    fmemcpy((char __far *)(text(e) + s0 + n), (char __far *)(text(e) + s1), e->len - s1 + 1);
    if (n)
        fmemcpy((char __far *)(text(e) + s0), s, n);
    e->len = e->len - (s1 - s0) + n;
    e->sel0 = e->sel1 = s0 + n;
    e->modified = 1;
    refresh(h, e);
    notify(h, e, EN_CHANGE);
}

static WORD pos_from_x(HWND h, PED e, int x, int y)
{
    HDC hdc = GetDC(h);
    HFONT old = SelectObject(hdc, e->font ? e->font : GetStockObject(SYSTEM_FONT));
    WORD st, n, i;
    if (estyle(h) & ES_MULTILINE) {
        int line = e->topline + y / line_height(h, e);
        if (line >= line_count(e))
            line = line_count(e) - 1;
        st = line_start(e, line);
        n = line_len(e, st);
    } else {
        st = e->first;
        n = e->len - st;
    }
    for (i = 0; i < n; i++)
        if (inset(h, e) + text_x(hdc, e, st, i + 1) - (text_x(hdc, e, st + i, 1) / 2) > x)
            break;
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    return st + i;
}

LRESULT WINAPI EditWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PED e;
    if (!valid_wnd(h))
        return 0;
    e = ed_of(h);
    if (msg == WM_NCCREATE) {
        e = (PED)LocalAlloc(LPTR, sizeof(ED));
        if (!e)
            return 0;
        /* fixed: e->text is used as the pointer (a moveable handle is not one) */
        e->text = (WORD)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, 32);
        e->size = 32;
        e->parent = GetParent(h);
        if (estyle(h) & ES_PASSWORD)
            e->password = '*';
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
        LocalFree((HLOCAL)e->text);
        LocalFree((HLOCAL)e);
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
        e->font = (HFONT)wp;
        if (LOWORD(lp))
            InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return (LONG)(WORD)e->font;
    case WM_SETTEXT:
        set_text(e, (LPCSTR)lp);
        e->modified = 0;
        refresh(h, e);
        notify(h, e, EN_CHANGE);
        return 1;
    case WM_GETTEXT:
        if ((int)wp <= 0)
            return 0;
        lstr_cpyn((LPSTR)lp, (LPCSTR)MAKELP(get_ds(), e->text), wp);
        return lstr_len((LPCSTR)lp);
    case WM_GETTEXTLENGTH:
        return e->len;
    case WM_SETFOCUS:
        e->focus = 1;
        CreateCaret(h, 0, 1, line_height(h, e));
        place_caret(h, e);
        ShowCaret(h);
        InvalidateRect(h, NULL, TRUE);
        notify(h, e, EN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        e->focus = 0;
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
        e->sel0 = e->sel1 = pos_from_x(h, e, (int)LOWORD(lp), (int)HIWORD(lp));
        SetCapture(h);
        refresh(h, e);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == h) {
            WORD p = pos_from_x(h, e, (int)LOWORD(lp), (int)HIWORD(lp));
            if (p != e->sel1) {
                e->sel1 = p;
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
            if (e->sel0 == e->sel1 && e->sel1 > 0)
                e->sel0 = e->sel1 - 1;
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
        WORD p = e->sel1;
        switch (wp) {
        case VK_LEFT: if (p) p--; break;
        case VK_RIGHT: if (p < e->len) p++; break;
        case VK_HOME:
            p = (estyle(h) & ES_MULTILINE) ? line_start(e, line_of(e, p)) : 0;
            break;
        case VK_END:
            p = (estyle(h) & ES_MULTILINE) ? line_start(e, line_of(e, p)) +
                line_len(e, line_start(e, line_of(e, p))) : e->len;
            break;
        case VK_DELETE:
            if (estyle(h) & ES_READONLY)
                return 0;
            if (e->sel0 == e->sel1 && e->sel1 < e->len)
                e->sel1++;
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
        e->sel1 = p;
        if (!shift)
            e->sel0 = p;
        refresh(h, e);
        return 0;
    }
    /* ---- EM_ messages ---- */
    case EM_GETSEL:
        return MAKELONG(e->sel0 < e->sel1 ? e->sel0 : e->sel1, e->sel0 < e->sel1 ? e->sel1 : e->sel0);
    case EM_SETSEL: {
        int a = (int)LOWORD(lp), b = (int)HIWORD(lp);
        if (a == -1) {
            e->sel0 = e->sel1;
        } else {
            e->sel0 = (WORD)(a < 0 ? 0 : a > (int)e->len ? e->len : a);
            e->sel1 = (WORD)(b < 0 || b > (int)e->len ? e->len : b);
        }
        refresh(h, e);
        return 1;
    }
    case EM_REPLACESEL:
        replace_sel(h, e, (LPCSTR)lp);
        return 0;
    case EM_LIMITTEXT:
        e->limit = wp;
        return 0;
    case EM_GETLINECOUNT:
        return line_count(e);
    case EM_LINEINDEX: {
        int line = (int)wp == -1 ? line_of(e, e->sel1) : (int)wp;
        if (line >= line_count(e))
            return -1;
        return line_start(e, line);
    }
    case EM_LINELENGTH: {
        WORD at = (int)wp == -1 ? e->sel1 : wp;
        return line_len(e, line_start(e, line_of(e, at)));
    }
    case EM_LINEFROMCHAR:
        return line_of(e, (int)wp == -1 ? e->sel1 : wp);
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
        return e->modified;
    case EM_SETMODIFY:
        e->modified = wp != 0;
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
        e->password = (char)wp;
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case EM_GETPASSWORDCHAR:
        return (BYTE)e->password;
    case EM_CANUNDO:
    case EM_UNDO:
    case EM_EMPTYUNDOBUFFER:
        return 0;
    case EM_GETFIRSTVISIBLELINE:
        return (estyle(h) & ES_MULTILINE) ? e->topline : e->first;
    case EM_LINESCROLL:
        e->topline += (int)HIWORD(lp);
        if (e->topline < 0)
            e->topline = 0;
        InvalidateRect(h, NULL, TRUE);
        return 1;
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
