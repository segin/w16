/*
 * edit.c - the EDIT class: single-line and multi-line edit controls.
 *
 * The state (ED) is a block of USER's object heap (uheap.c), not of
 * DGROUP (3.1's edit control costs DGROUP only its WND: US15), the
 * window's first extra word the state's handle, reached through E() on
 * every use (nothing in the heap is held across a call that allocates in
 * it or sends a message).
 *
 * The text, as on 3.1 (probe US20), is a moveable local block, never
 * locked, in the local heap of the data segment the window's hInstance
 * names - the application's own DS for a control it creates, so that
 * EM_GETHANDLE gives it a handle it can LocalLock, LocalReAlloc and give
 * back with EM_SETHANDLE (Notepad reads and writes files so). A dialog
 * without DS_LOCALEDIT gives its edit controls a 2000-byte segment of
 * their own instead (dlg.c); a control whose hInstance has no local heap
 * gets a private one. The block is len + 33 bytes when made or grown
 * (3.1: 34 empty, 30038 for 30005 characters; never shrunk by WM_SETTEXT;
 * EM_SETHANDLE sizes the new block so, 20 bytes holding 8 characters
 * became 42); EM_SETHANDLE leaves the old block to the application and
 * clears the modified flag; DestroyWindow frees the block. Its characters
 * are read where they are each time, the length is the control's (3.1:
 * text changed behind the control's back is seen up to the old length).
 * A pointer into it is made by lh_deref (LocalLock, LocalUnlock) and is
 * good until the next allocation in that heap. Single-line controls keep
 * their text the same way, but EM_GETHANDLE gives them 0 (3.1).
 *
 * A multi-line control keeps its lines' starts in a second block of the
 * same heap, 2 bytes a line (3.1 keeps them there too: Notepad's limit
 * depends on the number of lines, US21, ARCHITECTURE.md 15.1), made again
 * whenever the text, the width or the font changes. Without
 * ES_AUTOHSCROLL its lines wrap at its format rectangle (probe US35 on
 * Windows 3.1, ARCHITECTURE.md 10.4):
 *   - a line holds what fits (a character fits while the line's width
 *     with it is no more than the rectangle's); then it breaks after the
 *     last space or tab before the character that did not fit, or after
 *     that character when it is itself the first space after a word (one
 *     space may hang past the edge), else at that character (a word
 *     longer than the line is cut; at least one character a line);
 *     hyphens and other punctuation are no break;
 *   - CR LF ends a line, and so does CR CR LF (EM_FMTLINES' soft break);
 *     a lone CR or LF is a character like any other; neither belongs to
 *     the line (EM_LINELENGTH, EM_GETLINE);
 *   - EM_FMTLINES TRUE writes CR CR LF into the text at every soft
 *     break, FALSE takes every CR CR LF out again;
 *   - a position at a soft break is the next line's start; the caret is
 *     shown at the end of the line before after End, Up, Down or a click
 *     past the line's end, and after typing there.
 * The format rectangle (EM_GETRECT) is the client area, 4 pixels in with
 * WS_BORDER (whose frame the control draws in its client area, as a
 * single-line one), cut to whole lines; EM_SETRECT sets it (the border's
 * inset applied again), a resize puts it back to the client area.
 *
 * Undo (EM_UNDO, WM_UNDO, Alt+Backspace; EM_CANUNDO, EM_EMPTYUNDOBUFFER):
 * one record, what the last change inserted and deleted; see undo_*.
 *
 * Drawing (measured on US4's dialogs; UNVERIFIED in detail): the text
 * starts 1 pixel inside the border, the selection in the Highlight colours.
 *
 * Window extra bytes (3.1's class has 6, US2): word 0 the state block.
 */
#include "user.h"

typedef struct ED {
    WORD   text;                /* moveable local block of hseg, NUL-terminated */
    HANDLE hseg;                /* the segment its heap is in (a global handle) */
    WORD   sel;                 /* its selector, taken again after each allocation */
    HANDLE own;                 /* a private segment, freed with the control */
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
    BYTE   framed;              /* draws its own frame (see fmt_rect) */
    BYTE   forcesel;            /* shows its selection unfocused (edit_show_sel) */
    /* multi-line */
    WORD   lines;               /* moveable local block of hseg: the lines' starts */
    WORD   nlines;
    RECT   fmt;                 /* the format rectangle */
    BYTE   eol;                 /* the caret shown at the end of the line before */
    BYTE   fmtlines;            /* EM_FMTLINES TRUE */
    BYTE   wrapmode;            /* its lines wrap (no ES_AUTOHSCROLL, WS_HSCROLL) */
    BYTE   ro;                  /* read-only (EM_SETREADONLY; ES_READONLY for 3.1 programs) */
    FARPROC wbp;                /* EM_SETWORDBREAKPROC */
    /* undo */
    BYTE   ukind;               /* 0 nothing to undo */
    BYTE   utyped;              /* the record is typing that may go on */
    WORD   upos, uins;          /* uins characters inserted at upos */
    WORD   ubuf, udel;          /* udel characters deleted there (block of hseg) */
} ED;
typedef WORD PED;               /* the state block's handle (object heap) */
#define E(e)    ((ED __far *)UH_PTR(&uhObj, (e)))

static void relayout(HWND h, PED e);
static void set_fmt(HWND h, PED e, const RECT __far *r);

static PED ed_of(HWND h)
{
    return (PED)GetWindowWord(h, 0);
}

static DWORD estyle(HWND h)
{
    return GetWindowLong(h, GWL_STYLE);
}

static WORD seg_sel(HANDLE h)
{
    return h ? HIWORD(GlobalHandle((UINT)h)) : 0;
}

/* the text now: good until the next allocation in its heap */
static char __far *text(PED e)
{
    WORD sel = E(e)->sel;
    return (char __far *)MK_FP(sel, lh_deref(sel, E(e)->text));
}

static HFONT efont(PED e)
{
    return E(e)->font ? E(e)->font : GetStockObject(SYSTEM_FONT);
}

/* a 2000-byte segment made a local heap, for edit controls' text (3.1: a
   dialog's without DS_LOCALEDIT, GlobalSize 7D0h); 0 if none */
HANDLE edit_segment(void)
{
    HANDLE g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 2000);
    if (g && !LocalInit(seg_sel(g), 0x12, 1999)) {
        GlobalFree(g);
        g = 0;
    }
    return g;
}

/* does segment h have a local heap? (an instance's data segment has its
   heap's offset at 6, Pietrek ch. 2; a module database starts "NE") */
static BOOL has_heap(HANDLE h)
{
    WORD sel = seg_sel(h);
    DWORD n = sel ? GlobalSize(h) : 0;
    if (n < 0x10)
        return FALSE;
    if (*(WORD __far *)MK_FP(sel, 0) == 0x454E)
        return FALSE;
    return *(WORD __far *)MK_FP(sel, 6) != 0 && *(WORD __far *)MK_FP(sel, 6) < n;
}

static void notify(HWND h, PED e, WORD code)
{
    send_message(E(e)->parent, WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

/* the text's block made n bytes. KERNEL's LocalReAlloc grows a block in
   place only into a free block after it, and otherwise needs room for a
   copy, which a 31.5K text in a 53K segment never has (US20 part H: no
   flags grow a 31501-byte block at the heap's end by 32, on 3.1 either);
   yet 3.1's edit control grows it there when text is added (US20 part G;
   US21: Notepad's segment grew by 3744 bytes, the text in place, free
   space after it), though not when another block follows it (part F).
   So with force, when the plain LocalReAlloc fails, a block too big for
   the free space there is is allocated, which makes KERNEL grow the heap
   at its end, freed, and the LocalReAlloc tried again in place (flags 0:
   joined with a free block after it, which there is only if the text was
   the last block). UNVERIFIED how 3.1 does it (its segment grew by
   less) */
static BOOL resize(PED e, WORD n, BOOL force)
{
    WORD b, cur, t;
    int tries;
    for (tries = 0;; tries++) {
        /* the retry in place only: with the text not the heap's last
           block 3.1 does not grow it (US20 part F) */
        b = lh_realloc(E(e)->sel, E(e)->text, n, tries ? 0 : LMEM_MOVEABLE);
        E(e)->sel = seg_sel(E(e)->hseg);
        if (b || tries || !force)
            break;
        cur = lh_size(E(e)->sel, E(e)->text);
        if (n <= cur)
            break;
        t = lh_alloc(E(e)->sel, LMEM_FIXED, n - cur > 4096 - 64 ? n - cur + 64 : 4096);
        E(e)->sel = seg_sel(E(e)->hseg);
        if (!t)
            break;
        lh_free(E(e)->sel, t);
    }
    if (!b)
        return FALSE;
    E(e)->text = b;
    return TRUE;
}

/* room for need characters and the NUL: need + 33 bytes, or, when typing
   (step), 32 more than the block has if that is enough (3.1: 31502,
   31534, 31566 while 40 characters were typed; WM_SETTEXT of 30005
   characters: 30038) */
static BOOL grow(PED e, WORD need, BOOL step)
{
    WORD cur = lh_size(E(e)->sel, E(e)->text);
    if (need + 1 <= cur)
        return TRUE;
    if (need > 0xFFFF - 33)
        return FALSE;
    if (step && cur <= 0xFFFF - 32 && need + 1 <= cur + 32)
        return resize(e, cur + 32, TRUE);
    return resize(e, need + 33, TRUE);
}

static int line_height(HWND h, PED e)
{
    HDC hdc = ctl_dc(h);
    TEXTMETRIC tm;
    HFONT old = SelectObject(hdc, efont(e));
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    /* UNVERIFIED: Windows 3.1 highlights a selection in MS Sans Serif 8
       (13 high) over 14 rows (US4's Run dialog); the rule is not known */
    return tm.tmHeight > 0 ? tm.tmHeight : 1;
}

/* A single-line edit with WS_BORDER, and a combo box's edit (which has no
   WS_BORDER), draw their frame themselves, in the client area, which is
   the whole window (probe US34 on 3.1: GetClientRect 42 x 20 for a field
   42 x 20; a combo box's edit painted alone keeps its frame). The text's
   rectangle (EM_GETRECT) is then fixed by the font, not by the height:
   q = min(f, s) / 4 in from the top (f the font's height, s the system
   font's) and as far from left and right (half the average width for a
   fixed-pitch font: 4 for SYSTEM_FIXED_FONT, where q is 3), f high but
   no lower than q from the bottom (3 3 39 16 in MS Sans Serif 8 and
   42 x 20, 23 high too; 3 3 39 13 when 16 high; 4 4 44 20 in the system
   font, 48 x 24 or 28). Without the frame it is the font's height at the
   top of the client area (US35: 0 0 130 16 in the system font) */
static void fmt_rect(HWND h, PED e, RECT *r)
{
    HDC hdc = ctl_dc(h);
    TEXTMETRIC tm;
    HFONT old = SelectObject(hdc, efont(e));
    int qv, qh;
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    GetClientRect(h, r);
    if (!E(e)->framed) {
        if (tm.tmHeight < r->bottom)
            r->bottom = tm.tmHeight;
        return;
    }
    qv = (tm.tmHeight < cySysChar ? tm.tmHeight : cySysChar) / 4;
    qh = (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) ? qv : tm.tmAveCharWidth / 2;
    r->left = qh;
    r->right -= qh;
    r->bottom = qv + tm.tmHeight < r->bottom - qv ? qv + tm.tmHeight : r->bottom - qv;
    r->top = qv;
}

/* a single-line field's caret width (US34 on 3.1: 1 in MS Sans Serif 8,
   2 in the system font and in MS Sans Serif 10; f + 1 high in a framed
   field): min(f, s) / 8, at least 1. UNVERIFIED beyond those three */
static int caret_w(HWND h, PED e)
{
    int f = line_height(h, e), w = (f < cySysChar ? f : cySysChar) / 8;
    return w > 0 ? w : 1;
}

/* where the text starts inside a single-line edit (y: down, else
   across): a framed one's (fmt_rect); another's half the room the font
   leaves, both across and down */
static int inset_xy(HWND h, PED e, BOOL y)
{
    RECT r;
    int d;
    if (E(e)->framed) {
        fmt_rect(h, e, &r);
        return y ? r.top : r.left;
    }
    GetClientRect(h, &r);
    d = (r.bottom - r.top - line_height(h, e)) / 2;
    return d > 0 ? d : 0;
}
#define inset(h, e) inset_xy((h), (e), FALSE)
#define inset_y(h, e) inset_xy((h), (e), TRUE)

/* ---- tabs ----
   A multi-line control expands tabs (3.1 SDK, EM_SETTABSTOPS: stops in
   dialog units, every 32 by default), the units its own font's (US35 on
   3.1: a bold MS Sans Serif 8's tabs every 56 pixels, the system font's
   every 64): the stops in pixels for TabbedTextOut, into buf; returns
   their number */
static int tab_stops(PED e, int bx, int *buf, int max)
{
    int i, n = 0;
    if (!E(e)->ntabs || !E(e)->tabs) {
        buf[0] = 32 * bx / 4;
        return 1;
    }
    for (i = 0; i < (int)E(e)->ntabs && i < max; i++, n++)
        buf[i] = ((int __far *)UH_PTR(&uhObj, E(e)->tabs))[i] * bx / 4;
    return n;
}

/* the stops for one measuring or drawing pass */
typedef struct TABS {
    int n, *st, buf[16], deftab;
} TABS;

static void tabs_begin(HDC hdc, PED e, TABS *tc)
{
    static char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int bx;
    tc->st = tc->buf;
    if (E(e)->ntabs > 16) {
        tc->st = (int __near *)LocalAlloc(LPTR, E(e)->ntabs * sizeof(int));
        if (!tc->st)
            tc->st = tc->buf;
    }
    /* the font's average width as dialogs take it (dlg.c) */
    bx = (LOWORD(GetTextExtent(hdc, letters, 52)) / 26 + 1) / 2;
    tc->n = tab_stops(e, bx, tc->st, tc->st == tc->buf ? 16 : E(e)->ntabs);
    /* past the last of several: TabbedTextOut's default (rect.c) */
    tc->deftab = 8 * bx;
}

static void tabs_end(TABS *tc)
{
    if (tc->st != tc->buf)
        LocalFree((HLOCAL)tc->st);
}

/* as TabbedTextOut's (rect.c next_stop) */
static int next_stop(int x, int tw)
{
    if (tw < 0)
        return (int)((WORD)x / (WORD)tw + 1) * tw;
    return (x / tw + 1) * tw;
}

static int tab_next(const TABS *tc, int x)
{
    int k;
    if (tc->n > 1) {
        for (k = 0; k < tc->n; k++)
            if (tc->st[k] > x)
                return tc->st[k];
        return tc->deftab ? next_stop(x, tc->deftab) : x;
    }
    return tc->st[0] ? next_stop(x, tc->st[0]) : x;
}

/* ---- characters' widths: one table, kept for the last font measured
   (a block of USER's object heap) ---- */
static WORD cwtab;
static HFONT cwfont;
static int cwkey[4];

static int __far *widths(HDC hdc, HFONT f)
{
    TEXTMETRIC tm;
    int __far *t;
    int i;
    GetTextMetrics(hdc, &tm);
    if (cwtab && cwfont == f && cwkey[0] == tm.tmHeight && cwkey[1] == tm.tmAveCharWidth &&
        cwkey[2] == tm.tmMaxCharWidth && cwkey[3] == tm.tmWeight)
        return (int __far *)UH_PTR(&uhObj, cwtab);
    if (!cwtab)
        cwtab = uh_alloc(&uhObj, LMEM_FIXED, 256 * sizeof(int));
    if (!cwtab)
        return 0;
    t = (int __far *)UH_PTR(&uhObj, cwtab);
    for (i = 0; i < 256; i++) {
        char c = (char)i;
        /* how far a character advances: its extent less the overhang */
        t[i] = LOWORD(GetTextExtent(hdc, &c, 1)) - tm.tmOverhang;
    }
    cwfont = f;
    cwkey[0] = tm.tmHeight;
    cwkey[1] = tm.tmAveCharWidth;
    cwkey[2] = tm.tmMaxCharWidth;
    cwkey[3] = tm.tmWeight;
    return (int __far *)UH_PTR(&uhObj, cwtab);
}

/* a measuring pass: the DC with the font, the widths, the tab stops */
typedef struct MEAS {
    HDC hdc;
    HFONT old;
    int __far *cw;
    TABS tc;
} MEAS;

static void meas_begin(HWND h, PED e, MEAS *m)
{
    m->hdc = ctl_dc(h);
    m->old = SelectObject(m->hdc, efont(e));
    m->cw = widths(m->hdc, efont(e));
    tabs_begin(m->hdc, e, &m->tc);
}

static void meas_end(HWND h, MEAS *m)
{
    tabs_end(&m->tc);
    SelectObject(m->hdc, m->old);
    ReleaseDC(h, m->hdc);
}

static int ch_w(MEAS *m, BYTE c)
{
    char ch;
    if (m->cw)
        return m->cw[c];
    ch = (char)c;
    return LOWORD(GetTextExtent(m->hdc, &ch, 1));
}

/* the width of text [a, b) starting at x0 from its line's origin */
static int span_w(MEAS *m, PED e, WORD a, WORD b, int x0)
{
    const char __far *t = text(e);
    int x = x0;
    for (; a < b; a++)
        x = t[a] == '\t' ? tab_next(&m->tc, x) : x + ch_w(m, (BYTE)t[a]);
    return x;
}

/* ---- the lines of a multi-line control ---- */
static WORD __far *ltab(PED e)
{
    WORD sel = E(e)->sel;
    return (WORD __far *)MK_FP(sel, lh_deref(sel, E(e)->lines));
}

static WORD lstart(PED e, int i)
{
    if (i <= 0 || !E(e)->lines)
        return 0;
    if (i >= (int)E(e)->nlines)
        return E(e)->len;
    return ltab(e)[i];
}

static int line_count(PED e)
{
    return E(e)->nlines ? E(e)->nlines : 1;
}

/* where line i's characters end: before the CR LF (or CR CR LF) that
   ends it, if one does */
static WORD line_end(PED e, int i)
{
    WORD s = lstart(e, i), end = i + 1 < line_count(e) ? lstart(e, i + 1) : E(e)->len;
    const char __far *t = text(e);
    if (end - s >= 2 && t[end - 1] == '\n' && t[end - 2] == '\r') {
        end -= 2;
        if (end > s && t[end - 1] == '\r')
            end--;
    }
    return end;
}

/* does line i end in a break of the text's own (CR LF), not a wrap? */
static BOOL line_hard(PED e, int i)
{
    return i + 1 >= line_count(e) || line_end(e, i) != lstart(e, i + 1);
}

/* the line position p is on (a soft break's position is the next
   line's start) */
static int line_of(PED e, WORD p)
{
    int lo = 0, hi = line_count(e) - 1;
    WORD __far *lt;
    if (!E(e)->lines || hi <= 0)
        return 0;
    lt = ltab(e);
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (lt[mid] <= p)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

/* the line the caret is shown on: the line before when it stands at a
   soft break with eol */
static int caret_line(PED e)
{
    WORD p = E(e)->sel1;
    int i = line_of(e, p);
    if (E(e)->eol && i > 0 && p == lstart(e, i) && !line_hard(e, i - 1))
        i--;
    return i;
}

static BOOL is_delim(char c)
{
    return c == ' ' || c == '\t';
}

/* the application's word-break procedure (3.1 SDK, EM_SETWORDBREAKPROC:
   the text, a position, the text's length, WB_LEFT/WB_RIGHT/
   WB_ISDELIMITER); 3.1 passes the whole text and its length (US35) */
static int call_wbp(PED e, int ich, int code)
{
    FARPROC f = E(e)->wbp;
    int r;
    r = ((int (__far __pascal *)(LPSTR, int, int, int))f)(text(e), ich, E(e)->len, code);
    return r;
}

/* where a line from s breaks when character c does not fit (see the
   header) */
static WORD wrap_break(PED e, WORD s, WORD c, WORD lend)
{
    const char __far *t;
    WORD p;
    if (E(e)->wbp) {
        /* 3.1 asks WB_ISDELIMITER of that character (a break after it),
           then of the one before (a break at it), then WB_LEFT (US35) */
        int r;
        /* a delimiter that is a space hangs; another starts the next
           line (US35: a hyphen) */
        if (call_wbp(e, c, WB_ISDELIMITER))
            return text(e)[c] == ' ' ? c + 1 : c;
        if (c > s && call_wbp(e, c - 1, WB_ISDELIMITER))
            return c;
        r = call_wbp(e, c, WB_LEFT);
        if (r > (int)s && r <= (int)c)
            return r;
        return c > s ? c : s + 1;
    }
    (void)lend;
    t = text(e);
    if (c > s && is_delim(t[c]) && !is_delim(t[c - 1]))
        return c + 1;
    /* back to the last space or tab; a lone CR or LF stops the search
       (US35: "and lone" CR "C" cut after the C) */
    for (p = c; p > s; p--) {
        if (is_delim(t[p - 1]))
            return p;
        if (t[p - 1] == '\r' || t[p - 1] == '\n')
            break;
    }
    return c > s ? c : s + 1;
}

/* where the line from s ends: the next line's start; *more FALSE when
   s's line is the last */
static WORD scan_line(PED e, MEAS *m, WORD s, BOOL wrap, int W, BOOL *more)
{
    const char __far *t = text(e);
    WORD i, len = E(e)->len, next;
    int __far *cw = m->cw;
    int x = 0;
    *more = FALSE;
    for (i = s; i < len; i++) {
        char c = t[i];
        if (c == '\r') {
            if (i + 1 < len && t[i + 1] == '\n') {
                *more = TRUE;
                return i + 2;
            }
            if (i + 2 < len && t[i + 1] == '\r' && t[i + 2] == '\n') {
                *more = TRUE;
                return i + 3;
            }
        }
        if (wrap) {
            int nx = c == '\t' ? tab_next(&m->tc, x) : x + (cw ? cw[(BYTE)c] : ch_w(m, (BYTE)c));
            if (nx > W) {
                next = wrap_break(e, s, i, len);
                t = text(e);
                /* a break just before a CR LF takes it (US35: "abcdefghij "
                   CR LF "xyz" with 10 characters a line: two lines) */
                if (next + 1 < len && t[next] == '\r' && t[next + 1] == '\n')
                    next += 2;
                else if (next + 2 < len && t[next] == '\r' && t[next + 1] == '\r' &&
                         t[next + 2] == '\n')
                    next += 3;
                *more = next < len;
                return next;
            }
            x = nx;
        }
    }
    return len;
}

static BOOL wrapping(HWND h)
{
    /* not with ES_AUTOHSCROLL or WS_HSCROLL (US35 on 3.1: a control with a
       horizontal bar keeps its lines whole) */
    return !(estyle(h) & (ES_AUTOHSCROLL | WS_HSCROLL));
}

/* room in the line table for n lines */
static BOOL ltab_room(PED e, WORD n)
{
    WORD cap, b;
    if (!E(e)->lines) {
        b = lh_alloc(E(e)->sel, LMEM_MOVEABLE, 64 * 2);
        E(e)->sel = seg_sel(E(e)->hseg);
        if (!b)
            return FALSE;
        E(e)->lines = b;
    }
    cap = lh_size(E(e)->sel, E(e)->lines) / 2;
    if (n <= cap)
        return TRUE;
    if (n > 0x7FF0)
        return FALSE;
    b = lh_realloc(E(e)->sel, E(e)->lines, (n + 64) * 2, LMEM_MOVEABLE);
    E(e)->sel = seg_sel(E(e)->hseg);
    if (!b)
        return FALSE;
    E(e)->lines = b;
    return TRUE;
}

/* the lines made again from the text, the format rectangle's width
   and the font */
static void relayout(HWND h, PED e)
{
    MEAS m;
    WORD s = 0, n = 0;
    int W;
    BOOL wrap, more;
    if (!E(e)->multi)
        return;
    wrap = wrapping(h);
    E(e)->wrapmode = (BYTE)wrap;
    W = E(e)->fmt.right - E(e)->fmt.left;
    meas_begin(h, e, &m);
    /* the table is made only for a second line (an empty control has
       none: US20 part F, where its 128 bytes changed what LocalReAlloc
       could do with the block EM_SETHANDLE gave) */
    for (;;) {
        if (n) {
            if (!ltab_room(e, n + 1))
                break;          /* no room: the rest stays on the last line */
            if (n == 1)
                ltab(e)[0] = 0;
            ltab(e)[n] = s;
        }
        n++;
        s = scan_line(e, &m, s, wrap, W, &more);
        if (!more)
            break;
    }
    meas_end(h, &m);
    E(e)->nlines = n ? n : 1;
}

/* the lines made again after a change at pos (rem characters taken out,
   ins put in): from the change's line (see below) until a line starts
   where an old one did (shifted), the rest moved (typing at the end of
   a 20K text took 38 ms a character with the whole text wrapped again,
   3.1 5 ms) */
#define RL_MAX 48
static void relayout_change(HWND h, PED e, WORD pos, WORD rem, WORD ins)
{
    MEAS m;
    WORD nw[RL_MAX], s, n = E(e)->nlines, j, cnt = 0, tail, k;
    int L, delta = (int)ins - (int)rem, W;
    BOOL wrap, more, synced = FALSE;
    WORD __far *lt;
    if (!E(e)->multi)
        return;
    if (!E(e)->lines || n < 2) {
        relayout(h, e);
        return;
    }
    /* from the change's line (the old table, old positions): a change
       does not pull words back up to the line before (3.1's Notepad:
       "thirteen", typed before "hello", went to the next line with it and
       stayed there as " fourteen" was typed and Backspace pressed after
       it, though it would fit on the first line again) */
    L = line_of(e, pos);
    s = lstart(e, L);
    wrap = wrapping(h);
    W = E(e)->fmt.right - E(e)->fmt.left;
    meas_begin(h, e, &m);
    j = n;
    for (;;) {
        if (cnt >= RL_MAX) {
            meas_end(h, &m);
            relayout(h, e);
            return;
        }
        nw[cnt++] = s;
        s = scan_line(e, &m, s, wrap, W, &more);
        if (!more)
            break;
        if (s > pos + ins) {
            /* an old line starting here, past the change? */
            WORD old = s - delta;
            int lo = L + 1, hi = n - 1;
            lt = ltab(e);
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (lt[mid] == old) {
                    if (old > pos + rem) {
                        j = mid;
                        synced = TRUE;
                    }
                    break;
                }
                if (lt[mid] < old)
                    lo = mid + 1;
                else
                    hi = mid - 1;
            }
            if (synced)
                break;
        }
    }
    meas_end(h, &m);
    tail = synced ? n - j : 0;
    if (!ltab_room(e, L + cnt + tail)) {
        relayout(h, e);
        return;
    }
    lt = ltab(e);
    if (tail) {
        fmemcpy(lt + L + cnt, lt + j, tail * 2);
        for (k = L + cnt; k < L + cnt + tail; k++)
            lt[k] += delta;
    }
    for (k = 0; k < cnt; k++)
        lt[L + k] = nw[k];
    E(e)->nlines = L + cnt + tail;
}

/* ---- the format rectangle (multi-line) ---- */
static int border_in(HWND h)
{
    /* UNVERIFIED on other displays: 4 on VGA in every font (US35) */
    return (estyle(h) & WS_BORDER) ? cySysChar / 4 : 0;
}

/* r (client coordinates) or the client area, less the border's inset,
   cut to whole lines; one narrower than a pixel is not taken (US35: a
   4-pixel-wide control keeps the lines it had) */
static void set_fmt(HWND h, PED e, const RECT __far *r)
{
    RECT c;
    int in = border_in(h), lh = line_height(h, e), n;
    if (r)
        c = *r;
    else
        GetClientRect(h, &c);
    InflateRect(&c, -in, -in);
    if (c.right - c.left < 1 && E(e)->fmt.right > E(e)->fmt.left)
        return;
    n = (c.bottom - c.top) / lh;
    if (n < 1)
        n = 1;
    c.bottom = c.top + n * lh;
    E(e)->fmt = c;
}

static int visible_lines(HWND h, PED e)
{
    int n = (E(e)->fmt.bottom - E(e)->fmt.top) / line_height(h, e);
    return n > 0 ? n : 1;
}

/* line i's offset for ES_CENTER and ES_RIGHT: half the room the line
   leaves, or all of it less a pixel, the line's trailing spaces counted
   (US35 on 3.1) */
static int align_x(HWND h, PED e, MEAS *m, int i)
{
    DWORD s = estyle(h);
    int w, W;
    if (!(s & (ES_CENTER | ES_RIGHT)))
        return 0;
    W = E(e)->fmt.right - E(e)->fmt.left;
    w = span_w(m, e, lstart(e, i), line_end(e, i), 0);
    /* never left of the rectangle (US35: a line wider than it at 0) */
    if (w >= W)
        return 0;
    if (s & ES_RIGHT)
        return W - w - 1;
    return (W - w) / 2;
}

/* the x of position p on line i, from the format rectangle's left */
static int pos_x(HWND h, PED e, MEAS *m, int i, WORD p)
{
    WORD s = lstart(e, i), le = line_end(e, i);
    if (p > le)
        p = le;
    return align_x(h, e, m, i) + span_w(m, e, s, p < s ? s : p, 0) - E(e)->xoff;
}

/* the position on line i at x (from the format rectangle's left): the
   nearest, the left one when x is half way (US35 on 3.1: Up from x 106
   to positions at 102 and 110 took 102, from 66 to 64 and 68 took 64);
   with floor the last whose x is not past it (Page Down from 32 to 29
   and 33 took 29); *eol set when that is the end of a wrapped line */
static WORD pos_at(HWND h, PED e, int i, int x, BYTE *eol, BOOL floor)
{
    MEAS m;
    WORD s, le, p;
    int cx, ax;
    const char __far *t;
    meas_begin(h, e, &m);
    s = lstart(e, i);
    le = line_end(e, i);
    ax = align_x(h, e, &m, i) - E(e)->xoff;
    cx = ax;
    t = text(e);
    for (p = s; p < le; p++) {
        int nx = t[p] == '\t' ? ax + tab_next(&m.tc, cx - ax) : cx + ch_w(&m, (BYTE)t[p]);
        if (floor ? x < nx : 2 * x <= cx + nx)
            break;
        cx = nx;
        t = text(e);
    }
    meas_end(h, &m);
    *eol = (BYTE)(p == le && !line_hard(e, i));
    return p;
}

/* ---- single-line measuring ---- */
static int text_x(HDC hdc, PED e, WORD start, WORD n)
{
    /* how far the text advances: its extent less the font's overhang (a
       synthesized bold's; US34 on 3.1, "(None)" in MS Sans Serif 8: the
       caret 38 after it, its highlight 39 wide; w16 had 39 and 40) */
    TEXTMETRIC tm;
    int x;
    if (!n)
        return 0;
    GetTextMetrics(hdc, &tm);
    if (E(e)->password) {
        char buf[64];
        WORD i;
        for (i = 0; i < n && i < sizeof buf; i++)
            buf[i] = E(e)->password;
        x = LOWORD(GetTextExtent(hdc, buf, n < sizeof buf ? n : sizeof buf));
    } else
        x = LOWORD(GetTextExtent(hdc, text(e) + start, n));
    return x - tm.tmOverhang;
}

/* ---- drawing ---- */
static void out_text(HDC hdc, HWND h, PED e, int x, int y, WORD start, WORD n, int origin)
{
    if (E(e)->multi) {
        TABS tc;
        tabs_begin(hdc, e, &tc);
        TabbedTextOut(hdc, x, y, text(e) + start, n, tc.n, tc.st, origin);
        tabs_end(&tc);
    } else if (E(e)->password) {
        char buf[64];
        WORD i;
        for (i = 0; i < n && i < sizeof buf; i++)
            buf[i] = E(e)->password;
        TextOut(hdc, x, y, buf, n < sizeof buf ? n : sizeof buf);
    } else
        TextOut(hdc, x, y, text(e) + start, n);
    (void)h;
}

static void draw_run(HDC hdc, HWND h, PED e, int x, int y, WORD start, WORD n, BOOL sel, int origin)
{
    if (!n)
        return;
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, syscolor[sel ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
    SetTextColor(hdc, syscolor[sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
    out_text(hdc, h, e, x, y, start, n, origin);
}

/* one line of text from a to z at x, y; the selection s0-s1 highlighted;
   xs(p) the x of position p */
static void paint_line(HDC hdc, HWND h, PED e, MEAS *m, int line, int x, int y, WORD a, WORD z,
                       WORD s0, WORD s1, BOOL showsel, int lh)
{
    if (showsel && s1 > s0 && s0 < z && s1 > a) {
        WORD p = s0 > a ? s0 : a, q = s1 < z ? s1 : z;
        int xp, xq;
        TEXTMETRIC tm;
        RECT b;
        if (m) {
            xp = x + span_w(m, e, a, p, 0);
            xq = x + span_w(m, e, a, q, 0);
        } else {
            xp = x + text_x(hdc, e, a, p - a);
            xq = xp + text_x(hdc, e, p, q - p);
        }
        draw_run(hdc, h, e, x, y, a, p - a, FALSE, x);
        draw_run(hdc, h, e, xq, y, q, z - q, FALSE, x);
        /* the selection: its background the text's extent, the overhang
           of a synthesized bold font included (US4: "notepad" selected in
           MS Sans Serif 8, 47 pixels for text advancing 46; US34: 39 for
           "(None)" advancing 38, but 38 for system-font text advancing
           38), a row more than the font's height in a framed field (US34:
           14 rows of MS Sans Serif 8, as far as the clip lets) */
        GetTextMetrics(hdc, &tm);
        SetRect(&b, xp, y, xq + tm.tmOverhang, y + lh + (E(e)->framed && !E(e)->multi ? 1 : 0));
        FillRect(hdc, &b, sysbrush[COLOR_HIGHLIGHT]);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, syscolor[COLOR_HIGHLIGHTTEXT]);
        out_text(hdc, h, e, xp, y, p, q - p, x);
    } else if (!IsWindowEnabled(h)) {
        /* a disabled edit control's text is drawn as usual (US4) */
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, syscolor[COLOR_WINDOWTEXT]);
        out_text(hdc, h, e, x, y, a, z - a, x);
    } else
        draw_run(hdc, h, e, x, y, a, z - a, FALSE, x);
    (void)line;
}

static void paint(HWND h, PED e, HDC hdc)
{
    RECT r;
    HFONT old = SelectObject(hdc, efont(e));
    HBRUSH b = (HBRUSH)(WORD)send_message(E(e)->parent, WM_CTLCOLOR, (WPARAM)hdc,
                                          MAKELONG(h, CTLCOLOR_EDIT));
    WORD s0 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel0 : E(e)->sel1, s1 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel1 : E(e)->sel0;
    BOOL showsel = E(e)->focus || E(e)->forcesel || (estyle(h) & ES_NOHIDESEL);
    int lh = line_height(h, e);
    GetClientRect(h, &r);
    FillRect(hdc, &r, b ? b : sysbrush[COLOR_WINDOW]);
    if (E(e)->framed) {
        /* a multi-line control's frame goes round its scroll bars too
           (US35 on 3.1): its line beside a bar is the bar's own */
        RECT fr = r;
        if (estyle(h) & WS_VSCROLL)
            fr.right++;
        if (estyle(h) & WS_HSCROLL)
            fr.bottom++;
        FrameRect(hdc, &fr, sysbrush[COLOR_WINDOWFRAME]);
    }
    SetTextColor(hdc, syscolor[COLOR_WINDOWTEXT]);
    if (E(e)->multi) {
        MEAS m;
        int i, n = line_count(e), rows = visible_lines(h, e);
        RECT f = E(e)->fmt;
        IntersectClipRect(hdc, f.left, f.top, f.right, f.bottom);
        meas_begin(h, e, &m);
        /* the measuring DC is another; this one keeps its own font */
        for (i = E(e)->topline; i < n && i < E(e)->topline + rows; i++) {
            WORD a = lstart(e, i), z = line_end(e, i);
            int x = f.left + align_x(h, e, &m, i) - E(e)->xoff;
            int y = f.top + (i - E(e)->topline) * lh;
            paint_line(hdc, h, e, &m, i, x, y, a, z, s0, s1, showsel, lh);
        }
        meas_end(h, &m);
    } else {
        if (E(e)->framed) {
            /* the text inside its rectangle down to q from the bottom
               (US34 on 3.1: a 16-high field's MS Sans Serif text cut at
               13, the selection's highlight at 17 in a 20-high one) */
            RECT f;
            fmt_rect(h, e, &f);
            IntersectClipRect(hdc, f.left, f.top, f.right, r.bottom - f.top);
        }
        paint_line(hdc, h, e, NULL, 0, inset(h, e), inset_y(h, e), E(e)->first, E(e)->len,
                   s0, s1, showsel, lh);
    }
    SelectObject(hdc, old);
}

static void place_caret(HWND h, PED e)
{
    HDC hdc;
    HFONT old;
    int x, y;
    if (!E(e)->focus)
        return;
    if (E(e)->multi) {
        MEAS m;
        int i = caret_line(e), rows = visible_lines(h, e);
        /* a caret on a line out of view is put far off (US35 on 3.1:
           GetCaretPos -20000, -20000) */
        if (i < E(e)->topline || i >= E(e)->topline + rows) {
            SetCaretPos(-20000, -20000);
            return;
        }
        meas_begin(h, e, &m);
        x = E(e)->fmt.left + pos_x(h, e, &m, i, E(e)->sel1);
        meas_end(h, &m);
        /* never past the rectangle's right edge less the caret's width
           (US35 on 3.1: 124 in a rectangle ending at 126) */
        if (x > E(e)->fmt.right - 2)
            x = E(e)->fmt.right - 2;
        y = E(e)->fmt.top + (i - E(e)->topline) * line_height(h, e);
        SetCaretPos(x, y);
        return;
    }
    hdc = ctl_dc(h);
    old = SelectObject(hdc, efont(e));
    y = inset_y(h, e);
    x = inset(h, e) + (E(e)->sel1 > E(e)->first ? text_x(hdc, e, E(e)->first, E(e)->sel1 - E(e)->first) : 0);
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    SetCaretPos(x, y);
}

/* the caret's x from the format rectangle's left (as shown: clamp
   for Up and Down, which take it) */
static int caret_x_c(HWND h, PED e, BOOL clamp)
{
    MEAS m;
    int x, W = E(e)->fmt.right - E(e)->fmt.left;
    meas_begin(h, e, &m);
    x = pos_x(h, e, &m, caret_line(e), E(e)->sel1);
    meas_end(h, &m);
    if (clamp && x > W - 2)
        x = W - 2;
    return x;
}
#define caret_x(h, e) caret_x_c((h), (e), FALSE)

/* keep the caret in view (single line, ES_AUTOHSCROLL; multi-line lines,
   and across with ES_AUTOHSCROLL) */
static void scroll_to_caret(HWND h, PED e)
{
    RECT r;
    if (E(e)->multi) {
        int line = caret_line(e), rows = visible_lines(h, e);
        DWORD st = estyle(h);
        /* only with ES_AUTOVSCROLL or a scroll bar (US35 on 3.1: Ctrl+End
           in a two-line control without either left the view at the
           top, the caret out of it) */
        if (st & (ES_AUTOVSCROLL | WS_VSCROLL)) {
            if (line < E(e)->topline)
                E(e)->topline = line;
            else if (line >= E(e)->topline + rows)
                E(e)->topline = line - rows + 1;
        }
        if (st & (ES_AUTOHSCROLL | WS_HSCROLL)) {
            /* a third of the width at a time, while the caret is left of
               the client area or at or past its right edge (US35 on 3.1:
               40 pixels in a 122-pixel rectangle to the left, the caret 1
               pixel from the client's left edge and 129 in a 130-pixel
               one still shown. UNVERIFIED: to the right 3.1 moved 128
               pixels in three steps) */
            int W = E(e)->fmt.right - E(e)->fmt.left, step = W / 3, x = caret_x(h, e);
            RECT c;
            int cw;
            GetClientRect(h, &c);
            cw = c.right;
            if (step < 1)
                step = 1;
            while (x + E(e)->fmt.left < 0 && E(e)->xoff > 0) {
                int d = E(e)->xoff < step ? E(e)->xoff : step;
                E(e)->xoff -= d;
                x += d;
            }
            while (x + E(e)->fmt.left >= cw) {
                E(e)->xoff += step;
                x -= step;
            }
        }
        return;
    }
    GetClientRect(h, &r);
    if (E(e)->framed)
        r.right -= 2;           /* as when the frame was outside the client */
    if (E(e)->sel1 < E(e)->first)
        E(e)->first = E(e)->sel1;
    else if (estyle(h) & ES_AUTOHSCROLL) {
        /* only then (US34 on 3.1: a combo box's edit without
           CBS_AUTOHSCROLL, its text wider than the field and selected
           whole, shows it from the start, cut at the right) */
        HDC hdc = ctl_dc(h);
        HFONT old = SelectObject(hdc, efont(e));
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
    /* the font's average as dialogs take it (US35 on 3.1: 8 pixels a
       character in the system font, whose tmAveCharWidth is 7) */
    static char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    HDC hdc = ctl_dc(h);
    HFONT old = SelectObject(hdc, efont(e));
    int w = (LOWORD(GetTextExtent(hdc, letters, 52)) / 26 + 1) / 2;
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    return w > 0 ? w : 1;
}

static int widest(HWND h, PED e)
{
    MEAS m;
    int w = 0, i, n = line_count(e);
    meas_begin(h, e, &m);
    for (i = 0; i < n; i++) {
        int x = span_w(&m, e, lstart(e, i), line_end(e, i), 0);
        if (x > w)
            w = x;
    }
    meas_end(h, &m);
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

static void refresh(HWND h, PED e)
{
    /* invalidated, not painted now: a dialog's focus goes to its edit
       control (selecting its text) while the dialog itself is still to be
       painted, and the dialog, without WS_CLIPCHILDREN, would paint over
       it (US4's Run dialog) */
    int top = E(e)->topline, xoff = E(e)->xoff;
    scroll_to_caret(h, e);
    HideCaret(h);
    InvalidateRect(h, NULL, TRUE);
    place_caret(h, e);
    ShowCaret(h);
    set_scrollbars(h, e);
    /* a scroll made by keys or typing is reported too (3.1: Notepad's
       bars follow Ctrl+End and typing, taken on EN_VSCROLL/EN_HSCROLL:
       ARCHITECTURE.md 15.1) */
    if (E(e)->multi && E(e)->topline != top)
        notify(h, e, EN_VSCROLL);
    if (E(e)->multi && E(e)->xoff != xoff)
        notify(h, e, EN_HSCROLL);
}

/* ---- undo ---- */
static void undo_empty(PED e)
{
    if (E(e)->ubuf)
        lh_free(E(e)->sel, E(e)->ubuf);
    E(e)->ubuf = 0;
    E(e)->udel = 0;
    E(e)->uins = 0;
    E(e)->ukind = 0;
    E(e)->utyped = 0;
}

/* the deleted text kept: n characters of text at pos, before what is
   kept (front) or after it */
static BOOL undo_keep(PED e, WORD pos, WORD n, BOOL front)
{
    WORD b, old = E(e)->udel;
    char __far *d;
    if (!n)
        return TRUE;
    if (E(e)->ubuf)
        b = lh_realloc(E(e)->sel, E(e)->ubuf, old + n, LMEM_MOVEABLE);
    else
        b = lh_alloc(E(e)->sel, LMEM_MOVEABLE, old + n);
    E(e)->sel = seg_sel(E(e)->hseg);
    if (!b)
        return FALSE;
    E(e)->ubuf = b;
    d = (char __far *)MK_FP(E(e)->sel, lh_deref(E(e)->sel, b));
    if (front) {
        fmemcpy(d + n, d, old);
        fmemcpy(d, text(e) + pos, n);
    } else
        fmemcpy(d + old, text(e) + pos, n);
    E(e)->udel = old + n;
    return TRUE;
}

/* what a change is, for undo */
#define K_OTHER   0             /* paste, cut, clear, EM_REPLACESEL */
#define K_TYPE    1             /* a character typed */
#define K_BACK    2             /* Backspace */
#define K_DEL     3             /* Delete */
#define K_NOUNDO  4             /* a multi-line control's EM_REPLACESEL */

/* a change about to be made: s0-s1 replaced by n characters.
   A multi-line control whose lines wrap (US35 on 3.1) keeps one change: an insertion
   alone (a character typed, a paste: the selection it replaced is not
   kept, and every character typed is a change of its own), or a deletion
   (Backspace, Delete, a cut), joined to the deletion before it when the
   two touch (Backspace after Backspace, Delete after Delete, a cut at
   the place an undone paste was); EM_REPLACESEL empties it. A
   single-line control, and a multi-line one that does not wrap, keeps
   the run typed with the text it replaced, and Backspaces and Deletes
   with it (3.1's Notepad without Word Wrap: "abc" typed over a
   selection undone whole, the selection put back; tests/scripts/np/
   undo.txt). */
static void undo_record(PED e, WORD s0, WORD s1, WORD n, int kind)
{
    if (kind == K_NOUNDO) {
        undo_empty(e);
        return;
    }
    if (E(e)->multi && E(e)->wrapmode) {
        if (!n && E(e)->ukind && !E(e)->uins) {
            if (s1 == E(e)->upos) {
                undo_keep(e, s0, s1 - s0, TRUE);
                E(e)->upos = s0;
                return;
            }
            if (s0 == E(e)->upos) {
                undo_keep(e, s0, s1 - s0, FALSE);
                return;
            }
        }
        undo_empty(e);
        E(e)->ukind = 1;
        E(e)->upos = s0;
        E(e)->uins = n;
        if (!n)
            undo_keep(e, s0, s1 - s0, FALSE);
        return;
    }
    /* typing, or a paste, right after the text the record inserted joins
       it (3.1's Notepad: a paste at the end of "abc" typed over "ld",
       then Undo, put back "ld") */
    if ((kind == K_TYPE || kind == K_OTHER) && n && s0 == s1 && E(e)->ukind && E(e)->uins &&
        s0 == E(e)->upos + E(e)->uins) {
        E(e)->uins += n;
        return;
    }
    /* Backspace after typing starts a deletion, the typing forgotten
       (3.1's Notepad: "hello world", two Backspaces, Undo: "ld" back) */
    if (kind == K_BACK && s1 == s0 + 1 && E(e)->ukind && E(e)->utyped) {
        if (!E(e)->uins && s1 == E(e)->upos) {
            undo_keep(e, s0, 1, TRUE);
            E(e)->upos = s0;
            return;
        }
    }
    if (kind == K_DEL && s1 == s0 + 1 && E(e)->ukind && E(e)->utyped && !E(e)->uins &&
        s0 == E(e)->upos) {
        undo_keep(e, s0, 1, FALSE);
        return;
    }
    undo_empty(e);
    E(e)->ukind = 1;
    E(e)->utyped = kind != K_OTHER;
    E(e)->upos = s0;
    E(e)->uins = n;
    undo_keep(e, s0, s1 - s0, FALSE);
}

/* ---- changing the text ---- */
static void set_text(HWND h, PED e, LPCSTR s)
{
    WORD n = s ? lstr_len(s) : 0;
    if (!grow(e, n, FALSE))
        return;
    if (n)
        fmemcpy((char __far *)text(e), s, n);
    text(e)[n] = 0;
    E(e)->len = n;
    E(e)->sel0 = E(e)->sel1 = 0;
    E(e)->first = 0;
    E(e)->topline = 0;
    E(e)->xoff = 0;
    E(e)->eol = 0;
    E(e)->fmtlines = 0;
    undo_empty(e);
    relayout(h, e);
}

/* s0-s1 made the n characters at s (a far pointer outside the text's
   heap), the caret after them, EN_UPDATE and EN_CHANGE sent; FALSE when
   the control went away meanwhile or there was no room */
static BOOL change(HWND h, PED e, WORD s0, WORD s1, LPCSTR s, WORD n, int kind)
{
    if (!grow(e, E(e)->len - (s1 - s0) + n, TRUE)) {
        notify(h, e, EN_ERRSPACE);
        return FALSE;
    }
    undo_record(e, s0, s1, n, kind);
    fmemcpy((char __far *)(text(e) + s0 + n), (char __far *)(text(e) + s1), E(e)->len - s1 + 1);
    if (n)
        fmemcpy((char __far *)(text(e) + s0), s, n);
    E(e)->len = E(e)->len - (s1 - s0) + n;
    E(e)->modified = 1;
    relayout_change(h, e, s0, s1 - s0, n);
    E(e)->sel0 = E(e)->sel1 = s0 + n;
    /* typed at a soft break: the caret stays at the end of the line it
       typed on (US35) */
    E(e)->eol = kind == K_TYPE;
    notify(h, e, EN_UPDATE);
    if (!valid_wnd(h) || ed_of(h) != e)
        return FALSE;
    refresh(h, e);
    notify(h, e, EN_CHANGE);
    return valid_wnd(h) && ed_of(h) == e;
}

static void replace_sel_n(HWND h, PED e, LPCSTR s, WORD n, int kind)
{
    WORD s0 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel0 : E(e)->sel1, s1 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel1 : E(e)->sel0;
    BOOL cut = FALSE;
    if (s1 > E(e)->len)
        s1 = E(e)->len;
    if (s0 > s1)
        s0 = s1;
    /* after EM_FMTLINES TRUE 3.1 takes nothing in: a character typed
       gives EN_UPDATE, EN_CHANGE and EN_MAXTEXT and marks the text
       changed, the text as it was (US35), until EM_FMTLINES FALSE or new
       text */
    if (E(e)->multi && E(e)->fmtlines) {
        E(e)->modified = 1;
        notify(h, e, EN_UPDATE);
        if (!valid_wnd(h) || ed_of(h) != e)
            return;
        notify(h, e, EN_CHANGE);
        if (!valid_wnd(h) || ed_of(h) != e)
            return;
        notify(h, e, EN_MAXTEXT);
        return;
    }
    /* past the limit what fits goes in (3.1: 5 characters replacing
       nothing at 29998 left 30000), then EN_MAXTEXT (US35: "UCM" for a
       paste cut short, "M" alone for a key that did not fit) */
    if (E(e)->limit && E(e)->len - (s1 - s0) + n > E(e)->limit) {
        WORD keep = E(e)->len - (s1 - s0);
        n = keep < E(e)->limit ? E(e)->limit - keep : 0;
        cut = TRUE;
    }
    if (s0 == s1 && !n) {
        if (cut)
            notify(h, e, EN_MAXTEXT);
        return;
    }
    /* a multi-line control takes a selection out and puts the new text
       in as two changes (US35: EN_UPDATE and EN_CHANGE twice) */
    if (E(e)->multi && E(e)->wrapmode && s1 > s0 && n) {
        if (!change(h, e, s0, s1, "", 0, kind == K_NOUNDO ? K_NOUNDO : K_OTHER))
            return;
        s1 = s0;
    }
    if (!change(h, e, s0, s1, s, n, kind))
        return;
    if (cut)
        notify(h, e, EN_MAXTEXT);
}

static void replace_sel(HWND h, PED e, LPCSTR s, int kind)
{
    replace_sel_n(h, e, s, s ? lstr_len(s) : 0, kind);
}

/* EM_UNDO: what the record inserted taken out, what it deleted put back;
   the record turned round (a second undo redoes) */
static BOOL undo(HWND h, PED e)
{
    WORD pos, ins, del, len;
    HLOCAL keep = 0;
    char __near *tmp = 0;
    if (!E(e)->ukind)
        return FALSE;
    pos = E(e)->upos;
    ins = E(e)->uins;
    del = E(e)->udel;
    len = E(e)->len;
    if (pos > len || ins > len - pos)
        return FALSE;
    /* the deleted text, copied out of the heap the change allocates in */
    if (del) {
        keep = LocalAlloc(LMEM_FIXED, del);
        if (!keep)
            return FALSE;
        tmp = (char __near *)keep;
        fmemcpy((char __far *)tmp,
                (char __far *)MK_FP(E(e)->sel, lh_deref(E(e)->sel, E(e)->ubuf)), del);
    }
    if (!grow(e, len - ins + del, FALSE)) {
        if (keep)
            LocalFree(keep);
        return FALSE;
    }
    /* the record turned round: what goes is kept, what comes is inserted */
    if (E(e)->ubuf)
        lh_free(E(e)->sel, E(e)->ubuf);
    E(e)->ubuf = 0;
    E(e)->udel = 0;
    undo_keep(e, pos, ins, FALSE);
    E(e)->uins = del;
    E(e)->utyped = 0;
    fmemcpy((char __far *)(text(e) + pos + del), (char __far *)(text(e) + pos + ins), len - pos - ins + 1);
    if (del)
        fmemcpy((char __far *)(text(e) + pos), (char __far *)tmp, del);
    if (keep)
        LocalFree(keep);
    E(e)->len = len - ins + del;
    E(e)->modified = 1;
    relayout_change(h, e, pos, ins, del);
    /* the text put back selected (US35) */
    E(e)->sel0 = pos;
    E(e)->sel1 = pos + del;
    E(e)->eol = 0;
    notify(h, e, EN_UPDATE);
    if (!valid_wnd(h) || ed_of(h) != e)
        return TRUE;
    refresh(h, e);
    notify(h, e, EN_CHANGE);
    return TRUE;
}

/* ---- EM_FMTLINES ---- */
static void fmt_lines(HWND h, PED e, BOOL on)
{
    E(e)->fmtlines = (BYTE)on;
    if (on) {
        /* CR CR LF at every soft break, from the end (the line table
           stays the same, shifted) */
        int i;
        WORD extra = 0, n = line_count(e);
        for (i = 0; i + 1 < (int)n; i++)
            if (!line_hard(e, i))
                extra += 3;
        if (!extra || !grow(e, E(e)->len + extra, FALSE))
            return;
        for (i = n - 2; i >= 0; i--) {
            WORD b = lstart(e, i + 1);
            char __far *t;
            if (!line_hard(e, i)) {
                t = text(e);
                fmemcpy(t + b + 3, t + b, E(e)->len - b + 1);
                t[b] = '\r';
                t[b + 1] = '\r';
                t[b + 2] = '\n';
                E(e)->len += 3;
            }
        }
    } else {
        /* every CR CR LF out */
        char __far *t = text(e);
        WORD i, k = 0, len = E(e)->len;
        for (i = 0; i < len; i++) {
            if (t[i] == '\r' && i + 2 < len && t[i + 1] == '\r' && t[i + 2] == '\n') {
                i += 2;
                continue;
            }
            t[k++] = t[i];
        }
        t[k] = 0;
        E(e)->len = k;
    }
    if (E(e)->sel0 > E(e)->len)
        E(e)->sel0 = E(e)->len;
    if (E(e)->sel1 > E(e)->len)
        E(e)->sel1 = E(e)->len;
    relayout(h, e);
    InvalidateRect(h, NULL, TRUE);
}

/* ---- the clipboard ---- */
static WORD copy_sel(HWND h, PED e)
{
    WORD s0 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel0 : E(e)->sel1, s1 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel1 : E(e)->sel0;
    HGLOBAL g;
    LPSTR p;
    if (s1 <= s0 || E(e)->password)
        return 0;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (DWORD)(s1 - s0) + 1);
    if (!g)
        return 0;
    p = GlobalLock(g);
    fmemcpy(p, text(e) + s0, s1 - s0);
    p[s1 - s0] = 0;
    GlobalUnlock(g);
    if (!OpenClipboard(h)) {
        GlobalFree(g);
        return 0;
    }
    EmptyClipboard();
    SetClipboardData(CF_TEXT, g);
    CloseClipboard();
    return s1 - s0;
}

static void paste(HWND h, PED e)
{
    HANDLE g;
    LPSTR p;
    WORD n;
    if (!OpenClipboard(h))
        return;
    g = GetClipboardData(CF_TEXT);
    p = g ? GlobalLock(g) : NULL;
    if (p) {
        n = lstr_len(p);
        /* a single-line control takes the first line (UNVERIFIED) */
        if (!E(e)->multi) {
            WORD i;
            for (i = 0; i < n && p[i] != '\r' && p[i] != '\n'; i++)
                ;
            n = i;
        }
        replace_sel_n(h, e, p, n, K_OTHER);
        GlobalUnlock(g);
    }
    CloseClipboard();
}

/* ---- moving by words: the start of the word after or before p (the
   application's procedure's WB_RIGHT and WB_LEFT, over the whole text) */
static WORD word_right(PED e, WORD p)
{
    const char __far *t;
    WORD len = E(e)->len;
    if (E(e)->wbp) {
        /* 3.1 asks WB_ISDELIMITER of p, WB_LEFT of p and WB_RIGHT of
           one past that (US35), nothing at the end */
        int a, r;
        if (p >= len)
            return len;
        call_wbp(e, p, WB_ISDELIMITER);
        a = call_wbp(e, p, WB_LEFT);
        a = a < 0 ? 0 : (WORD)a >= len ? len - 1 : a;
        r = call_wbp(e, a + 1, WB_RIGHT);
        return r < 0 ? 0 : (WORD)r > len ? len : (WORD)r;
    }
    t = text(e);
    if (p < len && t[p] == '\r') {
        /* a line break is a word of its own */
        while (p < len && (t[p] == '\r' || t[p] == '\n'))
            p++;
        return p;
    }
    while (p < len && !is_delim(t[p]) && t[p] != '\r')
        p++;
    while (p < len && is_delim(t[p]))
        p++;
    return p;
}

static WORD word_left(PED e, WORD p)
{
    const char __far *t;
    if (E(e)->wbp) {
        /* WB_LEFT of p, then WB_RIGHT of one past that (US35), nothing
           at the start */
        int r;
        if (!p)
            return 0;
        r = call_wbp(e, p, WB_LEFT);
        r = r < 0 ? 0 : (WORD)r > p ? p : r;
        if ((WORD)r < E(e)->len)
            call_wbp(e, r + 1, WB_RIGHT);
        return r;
    }
    t = text(e);
    if (p > 0 && t[p - 1] == '\n') {
        while (p > 0 && (t[p - 1] == '\r' || t[p - 1] == '\n'))
            p--;
        return p;
    }
    while (p > 0 && is_delim(t[p - 1]))
        p--;
    while (p > 0 && !is_delim(t[p - 1]) && t[p - 1] != '\n')
        p--;
    return p;
}

/* ---- the mouse ---- */
static WORD pos_from_point(HWND h, PED e, int x, int y, BYTE *eol)
{
    *eol = 0;
    if (E(e)->multi) {
        int lh = line_height(h, e), line;
        int dy = y - E(e)->fmt.top;
        line = E(e)->topline + (dy < 0 ? -1 : dy / lh);
        if (line < E(e)->topline && line < 0)
            line = 0;
        if (line >= line_count(e))
            line = line_count(e) - 1;
        if (line < 0)
            line = 0;
        return pos_at(h, e, line, x - E(e)->fmt.left, eol, FALSE);
    }
    {
        HDC hdc = ctl_dc(h);
        HFONT old = SelectObject(hdc, efont(e));
        WORD st = E(e)->first, n = E(e)->len - st, i;
        for (i = 0; i < n; i++)
            if (inset(h, e) + text_x(hdc, e, st, i + 1) - (text_x(hdc, e, st + i, 1) / 2) > x)
                break;
        SelectObject(hdc, old);
        ReleaseDC(h, hdc);
        return st + i;
    }
}

/* a combo box's edit, its list dropped: the selection shows, focus or
   not, until the edit next loses the focus (US34 on 3.1: a drop-down box
   in a window, the focus elsewhere, dropped and closed with
   CB_SHOWDROPDOWN: its text highlighted) */
void edit_show_sel(HWND h)
{
    PED e = valid_wnd(h) ? ed_of(h) : 0;
    if (!e || E(e)->forcesel)
        return;
    E(e)->forcesel = 1;
    InvalidateRect(h, NULL, TRUE);
}

#define EM_GETTHUMB (WM_USER + 14)

/* the keys (WM_KEYDOWN) */
static void key_down(HWND h, PED e, WORD vk)
{
    BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL ml = E(e)->multi;
    WORD p = E(e)->sel1, len = E(e)->len;
    BYTE eol = 0;
    const char __far *t = text(e);
    switch (vk) {
    case VK_LEFT:
        if (ctrl)
            p = word_left(e, p);
        else if (p >= 3 && ml && t[p - 1] == '\n' && t[p - 2] == '\r' && t[p - 3] == '\r')
            p -= 3;
        else if (p >= 2 && ml && t[p - 1] == '\n' && t[p - 2] == '\r')
            p -= 2;
        else if (p)
            p--;
        break;
    case VK_RIGHT:
        if (ctrl)
            p = word_right(e, p);
        else if (ml && p + 2 < len && t[p] == '\r' && t[p + 1] == '\r' && t[p + 2] == '\n')
            p += 3;
        else if (ml && p + 1 < len && t[p] == '\r' && t[p + 1] == '\n')
            p += 2;
        else if (p < len)
            p++;
        break;
    /* with Ctrl, the start and the end of the text (3.1: Notepad,
       Ctrl+Home after typing 45 lines went to the first) */
    case VK_HOME:
        p = ml && !ctrl ? lstart(e, caret_line(e)) : 0;
        break;
    case VK_END:
        if (ml && !ctrl) {
            int i = caret_line(e);
            p = line_end(e, i);
            /* the end of a wrapped line: the next line's start, the caret
               at this line's end (US35) */
            eol = (BYTE)!line_hard(e, i);
        } else
            p = len;
        break;
    case VK_DELETE:
        if (E(e)->ro)
            return;
        if (shift) {                /* cut */
            copy_sel(h, e);
            replace_sel(h, e, "", K_OTHER);
            return;
        }
        if (E(e)->sel0 == E(e)->sel1 && E(e)->sel1 < len) {
            WORD q = p + 1;
            if (ml && p + 2 < len && t[p] == '\r' && t[p + 1] == '\r' && t[p + 2] == '\n')
                q = p + 3;
            else if (ml && p + 1 < len && t[p] == '\r' && t[p + 1] == '\n')
                q = p + 2;
            E(e)->sel0 = p;
            E(e)->sel1 = q;
        }
        replace_sel(h, e, "", K_DEL);
        return;
    case VK_INSERT:
        if (shift) {
            if (!E(e)->ro)
                paste(h, e);
        } else if (ctrl)
            copy_sel(h, e);
        return;
    case VK_UP:
    case VK_DOWN:
        if (ml) {
            int line = caret_line(e) + (vk == VK_UP ? -1 : 1);
            /* the caret's x kept (US35 on 3.1: the x it has now, past the
               rectangle's edge where it is shown, not a column remembered
               past a shorter line) */
            if (line >= 0 && line < line_count(e))
                /* from the end of a wrapped line a tie goes right (US35:
                   x 123 between 121 and the line's end at 125 took the
                   end; UNVERIFIED as a rule) */
                p = pos_at(h, e, line, caret_x(h, e) + (E(e)->eol && caret_line(e) != line_of(e, E(e)->sel1)),
                           &eol, FALSE);
            else
                eol = E(e)->eol;
            break;
        }
        return;
    case VK_PRIOR:
    case VK_NEXT:
        /* a page (the lines shown less one) up or down, the caret and
           the view together (3.1: Notepad, two Page Downs from the top
           showed line 43 at the top with the caret on it. UNVERIFIED
           near the ends) */
        if (ml) {
            int n = line_count(e), page = visible_lines(h, e) - 1, d, line, top, x = caret_x(h, e);
            if (page < 1)
                page = 1;
            d = vk == VK_PRIOR ? -page : page;
            line = caret_line(e) + d;
            line = line < 0 ? 0 : line > n - 1 ? n - 1 : line;
            top = E(e)->topline + d;
            top = top < 0 ? 0 : top > n - 1 ? n - 1 : top;
            p = pos_at(h, e, line, x, &eol, TRUE);
            E(e)->sel1 = p;
            E(e)->eol = eol;
            if (!shift)
                E(e)->sel0 = p;
            scroll_to(h, e, top, E(e)->xoff, TRUE, FALSE, FALSE);
            refresh(h, e);
        }
        return;
    default:
        return;
    }
    E(e)->sel1 = p;
    E(e)->eol = eol;
    if (!shift)
        E(e)->sel0 = p;
    refresh(h, e);
}

LRESULT WINAPI __loadds EditWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PED e;
    if (!valid_wnd(h))
        return 0;
    e = ed_of(h);
    if (msg == WM_NCCREATE) {
        WORD t;
        HANDLE seg = (HANDLE)GetWindowWord(h, GWW_HINSTANCE), own = 0;
        if (!has_heap(seg))
            seg = own = edit_segment();
        if (!seg)
            return 0;
        e = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, sizeof(ED));
        if (!e) {
            if (own)
                GlobalFree(own);
            return 0;
        }
        t = lh_alloc(seg_sel(seg), LMEM_MOVEABLE | LMEM_ZEROINIT, 33);
        if (!t) {
            uh_free(&uhObj, e);
            if (own)
                GlobalFree(own);
            return 0;
        }
        E(e)->text = t;
        E(e)->hseg = seg;
        E(e)->sel = seg_sel(seg);
        E(e)->own = own;
        E(e)->limit = 30000;        /* (3.1 SDK; US20) */
        E(e)->multi = (estyle(h) & ES_MULTILINE) != 0;
        E(e)->parent = GetParent(h);
        if (estyle(h) & ES_PASSWORD)
            E(e)->password = '*';
        /* ES_READONLY counts for a program marked for 3.1 only (US35, a
           3.0 program's controls made with it took typing, Delete and
           pastes; EM_SETREADONLY stopped them) */
        E(e)->ro = (estyle(h) & ES_READONLY) && q_of(cur_hq())->expwinver >= 0x030A;
        /* single-line, no scroll bars, WS_BORDER or a combo box's; or
           multi-line with WS_BORDER (US35: its client area the window
           less its scroll bars): its own frame */
        E(e)->framed = E(e)->multi ? (estyle(h) & WS_BORDER) != 0 :
                       !(estyle(h) & (WS_VSCROLL | WS_HSCROLL)) &&
                       ((estyle(h) & WS_BORDER) || is_combo(E(e)->parent));
        SetWindowWord(h, 0, (WORD)e);
        {
            CREATESTRUCT __far *cs = (CREATESTRUCT __far *)lp;
            if (E(e)->multi)
                SetRect(&E(e)->fmt, 0, 0, cs->cx, cs->cy);
            if (cs->lpszName && !ISATOM(cs->lpszName))
                set_text(h, e, cs->lpszName);
        }
        return DefWindowProc(h, msg, wp, lp);
    }
    if (!e)
        return DefWindowProc(h, msg, wp, lp);
    switch (msg) {
    case WM_CREATE:
        if (E(e)->multi) {
            set_fmt(h, e, NULL);
            relayout(h, e);
        }
        return 0;
    case WM_NCDESTROY:
        uh_free(&uhObj, E(e)->tabs);
        if (E(e)->ubuf)
            lh_free(E(e)->sel, E(e)->ubuf);
        if (E(e)->lines)
            lh_free(E(e)->sel, E(e)->lines);
        lh_free(E(e)->sel, E(e)->text);
        if (E(e)->own)
            GlobalFree(E(e)->own);
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
    case WM_SIZE:
        if (E(e)->multi) {
            set_fmt(h, e, NULL);
            relayout(h, e);
            if (E(e)->topline > line_count(e) - 1)
                E(e)->topline = line_count(e) - 1;
            InvalidateRect(h, NULL, TRUE);
            if (E(e)->focus)
                place_caret(h, e);
        }
        return 0;
    case WM_SETFONT:
        E(e)->font = (HFONT)wp;
        if (E(e)->multi) {
            /* the rectangle's lines counted again in the new font (US35:
               4,4,196,95 after 4,4,196,84 in MS Sans Serif 8) */
            set_fmt(h, e, NULL);
            relayout(h, e);
            if (E(e)->focus) {
                DestroyCaret();
                CreateCaret(h, 0, 2, line_height(h, e));
                place_caret(h, e);
                ShowCaret(h);
            }
        }
        if (LOWORD(lp))
            InvalidateRect(h, NULL, TRUE);
        return E(e)->multi;     /* (US35: 1 from a multi-line control) */
    case WM_GETFONT:
        return (LONG)(WORD)E(e)->font;
    case WM_SETTEXT:
        /* a bad string, a bad buffer (WM_GETTEXT, EM_GETLINE, EM_REPLACESEL):
           0, the text as it was (3.1) */
        if (GP_BAD(lp))
            return 0;
        set_text(h, e, (LPCSTR)lp);
        E(e)->modified = 0;
        refresh(h, e);
        /* a multi-line control notifies nothing (US35) */
        if (!E(e)->multi)
            notify(h, e, EN_CHANGE);
        return 1;
    case WM_GETTEXT:
        if ((int)wp <= 0 || GP_BAD(lp))
            return 0;
    {
        /* the control's length of what is in the block (US20) */
        WORD n = E(e)->len < wp - 1 ? E(e)->len : wp - 1;
        fmemcpy((LPSTR)lp, text(e), n);
        ((LPSTR)lp)[n] = 0;
        return n;
    }
    case WM_GETTEXTLENGTH:
        return E(e)->len;
    case WM_SETFOCUS:
        E(e)->focus = 1;
        /* two pixels wide in Notepad's multi-line control, one in the
           Run dialog's single-line one (3.1; UNVERIFIED which of style
           and font decides) */
        CreateCaret(h, 0, E(e)->multi ? 2 : caret_w(h, e),
                    line_height(h, e) + (E(e)->framed && !E(e)->multi ? 1 : 0));
        place_caret(h, e);
        ShowCaret(h);
        InvalidateRect(h, NULL, TRUE);
        notify(h, e, EN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        E(e)->focus = 0;
        E(e)->forcesel = 0;
        DestroyCaret();
        InvalidateRect(h, NULL, TRUE);
        notify(h, e, EN_KILLFOCUS);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS |
               ((estyle(h) & ES_MULTILINE) && (estyle(h) & ES_WANTRETURN) ? DLGC_WANTALLKEYS : 0);
    case WM_LBUTTONDOWN:
        SetFocus(h);
        {
            BYTE eol;
            WORD pos = pos_from_point(h, e, (int)LOWORD(lp), (int)HIWORD(lp), &eol);
            E(e)->sel1 = pos;
            if (!(wp & MK_SHIFT))
                E(e)->sel0 = pos;
            E(e)->eol = eol;
        }
        SetCapture(h);
        refresh(h, e);
        return 0;
    case WM_LBUTTONDBLCLK: {
        /* the word left of the place clicked, with the spaces after it,
           not before the line's start; the caret at its end (US35 on 3.1:
           a click at the start of "jumps" took "fox ") */
        BYTE eol;
        WORD pos = pos_from_point(h, e, (int)LOWORD(lp), (int)HIWORD(lp), &eol), a, b, ls;
        SetFocus(h);
        ls = E(e)->multi ? lstart(e, line_of(e, pos)) : 0;
        if (E(e)->wbp) {
            /* WB_LEFT of the place, WB_RIGHT of one past that (US35) */
            int r = call_wbp(e, pos, WB_LEFT);
            a = r < 0 ? 0 : (WORD)r > pos ? pos : r;
            r = a < E(e)->len ? call_wbp(e, a + 1, WB_RIGHT) : a;
            b = r < (int)a ? a : (WORD)r > E(e)->len ? E(e)->len : r;
        } else {
            a = word_left(e, pos);
            if (a < ls)
                a = ls;
            b = word_right(e, a);
        }
        E(e)->sel0 = a;
        E(e)->sel1 = b;
        E(e)->eol = 0;
        refresh(h, e);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == h) {
            BYTE eol;
            WORD p = pos_from_point(h, e, (int)LOWORD(lp), (int)HIWORD(lp), &eol);
            if (p != E(e)->sel1) {
                E(e)->sel1 = p;
                E(e)->eol = eol;
                refresh(h, e);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h)
            ReleaseCapture();
        return 0;
    case WM_SYSCHAR:
        /* Alt+Backspace: undo (3.1 SDK, Edit controls) */
        if (wp == VK_BACK) {
            if (!E(e)->ro)
                undo(h, e);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == 0x03) {                   /* Ctrl+C */
            copy_sel(h, e);
            return 1;
        }
        if (E(e)->ro)
            return 0;
        if (wp == 8) {
            if (E(e)->sel0 == E(e)->sel1 && E(e)->sel1 > 0) {
                const char __far *t = text(e);
                WORD p = E(e)->sel1;
                E(e)->sel0 = p - 1;
                if (E(e)->multi && p >= 2 && t[p - 1] == '\n' && t[p - 2] == '\r')
                    E(e)->sel0 = p >= 3 && t[p - 3] == '\r' ? p - 3 : p - 2;
            }
            replace_sel(h, e, "", K_BACK);
            return 1;
        }
        if (wp == 0x1A)                     /* Ctrl+Z (US35) */
            return undo(h, e);
        if (wp == 0x18) {                   /* Ctrl+X */
            copy_sel(h, e);
            replace_sel(h, e, "", K_OTHER);
            return 1;
        }
        if (wp == 0x16) {                   /* Ctrl+V */
            paste(h, e);
            return 1;
        }
        if (wp == '\r') {
            if (E(e)->multi)
                replace_sel(h, e, "\r\n", K_TYPE);
        } else if (wp >= ' ' || wp == '\t') {
            char c[2];
            c[0] = (char)wp;
            c[1] = 0;
            if (estyle(h) & ES_UPPERCASE)
                AnsiUpper(c);
            else if (estyle(h) & ES_LOWERCASE)
                AnsiLower(c);
            replace_sel(h, e, c, K_TYPE);
        }
        return 0;
    case WM_KEYDOWN:
        key_down(h, e, wp);
        return 0;
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
        E(e)->eol = 0;
        /* the caret brought into view whatever wParam says (US35) */
        refresh(h, e);
        return 1;
    }
    case EM_REPLACESEL:
        if (GP_BAD(lp))
            return 0;
        /* a wrapping multi-line control's empties the undo record (US35;
           UNVERIFIED without wrapping) */
        replace_sel(h, e, (LPCSTR)lp, E(e)->multi && E(e)->wrapmode ? K_NOUNDO : K_OTHER);
        return 1;
    case EM_LIMITTEXT:
        /* 0: as much as fits (3.1 answers the limit: 65535 for 0, US35) */
        E(e)->limit = wp ? wp : 0xFFFF;
        return E(e)->limit;
    case WM_USER + 13:              /* EM_GETHANDLE: multi-line only (US20) */
        return E(e)->multi ? E(e)->text : 0;
    case WM_USER + 12:              /* EM_SETHANDLE */
        if (E(e)->multi && wp) {
            WORD n;
            E(e)->text = wp;
            n = lstr_len(text(e));
            /* the line table made again after the new block is sized (it
               would otherwise take the room the block may move into:
               US20 part F, 31502 bytes on 3.1) */
            if (E(e)->lines) {
                lh_free(E(e)->sel, E(e)->lines);
                E(e)->lines = 0;
                E(e)->nlines = 0;
            }
            /* the block sized to the text, if KERNEL can (3.1: 20
               bytes holding 8 characters became 42, 3000 holding 2900
               2934; a 31502-byte one at the end of a full heap stayed) */
            if (n <= 0xFFFF - 33)
                resize(e, n + 33, FALSE);
            E(e)->len = n;
            E(e)->sel0 = E(e)->sel1 = 0;
            E(e)->first = 0;
            E(e)->topline = 0;
            E(e)->xoff = 0;
            E(e)->eol = 0;
            E(e)->fmtlines = 0;
            E(e)->modified = 0;
            undo_empty(e);
            relayout(h, e);
            refresh(h, e);
            return 1;           /* (US35) */
        }
        return 0;
    case EM_GETLINECOUNT:
        return E(e)->multi ? line_count(e) : 1;
    case EM_LINEINDEX: {
        int line;
        if (!E(e)->multi)
            return 0;
        /* -1: the line the caret shows on (US35: at a wrapped line's
           end, that line's start) */
        line = (int)wp == -1 ? caret_line(e) : (int)wp;
        if (line >= line_count(e) || line < 0)
            return -1;
        return lstart(e, line);
    }
    case EM_LINELENGTH: {
        int i;
        if (!E(e)->multi)
            return E(e)->len;
        if ((int)wp == -1) {
            /* the characters on the selection's lines not selected (3.1
               SDK); with no selection the caret's line */
            WORD s0 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel0 : E(e)->sel1, s1 = E(e)->sel0 < E(e)->sel1 ? E(e)->sel1 : E(e)->sel0;
            int a = line_of(e, s0), b = line_of(e, s1);
            if (s0 == s1)
                return line_end(e, a) - lstart(e, a);
            return (s0 - lstart(e, a)) + (line_end(e, b) > s1 ? line_end(e, b) - s1 : 0);
        }
        i = line_of(e, wp > E(e)->len ? E(e)->len : wp);
        return line_end(e, i) - lstart(e, i);
    }
    case EM_LINEFROMCHAR:
        if (!E(e)->multi)
            return 0;
        return line_of(e, (int)wp == -1 ? E(e)->sel1 : wp > E(e)->len ? E(e)->len : wp);
    case EM_GETLINE: {
        WORD st, n, max;
        if (GP_BAD(lp))
            return 0;
        max = *(WORD __far *)lp;
        if (!E(e)->multi) {
            st = 0;
            n = E(e)->len;
        } else {
            if ((int)wp >= line_count(e) || (int)wp < 0)
                return 0;
            st = lstart(e, wp);
            n = line_end(e, wp) - st;
        }
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
        E(e)->ro = wp != 0;
        return 1;
    }
    case EM_SETPASSWORDCHAR:
        E(e)->password = (char)wp;
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case EM_GETPASSWORDCHAR:
        return (BYTE)E(e)->password;
    case EM_CANUNDO:
        return E(e)->ukind != 0;
    case EM_UNDO:
    case WM_UNDO:
        return undo(h, e);
    case EM_EMPTYUNDOBUFFER:
        undo_empty(e);
        return 0;
    case EM_FMTLINES:
        if (E(e)->multi)
            fmt_lines(h, e, wp != 0);
        return wp != 0;
    case EM_SETWORDBREAKPROC:
        E(e)->wbp = (FARPROC)lp;
        if (E(e)->multi) {
            relayout(h, e);
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case EM_GETWORDBREAKPROC:
        return (LRESULT)E(e)->wbp;
    case EM_GETFIRSTVISIBLELINE:
        return E(e)->multi ? E(e)->topline : E(e)->first;
    case EM_LINESCROLL:
        /* across only with ES_AUTOHSCROLL or WS_HSCROLL (US35 on 3.1) */
        if (E(e)->multi)
            scroll_to(h, e, E(e)->topline + (int)LOWORD(lp),
                      E(e)->xoff + ((estyle(h) & (ES_AUTOHSCROLL | WS_HSCROLL)) ?
                                    (int)HIWORD(lp) * char_w(h, e) : 0),
                      TRUE, TRUE, TRUE);
        return 1;
    case WM_VSCROLL:
    case WM_HSCROLL: {
        BOOL v = msg == WM_VSCROLL;
        int n, page, cw, top, x, pos = (int)LOWORD(lp), d;
        if (!E(e)->multi)
            return 0;
        n = line_count(e);
        page = visible_lines(h, e) - 1;
        cw = char_w(h, e);
        top = E(e)->topline;
        x = E(e)->xoff;
        /* the scroll code EM_GETTHUMB (WM_USER+14, Schulman ch. 7): the
           position for its parent's bar, which Notepad takes after a
           scroll; the first line * 100 / (lines - 1) rounded (3.1:
           Notepad, 45 lines, first line 32: its thumb at 73) */
        if (wp == WM_USER + 14) {
            if (v)
                return n > 1 ? (LRESULT)(((long)top * 200 + (n - 1)) / (2L * (n - 1))) : 0;
            return widest(h, e) > 0 ? (LRESULT)(((long)x * 200 + widest(h, e)) / (2L * widest(h, e))) : 0;
        }
        if (page < 1)
            page = 1;
        /* 3.1 answers the lines asked for in the low word, TRUE in the
           high one; SB_TOP and SB_BOTTOM do nothing there (US35) */
        switch (wp) {
        case SB_LINEUP: if (v) top--; else x -= cw; break;
        case SB_LINEDOWN: if (v) top++; else x += cw; break;
        case SB_PAGEUP: if (v) top -= page; else x -= 8 * cw; break;
        case SB_PAGEDOWN: if (v) top += page; else x += 8 * cw; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            /* rounded to the nearest line (3.1: Notepad's 45 lines
               dragged, every screen a line further than truncating gave) */
            if (v)
                top = (int)(((long)pos * (n - 1) + 50) / 100);
            else
                x = (int)((long)pos * widest(h, e) / 100);
            break;
        default:
            return 0;
        }
        d = v ? top - E(e)->topline : (x - E(e)->xoff) / cw;
        /* line and page scrolls notify even when nothing moved;
           SB_THUMBPOSITION too (US6, US35) */
        scroll_to(h, e, top, x, v && wp != SB_THUMBTRACK, !v && wp != SB_THUMBTRACK,
                  wp != SB_THUMBTRACK);
        return MAKELONG(d, 1);
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
            if (n > 0x7FF0 / sizeof(int))
                return FALSE;   /* n * 2 would wrap (USER audit, sweep) */
            b = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, n * sizeof(int));
            if (!b)
                return FALSE;
            E(e)->tabs = b;
            t = (int __far *)UH_PTR(&uhObj, b);
            for (i = 0; i < n; i++)
                t[i] = ((int __far *)lp)[i];
            E(e)->ntabs = n;
        }
        relayout(h, e);
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
        if (GP_BAD(lp))
            return 0;
        if (E(e)->multi)
            *(RECT __far *)lp = E(e)->fmt;
        else {
            RECT r;
            fmt_rect(h, e, &r);
            *(RECT __far *)lp = r;
        }
        return 0;
    case EM_SETRECT:
    case EM_SETRECTNP:
        /* multi-line only (US35: a single-line control keeps its own) */
        if (E(e)->multi && !GP_BAD(lp)) {
            set_fmt(h, e, (const RECT __far *)lp);
            relayout(h, e);
            if (msg == EM_SETRECT)
                InvalidateRect(h, NULL, TRUE);
            if (E(e)->focus)
                place_caret(h, e);
        }
        return 1;
    case WM_NCCALCSIZE:
        /* a framed edit's frame is in its client area (US34); a
           multi-line one's client area is the window less its scroll
           bars (US35: 113 x 43 of 130 x 60 with both) */
        if (E(e)->framed) {
            if (E(e)->multi) {
                RECT __far *r = (RECT __far *)lp;
                if (estyle(h) & WS_VSCROLL)
                    r->right -= sysmet[SM_CXVSCROLL];
                if (estyle(h) & WS_HSCROLL)
                    r->bottom -= sysmet[SM_CYHSCROLL];
                if (r->right < r->left)
                    r->right = r->left;
                if (r->bottom < r->top)
                    r->bottom = r->top;
            }
            return 0;
        }
        break;
    case WM_NCPAINT:
        if (E(e)->framed && !(estyle(h) & (WS_VSCROLL | WS_HSCROLL)))
            return 0;
        if (E(e)->framed) {
            /* the scroll bars only: DefWindowProc would draw the border
               too, inside the client area's frame */
            PWND w = valid_wnd(h);
            DWORD st = w->style;
            LRESULT r;
            w->style &= ~WS_BORDER;
            r = DefWindowProc(h, msg, wp, lp);
            w = valid_wnd(h);
            if (w)
                w->style = (w->style & ~WS_BORDER) | (st & WS_BORDER);
            return r;
        }
        break;
    /* (US35: WM_CUT, WM_CLEAR and WM_PASTE answer 1, WM_COPY the
       characters copied) */
    case WM_CUT:
        copy_sel(h, e);
        if (!E(e)->ro)
            replace_sel(h, e, "", K_OTHER);
        return 1;
    case WM_CLEAR:
        if (!E(e)->ro)
            replace_sel(h, e, "", K_OTHER);
        return 1;
    case WM_COPY:
        return copy_sel(h, e);
    case WM_PASTE:
        if (!E(e)->ro)
            paste(h, e);
        return 1;
    }
    return DefWindowProc(h, msg, wp, lp);
}
