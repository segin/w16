/*
 * rect.c - rectangle functions and simple drawing: FillRect, FrameRect,
 * InvertRect, DrawFocusRect, DrawText, TabbedTextOut, GrayString.
 */
#include "user.h"

/* A caller's RECT is read and written through gp_copy (gp.asm), whose
   faults KERNEL recovers through USER's "__GP" table: what Windows 3.1
   does with a bad one (probe US27, Windows 3.1's results in
   tests/apps/us27ref.h) is in each function's comment. A freed or
   not-present selector (US27's F and P) faults on its first use; a RECT
   whose end is past the segment's limit (E) is read or written up to the
   limit. */
#define RSIZE sizeof(RECT)

/* a bad RECT: nothing written; the part below the limit is */
void WINAPI SetRect(RECT __far *r, int l, int t, int ri, int b)
{
    RECT n;
    n.left = l;
    n.top = t;
    n.right = ri;
    n.bottom = b;
    gp_copy(r, &n, RSIZE);
}

void WINAPI SetRectEmpty(RECT __far *r)
{
    gp_fill(r, 0, RSIZE);
}

/* either one bad: as much copied as could be (3.1: a source past the
   limit gives the destination its left and top) */
void WINAPI CopyRect(RECT __far *d, const RECT __far *s)
{
    gp_copy(d, s, RSIZE);
}

/* (3.1 returns no constant for a bad RECT: whatever AX held) */
BOOL WINAPI IsRectEmpty(const RECT __far *r)
{
    RECT a;
    if (gp_copy(&a, r, RSIZE) != RSIZE)
        return TRUE;
    return a.left >= a.right || a.top >= a.bottom;
}

BOOL WINAPI PtInRect(const RECT __far *r, POINT p)
{
    RECT a;
    if (gp_copy(&a, r, RSIZE) != RSIZE)
        return FALSE;
    return p.x >= a.left && p.x < a.right && p.y >= a.top && p.y < a.bottom;
}

/* the fields that could be read are changed (3.1: left and top of a
   RECT at a segment's edge) */
void WINAPI OffsetRect(RECT __far *r, int dx, int dy)
{
    RECT a;
    WORD n = gp_copy(&a, r, RSIZE) & ~1;
    a.left += dx;
    a.right += dx;
    a.top += dy;
    a.bottom += dy;
    gp_copy(r, &a, n);
}

void WINAPI InflateRect(RECT __far *r, int dx, int dy)
{
    RECT a;
    WORD n = gp_copy(&a, r, RSIZE) & ~1;
    a.left -= dx;
    a.right += dx;
    a.top -= dy;
    a.bottom += dy;
    gp_copy(r, &a, n);
}

/* a bad source: d made empty, FALSE (3.1) */
BOOL WINAPI IntersectRect(RECT __far *d, const RECT __far *a, const RECT __far *b)
{
    RECT r, x, y;
    if (gp_copy(&x, a, RSIZE) != RSIZE || gp_copy(&y, b, RSIZE) != RSIZE) {
        gp_fill(d, 0, RSIZE);
        return FALSE;
    }
    r.left = x.left > y.left ? x.left : y.left;
    r.top = x.top > y.top ? x.top : y.top;
    r.right = x.right < y.right ? x.right : y.right;
    r.bottom = x.bottom < y.bottom ? x.bottom : y.bottom;
    if (r.left >= r.right || r.top >= r.bottom) {
        gp_fill(d, 0, RSIZE);
        return FALSE;
    }
    return gp_copy(d, &r, RSIZE) == RSIZE;
}

/* a bad source: d left as it was, TRUE (3.1) */
BOOL WINAPI UnionRect(RECT __far *d, const RECT __far *a, const RECT __far *b)
{
    RECT x, y, r;
    BOOL ex, ey;
    if (gp_copy(&x, a, RSIZE) != RSIZE || gp_copy(&y, b, RSIZE) != RSIZE)
        return TRUE;
    ex = x.left >= x.right || x.top >= x.bottom;
    ey = y.left >= y.right || y.top >= y.bottom;
    if (ex && ey) {
        gp_fill(d, 0, RSIZE);
        return FALSE;
    }
    if (ex)
        r = y;
    else if (ey)
        r = x;
    else {
        r.left = x.left < y.left ? x.left : y.left;
        r.top = x.top < y.top ? x.top : y.top;
        r.right = x.right > y.right ? x.right : y.right;
        r.bottom = x.bottom > y.bottom ? x.bottom : y.bottom;
    }
    gp_copy(d, &r, RSIZE);
    return TRUE;
}

/* a bad source: d left as it was, TRUE (3.1) */
BOOL WINAPI SubtractRect(RECT __far *d, const RECT __far *a, const RECT __far *b)
{
    RECT i, x, y, r;
    if (gp_copy(&x, a, RSIZE) != RSIZE || gp_copy(&y, b, RSIZE) != RSIZE)
        return TRUE;
    r = x;
    if (IntersectRect(&i, &x, &y)) {
        if (i.top == x.top && i.bottom == x.bottom) {
            if (i.left == x.left)
                r.left = i.right;
            else if (i.right == x.right)
                r.right = i.left;
        } else if (i.left == x.left && i.right == x.right) {
            if (i.top == x.top)
                r.top = i.bottom;
            else if (i.bottom == x.bottom)
                r.bottom = i.top;
        }
    }
    gp_copy(d, &r, RSIZE);
    return !(r.left >= r.right || r.top >= r.bottom);
}

/* either one bad: FALSE (3.1) */
BOOL WINAPI EqualRect(const RECT __far *a, const RECT __far *b)
{
    RECT x, y;
    if (gp_copy(&x, a, RSIZE) != RSIZE || gp_copy(&y, b, RSIZE) != RSIZE)
        return FALSE;
    return x.left == y.left && x.top == y.top && x.right == y.right && x.bottom == y.bottom;
}

/* brushes 1..COLOR+1 mean system colour brushes (3.1 SDK, hbrBackground) */
HBRUSH sys_brush(HBRUSH b)
{
    if ((WORD)b && (WORD)b <= COLOR_BTNHIGHLIGHT + 1)
        return sysbrush[(WORD)b - 1];
    return b;
}

/* a bad RECT: 0, nothing drawn (3.1). The brush is a brush: a system
   colour's number + 1, which a class's hbrBackground may be, is no brush
   here, and nothing is drawn (US33 on 3.1: an owner that fills its
   selected items with (HBRUSH)(COLOR_HIGHLIGHT + 1) leaves them as they
   were, white on the list's background; w16 filled them with the
   highlight) */
int WINAPI FillRect(HDC hdc, const RECT __far *rp, HBRUSH b)
{
    RECT a, *r = &a;
    HBRUSH old;
    if (gp_copy(&a, rp, RSIZE) != RSIZE)
        return 0;
    if ((old = SelectObject(hdc, b)) == 0)
        return 0;
    PatBlt(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top, PATCOPY);
    SelectObject(hdc, old);
    return 1;
}

int WINAPI FrameRect(HDC hdc, const RECT __far *rp, HBRUSH b)
{
    RECT a, *r = &a;
    HBRUSH old;
    int w, h;
    if (gp_copy(&a, rp, RSIZE) != RSIZE)
        return 0;
    if ((old = SelectObject(hdc, b)) == 0)     /* (as FillRect) */
        return 0;
    w = r->right - r->left, h = r->bottom - r->top;
    if (w > 0 && h > 0) {
        PatBlt(hdc, r->left, r->top, w, 1, PATCOPY);
        PatBlt(hdc, r->left, r->bottom - 1, w, 1, PATCOPY);
        PatBlt(hdc, r->left, r->top, 1, h, PATCOPY);
        PatBlt(hdc, r->right - 1, r->top, 1, h, PATCOPY);
    }
    SelectObject(hdc, old);
    return 1;
}

void WINAPI InvertRect(HDC hdc, const RECT __far *rp)
{
    RECT a, *r = &a;
    if (gp_copy(&a, rp, RSIZE) != RSIZE)
        return;
    PatBlt(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top, DSTINVERT);
}

void WINAPI DrawFocusRect(HDC hdc, const RECT __far *rp)
{
    /* the dots on the screen's squares where x + y is even (US4): the
       gray pattern the other way round from the one GrayString uses,
       patterns being aligned to the screen */
    static HBRUSH dots;
    HBRUSH old;
    RECT a, *r = &a;
    int w, h;
    DWORD org, oldorg;
    if (gp_copy(&a, rp, RSIZE) != RSIZE)
        return;
    w = r->right - r->left, h = r->bottom - r->top;
    /* an empty rectangle: nothing (US33 on 3.1: the focus rectangle of an
       empty list box of variable heights, 0 high) */
    if (w <= 0 || h <= 0)
        return;
    if (!dots) {
        static WORD bits[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
        HBITMAP b = CreateBitmap(8, 8, 1, 1, bits);
        dots = CreatePatternBrush(b);
        DeleteObject(b);
        G_MAKEOBJECTPRIVATE(dots, TRUE);
    }
    /* aligned to the DC, whatever DC it was last used in (the brush kept
       the origin of its first realization: US32's focus rectangles came
       a pixel off 3.1's in one run and not in another): on 3.1 the dots
       in the background colour lie where x + y is odd in the DC's own
       coordinates, wherever the DC is on the screen (US33 dfr: the same
       rectangle in child windows an odd and an even number of pixels
       across; w16 had them where x + y is even) */
    org = GetDCOrg(hdc);
    UnrealizeObject(dots);
    oldorg = SetBrushOrg(hdc, ((LOWORD(org) + HIWORD(org)) & 1) ^ 1, 0);
    old = SelectObject(hdc, dots);
    /* the corners left out (US4) */
    PatBlt(hdc, r->left + 1, r->top, w - 2, 1, PATINVERT);
    PatBlt(hdc, r->left + 1, r->bottom - 1, w - 2, 1, PATINVERT);
    PatBlt(hdc, r->left, r->top + 1, 1, h - 2, PATINVERT);
    PatBlt(hdc, r->right - 1, r->top + 1, 1, h - 2, PATINVERT);
    SelectObject(hdc, old);
    SetBrushOrg(hdc, LOWORD(oldorg), HIWORD(oldorg));   /* the caller's, back */
}

/* ---- DrawText ---- */
/* A line with a prefix (&) or a tab is measured and drawn in pieces.
   GetTextExtent counts a synthesized bold font's overhang once per call,
   so each piece advances by its extent less the overhang and the line's
   width counts it once: on 3.1 "Run &Minimized" in a bold MS Sans Serif
   (Program Manager's Run box; COMMDLG's "File &Name:") is as wide as
   without the prefix, w16 put the overhang between the pieces (boot test
   runbrowse) */
static int text_width(HDC hdc, LPCSTR s, int n, BOOL prefix, BOOL tabs, int tabw)
{
    int w = 0, i, start = 0, oh;
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    oh = tm.tmOverhang;
    for (i = 0; i <= n; i++) {
        if (i == n || (prefix && s[i] == '&') || (tabs && s[i] == '\t')) {
            if (i > start)
                w += LOWORD(GetTextExtent(hdc, s + start, i - start)) - oh;
            if (i < n && tabs && s[i] == '\t')
                w = (w / tabw + 1) * tabw;
            if (i < n && prefix && s[i] == '&' && i + 1 < n && s[i + 1] == '&') {
                w += LOWORD(GetTextExtent(hdc, "&", 1)) - oh;
                i++;
            }
            start = i + 1;
        }
    }
    return w > 0 ? w + oh : w;
}

static void draw_line(HDC hdc, int x, int y, LPCSTR s, int n, BOOL prefix, BOOL tabs, int tabw)
{
    int i, start = 0, cx = x, ul = -1;
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    for (i = 0; i <= n; i++) {
        if (i == n || (prefix && s[i] == '&') || (tabs && s[i] == '\t')) {
            if (i > start) {
                TextOut(hdc, cx, y, s + start, i - start);
                cx += LOWORD(GetTextExtent(hdc, s + start, i - start)) - tm.tmOverhang;
            }
            if (i < n && tabs && s[i] == '\t')
                cx = x + ((cx - x) / tabw + 1) * tabw;
            else if (i < n && prefix && s[i] == '&') {
                if (i + 1 < n && s[i + 1] == '&') {
                    TextOut(hdc, cx, y, "&", 1);
                    cx += LOWORD(GetTextExtent(hdc, "&", 1)) - tm.tmOverhang;
                    i++;
                } else if (i + 1 < n && ul < 0) {
                    ul = cx;
                    /* underline the next character */
                    {
                        int cw = LOWORD(GetTextExtent(hdc, s + i + 1, 1));
                        HPEN pen = CreatePen(PS_SOLID, 1, GetTextColor(hdc));
                        HPEN old = SelectObject(hdc, pen);
                        MoveTo(hdc, cx, y + tm.tmAscent + 1);
                        LineTo(hdc, cx + cw, y + tm.tmAscent + 1);
                        SelectObject(hdc, old);
                        DeleteObject(pen);
                    }
                }
            }
            start = i + 1;
        }
    }
}

/* DrawText, laid out as Windows 3.1 does (probe US3, tests/apps/us3.c):
   - centring rounds down (an odd remainder goes right of the text, also
     when the text is wider than the rectangle);
   - DT_WORDBREAK breaks only at blanks: a word wider than the rectangle
     stays whole on its line (clipped) and widens DT_CALCRECT's result; a
     line ending at a break is measured with the blank it broke at, which
     moves centred and right-aligned lines by that blank;
   - with DT_SINGLELINE, DT_VCENTER/DT_BOTTOM move the text down by the
     difference of heights (halved toward zero for DT_VCENTER) and the
     value returned, and DT_CALCRECT's height, include that offset. */
static int floor_half(int v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

/* a bad string or RECT: 0, nothing drawn, the RECT as it was (3.1) */
int WINAPI DrawText(HDC hdc, LPCSTR s, int n, RECT __far *r, UINT fmt)
{
    TEXTMETRIC tm;
    int lh, y, total = 0, tabw;
    BOOL prefix = !(fmt & DT_NOPREFIX), tabs = (fmt & DT_EXPANDTABS) != 0;
    int width;
    if (GP_BAD(s) || GP_BAD(r))
        return 0;
    width = r->right - r->left;
    if (n < 0)
        n = lstr_len(s);
    GetTextMetrics(hdc, &tm);
    lh = tm.tmHeight + ((fmt & DT_EXTERNALLEADING) ? tm.tmExternalLeading : 0);
    tabw = tm.tmAveCharWidth * ((fmt & DT_TABSTOP) ? HIBYTE(fmt) : 8);
    if (!tabw)
        tabw = 8 * tm.tmAveCharWidth;
    y = r->top;
    if (fmt & DT_SINGLELINE) {
        int w = text_width(hdc, s, n, prefix, tabs, tabw), x, dy = 0;
        if (fmt & DT_VCENTER)
            dy = (r->bottom - r->top - lh) / 2;
        else if (fmt & DT_BOTTOM)
            dy = r->bottom - r->top - lh;
        y += dy;
        x = (fmt & DT_CENTER) ? r->left + floor_half(width - w) :
            (fmt & DT_RIGHT) ? r->right - w : r->left;
        if (fmt & DT_CALCRECT) {
            r->right = r->left + w;
            r->bottom = r->top + dy + lh;
            return dy + lh;
        }
        if (!(fmt & DT_NOCLIP)) {
            SaveDC(hdc);
            IntersectClipRect(hdc, r->left, r->top, r->right, r->bottom);
        }
        draw_line(hdc, x, y, s, n, prefix, tabs, tabw);
        if (!(fmt & DT_NOCLIP))
            RestoreDC(hdc, -1);
        return dy + lh;
    }
    /* several lines, at CR/LF and, with DT_WORDBREAK, at blanks */
    {
        int i = 0, maxw = 0;
        if (!(fmt & DT_NOCLIP) && !(fmt & DT_CALCRECT)) {
            SaveDC(hdc);
            IntersectClipRect(hdc, r->left, r->top, r->right, r->bottom);
        }
        while (i < n) {
            int start = i, end = i, w, mw, x;
            BOOL broke = FALSE;
            while (end < n && s[end] != '\r' && s[end] != '\n')
                end++;
            if (fmt & DT_WORDBREAK) {
                /* the longest run of whole words that fits */
                /* when even the first word does not fit, the line ends
                   after it and the next one starts with the blank (US3:
                   "Clipboard Viewer" in 40 pixels of the system font
                   gives three lines) */
                int k, fit = -1, first = -1;
                for (k = start; k <= end; k++) {
                    if (k == end || s[k] == ' ') {
                        if (first < 0)
                            first = k;
                        if (text_width(hdc, s + start, k - start, prefix, tabs, tabw) <= width)
                            fit = k;
                        else
                            break;
                    }
                }
                /* ... but only when drawing centred or right-aligned text:
                   DT_CALCRECT and left-aligned text break there as at any
                   blank (US3) */
                if (fit < 0 && !(fmt & DT_CALCRECT) && (fmt & (DT_CENTER | DT_RIGHT)))
                    end = first;
                else if (fit < 0 && first < end) {
                    end = first;
                    broke = TRUE;
                } else if (fit >= 0 && fit < end) {
                    end = fit;
                    broke = TRUE;
                }
            }
            w = text_width(hdc, s + start, end - start, prefix, tabs, tabw);
            /* measured with the blank it broke at */
            mw = broke ? text_width(hdc, s + start, end - start + 1, prefix, tabs, tabw) : w;
            /* centred and right-aligned lines count the blank only while
               it still fits (US3: "a b c d e f g h" in 75 pixels) */
            if (mw > width && (fmt & (DT_CENTER | DT_RIGHT)))
                mw = w;
            if (mw > maxw)
                maxw = mw;
            x = (fmt & DT_CENTER) ? r->left + floor_half(width - mw) :
                (fmt & DT_RIGHT) ? r->right - mw : r->left;
            if (!(fmt & DT_CALCRECT))
                draw_line(hdc, x, y, s + start, end - start, prefix, tabs, tabw);
            y += lh;
            total += lh;
            i = end;
            if (broke)
                i++;
            else {
                if (i < n && s[i] == '\r')
                    i++;
                if (i < n && s[i] == '\n')
                    i++;
            }
        }
        if (fmt & DT_CALCRECT) {
            r->right = r->left + maxw;
            r->bottom = r->top + total;
        } else if (!(fmt & DT_NOCLIP))
            RestoreDC(hdc, -1);
    }
    return total;
}

/* the default tab: 8 of the font's average characters, the dialog base
   unit's average of the 52 letters (US33 tto on 3.1: 56 for a bold MS
   Sans Serif 8, whose tmAveCharWidth is 6 and "0" 8 wide; 64 for the
   system font, 48 for ANSI_VAR_FONT). w16 took 8 widths of "0", 64 */
static int default_tab(HDC hdc)
{
    static char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return 8 * ((LOWORD(GetTextExtent(hdc, letters, 52)) / 26 + 1) / 2);
}

/* the next stop every tw from the origin, x past it; a negative tw
   divides as an unsigned number (US33: one stop of -30 puts any text
   after its tab at -30, the next tab at -60) */
static int next_stop(int x, int tw)
{
    if (tw < 0)
        return (int)((WORD)x / (WORD)tw + 1) * tw;
    return (x / tw + 1) * tw;
}

/* a bad string or tab array: 0, nothing drawn (3.1) */
LONG WINAPI TabbedTextOut(HDC hdc, int x, int y, LPCSTR s, int n, int ntabs,
                          int __far *tabs, int origin)
{
    int i, start = 0, cx = x, k = 0, h, deftab;
    if (GP_BAD(s) || GP_BAD(tabs))
        return 0;
    h = HIWORD(GetTextExtent(hdc, "A", 1));
    deftab = default_tab(hdc);
    for (i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t') {
            if (i > start) {
                TextOut(hdc, cx, y, s + start, i - start);
                cx += LOWORD(GetTextExtent(hdc, s + start, i - start));
            }
            if (i < n) {
                int stop;
                if (ntabs > 1 && tabs) {
                    while (k < ntabs && origin + tabs[k] <= cx)
                        k++;
                    stop = k < ntabs ? origin + tabs[k] :
                           origin + next_stop(cx - origin, deftab);
                } else {
                    int tw = (ntabs == 1 && tabs) ? tabs[0] : deftab;
                    stop = origin + (tw ? next_stop(cx - origin, tw) : cx - origin);
                }
                cx = stop;
            }
            start = i + 1;
        }
    }
    return MAKELONG(cx - x, h);
}

/* a bad string or tab array: 0 (3.1) */
DWORD WINAPI GetTabbedTextExtent(HDC hdc, LPCSTR s, int n, int ntabs, int __far *tabs)
{
    int i, start = 0, cx = 0, k = 0, h, deftab;
    if (GP_BAD(s) || GP_BAD(tabs))
        return 0;
    h = HIWORD(GetTextExtent(hdc, "A", 1));
    deftab = default_tab(hdc);
    for (i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t') {
            if (i > start)
                cx += LOWORD(GetTextExtent(hdc, s + start, i - start));
            if (i < n) {
                if (ntabs > 1 && tabs) {
                    while (k < ntabs && tabs[k] <= cx)
                        k++;
                    cx = k < ntabs ? tabs[k] : next_stop(cx, deftab);
                } else {
                    int tw = (ntabs == 1 && tabs) ? tabs[0] : deftab;
                    cx = tw ? next_stop(cx, tw) : cx;
                }
            }
            start = i + 1;
        }
    }
    return MAKELONG(cx, h);
}

/* GrayString: the text drawn through a mono bitmap with the gray pattern
   (3.1 SDK); a NULL output function means TextOut of a string */
BOOL WINAPI GrayString(HDC hdc, HBRUSH br, GRAYSTRINGPROC f, LPARAM data, int n,
                       int x, int y, int cx, int cy)
{
    HDC mem;
    HBITMAP bm, old;
    HBRUSH ob;
    COLORREF tc, bc;
    /* a bad string (no output function): 0 (3.1: its GDI call faults) */
    if (!f && GP_BAD(data))
        return FALSE;
    if (!f && !n)
        n = lstr_len((LPCSTR)data);
    if (!cx || !cy) {
        DWORD e = GetTextExtent(hdc, (LPCSTR)data, n);
        cx = LOWORD(e);
        cy = HIWORD(e);
    }
    mem = CreateCompatibleDC(hdc);
    bm = CreateBitmap(cx, cy, 1, 1, NULL);
    old = SelectObject(mem, bm);
    PatBlt(mem, 0, 0, cx, cy, WHITENESS);
    { HFONT f0 = SelectObject(hdc, GetStockObject(SYSTEM_FONT)); SelectObject(hdc, f0); SelectObject(mem, f0); }
    SetTextColor(mem, RGB(0, 0, 0));
    SetBkColor(mem, RGB(255, 255, 255));
    if (f)
        ((BOOL (__far __pascal *)(HDC, LPARAM, int))f)(mem, data, n);
    else
        TextOut(mem, 0, 0, (LPCSTR)data, n);
    /* the pattern kept aligned with the screen: text pixels stay where
       x + y is even (US4's disabled buttons) */
    {
        DWORD org = GetDCOrg(hdc);
        POINT pt;
        pt.x = x;
        pt.y = y;
        LPtoDP(hdc, &pt, 1);
        UnrealizeObject(hbrGray);
        SetBrushOrg(mem, (LOWORD(org) + HIWORD(org) + pt.x + pt.y) & 1, 0);
    }
    ob = SelectObject(mem, hbrGray);
    PatBlt(mem, 0, 0, cx, cy, 0x00FA0089L);     /* DPo: gray the text */
    SelectObject(mem, ob);
    ob = SelectObject(hdc, br ? sys_brush(br) : GetStockObject(BLACK_BRUSH));
    tc = SetTextColor(hdc, RGB(0, 0, 0));
    bc = SetBkColor(hdc, RGB(255, 255, 255));
    BitBlt(hdc, x, y, cx, cy, mem, 0, 0, 0x00B8074AL);  /* PSDPxax */
    SetTextColor(hdc, tc);
    SetBkColor(hdc, bc);
    SelectObject(hdc, ob);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    return TRUE;
}
