/*
 * misc.c - GDI's initialisation, palettes (for a non-palette device such
 * as VGA), Escape, MulDiv and the small USER services.
 */
#include "gdi.h"

HINSTANCE gdi_inst;

int __cdecl __far gdi_init(WORD hinst)
{
    gdi_inst = hinst;
    xheap_init();
    font_init();
    stock_init();
    return 1;
}

/* ---- palettes ---- */

/* UNVERIFIED: the default palette's 20 entries (the static colours) */
static const PALETTEENTRY defpal[20] = {
    {0, 0, 0, 0}, {128, 0, 0, 0}, {0, 128, 0, 0}, {128, 128, 0, 0},
    {0, 0, 128, 0}, {128, 0, 128, 0}, {0, 128, 128, 0}, {192, 192, 192, 0},
    {192, 220, 192, 0}, {166, 202, 240, 0}, {255, 251, 240, 0}, {160, 160, 164, 0},
    {128, 128, 128, 0}, {255, 0, 0, 0}, {0, 255, 0, 0}, {255, 255, 0, 0},
    {0, 0, 255, 0}, {255, 0, 255, 0}, {0, 255, 255, 0}, {255, 255, 255, 0}
};

/* the entries are in GDI's heaps (GD4: 3.1's palette took 1Ah bytes of
   DGROUP with 16 entries and with 256) */
static HPALETTE pal_new(WORD n, const PALETTEENTRY __far *pe)
{
    XP x = xalloc(n * sizeof(PALETTEENTRY));
    HANDLE h;
    LPPALOBJ p;
    if (!x)
        return 0;
    h = obj_alloc(PAL_MAGIC, sizeof(PALOBJ));
    if (!h) {
        xfree(x);
        return 0;
    }
    p = obj_get(h, PAL_MAGIC);
    p->palVersion = 0x300;
    p->nEntries = n;
    p->xEntries = x;
    mem_copy(xptr(x), pe, n * sizeof(PALETTEENTRY));
    return h;
}

void pal_free(HPALETTE h)
{
    LPPALOBJ p = obj_get(h, PAL_MAGIC);
    if (!p)
        return;
    xfree(p->xEntries);
    obj_free(h);
}

static PALETTEENTRY __far *pal_entries(LPPALOBJ p)
{
    return xptr(p->xEntries);
}

HPALETTE pal_default(void)
{
    return pal_new(20, defpal);
}

typedef struct { WORD palVersion, palNumEntries; PALETTEENTRY pe[1]; } LOGPALETTE;

HPALETTE GDIAPI CreatePalette(const LOGPALETTE __far *lp)          /* GDI.360 */
{
    return lp ? pal_new(lp->palNumEntries, lp->pe) : 0;
}

WORD GDIAPI GetPaletteEntries(HPALETTE h, WORD start, WORD n, PALETTEENTRY __far *pe) /* GDI.363 */
{
    LPPALOBJ p = obj_get(h, PAL_MAGIC);
    WORD i;
    if (!p)
        return 0;
    if (!pe)
        return p->nEntries;
    for (i = 0; i < n && start + i < p->nEntries; i++)
        pe[i] = pal_entries(p)[start + i];
    return i;
}

WORD GDIAPI SetPaletteEntries(HPALETTE h, WORD start, WORD n, const PALETTEENTRY __far *pe) /* GDI.364 */
{
    LPPALOBJ p = obj_get(h, PAL_MAGIC);
    WORD i;
    if (!p)
        return 0;
    for (i = 0; i < n && start + i < p->nEntries; i++)
        pal_entries(p)[start + i] = pe[i];
    return i;
}

HPALETTE GDIAPI GDISelectPalette(HDC hdc, HPALETTE h, BOOL bk)     /* GDI.361 */
{
    LPDC dc = dc_get(hdc);
    HPALETTE old;
    (void)bk;
    if (!dc || !obj_get(h, PAL_MAGIC))
        return 0;
    old = dc->hPal;
    dc->hPal = h;
    return old;
}

/* a non-palette device (VGA: no RC_PALETTE) maps nothing. A DWORD
   (Schulman: DWORD GdiRealizePalette): 3.1 returned 0 in DX:AX, and USER
   took a nonzero DX as a palette change (GD2, Calculator's menu) */
DWORD GDIAPI GDIRealizePalette(HDC hdc)                            /* GDI.362 */
{
    (void)hdc;
    return 0;
}

int GDIAPI RealizeDefaultPalette(HDC hdc)                          /* GDI.365 */
{
    (void)hdc;
    return 0;
}

BOOL GDIAPI IsDCCurrentPalette(HDC hdc)                            /* GDI.412 */
{
    (void)hdc;
    return FALSE;
}

WORD GDIAPI GetSystemPaletteUse(HDC hdc)                           /* GDI.374 */
{
    (void)hdc;
    return 1;                           /* SYSPAL_STATIC */
}

WORD GDIAPI SetSystemPaletteUse(HDC hdc, WORD u)                   /* GDI.373 */
{
    (void)hdc;
    (void)u;
    return 1;
}

WORD GDIAPI GetSystemPaletteEntries(HDC hdc, WORD start, WORD n, PALETTEENTRY __far *pe) /* GDI.375 */
{
    (void)hdc; (void)start; (void)n; (void)pe;
    return 0;
}

WORD GDIAPI GetNearestPaletteIndex(HPALETTE h, COLORREF c)         /* GDI.370 */
{
    LPPALOBJ p = obj_get(h, PAL_MAGIC);
    PALETTEENTRY __far *e;
    WORD i, best = 0;
    long bd = 0x7FFFFFFFL;
    if (!p)
        return 0;
    e = pal_entries(p);
    for (i = 0; i < p->nEntries; i++) {
        long dr = (long)e[i].peRed - (BYTE)c, dg = (long)e[i].peGreen - (BYTE)(c >> 8);
        long db = (long)e[i].peBlue - (BYTE)(c >> 16), d = dr * dr + dg * dg + db * db;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* ---- misc ---- */

/* GDI.128: the SDK's MulDiv, rounded; UNVERIFIED overflow result */
int GDIAPI MulDiv(int a, int b, int c)                             /* GDI.128 */
{
    long p = (long)a * b, q;
    if (!c)
        return -32768;
    q = (p < 0) != (c < 0) ? (p - c / 2) / c : (p + c / 2) / c;
    if (q > 32767 || q < -32768)
        return -32768;
    return (int)q;
}

int GDIAPI Escape(HDC hdc, int fn, int n, LPCSTR in, void __far *out) /* GDI.38 */
{
    LPDC dc = dc_get(hdc);
    (void)n;
    if (!dc || !dc->dev->fn[DRV_CONTROL])
        return 0;
    return ((DRVCONTROL)dc->dev->fn[DRV_CONTROL])(dc->lpPDevice, fn, (LPVOID)in, out);
}

/* USER's last initialisation call: the desktop brush (Schulman); nothing
   to do here */
void GDIAPI FinalGdiInit(HBRUSH h)                                 /* GDI.405 */
{
    (void)h;
}

WORD GDIAPI ShrinkGdiHeap(void)                                    /* GDI.354 */
{
    return LocalCompact(0xFFFF);
}

BOOL GDIAPI IsValidMetaFile(HANDLE h)                              /* GDI.410 */
{
    (void)h;
    return FALSE;
}

BOOL GDIAPI DeleteMetaFile(HANDLE h)                               /* GDI.127 */
{
    return GlobalFree(h) == 0;
}

int GDIAPI FixupBogusPublisherMetafile(HANDLE h, DWORD d)          /* GDI.464 */
{
    (void)h;
    (void)d;
    return 0;
}

BOOL GDIAPI GetCharABCWidths(HDC hdc, WORD a, WORD b, void __far *p) /* GDI.307 */
{
    (void)hdc; (void)a; (void)b; (void)p;
    return FALSE;                       /* TrueType only */
}

WORD GDIAPI GetRasterizerCaps(void __far *p, int n)               /* GDI.313 */
{
    if (p && n >= 4) {
        ((short __far *)p)[0] = 4;
        ((short __far *)p)[1] = 0;      /* no TrueType rasterizer yet */
    }
    return TRUE;
}
