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
 */
#include "vga.h"

#define CHUNK 512

static BYTE ibuf[CHUNK];                /* colour indices, or mono values */
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

/* pixels [x, x + n) of a DIB line into ibuf (index or mono value) */
static void dib_read(const BYTE __far *row, LPBITMAPINFOHEADER bi, int x, int n, int mono)
{
    int i, v = 0;
    for (i = 0; i < n; i++) {
        int px = x + i;
        switch (bi->biBitCount) {
        case 1:
            v = (row[px >> 3] >> (7 - (px & 7))) & 1;
            break;
        case 4:
            v = px & 1 ? row[px >> 1] & 15 : row[px >> 1] >> 4;
            break;
        case 8:
            v = row[px];
            break;
        case 24: {
            const BYTE __far *q = row + px * 3;
            COLORREF c = RGB(q[2], q[1], q[0]);
            ibuf[i] = (BYTE)(mono ? rgb_mono(c) : rgb_index(c));
            continue;
        }
        }
        ibuf[i] = mono ? map_mono[v] : map_idx[v];
    }
}

/* ibuf's n pixels to line y of D from x */
static void put_indices(SURF *D, int y, int x, int n)
{
    int p, b, b0 = x >> 3, b1 = (x + n - 1) >> 3;
    BYTE __far *l;
    BYTE m, v, bit;
    for (p = 0; p < D->planes; p++) {
        if (D->screen)
            scr_plane(p);
        l = surf_line(D, y, p);
        for (b = b0; b <= b1; b++) {
            m = 0;
            v = 0;
            for (bit = 0; bit < 8; bit++) {
                int px = b * 8 + bit - x;
                if (px < 0 || px >= n)
                    continue;
                m |= (BYTE)(0x80 >> bit);
                if (D->planes == 1 ? ibuf[px] : (ibuf[px] >> p) & 1)
                    v |= (BYTE)(0x80 >> bit);
            }
            l[b] = (BYTE)((l[b] & ~m) | v);
        }
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
    if (bi->biCompression != BI_RGB)
        return 0;                       /* RLE: not yet */
    stride = dib_stride(bi);
    if (!bits) {
        bi->biSizeImage = (DWORD)stride * (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
        return scans;
    }
    w = (int)bi->biWidth < S.width ? (int)bi->biWidth : S.width;
    if (start >= (WORD)S.height)
        return 0;
    if (start + scans > (WORD)S.height)
        scans = S.height - start;
    if (!get) {
        build_map(bi);
        for (j = 0; j < scans; j++) {
            y = S.height - 1 - (start + j);
            for (k = 0; k < w; k += CHUNK) {
                n = w - k > CHUNK ? CHUNK : w - k;
                dib_read(bits + (DWORD)j * stride, bi, k, n, S.planes == 1);
                put_indices(&S, y, k, n);
            }
        }
        return scans;
    }
    /* get: the image size (VG1: 32 for an 8 x 8 4-bit DIB), the colour
       table, then the pixels */
    bi->biSizeImage = (DWORD)stride * (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
    {
        RGBQUAD __far *t = (RGBQUAD __far *)((BYTE __far *)bi + (WORD)bi->biSize);
        int nt = bi->biBitCount == 24 ? 0 : 1 << bi->biBitCount;
        /* a monochrome bitmap: black first, white last, the rest 0
           (VG7: 3.1's driver for 4- and 8-bit DIBs) */
        for (i = 0; i < nt; i++) {
            COLORREF c = 0;
            if (bi->biBitCount == 1 || S.planes == 1)
                c = i == nt - 1 ? RGB(255, 255, 255) : 0;
            else if (i < 16)
                c = index_rgb(dib_to_index[i]);
            t[i].rgbRed = GetRValue(c);
            t[i].rgbGreen = GetGValue(c);
            t[i].rgbBlue = GetBValue(c);
            t[i].rgbReserved = 0;
        }
    }
    for (j = 0; j < scans; j++) {
        BYTE __far *row = bits + (DWORD)j * stride;
        y = S.height - 1 - (start + j);
        for (k = 0; k < (int)stride; k++)
            row[k] = 0;
        for (k = 0; k < w; k += CHUNK) {
            n = w - k > CHUNK ? CHUNK : w - k;
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
                    row[px * 3] = GetBValue(c);
                    row[px * 3 + 1] = GetGValue(c);
                    row[px * 3 + 2] = GetRValue(c);
                    break;
                }
                }
            }
        }
    }
    return scans;
}

/* WORD SetDIBitsToDevice(lpDestDev, X, Y, iScan, cScans, lpClipRect,
   lpDrawMode, lpDIBits, lpBitmapInfo, lpTranslate): X, Y are the whole
   DIB's top left; the lines iScan .. iScan + cScans - 1 (from the bottom)
   are given (ARCHITECTURE.md 11.4: WinMine's bands) */
WORD EXPORT DIBScreenBlt(LPVOID lpDev, short X, short Y, WORD scan, WORD scans, LPRECT clip,
                         LPDRAWMODE dm, LPBYTE bits, LPBITMAPINFOHEADER bi, LPWORD xlate)
{
    SURF D;
    RECT r;
    WORD stride, j;
    int h, y, x0, x1, k, n;
    (void)dm;
    (void)xlate;
    if (!surf_get(&D, lpDev) || !bi || !bits)
        return 0;
    if (bi->biPlanes != 1 || bi->biCompression != BI_RGB)
        return 0;
    if (bi->biBitCount != 1 && bi->biBitCount != 4 && bi->biBitCount != 8 && bi->biBitCount != 24)
        return 0;
    stride = dib_stride(bi);
    h = (int)bi->biHeight;
    r.left = X;
    r.right = X + (int)bi->biWidth;
    r.bottom = Y + h - scan;
    r.top = r.bottom - scans;
    if (!clip_rect(&r, clip, &D))
        return scans;
    build_map(bi);
    if (D.screen)
        scr_begin(r.left, r.top, r.right, r.bottom);
    x0 = r.left;
    x1 = r.right;
    for (j = 0; j < scans; j++) {
        y = Y + h - 1 - (scan + j);
        if (y < r.top || y >= r.bottom)
            continue;
        for (k = x0; k < x1; k += CHUNK) {
            n = x1 - k > CHUNK ? CHUNK : x1 - k;
            dib_read(bits + (DWORD)j * stride, bi, k - X, n, D.planes == 1);
            put_indices(&D, y, k, n);
        }
    }
    if (D.screen)
        scr_end();
    return scans;
}
