/*
 * gd7.c - DIBs drawn on the 16-colour VGA, as programs draw them: 1, 4, 8
 * and 24-bit and RLE4/RLE8 DIBs, with DIB_RGB_COLORS and DIB_PAL_COLORS
 * (the default palette, and a palette of its own selected, realized or
 * not), through SetDIBits, CreateDIBitmap + BitBlt, SetDIBitsToDevice (a
 * memory DC and the screen), StretchDIBits (1:1, 2:1, part of the source,
 * SRCINVERT); RLE with the delta, end-of-line and end-of-bitmap escapes
 * over pixels already there, RLE with a starting scan line, a DIB over
 * 64K. Each result is read back with GetPixel and hashed (one checked
 * value a case; the rows' hashes unchecked, to find where two runs part).
 *
 * Written for QuickTime 2.1.2's installer, whose banner (a 487 x 74 RLE4
 * DIB given to CreateDIBitmap, DIB_RGB_COLORS, then BitBlt'ed) came out
 * black on w16: w16's VGA.DRV refused RLE in DeviceBitmapBits. Run as
 * the shell of Windows 3.1 with Microsoft's GDI and VGA.DRV (the
 * reference: tools/obsref.py of its log is gd7ref.h), and with w16's GDI,
 * w16's VGA.DRV or both (tests/gdiheap.sh -p build/dos/GD7.EXE [-r] [-v];
 * the boot-test case gdidib).
 */
#include <windows.h>

#define TK_REF "gd7ref.h"
#include "tklog.h"

extern WORD __cdecl __far get_ahincr(void);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define CW 40                           /* the colour memory bitmap read back */
#define CH 14

static HINSTANCE hinst;
static HDC hscr, cdc;
static HBITMAP cbm, cold;

/* the DIB: a header and up to 256 RGBQUADs or WORD indices */
static struct { BITMAPINFOHEADER h; RGBQUAD c[256]; } bi;
static BYTE bits[2048];
static WORD nbits;                      /* bytes of bits used */

static void zero(void FAR *p, WORD n)
{
    BYTE FAR *q = p;
    while (n--)
        *q++ = 0;
}

static void out_byte(BYTE b)
{
    char t[3];
    t[0] = "0123456789ABCDEF"[b >> 4];
    t[1] = "0123456789ABCDEF"[b & 15];
    t[2] = 0;
    out(t);
}

static void obs_dw_env(const char *name, DWORD v)
{
    out("OBS ~");
    out(name);
    out("=");
    outhex(HIWORD(v));
    outhex(LOWORD(v));
    out("\r\n");
}

/* a pixel into a running hash */
static WORD mix(WORD h, DWORD c)
{
    return (WORD)((h << 5) | (h >> 11)) + LOWORD(c) + HIWORD(c) * 3 + 1;
}

/* the VGA's 16 colours as letters (anything else '?') */
static char letter(DWORD c)
{
    static const DWORD vga[16] = {
        0x000000L, 0x000080L, 0x008000L, 0x008080L, 0x800000L, 0x800080L, 0x808000L, 0xC0C0C0L,
        0x808080L, 0x0000FFL, 0x00FF00L, 0x00FFFFL, 0xFF0000L, 0xFF00FFL, 0xFFFF00L, 0xFFFFFFL };
    int i;
    for (i = 0; i < 16; i++)
        if (vga[i] == c)
            return "0123456789ABCDEF"[i];
    return '?';
}

static int unchecked;                   /* the result depends on a new bitmap's memory */

/* the area [0, CW) x [0, CH) of cdc, hashed (checked); its rows as
   letters (unchecked, to see where two runs part) */
static void result(const char *name)
{
    WORD h = 0, y, x;
    char row[CW + 1];
    for (y = 0; y < CH; y++) {
        for (x = 0; x < CW; x++) {
            DWORD c = GetPixel(cdc, x, y);
            h = mix(h, c);
            row[x] = letter(c);
        }
        row[CW] = 0;
        out("ROW ");
        out(row);
        out("\r\n");
    }
    if (unchecked)
        obs_env(name, h);
    else
        obs(name, h);
}

/* what was there before (magenta: in none of the DIBs) */
static void magenta(HDC dc, int w, int h)
{
    HBRUSH b = CreateSolidBrush(RGB(255, 0, 255)), ob = SelectObject(dc, b);
    PatBlt(dc, 0, 0, w, h, PATCOPY);
    SelectObject(dc, ob);
    DeleteObject(b);
}

static void clear_c(void)
{
    HBRUSH ob = SelectObject(cdc, GetStockObject(LTGRAY_BRUSH));
    PatBlt(cdc, 0, 0, CW, CH, PATCOPY);
    SelectObject(cdc, ob);
}

/* ---- the DIBs ---- */

enum { F1, F4, F8, F24, FR4, FR8, FR4ODD, FR4FULL, FR8FULL, FCORE, NF };
static const char *fname[NF] = { "1-bit", "4-bit", "8-bit", "24-bit", "RLE4", "RLE8",
                                 "RLE4 odd", "RLE4 full", "RLE8 full", "core 4-bit" };

static const BYTE rle8[] = {
    0x04, 0x05, 0x00, 0x03, 0x01, 0x02, 0x03, 0x00, 0x02, 0x07, 0x00, 0x00,  /* line 0, 9 */
    0x00, 0x02, 0x03, 0x01, 0x05, 0x09, 0x00, 0x00,                          /* delta to 3, line 2 */
    0x10, 0x0A, 0x00, 0x00,                                                  /* line 3, all */
    0x00, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00, 0x0B, 0x0C, 0x00, 0x00,  /* line 4 */
    0x03, 0x0E, 0x00, 0x01                                                   /* line 5, end */
};
static const BYTE rle4[] = {
    0x05, 0x12, 0x00, 0x03, 0x34, 0x50, 0x02, 0x66, 0x00, 0x00,
    0x00, 0x02, 0x02, 0x01, 0x04, 0x78, 0x00, 0x00,
    0x10, 0x9A, 0x00, 0x00,
    0x00, 0x05, 0x12, 0x34, 0x50, 0x00, 0x0B, 0xCD, 0x00, 0x00,
    0x03, 0xEF, 0x00, 0x01
};
/* every pixel given (the others skip some: those keep what was there,
   which for a new bitmap is whatever its memory held on 3.1) */
static const BYTE rle8full[] = {
    0x10, 0x01, 0x00, 0x00,
    0x00, 0x05, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x0B, 0x07, 0x00, 0x00,
    0x08, 0x08, 0x08, 0x09, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x00, 0x00,
    0x03, 0x0A, 0x00, 0x03, 0x0B, 0x0C, 0x0D, 0x00, 0x0A, 0x0E, 0x00, 0x00,
    0x10, 0x0F, 0x00, 0x01
};
static const BYTE rle4full[] = {
    0x10, 0x12, 0x00, 0x00,
    0x00, 0x05, 0x34, 0x56, 0x70, 0x00, 0x0B, 0x89, 0x00, 0x00,
    0x08, 0xAB, 0x08, 0xCD, 0x00, 0x00,
    0x00, 0x10, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x00, 0x00,
    0x03, 0xF0, 0x00, 0x03, 0x1E, 0x20, 0x0A, 0x3D, 0x00, 0x00,
    0x10, 0x4E, 0x00, 0x01
};
static const BYTE rle4odd[] = {                       /* 13 wide, 6 high */
    0x0D, 0x12, 0x00, 0x00,
    0x00, 0x07, 0x12, 0x34, 0x56, 0x70, 0x06, 0xAB, 0x00, 0x00,
    0x07, 0x3C, 0x00, 0x03, 0xDE, 0xF0, 0x03, 0x45, 0x00, 0x00,
    0x00, 0x0D, 0x9A, 0xBC, 0xDE, 0xF1, 0x23, 0x45, 0x60, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x00, 0x02, 0x04, 0x00, 0x02, 0x88, 0x00, 0x00,
    0x0D, 0x0F, 0x00, 0x00, 0x00, 0x01
};

/* colour table entry i of an n-colour table */
static void table(int n)
{
    int i;
    for (i = 0; i < n; i++) {
        bi.c[i].rgbRed = (BYTE)(i * 37 + 11);
        bi.c[i].rgbGreen = (BYTE)(i * 91 + 5);
        bi.c[i].rgbBlue = (BYTE)(255 - i * 13);
        bi.c[i].rgbReserved = 0;
    }
    /* some of the VGA's own colours */
    if (n >= 16) {
        bi.c[1].rgbRed = 255; bi.c[1].rgbGreen = 0; bi.c[1].rgbBlue = 0;
        bi.c[2].rgbRed = 0; bi.c[2].rgbGreen = 128; bi.c[2].rgbBlue = 0;
        bi.c[3].rgbRed = 255; bi.c[3].rgbGreen = 255; bi.c[3].rgbBlue = 255;
        bi.c[4].rgbRed = 0; bi.c[4].rgbGreen = 0; bi.c[4].rgbBlue = 0;
    }
}

/* the DIB of format f into bi/bits: 16 x 6 (13 x 6 for RLE4 odd) */
static void make(int f)
{
    int bpp, x, y;
    WORD stride;
    zero(&bi, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = f == FR4ODD ? 13 : 16;
    bi.h.biHeight = 6;
    bi.h.biPlanes = 1;
    bpp = f == F1 ? 1 : f == F8 || f == FR8 || f == FR8FULL ? 8 : f == F24 ? 24 : 4;
    bi.h.biBitCount = bpp;
    if (bpp <= 8)
        table(1 << bpp);
    stride = (WORD)((((int)bi.h.biWidth * bpp + 31) / 32) * 4);
    zero(bits, sizeof bits);
    if (f >= FR4 && f <= FR8FULL) {
        const BYTE *s = f == FR8 ? rle8 : f == FR4 ? rle4 : f == FR4ODD ? rle4odd
                        : f == FR4FULL ? rle4full : rle8full;
        nbits = f == FR8 ? sizeof rle8 : f == FR4 ? sizeof rle4 : f == FR4ODD ? sizeof rle4odd
                : f == FR4FULL ? sizeof rle4full : sizeof rle8full;
        for (x = 0; x < (int)nbits; x++)
            bits[x] = s[x];
        bi.h.biCompression = bpp == 8 ? BI_RLE8 : BI_RLE4;
        bi.h.biSizeImage = nbits;
        return;
    }
    nbits = stride * 6;
    for (y = 0; y < 6; y++)
        for (x = 0; x < (int)bi.h.biWidth; x++) {
            BYTE *r = bits + y * stride;
            WORD v = (WORD)(x * 7 + y * 13 + x * y);
            switch (bpp) {
            case 1:
                if (v & 1)
                    r[x >> 3] |= (BYTE)(0x80 >> (x & 7));
                break;
            case 4:
                r[x >> 1] |= (BYTE)(x & 1 ? v & 15 : (v & 15) << 4);
                break;
            case 8:
                r[x] = (BYTE)(v * 5);
                break;
            case 24:
                r[x * 3] = (BYTE)(v * 23);
                r[x * 3 + 1] = (BYTE)(v * 41 + 128);
                r[x * 3 + 2] = (BYTE)(v * 59);
                break;
            }
        }
    if (f == FCORE) {
        /* the same 4-bit DIB with a BITMAPCOREHEADER and RGBTRIPLEs */
        static BYTE t[12 + 16 * 3];
        int i;
        BITMAPCOREHEADER *c = (BITMAPCOREHEADER *)t;
        for (i = 0; i < 16; i++) {
            t[12 + i * 3] = bi.c[i].rgbBlue;
            t[12 + i * 3 + 1] = bi.c[i].rgbGreen;
            t[12 + i * 3 + 2] = bi.c[i].rgbRed;
        }
        c->bcSize = sizeof *c;
        c->bcWidth = 16;
        c->bcHeight = 6;
        c->bcPlanes = 1;
        c->bcBitCount = 4;
        zero(&bi, sizeof bi);
        for (i = 0; i < (int)sizeof t; i++)
            ((BYTE *)&bi)[i] = t[i];
    }
}

/* DIB_PAL_COLORS: the table made WORD indices (0..n-1 in a scrambled order) */
static void pal_indices(void)
{
    WORD *w;
    int n, i, hdr;
    if (bi.h.biSize == sizeof(BITMAPCOREHEADER)) {
        hdr = sizeof(BITMAPCOREHEADER);
        n = 16;
    } else {
        hdr = sizeof(BITMAPINFOHEADER);
        n = bi.h.biBitCount > 8 ? 0 : 1 << bi.h.biBitCount;
    }
    w = (WORD *)((BYTE *)&bi + hdr);
    for (i = 0; i < n; i++)
        w[i] = (WORD)((i * 5 + 3) % 20);   /* past a 16-entry palette too */
}

/* ---- the ways to draw ---- */

enum { P_SETDIBITS, P_CREATE, P_TODEV, P_TODEVSCR, P_STRETCH, P_STRETCH2, P_STRETCHPART,
       P_STRETCHINV, NP };
static const char *pname[NP] = { "SetDIBits", "CreateDIBitmap", "SetDIBitsToDevice",
                                 "SetDIBitsToDevice screen", "StretchDIBits",
                                 "StretchDIBits 2:1", "StretchDIBits part", "StretchDIBits SRCINVERT" };

static int dib_w(void)
{
    return bi.h.biSize == sizeof(BITMAPCOREHEADER) ? ((BITMAPCOREHEADER *)&bi)->bcWidth
                                                   : (int)bi.h.biWidth;
}

static int dib_h(void)
{
    return bi.h.biSize == sizeof(BITMAPCOREHEADER) ? ((BITMAPCOREHEADER *)&bi)->bcHeight
                                                   : (int)bi.h.biHeight;
}

static HPALETTE pal;                    /* the palette of its own, or 0 */
static int realize;

static HPALETTE pal_on(HDC dc)
{
    HPALETTE o;
    if (!pal)
        return 0;
    o = SelectPalette(dc, pal, FALSE);
    if (realize)
        RealizePalette(dc);
    return o;
}

static void pal_off(HDC dc, HPALETTE o)
{
    if (o)
        SelectPalette(dc, o, FALSE);
}

/* draw the DIB with path p into cdc at (2, 1) (rows from the top) and
   observe; start/n: the scan lines given (0, height: all) */
static void draw(int p, WORD usage, WORD start, WORD n, const char *name)
{
    int w = dib_w(), h = dib_h(), r = 0;
    HDC t;
    HBITMAP b, ob;
    HPALETTE op;
    char nm[64];
    int i = 0, k;
    clear_c();
    switch (p) {
    case P_SETDIBITS:
        b = CreateCompatibleBitmap(hscr, w, h);
        t = CreateCompatibleDC(hscr);
        ob = SelectObject(t, b);
        magenta(t, w, h);
        SelectObject(t, ob);
        op = pal_on(hscr);
        r = SetDIBits(hscr, b, start, n, bits, (BITMAPINFO FAR *)&bi, usage);
        pal_off(hscr, op);
        SelectObject(t, b);
        BitBlt(cdc, 2, 1, w, h, t, 0, 0, SRCCOPY);
        SelectObject(t, ob);
        DeleteDC(t);
        DeleteObject(b);
        break;
    case P_CREATE:
        op = pal_on(hscr);
        b = CreateDIBitmap(hscr, (BITMAPINFOHEADER FAR *)&bi, CBM_INIT, bits,
                           (BITMAPINFO FAR *)&bi, usage);
        pal_off(hscr, op);
        r = b != 0;
        if (b) {
            t = CreateCompatibleDC(hscr);
            ob = SelectObject(t, b);
            BitBlt(cdc, 2, 1, w, h, t, 0, 0, SRCCOPY);
            SelectObject(t, ob);
            DeleteDC(t);
            DeleteObject(b);
        }
        break;
    case P_TODEV:
        op = pal_on(cdc);
        r = SetDIBitsToDevice(cdc, 2, 1, w, h, 0, 0, start, n, bits, (BITMAPINFO FAR *)&bi, usage);
        pal_off(cdc, op);
        break;
    case P_TODEVSCR:
        /* the screen's top left (the probe is the shell: the desktop) */
        magenta(hscr, CW, CH);
        op = pal_on(hscr);
        r = SetDIBitsToDevice(hscr, 2, 1, w, h, 0, 0, start, n, bits, (BITMAPINFO FAR *)&bi, usage);
        pal_off(hscr, op);
        BitBlt(cdc, 0, 0, CW, CH, hscr, 0, 0, SRCCOPY);
        break;
    case P_STRETCH:
    case P_STRETCH2:
    case P_STRETCHPART:
    case P_STRETCHINV:
        op = pal_on(cdc);
        if (p == P_STRETCH)
            r = StretchDIBits(cdc, 2, 1, w, h, 0, 0, w, h, bits, (BITMAPINFO FAR *)&bi, usage, SRCCOPY);
        else if (p == P_STRETCH2)
            r = StretchDIBits(cdc, 2, 1, 2 * w, 2 * h, 0, 0, w, h, bits, (BITMAPINFO FAR *)&bi,
                              usage, SRCCOPY);
        else if (p == P_STRETCHPART)
            r = StretchDIBits(cdc, 2, 1, 8, 3, 3, 1, 8, 3, bits, (BITMAPINFO FAR *)&bi, usage, SRCCOPY);
        else
            r = StretchDIBits(cdc, 2, 1, w, h, 0, 0, w, h, bits, (BITMAPINFO FAR *)&bi, usage, SRCINVERT);
        pal_off(cdc, op);
        break;
    }
    for (k = 0; name[k] && i < 48; k++)
        nm[i++] = name[k];
    nm[i++] = ':';
    nm[i++] = ' ';
    for (k = 0; pname[p][k] && i < 63; k++)
        nm[i++] = pname[p][k];
    nm[i] = 0;
    result(nm);
    for (k = 0; k < 8 && i < 60; k++)
        nm[i++] = " return"[k];
    nm[i] = 0;
    obs(nm, (WORD)r);
}

static void sec_formats(const char *usage_name, WORD usage)
{
    int f, p, i, k;
    char nm[48];
    for (f = 0; f < NF; f++)
        for (p = 0; p < NP; p++) {
            make(f);
            if (usage == DIB_PAL_COLORS)
                pal_indices();
            i = 0;
            for (k = 0; fname[f][k]; k++)
                nm[i++] = fname[f][k];
            nm[i++] = ' ';
            for (k = 0; usage_name[k] && i < 47; k++)
                nm[i++] = usage_name[k];
            nm[i] = 0;
            /* RLE with skips into a new bitmap: 3.1 leaves the memory's
               old contents there */
            unchecked = (f == FR4 || f == FR8 || f == FR4ODD) &&
                        (p == P_CREATE || p >= P_STRETCH);
            draw(p, usage, 0, dib_h(), nm);
            unchecked = 0;
        }
}

/* scan lines from 2, 3 of them: RLE8 and 8-bit */
static void sec_start(void)
{
    make(FR8);
    draw(P_SETDIBITS, DIB_RGB_COLORS, 2, 3, "RLE8 lines 2-4");
    draw(P_TODEV, DIB_RGB_COLORS, 2, 3, "RLE8 lines 2-4");
    draw(P_TODEVSCR, DIB_RGB_COLORS, 2, 3, "RLE8 lines 2-4");
    make(FR4);
    draw(P_SETDIBITS, DIB_RGB_COLORS, 2, 3, "RLE4 lines 2-4");
    draw(P_TODEV, DIB_RGB_COLORS, 2, 3, "RLE4 lines 2-4");
    draw(P_TODEVSCR, DIB_RGB_COLORS, 2, 3, "RLE4 lines 2-4");
    make(F8);
    draw(P_SETDIBITS, DIB_RGB_COLORS, 2, 3, "8-bit lines 2-4");
    draw(P_TODEV, DIB_RGB_COLORS, 2, 3, "8-bit lines 2-4");
    draw(P_TODEVSCR, DIB_RGB_COLORS, 2, 3, "8-bit lines 2-4");
}

/* SetDIBitsToDevice on the screen: a clip rectangle; a destination
   smaller than the DIB; part of it */
static void sec_clip(void)
{
    static const char *nm[3] = { "clip 4,2-12,5", "cy 3", "part 3,1 8x3" };
    int f, k;
    for (f = 0; f < 3; f++)
        for (k = 0; k < 3; k++) {
            char n[48];
            int i = 0, j, r;
            make(f == 0 ? FR8FULL : f == 1 ? FR4FULL : F8);
            for (j = 0; fname[f == 0 ? FR8FULL : f == 1 ? FR4FULL : F8][j]; j++)
                n[i++] = fname[f == 0 ? FR8FULL : f == 1 ? FR4FULL : F8][j];
            n[i++] = ' ';
            for (j = 0; nm[k][j]; j++)
                n[i++] = nm[k][j];
            n[i] = 0;
            clear_c();
            magenta(hscr, CW, CH);
            SaveDC(hscr);
            if (k == 0)
                IntersectClipRect(hscr, 4, 2, 12, 5);
            if (k == 2)
                r = SetDIBitsToDevice(hscr, 2, 1, 8, 3, 3, 1, 0, 6, bits, (BITMAPINFO FAR *)&bi,
                                      DIB_RGB_COLORS);
            else
                r = SetDIBitsToDevice(hscr, 2, 1, 16, k ? 3 : 6, 0, 0, 0, 6, bits,
                                      (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
            RestoreDC(hscr, -1);
            BitBlt(cdc, 0, 0, CW, CH, hscr, 0, 0, SRCCOPY);
            result(n);
            obs(n[0] == '8' ? "8-bit screen return" : "RLE screen return", r);
        }
}

/* GetDIBits asked for RLE (DDK: the driver encodes) */
static void sec_getrle(void)
{
    static BYTE buf[512];
    HBITMAP b;
    int r, z;
    make(F4);
    b = CreateDIBitmap(hscr, (BITMAPINFOHEADER FAR *)&bi, CBM_INIT, bits, (BITMAPINFO FAR *)&bi,
                       DIB_RGB_COLORS);
    for (z = 0; z < 2; z++) {
        int bpp = z ? 8 : 4;
        WORD i, h = 0;
        make(F4);
        bi.h.biBitCount = bpp;
        bi.h.biCompression = z ? BI_RLE8 : BI_RLE4;
        bi.h.biSizeImage = sizeof buf;
        for (i = 0; i < sizeof buf; i++)
            buf[i] = 0xEE;
        r = GetDIBits(hscr, b, 0, 6, buf, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
        obs(z ? "GetDIBits RLE8 return" : "GetDIBits RLE4 return", (WORD)r);
        obs(z ? "GetDIBits RLE8 biSizeImage" : "GetDIBits RLE4 biSizeImage", (WORD)bi.h.biSizeImage);
        obs(z ? "GetDIBits RLE8 biCompression" : "GetDIBits RLE4 biCompression",
            (WORD)bi.h.biCompression);
        for (i = 0; i < sizeof buf; i++)
            h = mix(h, buf[i]);
        /* the bytes, unchecked */
        out(z ? "RLE8 BYTES" : "RLE4 BYTES");
        for (i = 0; i < 160; i++) {
            out(i % 32 ? " " : "\r\n ");
            out_byte(buf[i]);
        }
        out("\r\n");
        obs(z ? "GetDIBits RLE8 bytes" : "GetDIBits RLE4 bytes", h);
    }
    DeleteObject(b);
}

/* an 8-bit DIB of 200 x 400 (80000 bytes: lines across the 64K boundary) */
static void sec_huge(void)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 80000L);
    BYTE FAR *p;
    WORD sel, ah = get_ahincr(), h, x, y;
    HBITMAP b;
    HDC t;
    HBITMAP ob;
    int r;
    if (!g)
        return;
    p = GlobalLock(g);
    sel = SELECTOROF(p);
    {
        WORD off = 0, seg = 0;
        for (y = 0; y < 400; y++)
            for (x = 0; x < 200; x++) {
                *(BYTE FAR *)MAKELP(sel + seg * ah, off) = (BYTE)(x / 13 + y / 7 * 3);
                if (!++off)
                    seg++;
            }
    }
    zero(&bi, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = 200;
    bi.h.biHeight = 400;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 8;
    table(256);
    for (r = 0; r < 2; r++) {
        b = CreateCompatibleBitmap(hscr, 200, 400);
        t = CreateCompatibleDC(hscr);
        ob = SelectObject(t, b);
        PatBlt(t, 0, 0, 200, 400, WHITENESS);
        if (r) {
            /* on the screen (a memory DC draws nothing), read back */
            PatBlt(hscr, 0, 0, 200, 400, WHITENESS);
            obs("huge SetDIBitsToDevice", SetDIBitsToDevice(hscr, 0, 0, 200, 400, 0, 0, 0, 400, p,
                                                            (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS));
            BitBlt(t, 0, 0, 200, 400, hscr, 0, 0, SRCCOPY);
        } else {
            SelectObject(t, ob);
            obs("huge SetDIBits", SetDIBits(hscr, b, 0, 400, p, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS));
            SelectObject(t, b);
        }
        /* lines 60-80 from the top hold DIB lines 320-339 (the boundary
           is in line 327) */
        h = 0;
        for (y = 50; y < 90; y++)
            for (x = 0; x < 200; x += 3) {
                h = mix(h, GetPixel(t, x, y));
            }
        obs(r ? "huge SetDIBitsToDevice: boundary lines" : "huge SetDIBits: boundary lines", h);
        h = 0;
        for (y = 0; y < 400; y += 9)
            for (x = 1; x < 200; x += 11) {
                h = mix(h, GetPixel(t, x, y));
            }
        obs(r ? "huge SetDIBitsToDevice: all" : "huge SetDIBits: all", h);
        SelectObject(t, ob);
        DeleteDC(t);
        DeleteObject(b);
    }
    GlobalUnlock(g);
    GlobalFree(g);
}

/* a new bitmap's pixels; SetDIBitsToDevice on a monochrome memory DC */
static void sec_more(void)
{
    HBITMAP b = CreateCompatibleBitmap(hscr, 8, 2), ob;
    HDC t = CreateCompatibleDC(hscr);
    WORD x, h = 0;
    int f;
    ob = SelectObject(t, b);
    /* 3.1: what the memory held (w16: black) */
    obs_dw_env("new compatible bitmap pixel", GetPixel(t, 0, 0));
    SelectObject(t, ob);
    DeleteObject(b);
    b = CreateBitmap(48, 8, 1, 1, NULL);
    ob = SelectObject(t, b);
    for (f = 0; f < NF; f++) {
        char nm[40];
        int i = 0, k;
        make(f);
        PatBlt(t, 0, 0, 48, 8, WHITENESS);
        PatBlt(t, 0, 0, 48, 2, BLACKNESS);
        k = SetDIBitsToDevice(t, 2, 1, dib_w(), dib_h(), 0, 0, 0, dib_h(), bits,
                              (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
        h = 0;
        out("MONO");
        for (x = 0; x < 48 * 8; x++) {
            DWORD c = GetPixel(t, x % 48, x / 48);
            h = mix(h, c);
            if (x % 48 == 0)
                out("\r\n ");
            out(c ? "1" : "0");
        }
        out("\r\n");
        for (i = 0; fname[f][i]; i++)
            nm[i] = fname[f][i];
        for (k = 0; " mono SetDIBitsToDevice"[k]; k++)
            nm[i++] = " mono SetDIBitsToDevice"[k];
        nm[i] = 0;
        obs(nm, h);
    }
    SelectObject(t, ob);
    DeleteObject(b);
    DeleteDC(t);
}

/* the shape of QuickTime's banner: an RLE4 DIB of odd width, long runs */
static void sec_banner(void)
{
    static const BYTE l[] = { 0x7F, 0x13, 0x7F, 0x31, 0x7F, 0x57, 0x6A, 0x9C, 0x00, 0x00 };
    HBITMAP b, ob;
    HDC t;
    WORD i, y, x, h = 0;
    BYTE *q = bits;
    make(FR4);
    bi.h.biWidth = 487;
    bi.h.biHeight = 10;
    for (y = 0; y < 10; y++) {
        for (i = 0; i < sizeof l; i++)
            *q++ = l[i];
        q[-9] = (BYTE)(0x10 + y);       /* each line its own colours */
    }
    q[-2] = 0;
    q[-1] = 1;
    bi.h.biSizeImage = q - bits;
    b = CreateDIBitmap(hscr, (BITMAPINFOHEADER FAR *)&bi, CBM_INIT, bits, (BITMAPINFO FAR *)&bi,
                       DIB_RGB_COLORS);
    t = CreateCompatibleDC(hscr);
    ob = SelectObject(t, b);
    for (y = 0; y < 10; y++)
        for (x = 0; x < 487; x += 2) {
            h = mix(h, GetPixel(t, x, y));
        }
    obs("banner-shaped RLE4", h);
    SelectObject(t, ob);
    DeleteDC(t);
    DeleteObject(b);
}

int __cdecl tmain(void)
{
    struct { WORD v, n; PALETTEENTRY e[20]; } lp;
    int i;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("GD7 start\r\n");
    hscr = GetDC(NULL);
    cdc = CreateCompatibleDC(hscr);
    cbm = CreateCompatibleBitmap(hscr, CW, CH);
    cold = SelectObject(cdc, cbm);
    out("GD7 rgb\r\n");
    sec_formats("RGB", DIB_RGB_COLORS);
    out("GD7 pal default\r\n");
    sec_formats("PAL default", DIB_PAL_COLORS);
    lp.v = 0x300;
    lp.n = 20;
    for (i = 0; i < 20; i++) {
        lp.e[i].peRed = (BYTE)(i * 53);
        lp.e[i].peGreen = (BYTE)(255 - i * 29);
        lp.e[i].peBlue = (BYTE)(i * 97 + 40);
        lp.e[i].peFlags = 0;
    }
    pal = CreatePalette((LOGPALETTE FAR *)&lp);
    out("GD7 pal own\r\n");
    sec_formats("PAL own", DIB_PAL_COLORS);
    sec_formats("RGB, palette", DIB_RGB_COLORS);
    realize = 1;
    out("GD7 pal realized\r\n");
    sec_formats("PAL realized", DIB_PAL_COLORS);
    realize = 0;
    DeleteObject(pal);
    pal = 0;
    out("GD7 start lines\r\n");
    sec_start();
    out("GD7 clip\r\n");
    sec_clip();
    out("GD7 getrle\r\n");
    sec_getrle();
    out("GD7 more\r\n");
    sec_more();
    out("GD7 banner\r\n");
    sec_banner();
    out("GD7 huge\r\n");
    sec_huge();
    SelectObject(cdc, cold);
    DeleteObject(cbm);
    DeleteDC(cdc);
    ReleaseDC(NULL, hscr);
    out("GD7 done\r\n");
    return failures;
}
