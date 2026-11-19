/*
 * gd5.c - the GDI audit's probe (docs/notes/gdi-audit-triage.md): what
 * Windows 3.1's GDI does where w16's was found wrong or doubtful, and the
 * regressions of the fixes. Most of it draws into memory bitmaps and reads
 * the result back (GetPixel, GetBitmapBits), so it compares pixels without
 * screenshots; a few parts use a window of its own (clip regions on a
 * window DC). Run as the shell of Windows 3.1 with Microsoft's GDI
 * (tests/gdiaudit.sh -r, whose logs tools/obsref.py turns into gd5ref.h)
 * and with w16's (the boot-test case gdiaudit). The riskiest parts (huge
 * point counts, a removed font still selected, GetDCState's handle) run
 * last. Values that depend on w16's own unverified shapes (ellipse
 * outlines) are kept out: only shapes whose result does not depend on
 * the outline's exact pixels are looked at.
 */
#include <windows.h>

#define TK_REF "gd5ref.h"
#include "tklog.h"

extern WORD __cdecl __far get_ahincr(void);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

typedef HANDLE (FAR PASCAL *PGETDCSTATE)(HDC);
typedef HRGN (FAR PASCAL *PGETCLIPRGN)(HDC);
static PGETDCSTATE getdcstate;
static PGETCLIPRGN getcliprgn;
static HINSTANCE hinst;
static HDC hscr;

/* ---- helpers: a monochrome memory bitmap to draw on ---- */

static HDC mdc;
static HBITMAP mbm, mold;
static int mw, mh;

static void mono_begin(int w, int h)
{
    mw = w;
    mh = h;
    mdc = CreateCompatibleDC(NULL);
    mbm = CreateBitmap(w, h, 1, 1, NULL);
    mold = SelectObject(mdc, mbm);
    PatBlt(mdc, 0, 0, w, h, WHITENESS);
}

static void mono_end(void)
{
    SelectObject(mdc, mold);
    DeleteDC(mdc);
    DeleteObject(mbm);
}

/* a hash of the bitmap's bits (at most 64 x 64) */
static WORD mono_hash(void)
{
    static BYTE buf[8 * 64];
    WORD h = 0, i, n = (WORD)(((mw + 15) / 16) * 2 * mh);
    if (n > sizeof buf)
        n = sizeof buf;
    GetBitmapBits(mbm, n, buf);
    for (i = 0; i < n; i++)
        h = h * 31 + buf[i];
    return h;
}

/* the black pixels of [x0, x1) x [y0, y1) */
static WORD black_in(int x0, int y0, int x1, int y1)
{
    WORD n = 0;
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += GetPixel(mdc, x, y) == 0;
    return n;
}

static void obs_dw(const char *name, DWORD v)
{
    char n[64];
    int i = 0;
    while (*name && i < 56)
        n[i++] = *name++;
    n[i] = '.'; n[i + 1] = 'l'; n[i + 2] = 0;
    obs(n, LOWORD(v));
    n[i + 1] = 'h';
    obs(n, HIWORD(v));
}

static void obs_rect(const char *name, RECT *r)
{
    char n[64];
    int i = 0;
    while (*name && i < 56)
        n[i++] = *name++;
    n[i] = '.'; n[i + 2] = 0;
    n[i + 1] = 'l'; obs(n, r->left);
    n[i + 1] = 't'; obs(n, r->top);
    n[i + 1] = 'r'; obs(n, r->right);
    n[i + 1] = 'b'; obs(n, r->bottom);
}

static WORD str_hash(const char *s)
{
    WORD h = 0;
    while (*s)
        h = h * 31 + (BYTE)*s++;
    return h;
}

static void fill(void FAR *p, BYTE v, WORD n)
{
    while (n--)
        ((BYTE FAR *)p)[n] = v;
}

/* ---- bitmaps (C4, H9) ---- */

static void try_bitmaps(const int (*t)[4], int n, int first)
{
    int i;
    for (i = 0; i < n; i++) {
        HBITMAP h;
        BITMAP bm;
        out("GD5 CreateBitmap ");
        outhex(first + i);
        out("\r\n");
        h = CreateBitmap(t[i][0], t[i][1], (BYTE)t[i][2], (BYTE)t[i][3], NULL);
        obsn("CreateBitmap ok", first + i, h != 0);
        if (h) {
            GetObject(h, sizeof bm, &bm);
            obsn("CreateBitmap bmWidthBytes", first + i, bm.bmWidthBytes);
            DeleteObject(h);
        }
    }
}

/* scan lines of 64K and more (C4): last, as 3.1 may not survive them */
static void sec_bigbitmaps(void)
{
    static const int t[][4] = {
        { 32767, 1, 1, 16 },            /* one line of all planes: 65534 bytes */
        { 32767, 1, 4, 4 },             /* 65536 */
        { 32767, 2, 2, 8 },             /* 65536 */
        { 16384, 1, 1, 32 },            /* 65536 */
        { 32767, 1, 3, 8 },             /* 98304 */
    };
    try_bitmaps(t, sizeof t / sizeof t[0], 0x10);
}

static void sec_bitmaps(void)
{
    static const int t[][4] = {
        { 30000, 3, 1, 1 },             /* 3750 bytes a line */
        { 8000, 10, 1, 8 },             /* 8000 */
        { 32760, 1, 1, 16 },            /* 65520 */
    };
    try_bitmaps(t, sizeof t / sizeof t[0], 0);
    /* a bitmap over 64K made with its bits (8000 x 100, 1000 bytes a line:
       lines cross the 64K boundaries), read back */
    {
        DWORD n = 100000L, k;
        HGLOBAL g1 = GlobalAlloc(GMEM_MOVEABLE, n), g2 = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n);
        BYTE FAR *p1 = g1 ? GlobalLock(g1) : 0, FAR *p2 = g2 ? GlobalLock(g2) : 0;
        WORD inc = get_ahincr(), diff = 0;
        HBITMAP h;
        LONG got;
        if (p1 && p2) {
            for (k = 0; k < n; k++)
                *(BYTE FAR *)MAKELP(SELECTOROF(p1) + HIWORD(k) * inc, LOWORD(k)) =
                    (BYTE)(k * 7 + (k >> 9));
            h = CreateBitmap(8000, 100, 1, 1, p1);
            out("GD5 huge read back\r\n");
            obs("huge CreateBitmap ok", h != 0);
            if (h) {
                got = GetBitmapBits(h, n, p2);
                obs_dw("huge GetBitmapBits", got);
                for (k = 0; k < n; k++)
                    if (*(BYTE FAR *)MAKELP(SELECTOROF(p1) + HIWORD(k) * inc, LOWORD(k)) !=
                        *(BYTE FAR *)MAKELP(SELECTOROF(p2) + HIWORD(k) * inc, LOWORD(k)))
                        diff++;
                obs("huge bits differing", diff);
                DeleteObject(h);
            }
        }
        if (g1) { GlobalUnlock(g1); GlobalFree(g1); }
        if (g2) { GlobalUnlock(g2); GlobalFree(g2); }
    }
}

/* ---- spans: a polygon left of the clip rectangle (C3) ---- */

static void sec_spans(void)
{
    static POINT thin[3] = { { 5, 2 }, { 5, 40 }, { 6, 40 } };
    WORD white;
    mono_begin(64, 64);
    white = mono_hash();
    SelectObject(mdc, GetStockObject(BLACK_BRUSH));
    SelectObject(mdc, GetStockObject(NULL_PEN));
    obs("spans IntersectClipRect", IntersectClipRect(mdc, 30, 0, 64, 64));
    Polygon(mdc, thin, 3);
    obs("spans thin polygon left of the clip: untouched", mono_hash() == white);
    SetROP2(mdc, R2_NOT);
    Polygon(mdc, thin, 3);
    obs("spans thin polygon R2_NOT: untouched", mono_hash() == white);
    mono_end();
}

/* ---- mapping modes (H7, H8) ---- */

static void sec_mapping(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    POINT p;
    SetMapMode(dc, MM_ISOTROPIC);
    SetWindowExt(dc, 640, 640);
    SetViewportExt(dc, 640, 640);
    obs_dw("iso 640/640 vpext", GetViewportExt(dc));
    SetWindowExt(dc, 1000, 1000);
    SetViewportExt(dc, 300, -200);
    obs_dw("iso 1000/1000 300,-200 vpext", GetViewportExt(dc));
    SetWindowExt(dc, 1000, 500);
    SetViewportExt(dc, 300, 300);
    obs_dw("iso 1000,500 300,300 vpext", GetViewportExt(dc));
    p.x = 100; p.y = 100;
    LPtoDP(dc, &p, 1);
    obs("iso 1000,500 LPtoDP x", p.x);
    obs("iso 1000,500 LPtoDP y", p.y);
    SetWindowExt(dc, 3, 7);
    SetViewportExt(dc, 32000, 32000);
    obs_dw("iso 3,7 32000 vpext", GetViewportExt(dc));
    obs_dw("iso 3,7 32000 wndext", GetWindowExt(dc));
    SetWindowExt(dc, 32000, 1);
    SetViewportExt(dc, 1, 32000);
    obs_dw("iso 32000,1 1,32000 vpext", GetViewportExt(dc));

    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExt(dc, 10, 10);
    SetViewportExt(dc, 10, 10);
    obs_dw("aniso ScaleWindowExt(0,1,1,1)", ScaleWindowExt(dc, 0, 1, 1, 1));
    obs_dw("aniso after ScaleWindowExt wndext", GetWindowExt(dc));
    obs_dw("aniso ScaleViewportExt(1,1,0,1)", ScaleViewportExt(dc, 1, 1, 0, 1));
    obs_dw("aniso after ScaleViewportExt vpext", GetViewportExt(dc));
    p.x = 5; p.y = 5;
    LPtoDP(dc, &p, 1);
    obs("aniso scaled LPtoDP x", p.x);
    obs("aniso scaled LPtoDP y", p.y);
    SetWindowExt(dc, 10, 10);
    SetViewportExt(dc, 10, 10);
    obs_dw("aniso ScaleWindowExt(1,0,1,1)", ScaleWindowExt(dc, 1, 0, 1, 1));
    obs_dw("aniso ScaleWindowExt(1,3,1,1)", ScaleWindowExt(dc, 1, 3, 1, 1));
    obs_dw("aniso after 1/3 wndext", GetWindowExt(dc));
    DeleteDC(dc);
}

/* ---- fonts in other mapping modes (H14, H15, Medium 15) ---- */

static void text_metrics(HDC dc, const char *pfx)
{
    TEXTMETRIC tm;
    char n[48];
    int i = 0, w[2];
    while (*pfx)
        n[i++] = *pfx++;
#define TM(field, v) (lstrcpy(n + i, field), obs(n, (WORD)(v)))
    TM(" GetTextMetrics", GetTextMetrics(dc, &tm));
    TM(" tmHeight", tm.tmHeight);
    TM(" tmAscent", tm.tmAscent);
    TM(" tmDescent", tm.tmDescent);
    TM(" tmInternalLeading", tm.tmInternalLeading);
    TM(" tmExternalLeading", tm.tmExternalLeading);
    TM(" tmAveCharWidth", tm.tmAveCharWidth);
    TM(" tmMaxCharWidth", tm.tmMaxCharWidth);
    TM(" GetCharWidth", GetCharWidth(dc, 'A', 'B', w));
    TM(" width A", w[0]);
    TM(" width B", w[1]);
    lstrcpy(n + i, " GetTextExtent");
    obs_dw(n, GetTextExtent(dc, "Hello", 5));
#undef TM
}

static void sec_fontmap(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    HFONT f, old;
    char face[LF_FACESIZE];
    text_metrics(dc, "text sys");
    SetMapMode(dc, MM_LOMETRIC);
    text_metrics(dc, "lomet sys");
    f = CreateFont(-50, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "MS Sans Serif");
    old = SelectObject(dc, f);
    text_metrics(dc, "lomet ms50");
    GetTextFace(dc, sizeof face, face);
    obs("lomet ms50 face hash", str_hash(face));
    SelectObject(dc, old);
    DeleteObject(f);
    f = CreateFont(60, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "MS Sans Serif");
    old = SelectObject(dc, f);
    text_metrics(dc, "lomet ms+60");
    SelectObject(dc, old);
    DeleteObject(f);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExt(dc, 1, 1);
    SetViewportExt(dc, 2, 2);
    f = CreateFont(-8, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "MS Sans Serif");
    old = SelectObject(dc, f);
    text_metrics(dc, "x2 ms-8");
    SelectObject(dc, old);
    DeleteObject(f);
    DeleteDC(dc);
}

/* ---- regions (Medium 5, Medium 8) ---- */

static void sec_regions(void)
{
    HRGN r = CreateRectRgn(0, 0, 100, 100), r2;
    RECT rc;
    SetRect(&rc, 10, 10, 10, 10);
    obs("RectInRegion empty", RectInRegion(r, &rc));
    SetRect(&rc, 50, 50, 10, 10);
    obs("RectInRegion inverted", RectInRegion(r, &rc));
    SetRect(&rc, 10, 50, 50, 10);
    obs("RectInRegion inverted y", RectInRegion(r, &rc));
    SetRect(&rc, 10, 10, 20, 20);
    obs("RectInRegion inside", RectInRegion(r, &rc));
    SetRect(&rc, 99, 99, 120, 120);
    obs("RectInRegion corner", RectInRegion(r, &rc));
    mono_begin(64, 64);
    SetRect(&rc, 10, 10, 10, 10);
    obs("RectVisible empty", RectVisible(mdc, &rc));
    SetRect(&rc, 50, 50, 10, 10);
    obs("RectVisible inverted", RectVisible(mdc, &rc));
    SetRect(&rc, 70, 70, 80, 80);
    obs("RectVisible outside", RectVisible(mdc, &rc));
    mono_end();
    DeleteObject(r);
    r = CreateRectRgn(0, -32768, 10, -32760);
    obs("rgn -32768 box", GetRgnBox(r, &rc));
    obs_rect("rgn -32768 box", &rc);
    obs("rgn -32768 PtInRegion", PtInRegion(r, 5, -32768));
    r2 = CreateRectRgn(20, 0, 30, 10);
    obs("rgn -32768 OR", CombineRgn(r, r, r2, RGN_OR));
    GetRgnBox(r, &rc);
    obs_rect("rgn -32768 OR box", &rc);
    obs("rgn -32768 OR PtInRegion", PtInRegion(r, 5, -32768));
    DeleteObject(r2);
    DeleteObject(r);
    r = CreateRectRgn(-10, -10, 32767, 32767);
    obs("rgn big OffsetRgn", OffsetRgn(r, 100, 100));
    GetRgnBox(r, &rc);
    obs_rect("rgn big offset box", &rc);
    DeleteObject(r);

}

/* ---- shapes whose pixels do not depend on outline details ---- */

static void sec_shapes(void)
{
    static POINT pp[8] = { { 4, 4 }, { 60, 4 }, { 60, 60 }, { 4, 60 },
                           { 20, 20 }, { 44, 20 }, { 44, 44 }, { 20, 44 } };
    static int cnt[2] = { 4, 4 };
    WORD white, black;
    mono_begin(64, 64);
    white = mono_hash();
    PatBlt(mdc, 0, 0, 64, 64, BLACKNESS);
    black = mono_hash();
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    /* Arc, Pie, Chord whose radials are the same point: the whole ellipse
       (Medium 17): any outline pixel at the left */
    obs("Arc same points", Arc(mdc, 2, 2, 62, 62, 62, 32, 62, 32));
    obs("Arc same points: left side drawn", black_in(0, 28, 6, 37) != 0);
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    SelectObject(mdc, GetStockObject(BLACK_BRUSH));
    obs("Pie same points", Pie(mdc, 2, 2, 62, 62, 62, 32, 62, 32));
    obs("Pie same points: centre filled", GetPixel(mdc, 20, 32) == 0);
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    obs("Chord same points", Chord(mdc, 2, 2, 62, 62, 62, 32, 62, 32));
    obs("Chord same points: centre filled", GetPixel(mdc, 20, 32) == 0);
    /* PolyPolygon: a square in a square (Medium 17) */
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    SelectObject(mdc, GetStockObject(NULL_PEN));
    obs("PolyPolygon alternate", PolyPolygon(mdc, pp, cnt, 2));
    obs("PolyPolygon alternate: hole", GetPixel(mdc, 32, 32) != 0);
    obs("PolyPolygon alternate: ring", GetPixel(mdc, 10, 32) == 0);
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    SetPolyFillMode(mdc, WINDING);
    PolyPolygon(mdc, pp, cnt, 2);
    obs("PolyPolygon winding: centre", GetPixel(mdc, 32, 32) == 0);
    SetPolyFillMode(mdc, ALTERNATE);
    /* the outlines of polygons given unclosed (the SDK: not closed) */
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    SelectObject(mdc, GetStockObject(BLACK_PEN));
    SelectObject(mdc, GetStockObject(NULL_BRUSH));
    obs("PolyPolygon outline", PolyPolygon(mdc, pp, cnt, 2));
    obs("PolyPolygon outline: top edge", GetPixel(mdc, 30, 4) == 0);
    obs("PolyPolygon outline: closing edge", GetPixel(mdc, 4, 30) == 0);
    obs("PolyPolygon outline: inner closing edge", GetPixel(mdc, 20, 30) == 0);
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    obs("PolyPolygon of 1", PolyPolygon(mdc, pp, cnt, 1));
    obs("PolyPolygon of 1: drawn", GetPixel(mdc, 30, 4) == 0);
    SelectObject(mdc, GetStockObject(BLACK_BRUSH));
    SelectObject(mdc, GetStockObject(NULL_PEN));
    /* an empty Rectangle (Medium 17) */
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    SelectObject(mdc, GetStockObject(BLACK_PEN));
    SelectObject(mdc, GetStockObject(NULL_BRUSH));
    obs("Rectangle l == r", Rectangle(mdc, 10, 10, 10, 20));
    obs("Rectangle l == r: black", black_in(0, 0, 64, 64));
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    Rectangle(mdc, 10, 10, 11, 20);
    obs("Rectangle 1 wide: black", black_in(0, 0, 64, 64));
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    Rectangle(mdc, 10, 10, 30, 10);
    obs("Rectangle t == b: black", black_in(0, 0, 64, 64));
    /* ellipses and round rectangles far bigger than the bitmap, which is
       inside them (C5, Medium 13): no outline here; the inside filled */
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    obs("big Ellipse hollow", Ellipse(mdc, -5000, -5000, 5064, 5064));
    obs("big Ellipse hollow: untouched", mono_hash() == white);
    SelectObject(mdc, GetStockObject(BLACK_BRUSH));
    obs("big Ellipse filled", Ellipse(mdc, -5000, -5000, 5064, 5064));
    obs("big Ellipse filled: all black", mono_hash() == black);
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    obs("huge Ellipse filled", Ellipse(mdc, -20000, -20000, 20064, 20064));
    obs("huge Ellipse filled: all black", mono_hash() == black);
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    obs("big RoundRect filled", RoundRect(mdc, -9000, -9000, 9064, 9064, 200, 200));
    obs("big RoundRect filled: all black", mono_hash() == black);
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    SelectObject(mdc, GetStockObject(NULL_BRUSH));
    obs("big RoundRect hollow", RoundRect(mdc, -9000, -9000, 9064, 9064, 200, 200));
    obs("big RoundRect hollow: untouched", mono_hash() == white);
    obs("big Arc", Arc(mdc, -9000, -9000, 9064, 9064, 0, 0, 10, 0));
    obs("big Arc: untouched", mono_hash() == white);
    mono_end();
}

/* ---- blts (H11, H16) ---- */

static void sec_blt(void)
{
    HDC sdc = CreateCompatibleDC(NULL);
    HBITMAP sbm = CreateBitmap(8, 8, 1, 1, NULL), sold = SelectObject(sdc, sbm);
    HBITMAP s16;
    PatBlt(sdc, 0, 0, 8, 8, BLACKNESS);
    mono_begin(32, 32);
    obs("BitBlt past the source", BitBlt(mdc, 0, 0, 16, 16, sdc, 0, 0, SRCCOPY));
    obs("BitBlt past the source: inside", GetPixel(mdc, 4, 4) == 0);
    obs("BitBlt past the source: outside", GetPixel(mdc, 12, 12) == 0);
    obs("BitBlt past the source: right", GetPixel(mdc, 12, 4) == 0);
    PatBlt(mdc, 0, 0, 32, 32, WHITENESS);
    obs("BitBlt before the source", BitBlt(mdc, 0, 0, 8, 8, sdc, -4, -4, SRCCOPY));
    obs("BitBlt before the source: (1,1)", GetPixel(mdc, 1, 1) == 0);
    obs("BitBlt before the source: (6,6)", GetPixel(mdc, 6, 6) == 0);
    /* StretchBlt from a DC that maps 1 logical unit to 2 pixels (H16) */
    s16 = CreateBitmap(16, 16, 1, 1, NULL);
    SelectObject(sdc, s16);
    PatBlt(sdc, 0, 0, 16, 16, WHITENESS);
    PatBlt(sdc, 0, 0, 8, 16, BLACKNESS);
    SetMapMode(sdc, MM_ANISOTROPIC);
    SetWindowExt(sdc, 1, 1);
    SetViewportExt(sdc, 2, 2);
    PatBlt(mdc, 0, 0, 32, 32, WHITENESS);
    obs("StretchBlt x2 source", StretchBlt(mdc, 0, 0, 16, 16, sdc, 0, 0, 8, 8, SRCCOPY));
    obs("StretchBlt x2 source: (2,2)", GetPixel(mdc, 2, 2) == 0);
    obs("StretchBlt x2 source: (12,2)", GetPixel(mdc, 12, 2) == 0);
    obs("StretchBlt x2 source: (7,2)", GetPixel(mdc, 7, 2) == 0);
    obs("StretchBlt x2 source: (8,2)", GetPixel(mdc, 8, 2) == 0);
    PatBlt(mdc, 0, 0, 32, 32, WHITENESS);
    obs("StretchBlt x2 to 32", StretchBlt(mdc, 0, 0, 32, 32, sdc, 0, 0, 8, 8, SRCCOPY));
    obs("StretchBlt x2 to 32: (14,2)", GetPixel(mdc, 14, 2) == 0);
    obs("StretchBlt x2 to 32: (17,2)", GetPixel(mdc, 17, 2) == 0);
    mono_end();
    SelectObject(sdc, sold);
    DeleteDC(sdc);
    DeleteObject(sbm);
    DeleteObject(s16);
}

/* ---- colour: pens across bitmaps (H5), pen handles reused (H1), a
   hatched brush's background (Medium 10) ---- */

static void sec_color(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bm = CreateCompatibleBitmap(hscr, 32, 32), old;
    HPEN p1, p2, op;
    HBRUSH hb, ob;
    int x;
    COLORREF c;
    SelectObject(dc, GetStockObject(WHITE_PEN));
    old = SelectObject(dc, bm);
    PatBlt(dc, 0, 0, 32, 32, BLACKNESS);
    MoveTo(dc, 0, 5);
    LineTo(dc, 20, 5);
    obs_dw("white pen before the bitmap", GetPixel(dc, 10, 5));
    SelectObject(dc, old);
    p1 = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
    SelectObject(dc, p1);
    SelectObject(dc, bm);
    MoveTo(dc, 0, 7);
    LineTo(dc, 20, 7);
    obs_dw("red pen before the bitmap", GetPixel(dc, 10, 7));
    SelectObject(dc, GetStockObject(BLACK_PEN));
    DeleteObject(p1);
    /* the same handle again, another colour */
    p1 = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
    op = SelectObject(dc, p1);
    MoveTo(dc, 0, 9);
    LineTo(dc, 20, 9);
    SelectObject(dc, op);
    DeleteObject(p1);
    p2 = CreatePen(PS_SOLID, 1, RGB(0, 0, 255));
    obs_env("pen handle reused", p1 == p2);
    SelectObject(dc, p2);
    MoveTo(dc, 0, 11);
    LineTo(dc, 20, 11);
    obs_dw("blue pen after a deleted red one", GetPixel(dc, 10, 11));
    SelectObject(dc, op);
    DeleteObject(p2);
    /* hatched brush, background colour set after selecting it */
    hb = CreateHatchBrush(HS_CROSS, RGB(0, 0, 0));
    ob = SelectObject(dc, hb);
    PatBlt(dc, 0, 0, 32, 32, WHITENESS);
    SetBkColor(dc, RGB(255, 0, 0));
    PatBlt(dc, 0, 16, 16, 16, PATCOPY);
    c = 0;
    for (x = 0; x < 16 && !c; x++)
        c = GetPixel(dc, x, 17);
    obs_dw("hatch bk set after selecting", c);
    SelectObject(dc, ob);
    DeleteObject(hb);
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
}

/* ---- palettes (H3) ---- */

static void sec_palette(void)
{
    struct { WORD v, n; PALETTEENTRY e[2]; } lp = { 0x300, 2, { { 1, 2, 3, 0 }, { 4, 5, 6, 0 } } };
    HDC dc = CreateCompatibleDC(NULL);
    HPALETTE p = CreatePalette((LOGPALETTE FAR *)&lp), old;
    WORD n = 0;
    old = SelectPalette(dc, p, FALSE);
    obs("palette selected: old is DEFAULT_PALETTE", old == GetStockObject(DEFAULT_PALETTE));
    obs("DeleteObject selected palette", DeleteObject(p));
    obs("palette after DeleteObject: GetObject", GetObject(p, 2, &n));
    obs("palette after DeleteObject: IsGDIObject", IsGDIObject(p));
    obs("SelectPalette back returns it", SelectPalette(dc, old, FALSE) == p);
    obs("DeleteObject palette after deselecting", DeleteObject(p));
    DeleteDC(dc);
}

/* ---- DIBs (H17) ---- */

static void sec_dib(void)
{
    struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } bi;
    struct { BITMAPCOREHEADER h; RGBTRIPLE c[2]; } bc;
    struct { BITMAPINFOHEADER h; WORD c[2]; } bp;
    static BYTE bits[8 * 4], src[8 * 4] = { 0xF0, 0, 0, 0, 0xF0, 0, 0, 0, 0xF0, 0, 0, 0, 0xF0, 0, 0, 0,
                                           0x0F, 0, 0, 0, 0x0F, 0, 0, 0, 0x0F, 0, 0, 0, 0x0F, 0, 0, 0 };
    HBITMAP bm = CreateBitmap(8, 8, 1, 1, NULL);
    int r;

    fill(&bi, 0x55, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = 8;
    bi.h.biHeight = 8;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 1;
    bi.h.biCompression = 0;
    bi.h.biSizeImage = 0;
    r = GetDIBits(hscr, bm, 0, 8, NULL, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
    obs("GetDIBits NULL bits", r);
    obs_dw("GetDIBits NULL bits biSizeImage", bi.h.biSizeImage);
    obs_dw("GetDIBits NULL bits colour 0", *(DWORD *)&bi.c[0]);
    obs_dw("GetDIBits NULL bits colour 1", *(DWORD *)&bi.c[1]);
    fill(bi.c, 0x55, sizeof bi.c);
    r = GetDIBits(hscr, bm, 0, 8, bits, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
    obs("GetDIBits", r);
    obs_dw("GetDIBits colour 0", *(DWORD *)&bi.c[0]);
    obs_dw("GetDIBits colour 1", *(DWORD *)&bi.c[1]);
    fill(&bc, 0x55, sizeof bc);
    bc.h.bcSize = sizeof bc.h;
    bc.h.bcWidth = 8;
    bc.h.bcHeight = 8;
    bc.h.bcPlanes = 1;
    bc.h.bcBitCount = 1;
    r = GetDIBits(hscr, bm, 0, 8, bits, (BITMAPINFO FAR *)&bc, DIB_RGB_COLORS);
    obs("GetDIBits core", r);
    obs("GetDIBits core colour 0 rg", *(WORD *)&bc.c[0]);
    obs("GetDIBits core colour 0 b, 1 r", *(WORD *)((BYTE *)&bc.c[0] + 2));
    obs("GetDIBits core colour 1 gb", *(WORD *)((BYTE *)&bc.c[1] + 1));
    obs("GetDIBits core header size", (WORD)bc.h.bcSize);
    fill(&bp, 0x55, sizeof bp);
    bp.h.biSize = sizeof bp.h;
    bp.h.biWidth = 8;
    bp.h.biHeight = 8;
    bp.h.biPlanes = 1;
    bp.h.biBitCount = 1;
    bp.h.biCompression = 0;
    bp.h.biSizeImage = 0;
    r = GetDIBits(hscr, bm, 0, 8, bits, (BITMAPINFO FAR *)&bp, DIB_PAL_COLORS);
    obs("GetDIBits pal", r);
    obs("GetDIBits pal word 0", bp.c[0]);
    obs("GetDIBits pal word 1", bp.c[1]);
    DeleteObject(bm);
    /* SetDIBitsToDevice on a memory DC */
    mono_begin(16, 16);
    fill(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = 8;
    bi.h.biHeight = 8;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 1;
    bi.c[1].rgbRed = bi.c[1].rgbGreen = bi.c[1].rgbBlue = 255;
    r = SetDIBitsToDevice(mdc, 2, 2, 8, 8, 0, 0, 0, 8, src, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
    obs("SetDIBitsToDevice memory DC", r);
    obs("SetDIBitsToDevice memory DC: hash", mono_hash());
    PatBlt(mdc, 0, 0, 16, 16, WHITENESS);
    r = SetDIBitsToDevice(mdc, 2, 2, 8, 8, 0, 0, 0, 4, src, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
    obs("SetDIBitsToDevice 4 lines", r);
    obs("SetDIBitsToDevice 4 lines: hash", mono_hash());
    mono_end();
}

/* ---- text (Medium 14) and the small ones (Low) ---- */

static void sec_misc(void)
{
    static int dx[3] = { 10, 20, 30 };
    WORD buf[4];
    HDC dc = CreateCompatibleDC(NULL);
    SetTextAlign(dc, TA_UPDATECP);
    MoveTo(dc, 0, 0);
    obs("ExtTextOut dx UPDATECP", ExtTextOut(dc, 0, 0, 0, NULL, "abc", 3, dx));
    obs_dw("ExtTextOut dx UPDATECP: position", GetCurrentPosition(dc));
    SetTextAlign(dc, TA_UPDATECP | TA_RIGHT);
    MoveTo(dc, 100, 0);
    ExtTextOut(dc, 0, 0, 0, NULL, "abc", 3, dx);
    obs_dw("ExtTextOut dx UPDATECP right: position", GetCurrentPosition(dc));
    SetTextAlign(dc, 0);
    obs_dw("SetMapperFlags", SetMapperFlags(dc, 0x12345678L));
    obs_dw("SetMapperFlags again", SetMapperFlags(dc, 0));
    DeleteDC(dc);
    obs("DeleteMetaFile(0)", DeleteMetaFile(0));
    buf[0] = buf[1] = buf[2] = buf[3] = 0x5555;
    obs("GetRasterizerCaps 6", GetRasterizerCaps((LPRASTERIZER_STATUS)buf, 6));
    obs("GetRasterizerCaps 6 [0]", buf[0]);
    obs("GetRasterizerCaps 6 [1]", buf[1]);
    obs("GetRasterizerCaps 6 [2]", buf[2]);
    buf[0] = buf[1] = buf[2] = buf[3] = 0x5555;
    obs("GetRasterizerCaps 4", GetRasterizerCaps((LPRASTERIZER_STATUS)buf, 4));
    obs("GetRasterizerCaps 4 [0]", buf[0]);
    obs("GetRasterizerCaps 4 [2]", buf[2]);
}

/* ---- a window DC's clip region (C6, H6) ---- */

static void sec_window(void)
{
    WNDCLASS wc;
    HWND w;
    HDC dc;
    HRGN r, c;
    RECT rc;
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "GD5";
    RegisterClass(&wc);
    w = CreateWindow("GD5", "", WS_POPUP | WS_VISIBLE, 100, 80, 300, 200, NULL, NULL, hinst, NULL);
    UpdateWindow(w);
    /* a clip region reaching the coordinate limit (C6) */
    dc = GetDC(w);
    r = CreateRectRgn(0, 0, 32767, 32767);
    obs("clip to 32767: SelectClipRgn", SelectClipRgn(dc, r));
    obs("clip to 32767: GetClipBox", GetClipBox(dc, &rc));
    obs_rect("clip to 32767: box", &rc);
    PatBlt(dc, 0, 0, 50, 50, BLACKNESS);
    obs("clip to 32767: drawn", GetPixel(dc, 10, 10) == 0);
    DeleteObject(r);
    ReleaseDC(w, dc);
    dc = GetDC(w);
    r = CreateRectRgn(-32768, -32768, 32767, 32767);
    obs("clip to both limits: SelectClipRgn", SelectClipRgn(dc, r));
    PatBlt(dc, 0, 0, 50, 50, WHITENESS);
    obs("clip to both limits: drawn", GetPixel(dc, 10, 10) != 0);
    DeleteObject(r);
    ReleaseDC(w, dc);
    /* IntersectClipRect/ExcludeClipRect with no clip region (H6) */
    dc = GetDC(w);
    obs("IntersectClipRect fresh", IntersectClipRect(dc, 10, 10, 20, 20));
    c = getcliprgn ? getcliprgn(dc) : 0;
    obs("IntersectClipRect fresh: clip region", c != 0);
    if (c) {
        obs("IntersectClipRect fresh: GetRgnBox", GetRgnBox(c, &rc));
        obs_rect("IntersectClipRect fresh: rgn", &rc);
    }
    GetClipBox(dc, &rc);
    obs_rect("IntersectClipRect fresh: clip box", &rc);
    ReleaseDC(w, dc);
    dc = GetDC(w);
    obs("ExcludeClipRect fresh", ExcludeClipRect(dc, 10, 10, 20, 20));
    c = getcliprgn ? getcliprgn(dc) : 0;
    if (c) {
        obs("ExcludeClipRect fresh: GetRgnBox", GetRgnBox(c, &rc));
        obs_rect("ExcludeClipRect fresh: rgn", &rc);
    }
    GetClipBox(dc, &rc);
    obs_rect("ExcludeClipRect fresh: clip box", &rc);
    ReleaseDC(w, dc);
    /* the window off the left of the screen and wider than it */
    MoveWindow(w, -100, 80, 800, 200, TRUE);
    UpdateWindow(w);
    dc = GetDC(w);
    ExcludeClipRect(dc, 0, 0, 1, 1);
    PatBlt(dc, 0, 0, 800, 200, BLACKNESS);
    obs("off-left window, ExcludeClipRect: (700,50) drawn", GetPixel(dc, 700, 50) == 0);
    obs("off-left window, ExcludeClipRect: (300,50) drawn", GetPixel(dc, 300, 50) == 0);
    ReleaseDC(w, dc);
    dc = GetDC(w);
    IntersectClipRect(dc, 600, 0, 800, 200);
    PatBlt(dc, 0, 0, 800, 200, WHITENESS);
    obs("off-left window, IntersectClipRect: (700,50) drawn", GetPixel(dc, 700, 50) != 0);
    ReleaseDC(w, dc);
    DestroyWindow(w);
}

/* ---- GetDCState's handle (C7) ---- */

static void sec_dcstate(void)
{
    HDC dc = CreateCompatibleDC(NULL), dc2;
    HANDLE s;
    RECT rc;
    if (!getdcstate)
        return;
    s = getdcstate(dc);
    obs("GetDCState", s != 0);
    obs("GetDCState IsGDIObject", IsGDIObject(s));
    obs("DeleteDC of GetDCState's", DeleteDC(s));
    obs("the DC still works: PatBlt", PatBlt(dc, 0, 0, 1, 1, BLACKNESS));
    obs("the DC still works: GetClipBox", GetClipBox(dc, &rc));
    DeleteDC(dc);
    /* a DC of another device's driver: CreateDC twice, a state deleted */
    dc = CreateDC("DISPLAY", NULL, NULL, NULL);
    dc2 = CreateDC("DISPLAY", NULL, NULL, NULL);
    s = getdcstate(dc);
    DeleteDC(s);
    obs("CreateDC after a state's DeleteDC: PatBlt", PatBlt(dc2, 0, 0, 1, 1, DSTINVERT));
    PatBlt(dc2, 0, 0, 1, 1, DSTINVERT);
    DeleteDC(dc);
    DeleteDC(dc2);
}

/* ---- huge point counts (C5) ---- */

static void sec_points(void)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16383L * sizeof(POINT));
    POINT FAR *p = g ? (POINT FAR *)GlobalLock(g) : 0;
    WORD i;
    HRGN r;
    RECT rc;
    if (!p)
        return;
    for (i = 0; i < 16383; i++) {
        p[i].x = i & 1 ? 20 : 2;
        p[i].y = 2;
    }
    mono_begin(64, 64);
    obs("Polyline 16383", Polyline(mdc, p, 16383));
    obs("Polyline 16383: row 2", black_in(0, 2, 32, 3));
    obs("Polyline 16383: elsewhere", black_in(0, 3, 32, 6));
    for (i = 0; i < 16383; i++) {
        p[i].x = i & 1 ? 30 : 4;
        p[i].y = i & 2 ? 30 : 4;
    }
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    obs("Polyline 16383 again", Polyline(mdc, p, 16383));
    obs("Polyline 16383 again: (4,4)", GetPixel(mdc, 4, 4) == 0);
    p[0].x = 10; p[0].y = 10;
    p[1].x = 40; p[1].y = 10;
    p[2].x = 40; p[2].y = 40;
    for (i = 3; i < 16383; i++)
        p[i] = p[2];
    PatBlt(mdc, 0, 0, 64, 64, WHITENESS);
    SelectObject(mdc, GetStockObject(BLACK_BRUSH));
    SelectObject(mdc, GetStockObject(NULL_PEN));
    obs("Polygon 16383", Polygon(mdc, p, 16383));
    obs("Polygon 16383: inside", GetPixel(mdc, 35, 20) == 0);
    r = CreatePolygonRgn(p, 16383, ALTERNATE);
    obs("CreatePolygonRgn 16383", r != 0);
    if (r) {
        obs("CreatePolygonRgn 16383: GetRgnBox", GetRgnBox(r, &rc));
        obs_rect("CreatePolygonRgn 16383: box", &rc);
        DeleteObject(r);
    }
    obs("Polyline -1", Polyline(mdc, p, -1));
    obs("Polygon -1", Polygon(mdc, p, -1));
    obs("GDI still draws", PatBlt(mdc, 0, 0, 64, 64, BLACKNESS));
    obs("GDI still draws: black", GetPixel(mdc, 63, 63) == 0);
    mono_end();
    GlobalUnlock(g);
    GlobalFree(g);
}

/* ---- fonts removed while selected (C2), and by path (H13) ---- */

static int face_is(HDC dc, const char *face)
{
    HFONT f = CreateFont(-10, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, face), o;
    char got[LF_FACESIZE];
    o = SelectObject(dc, f);
    GetTextFace(dc, sizeof got, got);
    SelectObject(dc, o);
    DeleteObject(f);
    return !lstrcmpi(got, face);
}

static void sec_fonts(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    HFONT f = CreateFont(-11, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "Small Fonts"), o;
    TEXTMETRIC tm;
    char got[LF_FACESIZE];
    o = SelectObject(dc, f);
    GetTextMetrics(dc, &tm);
    obs("Small Fonts tmHeight", tm.tmHeight);
    GetTextFace(dc, sizeof got, got);
    obs("Small Fonts face hash", str_hash(got));
    obs("RemoveFontResource SMALLE.FON (selected)", RemoveFontResource("SMALLE.FON"));
    obs("after: GetTextMetrics", GetTextMetrics(dc, &tm));
    obs("after: tmHeight", tm.tmHeight);
    GetTextFace(dc, sizeof got, got);
    obs("after: face hash", str_hash(got));
    obs_dw("after: GetTextExtent", GetTextExtent(dc, "Hello", 5));
    mono_begin(64, 16);
    SelectObject(mdc, f);
    obs("after: TextOut", TextOut(mdc, 0, 0, "Hi", 2));
    obs("after: TextOut: hash", mono_hash());
    SelectObject(mdc, GetStockObject(SYSTEM_FONT));
    mono_end();
    SelectObject(dc, o);
    DeleteObject(f);
    obs("after deselecting: Small Fonts", face_is(dc, "Small Fonts"));
    obs("AddFontResource SMALLE.FON", AddFontResource("SMALLE.FON"));
    obs("added again: Small Fonts", face_is(dc, "Small Fonts"));
    /* by path */
    obs("AddFontResource path COURE.FON", AddFontResource("C:\\WINDOWS\\SYSTEM\\COURE.FON"));
    obs("RemoveFontResource path COURE.FON", RemoveFontResource("C:\\WINDOWS\\SYSTEM\\COURE.FON"));
    obs("after path removal: Courier", face_is(dc, "Courier"));
    obs("RemoveFontResource COURE.FON", RemoveFontResource("COURE.FON"));
    obs("after second removal: Courier", face_is(dc, "Courier"));
    obs("RemoveFontResource COURE.FON again", RemoveFontResource("COURE.FON"));
    obs("AddFontResource COURE.FON", AddFontResource("COURE.FON"));
    obs("re-added: Courier", face_is(dc, "Courier"));
    DeleteDC(dc);
}

/* ---- arguments a program should not pass (Medium 4, Medium 6) ---- */

static void sec_late(void)
{
    HPALETTE p = GetStockObject(DEFAULT_PALETTE);
    PALETTEENTRY e, was;
    HDC dc;
    GetPaletteEntries(p, 0, 1, &was);
    e.peRed = 1; e.peGreen = 2; e.peBlue = 3; e.peFlags = 0;
    obs("SetPaletteEntries DEFAULT_PALETTE", SetPaletteEntries(p, 0, 1, &e));
    GetPaletteEntries(p, 0, 1, &e);
    obs("DEFAULT_PALETTE entry 0 changed", e.peRed == 1 && e.peGreen == 2);
    SetPaletteEntries(p, 0, 1, &was);
    dc = CreateCompatibleDC((HDC)0x1236);
    obs("CreateCompatibleDC of a bad handle", dc != 0);
    if (dc)
        DeleteDC(dc);
}

int __cdecl tmain(void)
{
    HMODULE gdi;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("GD5 start\r\n");
    gdi = GetModuleHandle("GDI");
    getdcstate = (PGETDCSTATE)GetProcAddress(gdi, MAKEINTRESOURCE(179));
    getcliprgn = (PGETCLIPRGN)GetProcAddress(gdi, MAKEINTRESOURCE(173));
    hscr = GetDC(NULL);
    out("GD5 bitmaps\r\n");
    sec_bitmaps();
    out("GD5 spans\r\n");
    sec_spans();
    out("GD5 mapping\r\n");
    sec_mapping();
    out("GD5 fontmap\r\n");
    sec_fontmap();
    out("GD5 regions\r\n");
    sec_regions();
    out("GD5 shapes\r\n");
    sec_shapes();
    out("GD5 blt\r\n");
    sec_blt();
    out("GD5 color\r\n");
    sec_color();
    out("GD5 palette\r\n");
    sec_palette();
    out("GD5 dib\r\n");
    sec_dib();
    out("GD5 misc\r\n");
    sec_misc();
    out("GD5 window\r\n");
    sec_window();
    out("GD5 dcstate\r\n");
    sec_dcstate();
    out("GD5 points\r\n");
    sec_points();
    out("GD5 fonts\r\n");
    sec_fonts();
    out("GD5 late\r\n");
    sec_late();
    out("GD5 bigbitmaps\r\n");
    sec_bigbitmaps();
    ReleaseDC(NULL, hscr);
    out("GD5 done\r\n");
    return failures;
}
