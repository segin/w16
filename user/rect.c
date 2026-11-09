/*
 * rect.c - rectangle functions and simple drawing: FillRect, FrameRect,
 * InvertRect, DrawFocusRect, DrawText, TabbedTextOut, GrayString.
 */
#include "user.h"

void WINAPI SetRect(RECT __far *r, int l, int t, int ri, int b)
{
    r->left = l;
    r->top = t;
    r->right = ri;
    r->bottom = b;
}

void WINAPI SetRectEmpty(RECT __far *r)
{
    r->left = r->top = r->right = r->bottom = 0;
}

void WINAPI CopyRect(RECT __far *d, const RECT __far *s)
{
    *d = *s;
}

BOOL WINAPI IsRectEmpty(const RECT __far *r)
{
    return r->left >= r->right || r->top >= r->bottom;
}

BOOL WINAPI PtInRect(const RECT __far *r, POINT p)
{
    return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom;
}

void WINAPI OffsetRect(RECT __far *r, int dx, int dy)
{
    r->left += dx;
    r->right += dx;
    r->top += dy;
    r->bottom += dy;
}

void WINAPI InflateRect(RECT __far *r, int dx, int dy)
{
    r->left -= dx;
    r->right += dx;
    r->top -= dy;
    r->bottom += dy;
}

BOOL WINAPI IntersectRect(RECT __far *d, const RECT __far *a, const RECT __far *b)
{
    RECT r;
    r.left = a->left > b->left ? a->left : b->left;
    r.top = a->top > b->top ? a->top : b->top;
    r.right = a->right < b->right ? a->right : b->right;
    r.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (r.left >= r.right || r.top >= r.bottom) {
        SetRectEmpty(d);
        return FALSE;
    }
    *d = r;
    return TRUE;
}

BOOL WINAPI UnionRect(RECT __far *d, const RECT __far *a, const RECT __far *b)
{
    if (IsRectEmpty(a)) {
        if (IsRectEmpty(b)) {
            SetRectEmpty(d);
            return FALSE;
        }
        *d = *b;
        return TRUE;
    }
    if (IsRectEmpty(b)) {
        *d = *a;
        return TRUE;
    }
    {
        RECT r;
        r.left = a->left < b->left ? a->left : b->left;
        r.top = a->top < b->top ? a->top : b->top;
        r.right = a->right > b->right ? a->right : b->right;
        r.bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
        *d = r;
    }
    return TRUE;
}

BOOL WINAPI SubtractRect(RECT __far *d, const RECT __far *a, const RECT __far *b)
{
    RECT i;
    *d = *a;
    if (!IntersectRect(&i, a, b))
        return !IsRectEmpty(d);
    if (i.top == a->top && i.bottom == a->bottom) {
        if (i.left == a->left)
            d->left = i.right;
        else if (i.right == a->right)
            d->right = i.left;
    } else if (i.left == a->left && i.right == a->right) {
        if (i.top == a->top)
            d->top = i.bottom;
        else if (i.bottom == a->bottom)
            d->bottom = i.top;
    }
    return !IsRectEmpty(d);
}

BOOL WINAPI EqualRect(const RECT __far *a, const RECT __far *b)
{
    return a->left == b->left && a->top == b->top && a->right == b->right &&
           a->bottom == b->bottom;
}

/* brushes 1..COLOR+1 mean system colour brushes (3.1 SDK, hbrBackground) */
HBRUSH sys_brush(HBRUSH b)
{
    if ((WORD)b && (WORD)b <= COLOR_BTNHIGHLIGHT + 1)
        return sysbrush[(WORD)b - 1];
    return b;
}

int WINAPI FillRect(HDC hdc, const RECT __far *r, HBRUSH b)
{
    HBRUSH old = SelectObject(hdc, sys_brush(b));
    PatBlt(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top, PATCOPY);
    SelectObject(hdc, old);
    return 1;
}

int WINAPI FrameRect(HDC hdc, const RECT __far *r, HBRUSH b)
{
    HBRUSH old = SelectObject(hdc, sys_brush(b));
    int w = r->right - r->left, h = r->bottom - r->top;
    if (w > 0 && h > 0) {
        PatBlt(hdc, r->left, r->top, w, 1, PATCOPY);
        PatBlt(hdc, r->left, r->bottom - 1, w, 1, PATCOPY);
        PatBlt(hdc, r->left, r->top, 1, h, PATCOPY);
        PatBlt(hdc, r->right - 1, r->top, 1, h, PATCOPY);
    }
    SelectObject(hdc, old);
    return 1;
}

void WINAPI InvertRect(HDC hdc, const RECT __far *r)
{
    PatBlt(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top, DSTINVERT);
}

void WINAPI DrawFocusRect(HDC hdc, const RECT __far *r)
{
    /* the dots on the screen's squares where x + y is even (US4): the
       gray pattern the other way round from the one GrayString uses,
       patterns being aligned to the screen */
    static HBRUSH dots;
    HBRUSH old;
    int w = r->right - r->left, h = r->bottom - r->top;
    if (!dots) {
        static WORD bits[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
        HBITMAP b = CreateBitmap(8, 8, 1, 1, bits);
        dots = CreatePatternBrush(b);
        DeleteObject(b);
        G_MAKEOBJECTPRIVATE(dots, TRUE);
    }
    old = SelectObject(hdc, dots);
    /* the corners left out (US4) */
    PatBlt(hdc, r->left + 1, r->top, w - 2, 1, PATINVERT);
    PatBlt(hdc, r->left + 1, r->bottom - 1, w - 2, 1, PATINVERT);
    PatBlt(hdc, r->left, r->top + 1, 1, h - 2, PATINVERT);
    PatBlt(hdc, r->right - 1, r->top + 1, 1, h - 2, PATINVERT);
    SelectObject(hdc, old);
}

/* ---- DrawText ---- */
static int text_width(HDC hdc, LPCSTR s, int n, BOOL prefix, BOOL tabs, int tabw)
{
    int w = 0, i, start = 0;
    for (i = 0; i <= n; i++) {
        if (i == n || (prefix && s[i] == '&') || (tabs && s[i] == '\t')) {
            if (i > start)
                w += LOWORD(GetTextExtent(hdc, s + start, i - start));
            if (i < n && tabs && s[i] == '\t')
                w = (w / tabw + 1) * tabw;
            if (i < n && prefix && s[i] == '&' && i + 1 < n && s[i + 1] == '&') {
                w += LOWORD(GetTextExtent(hdc, "&", 1));
                i++;
            }
            start = i + 1;
        }
    }
    return w;
}

static void draw_line(HDC hdc, int x, int y, LPCSTR s, int n, BOOL prefix, BOOL tabs, int tabw)
{
    int i, start = 0, cx = x, ul = -1;
    TEXTMETRIC tm;
    for (i = 0; i <= n; i++) {
        if (i == n || (prefix && s[i] == '&') || (tabs && s[i] == '\t')) {
            if (i > start) {
                TextOut(hdc, cx, y, s + start, i - start);
                cx += LOWORD(GetTextExtent(hdc, s + start, i - start));
            }
            if (i < n && tabs && s[i] == '\t')
                cx = x + ((cx - x) / tabw + 1) * tabw;
            else if (i < n && prefix && s[i] == '&') {
                if (i + 1 < n && s[i + 1] == '&') {
                    TextOut(hdc, cx, y, "&", 1);
                    cx += LOWORD(GetTextExtent(hdc, "&", 1));
                    i++;
                } else if (i + 1 < n && ul < 0) {
                    ul = cx;
                    /* underline the next character */
                    GetTextMetrics(hdc, &tm);
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

int WINAPI DrawText(HDC hdc, LPCSTR s, int n, RECT __far *r, UINT fmt)
{
    TEXTMETRIC tm;
    int lh, y, total = 0, tabw;
    BOOL prefix = !(fmt & DT_NOPREFIX), tabs = (fmt & DT_EXPANDTABS) != 0;
    int width = r->right - r->left;
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

LONG WINAPI TabbedTextOut(HDC hdc, int x, int y, LPCSTR s, int n, int ntabs,
                          int __far *tabs, int origin)
{
    int i, start = 0, cx = x, k = 0, h = HIWORD(GetTextExtent(hdc, "A", 1));
    int deftab = 8 * LOWORD(GetTextExtent(hdc, "0", 1));
    for (i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t') {
            if (i > start) {
                TextOut(hdc, cx, y, s + start, i - start);
                cx += LOWORD(GetTextExtent(hdc, s + start, i - start));
            }
            if (i < n) {
                int stop;
                if (ntabs > 0 && tabs) {
                    while (k < ntabs && origin + tabs[k] <= cx)
                        k++;
                    stop = k < ntabs ? origin + tabs[k] :
                           origin + ((cx - origin) / deftab + 1) * deftab;
                } else {
                    int tw = (ntabs == 1 && tabs) ? tabs[0] : deftab;
                    stop = origin + ((cx - origin) / tw + 1) * tw;
                }
                cx = stop;
            }
            start = i + 1;
        }
    }
    return MAKELONG(cx - x, h);
}

DWORD WINAPI GetTabbedTextExtent(HDC hdc, LPCSTR s, int n, int ntabs, int __far *tabs)
{
    int i, start = 0, cx = 0, k = 0, h = HIWORD(GetTextExtent(hdc, "A", 1));
    int deftab = 8 * LOWORD(GetTextExtent(hdc, "0", 1));
    for (i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t') {
            if (i > start)
                cx += LOWORD(GetTextExtent(hdc, s + start, i - start));
            if (i < n) {
                if (ntabs > 1 && tabs) {
                    while (k < ntabs && tabs[k] <= cx)
                        k++;
                    cx = k < ntabs ? tabs[k] : (cx / deftab + 1) * deftab;
                } else {
                    int tw = (ntabs == 1 && tabs) ? tabs[0] : deftab;
                    cx = (cx / tw + 1) * tw;
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
