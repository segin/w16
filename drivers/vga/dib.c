/*
 * dib.c - device-independent bitmaps: DeviceBitmapBits (GDI's SetDIBits
 * and GetDIBits on memory bitmaps) and SetDIBitsToDevice (export 21,
 * DIBSCREENBLT).
 *
 * A DIB (SDK: BITMAPINFOHEADER) stores its lines bottom first, each
 * padded to 4 bytes, 1, 4, 8 or 24 bits a pixel; the colour table follows
 * the header. GDI hands the driver a BITMAPINFOHEADER always (it converts
 * a BITMAPCOREHEADER first: ARCHITECTURE.md 11.2). Colours map to the
 * nearest of the 16 (color.c); GetDIBits gives the 16 in the DIB order of
 * drivers/vga/resources.md.
 *
 * The bits are a huge pointer (DDK, DeviceBitmapBits: scan lines may cross
 * 64K boundaries): the next segment's selector is __AHINCR on, and a line
 * may start in one segment and end in the next.
 *
 * RLE8 and RLE4 (SDK Vol. 3, BITMAPINFOHEADER: encoded runs, absolute
 * runs word-aligned, the end-of-line, end-of-bitmap and delta escapes;
 * the DDK: the driver decodes them, and encodes them for GetDIBits). As
 * Windows 3.1's VGA.DRV does (GD7, tests/apps/gd7.c):
 * - pixels the stream skips (delta, an early end of line or of bitmap)
 *   keep what was there;
 * - the stream's first line is scan line iStart, and decoding goes on
 *   past cScans lines to the end of the bitmap or of the stream;
 *   DeviceBitmapBits and DIBScreenBlt then return 1;
 * - GetDIBits in RLE: lines iStart .. iStart + cScans - 1 each ended by
 *   an end of line, then an end of bitmap; biSizeImage set to the bytes
 *   written; returns 1. The runs as 3.1's encoder chose them for GD7's
 *   bitmaps: a run of two or more equal values (RLE4: bytes, two pixels)
 *   is a run, unless fewer than three values wait to go out absolute
 *   (absolute needs three), which then take the run in; a lone value or
 *   two at the end of a line are runs of 1. UNVERIFIED beyond GD7's
 *   lines: the choice for runs of more than two after one or two
 *   absolute values, and RLE4 lines of odd width.
 */
#include "vga.h"

#define CHUNK 512
#define DBATCH 8

static BYTE ibuf[CHUNK];                /* colour indices, or mono values */
static BYTE lbuf[CHUNK * 3 + 4];        /* the bytes of a DIB line across 64K */
static BYTE pbuf[4][CHUNK / 8 + 2];     /* packed planar bytes for one chunk */
static BYTE sbatch[DBATCH][4][80];      /* batched screen rows for DIBScreenBlt */
static short sbatch_y[DBATCH];
static BYTE map_idx[256], map_mono[256];

static void build_map(LPBITMAPINFOHEADER bi)
{
    RGBQUAD __far *t = (RGBQUAD __far *)((BYTE __far *)bi + (WORD)bi->biSize);
    int n, i;
    COLORREF c;
    if (bi->biBitCount > 8)
        return;
    n = 1 << bi->biBitCount;
    if (bi->biClrUsed && bi->biClrUsed < (DWORD)n)
        n = (int)bi->biClrUsed;
    for (i = 0; i < 256; i++) {
        map_idx[i] = 0;
        map_mono[i] = 0;
    }
    for (i = 0; i < n; i++) {
        c = RGB(t[i].rgbRed, t[i].rgbGreen, t[i].rgbBlue);
        map_idx[i] = (BYTE)rgb_index(c);
        map_mono[i] = (BYTE)rgb_mono(c);
    }
}

static WORD dib_stride(LPBITMAPINFOHEADER bi)
{
    return (WORD)((((DWORD)bi->biWidth * bi->biBitCount + 31) >> 5) << 2);
}

/* ---- the bits as a huge block ---- */

/* the byte off bytes into the bits */
static BYTE __far *dib_at(LPBYTE bits, DWORD off)
{
    DWORD o = (DWORD)OFFSETOF(bits) + off;
    return (BYTE __far *)MK_FP(SELECTOROF(bits) + (WORD)(o >> 16) * ahincr, (WORD)o);
}

/* n bytes from off: copied to or from buf, across segments */
static void dib_copy(LPBYTE bits, DWORD off, BYTE __far *buf, WORD n, int to_dib)
{
    while (n) {
        BYTE __far *p = dib_at(bits, off);
        WORD k = OFFSETOF(p) ? (WORD)(0 - OFFSETOF(p)) : 0xFFFF;
        if (k > n)
            k = n;
        if (to_dib)
            memcpyf(p, buf, k);
        else
            memcpyf(buf, p, k);
        n -= k;
        off += k;
        buf += k;
    }
}

/* the bytes of pixels [x, x + n) of the line at off: a pointer p such
   that p[i] is byte i of the line for the bytes those pixels use (the
   line itself, or lbuf when they cross 64K); *first, *count: those bytes */
static BYTE __far *dib_line(LPBYTE bits, DWORD off, WORD bpp, int x, int n, WORD *first,
                            WORD *count)
{
    WORD b0 = (WORD)(((DWORD)x * bpp) >> 3);
    WORD b1 = (WORD)(((DWORD)(x + n) * bpp + 7) >> 3);
    BYTE __far *p = dib_at(bits, off + b0);
    *first = b0;
    *count = b1 - b0;
    if ((DWORD)OFFSETOF(p) + (b1 - b0) <= 0x10000L)
        return p - b0;
    return (BYTE __far *)lbuf - b0;
}

/* pixels [x, x + n) of a DIB line into ibuf (index or mono value) */
static void dib_read(const BYTE __far *row, LPBITMAPINFOHEADER bi, int x, int n, int mono)
{
    int i;
    const BYTE *map = mono ? map_mono : map_idx;
    switch (bi->biBitCount) {
    case 1:
        for (i = 0; i < n; i++) {
            int px = x + i;
            ibuf[i] = map[(row[px >> 3] >> (7 - (px & 7))) & 1];
        }
        return;
    case 4:
        for (i = 0; i < n; i++) {
            int px = x + i;
            BYTE b = row[px >> 1];
            ibuf[i] = map[px & 1 ? (b & 15) : (b >> 4)];
        }
        return;
    case 8: {
        const BYTE __far *q = row + x;
        for (i = 0; i < n; i++)
            ibuf[i] = map[q[i]];
        return;
    }
    case 24: {
        const BYTE __far *q = row + (WORD)((DWORD)x * 3);
        if (mono) {
            for (i = 0; i < n; i++, q += 3)
                ibuf[i] = (BYTE)rgb_mono(RGB(q[2], q[1], q[0]));
        } else {
            for (i = 0; i < n; i++, q += 3)
                ibuf[i] = (BYTE)rgb_index(RGB(q[2], q[1], q[0]));
        }
        return;
    }
    }
}

/* pixels [x, x + n) of the line at off (bytes into the bits) to ibuf */
static void dib_read_at(LPBYTE bits, DWORD off, LPBITMAPINFOHEADER bi, int x, int n, int mono)
{
    WORD b0, nb;
    BYTE __far *row = dib_line(bits, off, bi->biBitCount, x, n, &b0, &nb);
    if (row + b0 == (BYTE __far *)lbuf)
        dib_copy(bits, off + b0, lbuf, nb, 0);
    dib_read(row, bi, x, n, mono);
}

/* pack n 4-bit colour indices from src (starting at pixel x) into planar
   buffers out0..out3 (indexed 0..nb-1 for screen bytes b0..b1) */
static void pack_planes_4(const BYTE *src, int x, int n,
                          BYTE *out0, BYTE *out1, BYTE *out2, BYTE *out3)
{
    int px = 0, bi = 0, bit;
    int lbit = x & 7;
    if (lbit) {
        BYTE p0 = 0, p1 = 0, p2 = 0, p3 = 0;
        for (bit = lbit; bit < 8 && px < n; bit++, px++) {
            BYTE bm = (BYTE)(0x80 >> bit), c = src[px];
            if (c & 1) p0 |= bm;
            if (c & 2) p1 |= bm;
            if (c & 4) p2 |= bm;
            if (c & 8) p3 |= bm;
        }
        out0[bi] = p0;
        out1[bi] = p1;
        out2[bi] = p2;
        out3[bi] = p3;
        bi++;
    }
    while (px + 8 <= n) {
        const BYTE *s = src + px;
        BYTE p0 = 0, p1 = 0, p2 = 0, p3 = 0, c;
        c = s[0]; if (c & 1) p0 |= 0x80; if (c & 2) p1 |= 0x80; if (c & 4) p2 |= 0x80; if (c & 8) p3 |= 0x80;
        c = s[1]; if (c & 1) p0 |= 0x40; if (c & 2) p1 |= 0x40; if (c & 4) p2 |= 0x40; if (c & 8) p3 |= 0x40;
        c = s[2]; if (c & 1) p0 |= 0x20; if (c & 2) p1 |= 0x20; if (c & 4) p2 |= 0x20; if (c & 8) p3 |= 0x20;
        c = s[3]; if (c & 1) p0 |= 0x10; if (c & 2) p1 |= 0x10; if (c & 4) p2 |= 0x10; if (c & 8) p3 |= 0x10;
        c = s[4]; if (c & 1) p0 |= 0x08; if (c & 2) p1 |= 0x08; if (c & 4) p2 |= 0x08; if (c & 8) p3 |= 0x08;
        c = s[5]; if (c & 1) p0 |= 0x04; if (c & 2) p1 |= 0x04; if (c & 4) p2 |= 0x04; if (c & 8) p3 |= 0x04;
        c = s[6]; if (c & 1) p0 |= 0x02; if (c & 2) p1 |= 0x02; if (c & 4) p2 |= 0x02; if (c & 8) p3 |= 0x02;
        c = s[7]; if (c & 1) p0 |= 0x01; if (c & 2) p1 |= 0x01; if (c & 4) p2 |= 0x01; if (c & 8) p3 |= 0x01;
        out0[bi] = p0;
        out1[bi] = p1;
        out2[bi] = p2;
        out3[bi] = p3;
        px += 8;
        bi++;
    }
    if (px < n) {
        BYTE p0 = 0, p1 = 0, p2 = 0, p3 = 0;
        for (bit = 0; px < n; bit++, px++) {
            BYTE bm = (BYTE)(0x80 >> bit), c = src[px];
            if (c & 1) p0 |= bm;
            if (c & 2) p1 |= bm;
            if (c & 4) p2 |= bm;
            if (c & 8) p3 |= bm;
        }
        out0[bi] = p0;
        out1[bi] = p1;
        out2[bi] = p2;
        out3[bi] = p3;
    }
}

/* write nb packed bytes from src to d[0..nb-1] with edge masks lmask/rmask */
static void write_plane_run(BYTE __far *d, const BYTE *src, int nb, BYTE lmask, BYTE rmask)
{
    int start = 0, end = nb;
    if (nb == 1) {
        lmask &= rmask;
        rmask = 0xFF;
    }
    if (lmask != 0xFF) {
        d[0] = (BYTE)((d[0] & ~lmask) | (src[0] & lmask));
        start = 1;
    }
    if (end > start && rmask != 0xFF) {
        int last = end - 1;
        d[last] = (BYTE)((d[last] & ~rmask) | (src[last] & rmask));
        end = last;
    }
    if (end > start)
        memcpyf(d + start, src + start, (WORD)(end - start));
}

/* the n pixels of src to line y of D from x */
static void put_indices(SURF *D, int y, int x, int n, const BYTE *src)
{
    int p, b0 = x >> 3, b1 = (x + n - 1) >> 3, nb = b1 - b0 + 1;
    BYTE lmask = (BYTE)(0xFF >> (x & 7));
    BYTE rmask = (BYTE)(0xFF << (7 - ((x + n - 1) & 7)));
    if (D->planes == 1) {
        int px = 0, bi = 0, bit, lbit = x & 7;
        if (lbit) {
            BYTE v = 0;
            for (bit = lbit; bit < 8 && px < n; bit++, px++)
                if (src[px])
                    v |= (BYTE)(0x80 >> bit);
            pbuf[0][bi++] = v;
        }
        while (px + 8 <= n) {
            const BYTE *s = src + px;
            BYTE v = 0;
            if (s[0]) v |= 0x80;
            if (s[1]) v |= 0x40;
            if (s[2]) v |= 0x20;
            if (s[3]) v |= 0x10;
            if (s[4]) v |= 0x08;
            if (s[5]) v |= 0x04;
            if (s[6]) v |= 0x02;
            if (s[7]) v |= 0x01;
            pbuf[0][bi++] = v;
            px += 8;
        }
        if (px < n) {
            BYTE v = 0;
            for (bit = 0; px < n; bit++, px++)
                if (src[px])
                    v |= (BYTE)(0x80 >> bit);
            pbuf[0][bi] = v;
        }
        write_plane_run(surf_line(D, y, 0) + b0, pbuf[0], nb, lmask, rmask);
        return;
    }
    pack_planes_4(src, x, n, pbuf[0], pbuf[1], pbuf[2], pbuf[3]);
    for (p = 0; p < 4; p++) {
        if (D->screen)
            scr_plane(p);
        write_plane_run(surf_line(D, y, p) + b0, pbuf[p], nb, lmask, rmask);
    }
}

/* line y of S from x, n pixels, into ibuf as colour indices (or mono) */
static void get_indices(SURF *S, int y, int x, int n)
{
    int p, i;
    for (i = 0; i < n; i++)
        ibuf[i] = 0;
    for (p = 0; p < S->planes; p++) {
        BYTE __far *l;
        if (S->screen)
            scr_plane(p);
        l = surf_line(S, y, p);
        for (i = 0; i < n; i++)
            if (l[(x + i) >> 3] & (0x80 >> ((x + i) & 7)))
                ibuf[i] |= (BYTE)(1 << p);
    }
}

/* ---- RLE ---- */

static int is_rle(LPBITMAPINFOHEADER bi)
{
    return bi->biCompression == BI_RLE8 && bi->biBitCount == 8 ||
           bi->biCompression == BI_RLE4 && bi->biBitCount == 4;
}

/* where decoded pixels go: DIB line l, x -> surface (xbase + x, ybase - l),
   kept to [x0, x1) and to the surface; the line is kept to [y0, y1) at
   y + shift (DIBScreenBlt: as if the stream started at line 0) */
typedef struct {
    SURF *D;
    long xbase, ybase, shift;
    long x0, y0, x1, y1;
    const BYTE *map;
} RLEDST;

static void rle_put(RLEDST *t, int line, int x, int n)
{
    long sx = t->xbase + x, y = t->ybase - line;
    int a = 0;
    if (y + t->shift < t->y0 || y + t->shift >= t->y1 || y < 0 || y >= t->D->height ||
        sx >= t->x1 || sx + n <= t->x0)
        return;
    if (sx < t->x0) {
        a = (int)(t->x0 - sx);
        sx = t->x0;
    }
    n -= a;
    if (sx + n > t->x1)
        n = (int)(t->x1 - sx);
    if (n > 0)
        put_indices(t->D, (int)y, (int)sx, n, ibuf + a);
}

/* decode the stream from DIB line `line` up to line `top` (excluded) */
static void rle_decode(LPBYTE bits, LPBITMAPINFOHEADER bi, int line, int top, RLEDST *t)
{
    DWORD pos = 0;
    int rle4 = bi->biCompression == BI_RLE4, x = 0, i;
    WORD c, d;
#define NEXT(v) ((v) = *dib_at(bits, pos++))
    while (line < top) {
        NEXT(c);
        NEXT(d);
        if (c) {                        /* c pixels of d (RLE4: its nibbles in turn) */
            for (i = 0; i < (int)c; i++)
                ibuf[i] = t->map[rle4 ? (i & 1 ? d & 15 : d >> 4) : d];
            rle_put(t, line, x, c);
            x += c;
        } else if (d == 0) {            /* end of line */
            x = 0;
            line++;
        } else if (d == 1) {            /* end of bitmap */
            return;
        } else if (d == 2) {            /* delta */
            NEXT(c);
            NEXT(d);
            x += c;
            line += d;
        } else {                        /* d pixels absolute, word-aligned */
            WORD nb = rle4 ? (d + 1) >> 1 : d, b = 0;
            for (i = 0; i < (int)nb; i++) {
                NEXT(b);
                if (rle4) {
                    ibuf[2 * i] = t->map[b >> 4];
                    ibuf[2 * i + 1] = t->map[b & 15];
                } else
                    ibuf[i] = t->map[b];
            }
            if (nb & 1)
                NEXT(b);
            rle_put(t, line, x, d);
            x += d;
        }
        if (x > 0x7000)
            x = 0x7000;
    }
#undef NEXT
}

/* GetDIBits in RLE: one line's values (DIB order) as runs */
static DWORD rle_out;                   /* bytes written */
static LPBYTE rle_bits;

static void rle_emit(BYTE b)
{
    *dib_at(rle_bits, rle_out++) = b;
}

/* v[0 .. n) (RLE4: bytes, each two pixels; npix pixels in all) */
static void rle_abs(const BYTE *v, int n, int rle4, int npix)
{
    int i;
    if (n == 0)
        return;
    if ((rle4 ? npix : n) < 3) {        /* too short for absolute: runs of 1 */
        for (i = 0; i < n; i++) {
            int k = rle4 ? (npix - 2 * i >= 2 ? 2 : 1) : 1;
            rle_emit((BYTE)k);
            rle_emit(v[i]);
        }
        return;
    }
    rle_emit(0);
    rle_emit((BYTE)(rle4 ? npix : n));
    for (i = 0; i < n; i++)
        rle_emit(v[i]);
    if (n & 1)
        rle_emit(0);
}

/* the values of one line, vals[0 .. n); RLE4: n bytes of two pixels,
   the last with one when the width (w) is odd */
static void rle_line(const BYTE *vals, int n, int rle4, int w)
{
    int i = 0, pend = 0, r;
    while (i < n) {
        r = 1;
        while (i + r < n && r < (rle4 ? 127 : 255) && vals[i + r] == vals[i])
            r++;
        if (r >= 2 && pend && pend < 3 && pend + r > (rle4 ? 120 : 250)) {
            /* too long to take in: the one or two as runs of 1 */
            rle_abs(vals + i - pend, pend, rle4, rle4 ? 2 * pend : pend);
            pend = 0;
        }
        if (r >= 2 && (pend == 0 || pend >= 3)) {
            int px = rle4 ? 2 * r : r;
            if (rle4 && i + r == n && (w & 1))
                px--;
            rle_abs(vals + i - pend, pend, rle4, rle4 ? 2 * pend : pend);
            pend = 0;
            rle_emit((BYTE)px);
            rle_emit(vals[i]);
            i += r;
            continue;
        }
        pend += r;
        i += r;
        if (pend >= (rle4 ? 120 : 250)) {
            int px = rle4 ? 2 * pend : pend;
            if (rle4 && i == n && (w & 1))
                px--;
            rle_abs(vals + i - pend, pend, rle4, px);
            pend = 0;
        }
    }
    if (pend) {
        int px = rle4 ? 2 * pend : pend;
        if (rle4 && (w & 1))
            px--;
        rle_abs(vals + n - pend, pend, rle4, px);
    }
}

/* the DIB value (colour table index) of a surface index or mono value */
static BYTE dib_value(int v, int planes, int bpp)
{
    if (planes == 1)
        return (BYTE)(v ? (bpp == 8 ? 0xFF : 15) : 0);
    return index_to_dib[v];
}

static WORD rle_get(SURF *S, LPBYTE bits, LPBITMAPINFOHEADER bi, WORD start, WORD scans, int w)
{
    static BYTE vals[CHUNK];
    int rle4 = bi->biCompression == BI_RLE4, step = rle4 ? 2 * CHUNK : CHUNK;
    int y, x, cw, k, n, i;
    WORD j;
    rle_out = 0;
    rle_bits = bits;
    for (j = 0; j < scans; j++) {
        y = S->height - 1 - (start + j);
        for (x = 0; x < w; x += step) {
            cw = w - x > step ? step : w - x;
            n = 0;
            for (k = 0; k < cw; k += CHUNK) {
                int m = cw - k > CHUNK ? CHUNK : cw - k;
                get_indices(S, y, x + k, m);
                for (i = 0; i < m; i++) {
                    BYTE v = dib_value(ibuf[i], S->planes, bi->biBitCount);
                    if (!rle4)
                        vals[n++] = v;
                    else if ((k + i) & 1)
                        vals[n - 1] |= v;
                    else
                        vals[n++] = (BYTE)(v << 4);
                }
            }
            rle_line(vals, n, rle4, cw);
        }
        rle_emit(0);
        rle_emit(0);
    }
    rle_emit(0);
    rle_emit(1);
    bi->biSizeImage = rle_out;
    return 1;
}

/* the colour table GetDIBits gives */
static void get_table(SURF *S, LPBITMAPINFOHEADER bi)
{
    RGBQUAD __far *t = (RGBQUAD __far *)((BYTE __far *)bi + (WORD)bi->biSize);
    int nt = bi->biBitCount == 24 ? 0 : 1 << bi->biBitCount, i;
    /* a monochrome bitmap: black first, white last, the rest 0
       (VG7: 3.1's driver for 4- and 8-bit DIBs) */
    for (i = 0; i < nt; i++) {
        COLORREF c = 0;
        if (bi->biBitCount == 1 || S->planes == 1)
            c = i == nt - 1 ? RGB(255, 255, 255) : 0;
        else if (i < 16)
            c = index_rgb(dib_to_index[i]);
        t[i].rgbRed = GetRValue(c);
        t[i].rgbGreen = GetGValue(c);
        t[i].rgbBlue = GetBValue(c);
        t[i].rgbReserved = 0;
    }
}

/* WORD DeviceBitmapBits(lpBitmap, fGet, iStart, cScans, lpDIBits,
   lpBitmapInfo, lpDrawMode, lpTranslate) - VGREC: pops 26 bytes */
WORD EXPORT DeviceBitmapBits(LPVOID lpBitmap, WORD get, WORD start, WORD scans, LPBYTE bits,
                             LPBITMAPINFOHEADER bi, LPDRAWMODE dm, LPWORD xlate)
{
    SURF S;
    WORD stride, j;
    int w, k, n, i, y;
    (void)dm;
    (void)xlate;
    if (!surf_get(&S, lpBitmap) || !bi)
        return 0;
    if (bi->biPlanes != 1 || (bi->biBitCount != 1 && bi->biBitCount != 4 &&
                              bi->biBitCount != 8 && bi->biBitCount != 24))
        return 0;
    if (bi->biCompression != BI_RGB && !is_rle(bi))
        return 0;
    stride = dib_stride(bi);
    if (!bits) {
        bi->biSizeImage = (DWORD)stride * (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
        return scans;
    }
    w = (int)bi->biWidth < S.width ? (int)bi->biWidth : S.width;
    if (start >= (WORD)S.height)
        return 0;
    if (scans > (WORD)S.height - start)
        scans = (WORD)S.height - start;
    if (!get && is_rle(bi)) {
        RLEDST t;
        build_map(bi);
        t.D = &S;
        t.xbase = 0;
        t.ybase = S.height - 1;
        t.shift = 0;
        t.x0 = 0;
        t.y0 = 0;
        t.x1 = w;
        t.y1 = S.height;
        t.map = S.planes == 1 ? map_mono : map_idx;
        rle_decode(bits, bi, start, S.height, &t);
        return 1;
    }
    if (!get) {
        build_map(bi);
        for (j = 0; j < scans; j++) {
            y = S.height - 1 - (start + j);
            for (k = 0; k < w; k += CHUNK) {
                n = w - k > CHUNK ? CHUNK : w - k;
                dib_read_at(bits, (DWORD)j * stride, bi, k, n, S.planes == 1);
                put_indices(&S, y, k, n, ibuf);
            }
        }
        return scans;
    }
    get_table(&S, bi);
    if (is_rle(bi))
        return rle_get(&S, bits, bi, start, scans, w);
    /* get: the image size (VG1: 32 for an 8 x 8 4-bit DIB), the colour
       table, then the pixels */
    bi->biSizeImage = (DWORD)stride * (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
    for (j = 0; j < scans; j++) {
        DWORD off = (DWORD)j * stride;
        y = S.height - 1 - (start + j);
        /* the pixels past the width: 0 */
        {
            WORD kw = 0;
            while (kw < stride) {
                WORD m = stride - kw > sizeof lbuf ? sizeof lbuf : stride - kw;
                memsetf(lbuf, 0, m);
                dib_copy(bits, off + kw, lbuf, m, 1);
                kw += m;
            }
        }
        for (k = 0; k < w; k += CHUNK) {
            WORD b0, nb;
            BYTE __far *row;
            n = w - k > CHUNK ? CHUNK : w - k;
            row = dib_line(bits, off, bi->biBitCount, k, n, &b0, &nb);
            if (row + b0 == (BYTE __far *)lbuf)
                memsetf(lbuf, 0, nb);
            get_indices(&S, y, k, n);
            for (i = 0; i < n; i++) {
                int px = k + i, v = ibuf[i];
                if (S.planes == 1) {
                    /* white is the table's last entry */
                    v = v ? 15 : 0;
                    if (bi->biBitCount == 8) {
                        row[px] = (BYTE)(v ? 0xFF : 0);
                        continue;
                    }
                }
                switch (bi->biBitCount) {
                case 1:
                    if (rgb_mono(index_rgb(v)))
                        row[px >> 3] |= (BYTE)(0x80 >> (px & 7));
                    break;
                case 4:
                    row[px >> 1] |= (BYTE)(px & 1 ? index_to_dib[v] : index_to_dib[v] << 4);
                    break;
                case 8:
                    row[px] = index_to_dib[v];
                    break;
                case 24: {
                    COLORREF c = index_rgb(v);
                    WORD o = (WORD)((DWORD)px * 3);
                    row[o] = GetBValue(c);
                    row[o + 1] = GetGValue(c);
                    row[o + 2] = GetRValue(c);
                    break;
                }
                }
            }
            if (row + b0 == (BYTE __far *)lbuf)
                dib_copy(bits, off + b0, lbuf, nb, 1);
        }
    }
    return scans;
}

/* WORD SetDIBitsToDevice(lpDestDev, X, Y, iScan, cScans, lpClipRect,
   lpDrawMode, lpDIBits, lpBitmapInfo, lpTranslate): X, Y are the whole
   DIB's top left; the lines iScan .. iScan + cScans - 1 (from the bottom)
   are given (ARCHITECTURE.md 11.4: WinMine's bands); returns the lines
   drawn, inside the clip rectangle (GD7). RLE: the stream's biHeight
   lines from line iScan on, returning 1; as 3.1's driver (GD7), a line is
   kept to the clip rectangle where it would be if the stream started at
   line 0, so with iScan > 0 up to iScan lines go out above the clip
   rectangle (on the screen still). */
WORD EXPORT DIBScreenBlt(LPVOID lpDev, short X, short Y, WORD scan, WORD scans, LPRECT clip,
                         LPDRAWMODE dm, LPBYTE bits, LPBITMAPINFOHEADER bi, LPWORD xlate)
{
    SURF D;
    RECT r;
    WORD stride, j;
    int h, y, x0, x1, k, n, rle;
    (void)dm;
    (void)xlate;
    if (!surf_get(&D, lpDev) || !bi || !bits)
        return 0;
    if (bi->biPlanes != 1)
        return 0;
    if (bi->biBitCount != 1 && bi->biBitCount != 4 && bi->biBitCount != 8 && bi->biBitCount != 24)
        return 0;
    rle = is_rle(bi);
    if (bi->biCompression != BI_RGB && !rle)
        return 0;
    stride = dib_stride(bi);
    h = (int)bi->biHeight;
    {
        long rl = X, rr = (long)X + bi->biWidth;
        long rt, rb;
        if (rle) {
            RLEDST t;
            rt = Y;
            rb = (long)Y + bi->biHeight;
            if (clip) {
                if (rl < clip->left) rl = clip->left;
                if (rt < clip->top) rt = clip->top;
                if (rr > clip->right) rr = clip->right;
                if (rb > clip->bottom) rb = clip->bottom;
            }
            if (rl >= rr || rt >= rb)
                return 1;
            build_map(bi);
            t.D = &D;
            t.xbase = X;
            t.ybase = (long)Y + bi->biHeight - 1;
            t.shift = scan;
            t.x0 = rl < 0 ? 0 : rl;
            t.y0 = rt;
            t.x1 = rr > D.width ? D.width : rr;
            t.y1 = rb;
            t.map = D.planes == 1 ? map_mono : map_idx;
            if (t.x0 >= t.x1)
                return 1;
            {
                int began = 0;
                if (D.screen) {
                    long sy0 = rt - scan, sy1 = rb;
                    if (sy0 < 0) sy0 = 0;
                    if (sy1 > D.height) sy1 = D.height;
                    if (sy0 < sy1) {
                        scr_begin((int)t.x0, (int)sy0, (int)t.x1, (int)sy1);
                        began = 1;
                    }
                }
                rle_decode(bits, bi, scan, scan + h, &t);
                if (began)
                    scr_end();
            }
            return 1;
        }
        rb = (long)Y + bi->biHeight - scan;
        rt = rb - scans;
        if (clip) {
            if (rl < clip->left) rl = clip->left;
            if (rt < clip->top) rt = clip->top;
            if (rr > clip->right) rr = clip->right;
            if (rb > clip->bottom) rb = clip->bottom;
        }
        if (rl < 0) rl = 0;
        if (rt < 0) rt = 0;
        if (rr > D.width) rr = D.width;
        if (rb > D.height) rb = D.height;
        if (rl >= rr || rt >= rb)
            return 0;
        r.left = (short)rl;
        r.top = (short)rt;
        r.right = (short)rr;
        r.bottom = (short)rb;
    }
    build_map(bi);
    if (D.screen)
        scr_begin(r.left, r.top, r.right, r.bottom);
    x0 = r.left;
    x1 = r.right;
    if (D.screen) {
        int b0 = x0 >> 3, b1 = (x1 - 1) >> 3, nb = b1 - b0 + 1;
        BYTE lmask = (BYTE)(0xFF >> (x0 & 7));
        BYTE rmask = (BYTE)(0xFF << (7 - ((x1 - 1) & 7)));
        int nbatch = 0, p, ri;
        for (j = 0; j < scans; j++) {
            long yl = (long)Y + bi->biHeight - 1 - ((long)scan + j);
            if (yl < r.top || yl >= r.bottom)
                continue;
            sbatch_y[nbatch] = (short)yl;
            for (k = x0; k < x1; ) {
                int kend = ((k >> 3) + (CHUNK >> 3)) << 3;
                int boff = (k >> 3) - b0;
                if (kend > x1)
                    kend = x1;
                n = kend - k;
                dib_read_at(bits, (DWORD)j * stride, bi, (int)((long)k - X), n, 0);
                pack_planes_4(ibuf, k, n,
                              sbatch[nbatch][0] + boff, sbatch[nbatch][1] + boff,
                              sbatch[nbatch][2] + boff, sbatch[nbatch][3] + boff);
                k = kend;
            }
            nbatch++;
            if (nbatch == DBATCH) {
                for (p = 0; p < 4; p++) {
                    scr_plane(p);
                    for (ri = 0; ri < nbatch; ri++)
                        write_plane_run((BYTE __far *)MK_FP(vga_sel, (WORD)sbatch_y[ri] * 80 + b0),
                                        sbatch[ri][p], nb, lmask, rmask);
                }
                nbatch = 0;
            }
        }
        if (nbatch) {
            for (p = 0; p < 4; p++) {
                scr_plane(p);
                for (ri = 0; ri < nbatch; ri++)
                    write_plane_run((BYTE __far *)MK_FP(vga_sel, (WORD)sbatch_y[ri] * 80 + b0),
                                    sbatch[ri][p], nb, lmask, rmask);
            }
        }
    } else {
        for (j = 0; j < scans; j++) {
            long yl = (long)Y + bi->biHeight - 1 - ((long)scan + j);
            if (yl < r.top || yl >= r.bottom)
                continue;
            y = (int)yl;
            for (k = x0; k < x1; k += CHUNK) {
                n = x1 - k > CHUNK ? CHUNK : x1 - k;
                dib_read_at(bits, (DWORD)j * stride, bi, (int)((long)k - X), n, D.planes == 1);
                put_indices(&D, y, k, n, ibuf);
            }
        }
    }
    if (D.screen)
        scr_end();
    return r.bottom - r.top;
}
