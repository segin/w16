/*
 * bitmap.c - memory bitmaps and DIBs.
 *
 * A bitmap's bits are a PBITMAP (DDK) the driver draws on directly. As
 * observed on 3.1 (GD1, tests/gdi): one global block holds the 20h-byte
 * PBITMAP and then the bits (bmBits = selector:0020h); bmWidthPlanes is
 * bmWidthBytes * bmHeight; bmScanSegment and bmFillBytes are 65536 divided
 * by (resp. modulo) the bytes of one scan line of all planes, also for
 * bitmaps under 64K; scan lines are stored with all planes of a line
 * together (DDK, PBITMAP). Bigger bitmaps span segments __AHINCR apart
 * with bmScanSegment lines in each (DDK).
 *
 * SetDIBits/GetDIBits go to the driver's DeviceBitmapBits with the DC's
 * DRAWMODE; a BITMAPCOREHEADER (OS/2 format) is first converted to a
 * BITMAPINFOHEADER on the stack (GD1: 3.1 passed such a copy, size 28h,
 * width, height, planes 1, the bit count, the rest zero).
 */
#include "gdi.h"

extern WORD __cdecl gdi_ahincr;         /* entry.asm: KERNEL's __AHINCR */

#define PBM_HDR 0x20

LPPBITMAP bm_pbitmap(HBITMAP h)
{
    BITMAPOBJ NEAR *b = obj_get(h, BITMAP_MAGIC);
    if (!b || !b->hBits)
        return 0;
    return (LPPBITMAP)MAKELP(b->sel, 0);
}

/* the address of scan line y, plane 0 */
BYTE __far *bm_scan(LPPBITMAP pb, int y)
{
    WORD seg = 0, line = y;
    DWORD scan = (DWORD)pb->bmWidthBytes * pb->bmPlanes;
    if (pb->bmSegmentIndex) {
        seg = y / pb->bmScanSegment;
        line = y % pb->bmScanSegment;
    }
    return (BYTE __far *)MAKELP(SELECTOROF(pb->bmBits) + seg * pb->bmSegmentIndex,
                                OFFSETOF(pb->bmBits) + (WORD)(line * scan));
}

HBITMAP bm_create(int w, int h, int planes, int bpp, const void __far *bits, WORD flags)
{
    HANDLE hb;
    BITMAPOBJ NEAR *b;
    LPPBITMAP pb;
    WORD wb, scan;
    DWORD total;
    int y;
    if (w <= 0 || h <= 0 || planes <= 0 || bpp <= 0)
        return 0;
    wb = (WORD)((((DWORD)w * bpp + 15) >> 4) << 1);
    scan = wb * planes;
    total = (DWORD)scan * h;
    hb = obj_alloc(BITMAP_MAGIC, sizeof(BITMAPOBJ));
    if (!hb)
        return 0;
    b = DEREF(hb);
    b->flags = flags;
    b->width = w;
    b->height = h;
    b->planes = planes;
    b->bpp = bpp;
    if (total + PBM_HDR <= 0xFFF0L) {
        b->hBits = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, PBM_HDR + total);
        if (!b->hBits)
            goto fail;
        pb = GlobalLock(b->hBits);
        pb->bmBits = (BYTE __far *)pb + PBM_HDR;
    } else {
        WORD perseg = (WORD)(65536L / scan), nseg = (h + perseg - 1) / perseg;
        DWORD size = (DWORD)(nseg - 1) * 65536L + (DWORD)(h - (nseg - 1) * perseg) * scan;
        b->hBits = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, PBM_HDR);
        b->hHuge = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, size);
        if (!b->hBits || !b->hHuge)
            goto fail;
        pb = GlobalLock(b->hBits);
        pb->bmBits = GlobalLock(b->hHuge);
        pb->bmSegmentIndex = gdi_ahincr;
    }
    b = DEREF(hb);
    b->sel = SELECTOROF(pb);
    pb->bmType = 0;
    pb->bmWidth = w;
    pb->bmHeight = h;
    pb->bmWidthBytes = wb;
    pb->bmPlanes = planes;
    pb->bmBitsPixel = bpp;
    pb->bmWidthPlanes = (DWORD)wb * h;
    pb->bmScanSegment = (short)(65536L / scan);
    pb->bmFillBytes = (short)(65536L % scan);
    /* UNVERIFIED: 3.1 gave some bitmaps a memory DC's PBITMAP here */
    pb->bmlpPDevice = display ? display->lpPDevice : 0;
    if (bits)
        for (y = 0; y < h; y++)
            mem_copy(bm_scan(pb, y), (const BYTE __far *)bits + (DWORD)y * scan, scan);
    return hb;
fail:
    b = DEREF(hb);
    if (b->hBits)
        GlobalFree(b->hBits);
    if (b->hHuge)
        GlobalFree(b->hHuge);
    obj_free(hb);
    return 0;
}

void bm_free(HBITMAP h)
{
    BITMAPOBJ NEAR *b = obj_get(h, BITMAP_MAGIC);
    if (!b)
        return;
    if (b->hHuge) {
        GlobalUnlock(b->hHuge);
        GlobalFree(b->hHuge);
    }
    if (b->hBits) {
        GlobalUnlock(b->hBits);
        GlobalFree(b->hBits);
    }
    obj_free(h);
}

/* ---- API ---- */

HBITMAP GDIAPI CreateBitmap(int w, int h, BYTE planes, BYTE bpp, const void __far *bits) /* GDI.48 */
{
    return bm_create(w, h, planes, bpp, bits, 0);
}

HBITMAP GDIAPI CreateBitmapIndirect(const BITMAP __far *bm)        /* GDI.49 */
{
    return bm_create(bm->bmWidth, bm->bmHeight, bm->bmPlanes, bm->bmBitsPixel, bm->bmBits, 0);
}

/* USER's: the same as CreateBitmap (Schulman: owned by USER) */
HBITMAP GDIAPI CreateUserBitmap(int w, int h, BYTE planes, BYTE bpp, const void __far *bits) /* GDI.407 */
{
    return bm_create(w, h, planes, bpp, bits, 0);
}

HBITMAP GDIAPI CreateCompatibleBitmap(HDC hdc, int w, int h)       /* GDI.51 */
{
    NPDC dc = dc_get(hdc);
    LPPBITMAP pb;
    HBITMAP hb;
    if (!dc)
        return 0;
    if (!w || !h)
        return bm_create(1, 1, 1, 1, 0, 0);
    if (dc->flags & DCF_MEMORY) {
        LPPBITMAP cur = dc->lpPDevice;
        hb = bm_create(w, h, cur->bmPlanes, cur->bmBitsPixel, 0, 0);
    } else
        hb = bm_create(w, h, dc->dev->info.dpPlanes, dc->dev->info.dpBitsPixel, 0, 0);
    pb = bm_pbitmap(hb);
    if (pb)
        pb->bmlpPDevice = dc_get(hdc)->lpPDevice;
    return hb;
}

HBITMAP GDIAPI CreateDiscardableBitmap(HDC hdc, int w, int h)      /* GDI.156 */
{
    HBITMAP hb = CreateCompatibleBitmap(hdc, w, h);
    BITMAPOBJ NEAR *b = obj_get(hb, BITMAP_MAGIC);
    if (b)
        b->flags |= BMF_DISCARDABLE;
    return hb;
}

HBITMAP GDIAPI CreateUserDiscardableBitmap(HDC hdc, int w, int h)  /* GDI.409 */
{
    return CreateDiscardableBitmap(hdc, w, h);
}

/* raw bits, in the PBITMAP's layout without the segment fill; buf may
   cross segments (__AHINCR apart) */
static DWORD bits_copy(HBITMAP h, DWORD n, void __far *buf, int get)
{
    LPPBITMAP pb = bm_pbitmap(h);
    WORD scan, sel, off;
    DWORD done = 0;
    int y;
    if (!pb)
        return 0;
    scan = pb->bmWidthBytes * pb->bmPlanes;
    sel = SELECTOROF(buf);
    off = OFFSETOF(buf);
    for (y = 0; y < pb->bmHeight && done < n; y++) {
        WORD k = n - done < scan ? (WORD)(n - done) : scan, part;
        BYTE __far *bm = bm_scan(pb, y);
        part = off && (WORD)(0x10000L - off) < k ? (WORD)(0x10000L - off) : k;
        if (get)
            mem_copy(MAKELP(sel, off), bm, part);
        else
            mem_copy(bm, MAKELP(sel, off), part);
        if (part < k) {
            sel += gdi_ahincr;
            if (get)
                mem_copy(MAKELP(sel, 0), bm + part, k - part);
            else
                mem_copy(bm + part, MAKELP(sel, 0), k - part);
            off = k - part;
        } else {
            off += k;
            if (!off && k)
                sel += gdi_ahincr;
        }
        done += k;
    }
    return done;
}

LONG GDIAPI GetBitmapBits(HBITMAP h, LONG n, void __far *buf)      /* GDI.74 */
{
    return n > 0 ? bits_copy(h, n, buf, 1) : 0;
}

LONG GDIAPI SetBitmapBits(HBITMAP h, DWORD n, const void __far *buf) /* GDI.106 */
{
    return bits_copy(h, n, (void __far *)buf, 0);
}

DWORD GDIAPI SetBitmapDimension(HBITMAP h, int x, int y)           /* GDI.163 */
{
    BITMAPOBJ NEAR *b = obj_get(h, BITMAP_MAGIC);
    DWORD old;
    if (!b)
        return 0;
    old = MAKELONG(b->sizeX, b->sizeY);
    b->sizeX = x;
    b->sizeY = y;
    return old;
}

DWORD GDIAPI GetBitmapDimension(HBITMAP h)                         /* GDI.162 */
{
    BITMAPOBJ NEAR *b = obj_get(h, BITMAP_MAGIC);
    return b ? MAKELONG(b->sizeX, b->sizeY) : 0;
}

/* ---- DIBs ---- */

typedef struct {
    DWORD biSize;
    long biWidth, biHeight;
    WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    long biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { DWORD bcSize; short bcWidth, bcHeight; WORD bcPlanes, bcBitCount; } BITMAPCOREHEADER;

/* a BITMAPINFO the driver can take: the caller's, or for a core header a
   converted copy in buf (header and up to 256 RGBQUADs) */
static void __far *dib_info(const void __far *bmi, WORD usage, BYTE __far *buf)
{
    const BITMAPCOREHEADER __far *bc = bmi;
    BITMAPINFOHEADER __far *bi = (BITMAPINFOHEADER __far *)buf;
    const BYTE __far *src;
    BYTE __far *dst;
    int n, i;
    if (bc->bcSize != sizeof(BITMAPCOREHEADER))
        return (void __far *)bmi;
    mem_set(buf, 0, sizeof(BITMAPINFOHEADER));
    bi->biSize = sizeof(BITMAPINFOHEADER);
    bi->biWidth = bc->bcWidth;
    bi->biHeight = bc->bcHeight;
    bi->biPlanes = 1;
    bi->biBitCount = bc->bcBitCount;
    n = bc->bcBitCount <= 8 ? 1 << bc->bcBitCount : 0;
    src = (const BYTE __far *)bmi + sizeof(BITMAPCOREHEADER);
    dst = buf + sizeof(BITMAPINFOHEADER);
    for (i = 0; i < n; i++) {
        if (usage == 1) {               /* DIB_PAL_COLORS: indices (WORDs) */
            dst[0] = src[0];
            dst[1] = src[1];
            src += 2;
            dst += 2;
        } else {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 0;
            src += 3;
            dst += 4;
        }
    }
    return buf;
}

static int dib_bits(HDC hdc, HBITMAP hbm, WORD start, WORD n, void __far *bits,
                    void __far *bmi, WORD usage, int get)
{
    NPDC dc = dc_get(hdc);
    LPPBITMAP pb = bm_pbitmap(hbm);
    BYTE info[sizeof(BITMAPINFOHEADER) + 256 * 4];
    void __far *bi;
    DRVDEVBMBITS fn;
    if (!dc || !pb || !bmi)
        return 0;
    fn = (DRVDEVBMBITS)dc->dev->fn[DRV_DEVICEBITMAPBITS];
    if (!fn)
        return 0;
    bi = dib_info(bmi, usage, info);
    /* UNVERIFIED: DIB_PAL_COLORS translation through the DC's palette */
    return fn(pb, get, start, n, bits, bi, &dc->dm, 0);
}

int GDIAPI SetDIBits(HDC hdc, HBITMAP hbm, WORD start, WORD n, const void __far *bits,
                     void __far *bmi, WORD usage)                  /* GDI.440 */
{
    return dib_bits(hdc, hbm, start, n, (void __far *)bits, bmi, usage, 0);
}

int GDIAPI GetDIBits(HDC hdc, HBITMAP hbm, WORD start, WORD n, void __far *bits,
                     void __far *bmi, WORD usage)                  /* GDI.441 */
{
    BITMAPINFOHEADER __far *bi = bmi;
    LPPBITMAP pb = bm_pbitmap(hbm);
    if (!pb || !bi)
        return 0;
    if (bi->biSize == sizeof(BITMAPINFOHEADER) && !bi->biBitCount) {
        /* UNVERIFIED: a zero bit count asks for the bitmap's format */
        bi->biWidth = pb->bmWidth;
        bi->biHeight = pb->bmHeight;
        bi->biPlanes = 1;
        bi->biBitCount = pb->bmPlanes * pb->bmBitsPixel;
        return pb->bmHeight;
    }
    if (bi->biSize == sizeof(BITMAPINFOHEADER) && !bits) {
        WORD bpp = bi->biBitCount;
        bi->biSizeImage = ((((DWORD)bi->biWidth * bpp + 31) >> 5) << 2) *
                          (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight);
    }
    if (!bits)
        return pb->bmHeight;
    return dib_bits(hdc, hbm, start, n, bits, bmi, usage, 1);
}

HBITMAP GDIAPI CreateDIBitmap(HDC hdc, const void __far *bmih, DWORD init,
                              const void __far *bits, void __far *bmi, WORD usage) /* GDI.442 */
{
    const BITMAPINFOHEADER __far *bi = bmih;
    const BITMAPCOREHEADER __far *bc = bmih;
    int w, h;
    HBITMAP hb;
    if (!bmih)
        return 0;
    if (bc->bcSize == sizeof(BITMAPCOREHEADER)) {
        w = bc->bcWidth;
        h = bc->bcHeight;
    } else {
        w = (int)bi->biWidth;
        h = (int)bi->biHeight;
    }
    hb = CreateCompatibleBitmap(hdc, w, h);
    if (hb && (init & 4) && bits && bmi)
        SetDIBits(hdc, hb, 0, h, bits, bmi, usage);
    return hb;
}

/* SetDIBitsToDevice: the driver's DIBScreenBlt (ordinal 21) draws scan
   lines iScan .. iScan + cScans - 1 of the DIB placed with its top left at
   X, Y; the destination rectangle is the clip rectangle, cut by the
   composite region (as 3.1 passed them, GD1 with WinMine). */
int GDIAPI SetDIBitsToDevice(HDC hdc, int x, int y, int cx, int cy, int xs, int ys,
                             WORD start, WORD n, void __far *bits, void __far *bmi,
                             WORD usage)                           /* GDI.443 */
{
    NPDC dc = dc_out(hdc);
    BYTE info[sizeof(BITMAPINFOHEADER) + 256 * 4];
    void __far *bi;
    DRVSETDIBTODEV fn;
    NPRGN rao;
    int p[2], X, Y;
    RECT d;
    WORD k;
    if (!dc || !bmi || !bits)
        return 0;
    fn = (DRVSETDIBTODEV)dc->dev->fn[DRV_SETDIBITSTODEVICE];
    if (!fn)
        return 0;
    bi = dib_info(bmi, usage, info);
    p[0] = x;
    p[1] = y;
    dc_lp_to_dp(dc, p, 1);
    d.left = p[0] + dc->dcOrg.x;
    d.top = p[1] + dc->dcOrg.y;
    d.right = d.left + cx;
    d.bottom = d.top + cy;
    X = d.left - xs;
    /* GD1: 3.1 passed Y for the top of the whole DIB (biHeight lines; WinMine's
       digits: Y = -158 for a 23-line band of a 276-line DIB at y 95) */
    Y = d.top + ys + cy - (int)((BITMAPINFOHEADER __far *)bi)->biHeight;
    rao = dc_rao(dc);
    for (k = 0; k < rao->n; k++) {
        RECT c = rao->r[k];
        if (c.left < d.left) c.left = d.left;
        if (c.top < d.top) c.top = d.top;
        if (c.right > d.right) c.right = d.right;
        if (c.bottom > d.bottom) c.bottom = d.bottom;
        if (c.left >= c.right || c.top >= c.bottom)
            continue;
        fn(dc->lpPDevice, X, Y, start, n, &c, &dc->dm, bits, bi, 0);
        rao = rgn_ptr(dc->hRao);
    }
    dc_update_bounds(dc, d.left, d.top, d.right, d.bottom);
    return n;
}
