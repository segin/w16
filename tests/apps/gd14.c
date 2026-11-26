/*
 * gd14.c - colour conversions in BitBlt and pattern brushes on the
 * 16-colour VGA, as StarOffice 3.1's StarView draws them:
 * - colour -> monochrome BitBlt: which DC's background colour picks the
 *   white pixels (the source's or the destination's), with SRCCOPY,
 *   SRCPAINT and a ROP with a brush; PALETTERGB (02xxxxxx) and
 *   non-VGA background colours;
 * - monochrome -> colour BitBlt: which DC's text and background colours
 *   the 0 and 1 bits take;
 * - StarView's disabled toolbar icon, call for call as w16's import trace
 *   logged it: a mask made from the colour strip with the background
 *   PALETTERGB(C0C0C0), the white pixels ORed in, then PSDPxax (B8074A)
 *   twice with two stock brushes, one pixel apart (3.1: a grey icon with a
 *   white shadow; w16 drew a grey box: it converted with the destination
 *   DC's background colour);
 * - pattern brushes: CreatePatternBrush of a monochrome bitmap (text and
 *   background colours), CreateDIBPatternBrush of 1- and 4-bit DIBs with
 *   DIB_RGB_COLORS and DIB_PAL_COLORS on a colour and a monochrome DC
 *   (StarView's checked buttons use a silver/white 1-bit DIB, which w16
 *   made a monochrome bitmap through the colours: all white), a colour
 *   pattern bitmap on a monochrome DC, and the brush origin
 *   (SetBrushOrg, UnrealizeObject, a DC at an odd place).
 * Each result is read back with GetPixel and hashed (one checked value a
 * case; the rows as letters unchecked, to see where two runs part). Run
 * as the shell of Windows 3.1 with Microsoft's GDI and VGA.DRV (the
 * reference: tools/obsref.py of its log is gd14ref.h), and with w16's GDI,
 * VGA.DRV or both (tests/gdiheap.sh -p build/dos/GD14.EXE [-r] [-v]; the
 * boot-test case gdiconv).
 */
#include <windows.h>

#define TK_REF "gd14ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define CW 48                           /* the bitmaps read back */
#define CH 16

static HINSTANCE hinst;
static HDC hscr, cdc, mdc, sdc;         /* colour, monochrome, second colour */
static HBITMAP cbm, mbm, sbm, cold, mold, sold;

static const COLORREF vga[16] = {
    0x000000L, 0x000080L, 0x008000L, 0x008080L, 0x800000L, 0x800080L, 0x808000L, 0xC0C0C0L,
    0x808080L, 0x0000FFL, 0x00FF00L, 0x00FFFFL, 0xFF0000L, 0xFF00FFL, 0xFFFF00L, 0xFFFFFFL };

static WORD mix(WORD h, DWORD c)
{
    return (WORD)((h << 5) | (h >> 11)) + LOWORD(c) + HIWORD(c) * 3 + 1;
}

static char letter(DWORD c)
{
    int i;
    for (i = 0; i < 16; i++)
        if (vga[i] == c)
            return "0123456789ABCDEF"[i];
    return '?';
}

static int unchecked;                   /* the next result not checked */

/* the area [0, w) x [0, h) of dc, hashed (checked); rows unchecked */
static void result(HDC dc, int w, int h, const char *name)
{
    WORD hs = 0, y, x;
    char row[CW + 1];
    for (y = 0; y < (WORD)h; y++) {
        for (x = 0; x < (WORD)w; x++) {
            DWORD c = GetPixel(dc, x, y);
            hs = mix(hs, c);
            row[x] = letter(c);
        }
        row[w] = 0;
        out("ROW ");
        out(row);
        out("\r\n");
    }
    if (unchecked)
        obs_env(name, hs);
    else
        obs(name, hs);
}

static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c), ob = SelectObject(dc, b);
    PatBlt(dc, x, y, w, h, PATCOPY);
    SelectObject(dc, ob);
    DeleteObject(b);
}

/* the colour bitmap: 16 columns of 3 pixels, one VGA colour each, the
   lower half the reverse order */
static void stripes(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        fill(cdc, i * 3, 0, 3, CH / 2, vga[i]);
        fill(cdc, i * 3, CH / 2, 3, CH / 2, vga[15 - i]);
    }
}

/* the monochrome bitmap: a pattern of its own (bit = (x + 2 y) % 5 < 2) */
static void mono_pattern(void)
{
    int x, y;
    PatBlt(mdc, 0, 0, CW, CH, WHITENESS);
    for (y = 0; y < CH; y++)
        for (x = 0; x < CW; x++)
            if ((x + 2 * y) % 5 < 2)
                SetPixel(mdc, x, y, 0);
}

static void colours(HDC dc, COLORREF text, COLORREF bk)
{
    SetTextColor(dc, text);
    SetBkColor(dc, bk);
}

/* ---- colour -> monochrome ---- */

static void c2m(const char *name, COLORREF sbk, COLORREF dbk, DWORD rop)
{
    stripes();
    mono_pattern();
    colours(cdc, RGB(255, 0, 0), sbk);
    colours(mdc, RGB(0, 0, 0), dbk);
    BitBlt(mdc, 0, 0, CW, CH, cdc, 0, 0, rop);
    colours(cdc, 0, RGB(255, 255, 255));
    colours(mdc, 0, RGB(255, 255, 255));
    result(mdc, CW, CH, name);
}

static void sec_c2m(void)
{
    HBRUSH ob;
    DWORD r;
    c2m("c2m src silver dst white", RGB(192, 192, 192), RGB(255, 255, 255), SRCCOPY);
    c2m("c2m src white dst silver", RGB(255, 255, 255), RGB(192, 192, 192), SRCCOPY);
    c2m("c2m src red dst black", RGB(255, 0, 0), RGB(0, 0, 0), SRCCOPY);
    c2m("c2m src PALETTERGB silver", 0x02C0C0C0L, RGB(255, 255, 255), SRCCOPY);
    c2m("c2m src PALETTEINDEX 7", 0x01000007L, RGB(255, 255, 255), SRCCOPY);
    c2m("c2m src 200,200,200", RGB(200, 200, 200), RGB(255, 255, 255), SRCCOPY);
    c2m("c2m src 130,130,130", RGB(130, 130, 130), RGB(255, 255, 255), SRCCOPY);
    c2m("c2m src grey SRCPAINT", RGB(128, 128, 128), RGB(255, 255, 255), SRCPAINT);
    c2m("c2m src grey SRCAND", RGB(128, 128, 128), RGB(192, 192, 192), SRCAND);
    /* with a brush on the monochrome DC (gray: a dither there) */
    /* with a solid brush on the monochrome DC (gray). Unchecked: on 3.1
       Microsoft's VGA.DRV takes the brush as black here (with either GDI)
       although PatBlt of the same brush on the same DC draws its 50%
       dither (below); w16's VGA.DRV draws the dither in both */
    ob = SelectObject(mdc, GetStockObject(GRAY_BRUSH));
    unchecked = 1;
    c2m("c2m src silver PSDPxax", RGB(192, 192, 192), RGB(255, 255, 255), 0x00B8074AL);
    unchecked = 0;
    SelectObject(mdc, ob);
    /* a monochrome pattern brush on the monochrome DC: whose colours does
       the pattern take, the destination's or the source's? */
    {
        static const WORD chk[8] = { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA };
        HBITMAP pb = CreateBitmap(8, 8, 1, 1, chk);
        HBRUSH b = CreatePatternBrush(pb);
        ob = SelectObject(mdc, b);
        stripes();
        mono_pattern();
        colours(cdc, RGB(255, 255, 255), RGB(192, 192, 192));
        colours(mdc, RGB(255, 255, 255), 0);
        BitBlt(mdc, 0, 0, CW, CH, cdc, 0, 0, 0x00C000CAL);         /* MERGECOPY */
        colours(cdc, 0, RGB(255, 255, 255));
        colours(mdc, 0, RGB(255, 255, 255));
        result(mdc, CW, CH, "c2m MERGECOPY mono brush, dst white/black, src white/silver");
        SelectObject(mdc, ob);
        DeleteObject(b);
        DeleteObject(pb);
    }
    r = SetBkColor(cdc, 0x02C0C0C0L);
    r = SetBkColor(cdc, RGB(255, 255, 255));
    obs("SetBkColor PALETTERGB lo", LOWORD(r));
    obs("SetBkColor PALETTERGB hi", HIWORD(r));
    SetBkColor(cdc, 0x02C0C0C0L);
    r = GetBkColor(cdc);
    obs("GetBkColor PALETTERGB lo", LOWORD(r));
    obs("GetBkColor PALETTERGB hi", HIWORD(r));
    SetBkColor(cdc, RGB(255, 255, 255));
}

/* ---- monochrome -> colour ---- */

static void m2c(const char *name, COLORREF dt, COLORREF dbk, COLORREF st, COLORREF sbk, DWORD rop)
{
    stripes();
    mono_pattern();
    colours(cdc, dt, dbk);
    colours(mdc, st, sbk);
    BitBlt(cdc, 0, 0, CW, CH, mdc, 0, 0, rop);
    colours(cdc, 0, RGB(255, 255, 255));
    colours(mdc, 0, RGB(255, 255, 255));
    result(cdc, CW, CH, name);
}

static void sec_m2c(void)
{
    HBRUSH b, ob;
    m2c("m2c dst red/blue src green/yellow", RGB(255, 0, 0), RGB(0, 0, 255), RGB(0, 255, 0),
        RGB(255, 255, 0), SRCCOPY);
    m2c("m2c dst black/white SRCAND", 0, RGB(255, 255, 255), RGB(255, 0, 0), RGB(0, 0, 255), SRCAND);
    m2c("m2c dst black/white SRCPAINT", 0, RGB(255, 255, 255), 0, RGB(255, 255, 255), SRCPAINT);
    m2c("m2c dst grey/silver DSna", RGB(128, 128, 128), RGB(192, 192, 192), 0, RGB(255, 255, 255),
        0x00220326L);
    b = CreateSolidBrush(RGB(128, 0, 128));
    ob = SelectObject(cdc, b);
    m2c("m2c PSDPxax purple brush", 0, RGB(255, 255, 255), 0, RGB(255, 255, 255), 0x00B8074AL);
    m2c("m2c DSPDxax purple brush", 0, RGB(255, 255, 255), 0, RGB(255, 255, 255), 0x00E20746L);
    m2c("m2c PSDPxax text red", RGB(255, 0, 0), RGB(255, 255, 255), 0, RGB(255, 255, 255),
        0x00B8074AL);
    SelectObject(cdc, ob);
    DeleteObject(b);
}

/* ---- StarView's disabled toolbar icon ---- */

/* a 16 x 15 icon on silver: a black frame, a white inside, red and blue
   dots, a grey shadow; the strip holds it twice (at 0 and 16) */
static void icon(HDC dc, int x0)
{
    fill(dc, x0, 0, 16, 15, RGB(192, 192, 192));
    fill(dc, x0 + 2, 1, 11, 12, 0);
    fill(dc, x0 + 3, 2, 9, 10, RGB(255, 255, 255));
    fill(dc, x0 + 5, 4, 2, 2, RGB(255, 0, 0));
    fill(dc, x0 + 8, 7, 2, 3, RGB(0, 0, 255));
    fill(dc, x0 + 13, 3, 2, 11, RGB(128, 128, 128));
    fill(dc, x0 + 4, 13, 11, 1, RGB(128, 128, 128));
}

static void sec_disabled(void)
{
    HBITMAP strip = CreateCompatibleBitmap(hscr, 32, 15), ostrip;
    HBITMAP mask = CreateBitmap(32, 15, 1, 1, 0), omask;
    HBITMAP one = CreateBitmap(16, 15, 1, 1, 0);
    HDC sd = CreateCompatibleDC(0), md = CreateCompatibleDC(0), od = CreateCompatibleDC(0);
    HBITMAP oone;
    HBRUSH ob;
    ostrip = SelectObject(sd, strip);
    omask = SelectObject(md, mask);
    icon(sd, 0);
    icon(sd, 16);
    /* the mask: the strip's background (PALETTERGB silver) white */
    SetBkColor(sd, 0x02C0C0C0L);
    BitBlt(md, 0, 0, 32, 15, sd, 0, 0, SRCCOPY);
    SetBkColor(sd, RGB(255, 255, 255));
    BitBlt(sd, 0, 0, 32, 15, md, 0, 0, 0x00220326L);     /* DSna: background black */
    result(md, 32, 15, "disabled: mask");
    result(sd, 32, 15, "disabled: strip");
    /* an enabled button: SRCAND the mask, SRCPAINT the strip */
    fill(cdc, 0, 0, CW, CH, RGB(192, 192, 192));
    SetTextColor(cdc, 0);
    SetBkColor(cdc, RGB(255, 255, 255));
    BitBlt(cdc, 2, 0, 16, 15, md, 0, 0, SRCAND);
    BitBlt(cdc, 2, 0, 16, 15, sd, 0, 0, SRCPAINT);
    /* a disabled one */
    oone = SelectObject(od, one);
    BitBlt(od, 0, 0, 16, 15, md, 16, 0, SRCCOPY);
    SetBkColor(sd, RGB(255, 255, 255));
    BitBlt(od, 0, 0, 16, 15, sd, 16, 0, SRCPAINT);
    SetBkColor(sd, RGB(255, 255, 255));
    result(od, 16, 15, "disabled: one");
    ob = SelectObject(cdc, GetStockObject(WHITE_BRUSH));
    BitBlt(cdc, 25, 1, 16, 15, od, 0, 0, 0x00B8074AL);
    SelectObject(cdc, GetStockObject(GRAY_BRUSH));
    BitBlt(cdc, 24, 0, 16, 15, od, 0, 0, 0x00B8074AL);
    SelectObject(cdc, ob);
    result(cdc, CW, CH, "disabled: buttons");
    SelectObject(od, oone);
    SelectObject(md, omask);
    SelectObject(sd, ostrip);
    DeleteDC(od);
    DeleteDC(md);
    DeleteDC(sd);
    DeleteObject(one);
    DeleteObject(mask);
    DeleteObject(strip);
}

/* ---- pattern brushes ---- */

static struct { BITMAPINFOHEADER h; RGBQUAD c[16]; BYTE bits[64]; } dib;
static int dibbpp;

/* an 8 x 8 DIB: 1-bit (rows alternate 55 AA ... and a corner) or 4-bit
   (index (x + y) % 16, so every colour of the table) */
static HGLOBAL make_dib(int bpp, const COLORREF *cols, int pal)
{
    HGLOBAL g;
    BYTE FAR *p;
    int i, n = bpp == 1 ? 2 : 16, y, x;
    WORD hd = sizeof(BITMAPINFOHEADER) + n * (pal ? 2 : 4), stride = bpp == 1 ? 4 : 4;
    WORD sz = hd + stride * 8;
    dibbpp = bpp;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sz);
    p = GlobalLock(g);
    dib.h.biSize = sizeof(BITMAPINFOHEADER);
    dib.h.biWidth = 8;
    dib.h.biHeight = 8;
    dib.h.biPlanes = 1;
    dib.h.biBitCount = bpp;
    dib.h.biCompression = 0;
    dib.h.biSizeImage = 0;
    dib.h.biClrUsed = 0;
    dib.h.biClrImportant = 0;
    for (i = 0; i < sizeof(BITMAPINFOHEADER); i++)
        p[i] = ((BYTE *)&dib.h)[i];
    for (i = 0; i < n; i++) {
        if (pal)
            ((WORD FAR *)(p + sizeof(BITMAPINFOHEADER)))[i] = (WORD)cols[i];
        else {
            BYTE FAR *q = p + sizeof(BITMAPINFOHEADER) + 4 * i;
            q[0] = GetBValue(cols[i]);
            q[1] = GetGValue(cols[i]);
            q[2] = GetRValue(cols[i]);
            q[3] = 0;
        }
    }
    for (y = 0; y < 8; y++) {
        BYTE FAR *r = p + hd + stride * y;     /* bottom-up: row 0 is the bottom */
        if (bpp == 1)
            r[0] = y == 0 ? 0xF0 : (y & 1) ? 0x55 : 0xAA;
        else
            for (x = 0; x < 8; x += 2)
                r[x / 2] = (BYTE)((((x + y) & 15) << 4) | ((x + 1 + y) & 15));
    }
    GlobalUnlock(g);
    return g;
}

static void brush_on(HDC dc, HBRUSH b, int w, int h, const char *name)
{
    HBRUSH ob = SelectObject(dc, b);
    PatBlt(dc, 0, 0, w, h, PATCOPY);
    SelectObject(dc, ob);
    result(dc, w, h, name);
}

static void sec_brushes(void)
{
    static const COLORREF sw[2] = { 0xC0C0C0L, 0xFFFFFFL };
    static const COLORREF rb[2] = { 0x0000FFL, 0xFF0000L };
    static const COLORREF odd[2] = { RGB(200, 100, 50), RGB(30, 160, 220) };
    static const COLORREF palidx[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    COLORREF all[16];
    HGLOBAL g;
    HBRUSH b;
    HBITMAP pb, opb;
    HDC pd;
    int i;
    static const WORD chk[8] = { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA };
    static const WORD cor[8] = { 0xF0, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
    for (i = 0; i < 16; i++)
        all[i] = vga[i];

    /* CreatePatternBrush of a monochrome bitmap */
    pb = CreateBitmap(8, 8, 1, 1, cor);
    b = CreatePatternBrush(pb);
    colours(cdc, 0, RGB(255, 255, 255));
    brush_on(cdc, b, 24, 16, "mono pattern black/white");
    colours(cdc, RGB(255, 0, 0), RGB(0, 0, 255));
    brush_on(cdc, b, 24, 16, "mono pattern red/blue");
    colours(cdc, RGB(200, 200, 200), RGB(100, 100, 100));
    brush_on(cdc, b, 24, 16, "mono pattern 200/100 grey");
    colours(mdc, 0, RGB(255, 255, 255));
    brush_on(mdc, b, 24, 16, "mono pattern on mono");
    colours(mdc, RGB(255, 255, 255), 0);
    brush_on(mdc, b, 24, 16, "mono pattern on mono reversed");
    colours(mdc, 0, RGB(255, 255, 255));
    colours(cdc, 0, RGB(255, 255, 255));
    DeleteObject(b);
    DeleteObject(pb);
    /* the 55/AA checkerboard, as StarView's */
    pb = CreateBitmap(8, 8, 1, 1, chk);
    b = CreatePatternBrush(pb);
    brush_on(cdc, b, 24, 16, "checkerboard black/white");
    DeleteObject(b);
    DeleteObject(pb);

    /* a colour pattern bitmap, on a colour and a monochrome DC */
    pb = CreateCompatibleBitmap(hscr, 8, 8);
    pd = CreateCompatibleDC(0);
    opb = SelectObject(pd, pb);
    for (i = 0; i < 8; i++)
        fill(pd, i, 0, 1, 8, vga[(i * 5 + 3) & 15]);
    fill(pd, 0, 0, 8, 2, RGB(255, 255, 255));
    fill(pd, 0, 6, 8, 1, RGB(192, 192, 192));
    SelectObject(pd, opb);
    DeleteDC(pd);
    b = CreatePatternBrush(pb);
    brush_on(cdc, b, 24, 16, "colour pattern");
    colours(mdc, 0, RGB(255, 255, 255));
    brush_on(mdc, b, 24, 16, "colour pattern on mono bk white");
    colours(mdc, 0, RGB(192, 192, 192));
    brush_on(mdc, b, 24, 16, "colour pattern on mono bk silver");
    colours(mdc, 0, RGB(255, 255, 255));
    DeleteObject(b);
    DeleteObject(pb);

    /* DIB pattern brushes */
    g = make_dib(1, sw, 0);
    b = CreateDIBPatternBrush(g, DIB_RGB_COLORS);
    GlobalFree(g);
    obs("DIB brush silver/white made", b != 0);
    colours(cdc, 0, RGB(255, 255, 255));
    brush_on(cdc, b, 24, 16, "DIB 1 silver/white");
    colours(cdc, RGB(255, 0, 0), RGB(0, 255, 0));
    brush_on(cdc, b, 24, 16, "DIB 1 silver/white, red/green");
    colours(cdc, 0, RGB(255, 255, 255));
    colours(mdc, 0, RGB(255, 255, 255));
    brush_on(mdc, b, 24, 16, "DIB 1 silver/white on mono");
    colours(mdc, RGB(255, 255, 255), 0);
    brush_on(mdc, b, 24, 16, "DIB 1 silver/white on mono reversed");
    colours(mdc, 0, RGB(255, 255, 255));
    DeleteObject(b);
    g = make_dib(1, rb, 0);
    b = CreateDIBPatternBrush(g, DIB_RGB_COLORS);
    GlobalFree(g);
    brush_on(cdc, b, 24, 16, "DIB 1 red/blue");
    brush_on(mdc, b, 24, 16, "DIB 1 red/blue on mono");
    DeleteObject(b);
    g = make_dib(1, odd, 0);
    b = CreateDIBPatternBrush(g, DIB_RGB_COLORS);
    GlobalFree(g);
    brush_on(cdc, b, 24, 16, "DIB 1 non-VGA colours");
    DeleteObject(b);
    g = make_dib(1, palidx + 7, 1);
    b = CreateDIBPatternBrush(g, DIB_PAL_COLORS);
    GlobalFree(g);
    brush_on(cdc, b, 24, 16, "DIB 1 PAL 7/8");
    brush_on(mdc, b, 24, 16, "DIB 1 PAL 7/8 on mono");
    DeleteObject(b);
    g = make_dib(4, all, 0);
    b = CreateDIBPatternBrush(g, DIB_RGB_COLORS);
    GlobalFree(g);
    brush_on(cdc, b, 24, 16, "DIB 4");
    brush_on(mdc, b, 24, 16, "DIB 4 on mono");
    colours(mdc, 0, RGB(192, 192, 192));
    brush_on(mdc, b, 24, 16, "DIB 4 on mono bk silver");
    colours(mdc, 0, RGB(255, 255, 255));
    DeleteObject(b);
    g = make_dib(4, palidx, 1);
    b = CreateDIBPatternBrush(g, DIB_PAL_COLORS);
    GlobalFree(g);
    brush_on(cdc, b, 24, 16, "DIB 4 PAL");
    DeleteObject(b);

    /* CreateBrushIndirect with BS_DIBPATTERN (StarView's page surround),
       the DIB freed at once; GetObject's LOGBRUSH */
    {
        LOGBRUSH lb, got;
        g = make_dib(1, sw, 0);
        lb.lbStyle = BS_DIBPATTERN;
        lb.lbColor = DIB_RGB_COLORS;
        lb.lbHatch = (int)g;
        b = CreateBrushIndirect(&lb);
        GlobalFree(g);
        obs("BS_DIBPATTERN made", b != 0);
        got.lbStyle = 0x5555;
        got.lbColor = 0x55555555L;
        obs("BS_DIBPATTERN GetObject", GetObject(b, sizeof got, &got));
        obs("BS_DIBPATTERN lbStyle", got.lbStyle);
        obs("BS_DIBPATTERN lbColor lo", LOWORD(got.lbColor));
        obs("BS_DIBPATTERN lbColor hi", HIWORD(got.lbColor));
        obs_env("BS_DIBPATTERN lbHatch", got.lbHatch);
        brush_on(cdc, b, 24, 16, "BS_DIBPATTERN silver/white");
        brush_on(mdc, b, 24, 16, "BS_DIBPATTERN silver/white on mono");
        DeleteObject(b);
        g = make_dib(1, palidx + 1, 1);
        lb.lbColor = DIB_PAL_COLORS;
        lb.lbHatch = (int)g;
        b = CreateBrushIndirect(&lb);
        GlobalFree(g);
        brush_on(cdc, b, 24, 16, "BS_DIBPATTERN PAL 1/2");
        DeleteObject(b);
        g = make_dib(1, rb, 0);
        b = CreateDIBPatternBrush(g, DIB_RGB_COLORS);
        GlobalFree(g);
        got.lbStyle = 0x5555;
        got.lbColor = 0x55555555L;
        GetObject(b, sizeof got, &got);
        obs("CreateDIBPatternBrush lbStyle", got.lbStyle);
        obs("CreateDIBPatternBrush lbColor lo", LOWORD(got.lbColor));
        obs("CreateDIBPatternBrush lbColor hi", HIWORD(got.lbColor));
        DeleteObject(b);
    }

    /* solid brushes made and first used on the monochrome DC, and made on
       the colour DC first (as VG1 does) */
    {
        static const COLORREF sc[8] = { RGB(128, 128, 128), RGB(192, 192, 192), RGB(100, 100, 100),
            RGB(200, 200, 200), RGB(60, 180, 240), RGB(255, 0, 0), RGB(128, 128, 64), RGB(0, 0, 128) };
        char nm[40];
        static const char hx[] = "0123456789ABCDEF";
        for (i = 0; i < 8; i++) {
            int k;
            lstrcpy(nm, "solid on mono 000000");
            for (k = 0; k < 6; k++)
                nm[14 + k] = hx[(sc[i] >> (20 - 4 * k)) & 15];
            b = CreateSolidBrush(sc[i]);
            brush_on(mdc, b, 8, 8, nm);
            DeleteObject(b);
            b = CreateSolidBrush(sc[i]);
            lstrcpy(nm, "solid on colour 000000");
            for (k = 0; k < 6; k++)
                nm[16 + k] = hx[(sc[i] >> (20 - 4 * k)) & 15];
            brush_on(cdc, b, 8, 8, nm);
            lstrcpy(nm, "solid colour, then mono 000000");
            for (k = 0; k < 6; k++)
                nm[24 + k] = hx[(sc[i] >> (20 - 4 * k)) & 15];
            brush_on(mdc, b, 8, 8, nm);
            DeleteObject(b);
        }
        brush_on(mdc, GetStockObject(GRAY_BRUSH), 8, 8, "stock gray on mono");
        brush_on(mdc, GetStockObject(LTGRAY_BRUSH), 8, 8, "stock ltgray on mono");
        brush_on(mdc, GetStockObject(DKGRAY_BRUSH), 8, 8, "stock dkgray on mono");
    }

    /* the brush origin */
    obs("memory DC brush origin x", LOWORD(GetBrushOrg(cdc)));
    obs("memory DC brush origin y", HIWORD(GetBrushOrg(cdc)));
    pb = CreateBitmap(8, 8, 1, 1, cor);
    b = CreatePatternBrush(pb);
    SetBrushOrg(cdc, 3, 2);
    UnrealizeObject(b);
    brush_on(cdc, b, 24, 16, "brush origin 3,2");
    SetBrushOrg(cdc, 0, 0);
    brush_on(cdc, b, 24, 16, "brush origin back, not unrealized");
    UnrealizeObject(b);
    brush_on(cdc, b, 24, 16, "brush origin 0,0");
    {
        HBRUSH ob = SelectObject(cdc, b);
        PatBlt(cdc, 0, 0, 24, 16, WHITENESS);
        PatBlt(cdc, 3, 5, 13, 7, PATCOPY);
        SelectObject(cdc, ob);
        result(cdc, 24, 16, "pattern at 3,5");
    }
    DeleteObject(b);
    DeleteObject(pb);
}

/* the same brushes on the screen, in a window at an odd place (the pattern
   follows the screen, or the window?) */
static void screen_at(int x, int y, const char *name)
{
    HWND w = CreateWindow("STATIC", "", WS_POPUP | WS_VISIBLE, x, y, 60, 30, 0, 0, hinst, 0);
    HDC dc = GetDC(w);
    static const WORD cor[8] = { 0xF0, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
    HBITMAP pb = CreateBitmap(8, 8, 1, 1, cor);
    HBRUSH b = CreatePatternBrush(pb), ob;
    char n[64];
    UpdateWindow(w);
    lstrcpy(n, name);
    lstrcat(n, " brush origin x");
    obs(n, LOWORD(GetBrushOrg(dc)));
    lstrcpy(n, name);
    lstrcat(n, " brush origin y");
    obs(n, HIWORD(GetBrushOrg(dc)));
    ob = SelectObject(dc, b);
    PatBlt(dc, 0, 0, 24, 16, PATCOPY);
    SelectObject(dc, ob);
    BitBlt(cdc, 0, 0, 24, 16, dc, 0, 0, SRCCOPY);
    result(cdc, 24, 16, name);
    ReleaseDC(w, dc);
    DestroyWindow(w);
    DeleteObject(b);
    DeleteObject(pb);
}

static void sec_screen(void)
{
    HWND w = CreateWindow("STATIC", "", WS_POPUP | WS_VISIBLE, 37, 53, 60, 30, 0, 0, hinst, 0);
    HDC dc = GetDC(w);
    static const WORD cor[8] = { 0xF0, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
    HBITMAP pb = CreateBitmap(8, 8, 1, 1, cor);
    HBRUSH b = CreatePatternBrush(pb), ob;
    screen_at(40, 48, "screen window at 40,48");
    screen_at(33, 50, "screen window at 33,50");
    screen_at(2, 3, "screen window at 2,3");
    UpdateWindow(w);
    ob = SelectObject(dc, b);
    PatBlt(dc, 0, 0, 24, 16, PATCOPY);
    SelectObject(dc, ob);
    BitBlt(cdc, 0, 0, 24, 16, dc, 0, 0, SRCCOPY);
    result(cdc, 24, 16, "screen window at 37,53");
    UnrealizeObject(b);
    SetBrushOrg(dc, 37 & 7, 53 & 7);
    ob = SelectObject(dc, b);
    PatBlt(dc, 0, 0, 24, 16, PATCOPY);
    SelectObject(dc, ob);
    BitBlt(cdc, 0, 0, 24, 16, dc, 0, 0, SRCCOPY);
    result(cdc, 24, 16, "screen window, origin set");
    /* the brush, realized here, in another window without
       UnrealizeObject: does it keep this window's origin? */
    {
        HWND w2 = CreateWindow("STATIC", "", WS_POPUP | WS_VISIBLE, 40, 120, 60, 30, 0, 0, hinst, 0);
        HDC dc2;
        UpdateWindow(w2);
        dc2 = GetDC(w2);
        ob = SelectObject(dc2, b);
        PatBlt(dc2, 0, 0, 24, 16, PATCOPY);
        SelectObject(dc2, ob);
        BitBlt(cdc, 0, 0, 24, 16, dc2, 0, 0, SRCCOPY);
        result(cdc, 24, 16, "second window at 40,120, not unrealized");
        /* and in a memory DC */
        brush_on(sdc, b, 24, 16, "memory DC, not unrealized");
        UnrealizeObject(b);
        brush_on(sdc, b, 24, 16, "memory DC, unrealized");
        ob = SelectObject(dc2, b);
        PatBlt(dc2, 0, 0, 24, 16, PATCOPY);
        SelectObject(dc2, ob);
        BitBlt(cdc, 0, 0, 24, 16, dc2, 0, 0, SRCCOPY);
        result(cdc, 24, 16, "second window after the memory DC");
        ReleaseDC(w2, dc2);
        DestroyWindow(w2);
    }
    ReleaseDC(w, dc);
    DestroyWindow(w);
    DeleteObject(b);
    DeleteObject(pb);
}

int __cdecl tmain(void)
{
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("GD14 start\r\n");
    hscr = GetDC(NULL);
    cdc = CreateCompatibleDC(hscr);
    cbm = CreateCompatibleBitmap(hscr, CW, CH);
    cold = SelectObject(cdc, cbm);
    mdc = CreateCompatibleDC(hscr);
    mbm = CreateBitmap(CW, CH, 1, 1, 0);
    mold = SelectObject(mdc, mbm);
    sdc = CreateCompatibleDC(hscr);
    sbm = CreateCompatibleBitmap(hscr, CW, CH);
    sold = SelectObject(sdc, sbm);
    out("GD14 c2m\r\n");
    sec_c2m();
    out("GD14 m2c\r\n");
    sec_m2c();
    out("GD14 disabled\r\n");
    sec_disabled();
    out("GD14 brushes\r\n");
    sec_brushes();
    out("GD14 screen\r\n");
    sec_screen();
    SelectObject(sdc, sold);
    DeleteObject(sbm);
    DeleteDC(sdc);
    SelectObject(mdc, mold);
    DeleteObject(mbm);
    DeleteDC(mdc);
    SelectObject(cdc, cold);
    DeleteObject(cbm);
    DeleteDC(cdc);
    ReleaseDC(NULL, hscr);
    out("GD14 done\r\n");
    return failures;
}
