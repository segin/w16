/*
 * color.c - colours: the 16 physical colours, ColorInfo, RealizeObject
 * (pens and brushes; no device fonts or device bitmaps), solid colours'
 * dither patterns.
 *
 * A colour index is the four planes' bits of a pixel: bit 0 plane 0,
 * bit 1 plane 1, bit 2 plane 2, bit 3 plane 3. Which colour each index
 * is, as Windows 3.1's VGA.DRV has them (VG1: each colour's planes in a
 * bitmap and its GetDIBits index): plane 0 red, 1 green, 2 blue, 3
 * intensity, in the order of a 4-bit DIB's colour table (black, maroon,
 * green, olive, navy, purple, teal, gray, silver, red, lime, yellow,
 * blue, fuchsia, cyan, white: drivers/vga/resources.md), so a colour's
 * index is also its DIB index.
 */
#include "vga.h"

static const COLORREF colors[16] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0),
    RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128), RGB(128, 128, 128),
    RGB(192, 192, 192), RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0),
    RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255)
};

const BYTE dib_to_index[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
const BYTE index_to_dib[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

COLORREF index_rgb(int i)
{
    return colors[i & 15];
}

/* The nearest of the 16 colours (ColorInfo; GDI's GetNearestColor, pen and
   text colours). Windows 3.1's VGA.DRV's choice, found from VG3 (all 2^24
   colours through GetNearestColor) and reproduced for every one of them
   (tests/vgacolor.py checks this rule against such a table): with the
   components sorted max >= mid >= min,
   - max <= 64: black;
   - min + mid > max and 2 min >= mid (no component far from the others):
     gray (max <= 160), silver (max <= 223) or white;
   - otherwise the max's channel, and the mid's when it is nearer the max
     than the min (2 mid > max + min), dark (max < 192) or bright. */
int rgb_index(COLORREF c)
{
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int md = r + g + b - mx - mn, bits = 0;
    if (mx <= 64)
        return 0;
    if (mn + md > mx && 2 * mn >= md)
        return mx <= 160 ? 7 : mx < 224 ? 8 : 15;
    if (r == mx || (r == md && 2 * md > mx + mn))
        bits |= 1;
    if (g == mx || (g == md && 2 * md > mx + mn))
        bits |= 2;
    if (b == mx || (b == md && 2 * md > mx + mn))
        bits |= 4;
    return mx >= 192 ? bits + 8 : bits;
}

/* The colour on a monochrome bitmap (1 white): white exactly when the
   nearest colour is silver, lime, yellow, fuchsia, cyan or white (VG3:
   GetNearestColor of all 2^24 colours on a monochrome bitmap's DC). */
static const BYTE mono_of[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1 };

int rgb_mono(COLORREF c)
{
    return mono_of[rgb_index(c)];
}

DWORD rgb_to_pc(COLORREF c)
{
    int i = rgb_index(c);
    return MKPC(i, mono_of[i]);
}

/* the 8 x 8 ordered-dither matrix (the standard recursive Bayer matrix:
   threshold of each pixel, 0-63) */
static const BYTE bayer[8][8] = {
    { 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
    { 3, 35, 11, 43, 1, 33, 9, 41 }, { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 }
};

/* A solid brush's 8 x 8 pattern for colour c: Windows 3.1's VGA.DRV's
   dithering, found from VG1 (patterns of solid brushes on a colour and a
   monochrome bitmap) and reproduced for every colour tried (tests/
   vgacolor.py; drivers/vga/README.md):
   - one of the 16 colours exactly: that colour (silver only this way);
   - else each component c becomes q = (c + 1) / 4 (0-64), a dark part
     A = 2 min(q, 32) and a bright part B = 2 max(q - 32, 0), both 0-64;
     each of 64 units u takes, per component, 255 if u < B, else 128 if
     u >= 64 - A, else 0: a corner of the cube of 0, 128, 255. A corner
     that is one of the colours counts for it; one that mixes 128 and 255
     is half the colour with 0 for its 128s, half the one with 255 (the
     odd unit to the latter);
   - the colours, in index order, take the matrix's thresholds in turn
     (the first the lowest), as many as their units.
   On a monochrome bitmap: white where the threshold is below
   (R + 2 G + B + 4) / 16 (at most 64), except that levels 16 and 48 have
   a diagonal pattern of their own. */
void dither(COLORREF c, BYTE pat[4][8], BYTE mono[8])
{
    BYTE col[16], seq[64];
    int comp[3], A[3], B[3], q, k, u, i, n, x, y, p, lv;
    BYTE cnt[27];
    comp[0] = GetRValue(c);
    comp[1] = GetGValue(c);
    comp[2] = GetBValue(c);
    for (i = 0; i < 16; i++)
        col[i] = 0;
    for (i = 0; i < 16 && colors[i] != (c & 0xFFFFFFL); i++)
        ;
    if (i < 16)
        col[i] = 64;
    else {
        for (k = 0; k < 3; k++) {
            q = (comp[k] + 1) >> 2;
            A[k] = 2 * (q < 32 ? q : 32);
            B[k] = 2 * (q > 32 ? q - 32 : 0);
        }
        for (i = 0; i < 27; i++)
            cnt[i] = 0;
        for (u = 0; u < 64; u++) {
            int v = 0, m = 1;
            for (k = 0; k < 3; k++, m *= 3) {
                int on = u >= 64 - A[k];
                v += m * (on && u < B[k] ? 2 : on ? 1 : 0);
            }
            cnt[v]++;
        }
        for (i = 0; i < 27; i++) {
            int lo = 0, hi = 0, has1 = 0, has2 = 0, d = i;
            if (!cnt[i])
                continue;
            for (k = 0; k < 3; k++, d /= 3) {
                if (d % 3 == 1) {
                    has1 = 1;
                    hi |= 1 << k;
                } else if (d % 3 == 2) {
                    has2 = 1;
                    lo |= 1 << k;
                    hi |= 1 << k;
                }
            }
            if (!has2)
                col[hi] += cnt[i];              /* black or a dark colour */
            else if (!has1)
                col[lo + 8] += cnt[i];          /* a bright colour */
            else {
                col[lo + 8] += cnt[i] / 2;
                col[hi + 8] += cnt[i] - cnt[i] / 2;
            }
        }
    }
    n = 0;
    for (i = 0; i < 16; i++)
        for (k = 0; k < col[i]; k++)
            seq[n++] = (BYTE)i;
    for (p = 0; p < 4; p++)
        for (y = 0; y < 8; y++) {
            BYTE v = 0;
            for (x = 0; x < 8; x++)
                if ((seq[bayer[y][x]] >> p) & 1)
                    v |= (BYTE)(0x80 >> x);
            pat[p][y] = v;
        }
    lv = (comp[0] + 2 * comp[1] + comp[2] + 4) >> 4;
    if (lv > 64)
        lv = 64;
    for (y = 0; y < 8; y++) {
        BYTE v = 0;
        if (lv == 16)
            v = (BYTE)(y & 1 ? 0x22 : 0x88);
        else if (lv == 48)
            v = (BYTE)(y & 1 ? 0x77 : 0xDD);
        else
            for (x = 0; x < 8; x++)
                if (bayer[y][x] < lv)
                    v |= (BYTE)(0x80 >> x);
        mono[y] = v;
    }
}

static int is_mono(LPVOID dev)
{
    LPPBITMAP b = (LPPBITMAP)dev;
    return dev && b->bmType == 0 && b->bmPlanes == 1 && b->bmBitsPixel == 1;
}

/* COLORREF ColorInfo(lpDevice, dwColorIn, lpPColor): a logical colour to
   the nearest physical one (lpPColor not NULL), or a physical colour back
   to its logical one */
DWORD EXPORT ColorInfo(LPVOID lpDevice, DWORD c, DWORD __far *pc)
{
    if (pc) {
        if (is_mono(lpDevice)) {
            int m = rgb_mono(c);
            *pc = MKPC(rgb_index(c), m);
            return m ? RGB(255, 255, 255) : 0;
        }
        *pc = rgb_to_pc(c);
        return colors[PC_INDEX(*pc)];
    }
    if (is_mono(lpDevice))
        return PC_MONO(c) ? RGB(255, 255, 255) : 0;
    return colors[PC_INDEX(c)];
}

/* the standard hatches, rows top first, bit 7 the left pixel
   (UNVERIFIED against 3.1's patterns) */
static const BYTE hatches[6][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },     /* HS_HORIZONTAL */
    { 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08 },     /* HS_VERTICAL */
    { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 },     /* HS_FDIAGONAL */
    { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 },     /* HS_BDIAGONAL */
    { 0x08, 0x08, 0x08, 0x08, 0xFF, 0x08, 0x08, 0x08 },     /* HS_CROSS */
    { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 }      /* HS_DIAGCROSS */
};

static BYTE ror8(BYTE v, int n)
{
    n &= 7;
    return n ? (BYTE)((v >> n) | (v << (8 - n))) : v;
}

/* a brush's pattern rows moved to the brush origin (ox, oy): row y of
   the result is the device's row y */
static void align(BYTE rows[8], int ox, int oy)
{
    BYTE t[8];
    int y;
    for (y = 0; y < 8; y++)
        t[y] = ror8(rows[(y - oy) & 7], ox);
    for (y = 0; y < 8; y++)
        rows[y] = t[y];
}

static void realize_brush(LBRUSH __far *lb, LPPBRUSH pb, LPVOID dev, DWORD org)
{
    int ox = (short)LOWORD(org), oy = (short)HIWORD(org), p, y;
    (void)dev;
    memsetf(pb, 0, sizeof(PBRUSH));
    pb->style = lb->lbStyle;
    switch (lb->lbStyle) {
    case BS_SOLID:
    default:
        dither(lb->lbColor, pb->color, pb->mono);
        break;
    case BS_HOLLOW:
        pb->flags = PBF_HOLLOW;
        return;
    case BS_HATCHED: {
        int h = lb->lbHatch, fg = rgb_index(lb->lbColor), bg = PC_INDEX(lb->lbBkColor);
        int fgm = rgb_mono(lb->lbColor), bgm = PC_MONO(lb->lbBkColor);
        if (h < 0 || h > 5)
            h = 0;
        pb->flags = PBF_HATCH;
        pb->fg = (BYTE)fg;
        pb->bg = (BYTE)bg;
        pb->fgmono = (BYTE)fgm;
        pb->bgmono = (BYTE)bgm;
        for (y = 0; y < 8; y++)
            pb->mask[y] = hatches[h][y];
        for (p = 0; p < 4; p++)
            for (y = 0; y < 8; y++)
                pb->color[p][y] = (BYTE)(((fg >> p) & 1 ? hatches[h][y] : 0) |
                                         ((bg >> p) & 1 ? ~hatches[h][y] : 0));
        for (y = 0; y < 8; y++)
            pb->mono[y] = (BYTE)((fgm ? hatches[h][y] : 0) | (bgm ? ~hatches[h][y] : 0));
        break;
    }
    case BS_PATTERN: {
        /* GDI passes the pattern bitmap's PBITMAP in lbColor (VGREC) */
        LPPBITMAP b = (LPPBITMAP)lb->lbColor;
        SURF s;
        if (!b || !surf_get(&s, b) || s.width <= 0 || s.height <= 0)
            break;
        if (s.planes == 1) {
            pb->flags = PBF_MONOPAT;
            for (y = 0; y < 8; y++)
                pb->mono[y] = *surf_line(&s, y % s.height, 0);
            for (p = 0; p < 4; p++)
                for (y = 0; y < 8; y++)
                    pb->color[p][y] = pb->mono[y];
        } else {
            for (p = 0; p < 4; p++)
                for (y = 0; y < 8; y++)
                    pb->color[p][y] = *surf_line(&s, y % s.height, p);
            for (y = 0; y < 8; y++) {
                BYTE m = 0;
                int x;
                for (x = 0; x < 8; x++) {
                    int i = 0;
                    for (p = 0; p < 4; p++)
                        if (pb->color[p][y] & (0x80 >> x))
                            i |= 1 << p;
                    if (mono_of[i])
                        m |= (BYTE)(0x80 >> x);
                }
                pb->mono[y] = m;
            }
        }
        break;
    }
    }
    for (p = 0; p < 4; p++)
        align(pb->color[p], ox, oy);
    align(pb->mono, ox, oy);
    align(pb->mask, ox, oy);
}

/* DWORD RealizeObject(lpDevice, wStyle, lpInObj, lpOutObj, lpTextXForm):
   the size of the physical object (lpOutObj NULL), or the object made;
   negative styles delete (nothing to free here). No device fonts (0) and
   no device bitmaps (0: GDI makes memory bitmaps). For brushes the last
   parameter is the brush origin itself, a POINT (VGREC: the DC's origin). */
DWORD EXPORT RealizeObject(LPVOID lpDevice, short style, LPVOID in, LPVOID out, LPVOID xform)
{
    if (style < 0)
        return 1;
    switch (style) {
    case OBJ_PEN:
        if (!out)
            return sizeof(PPEN);
        {
            LPEN __far *lp = (LPEN __far *)in;
            LPPPEN pp = (LPPPEN)out;
            int i = rgb_index(lp->lopnColor);
            pp->style = lp->lopnStyle;
            pp->color = (BYTE)i;
            pp->mono = (BYTE)rgb_mono(lp->lopnColor);
            if (is_mono(lpDevice))
                pp->color = (BYTE)(pp->mono ? 15 : 0);
        }
        return sizeof(PPEN);
    case OBJ_BRUSH:
        if (!out)
            return sizeof(PBRUSH);
        realize_brush((LBRUSH __far *)in, (LPPBRUSH)out, lpDevice, (DWORD)xform);
        return sizeof(PBRUSH);
    }
    return 0;
}
