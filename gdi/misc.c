/*
 * misc.c - GDI's initialisation, palettes (for a non-palette device such
 * as VGA), Escape, MulDiv and the small USER services.
 */
#include "gdi.h"

HINSTANCE gdi_inst;

int __cdecl __far gdi_init(WORD hinst)
{
    gdi_inst = hinst;
    /* [w16] ExtendedGdiHeap (gdi.h): read once, before the first object */
    gdi_extended = GetPrivateProfileInt("w16", "ExtendedGdiHeap", 0, "SYSTEM.INI") != 0;
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
    XP x = n <= 0x3FC0 ? xalloc(n * sizeof(PALETTEENTRY)) : 0;   /* (the size in a word) */
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
    mf_palette_gone(h);
    p = obj_get(h, PAL_MAGIC);
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
    if (!p || h == stock[15] || !pe)   /* GD5: 3.1 does not change DEFAULT_PALETTE */
        return 0;
    for (i = 0; i < n && start + i < p->nEntries; i++)
        pal_entries(p)[start + i] = pe[i];
    return i;
}

/* AnimatePalette's record in the metafiles the palette is listed in
   (meta.c). GD9 on 3.1: the start and the count, then not the entries
   but as many words of AnimatePalette's own arguments as the entries
   would take, as they lie on the stack (Pascal order): the entries'
   offset and selector, the count, the start, the palette (and past that
   what follows on 3.1's stack; zeros here) */
static void pal_record(HPALETTE h, WORD fn, WORD start, WORD n, const PALETTEENTRY __far *pe)
{
    XP x = n < 0x3FF0 ? xalloc((2 + 2 * n) * 2) : 0;
    WORD __far *w;
    WORD frame[5], i;
    if (!x)
        return;
    frame[0] = OFFSETOF(pe);
    frame[1] = SELECTOROF(pe);
    frame[2] = n;
    frame[3] = start;
    frame[4] = h;
    w = xptr(x);
    w[0] = start;
    w[1] = n;
    for (i = 0; i < 2 * n; i++)
        w[2 + i] = i < 5 ? frame[i] : 0;
    mf_palette_changed(h, fn, 2 + 2 * n, w);
    xfree(x);
}

/* VGA has no palette: the entries of a PC_RESERVED entry change, nothing
   is drawn again (UNVERIFIED on a palette device) */
void GDIAPI AnimatePalette(HPALETTE h, WORD start, WORD n, const PALETTEENTRY __far *pe) /* GDI.367 */
{
    LPPALOBJ p = obj_get(h, PAL_MAGIC);
    WORD i;
    if (!p || !pe)
        return;
    pal_record(h, MR_ANIMATEPALETTE, start, n, pe);
    p = obj_get(h, PAL_MAGIC);
    for (i = 0; i < n && start + i < p->nEntries; i++)
        if (pal_entries(p)[start + i].peFlags & 1)      /* PC_RESERVED */
            pal_entries(p)[start + i] = pe[i];
}

BOOL GDIAPI ResizePalette(HPALETTE h, WORD n)                      /* GDI.368 */
{
    LPPALOBJ p = obj_get(h, PAL_MAGIC);
    XP x;
    WORD old;
    if (!p || h == stock[15] || !n || n > 0x3FC0)
        return FALSE;
    x = xalloc(n * sizeof(PALETTEENTRY));
    if (!x)
        return FALSE;
    p = obj_get(h, PAL_MAGIC);
    old = p->nEntries;
    mem_copy(xptr(x), pal_entries(p), (old < n ? old : n) * sizeof(PALETTEENTRY));
    xfree(p->xEntries);
    p->xEntries = x;
    p->nEntries = n;
    mf_palette_changed(h, MR_RESIZEPALETTE, 1, &n);
    return TRUE;
}

HPALETTE GDIAPI GDISelectPalette(HDC hdc, HPALETTE h, BOOL bk)     /* GDI.361 */
{
    LPDC dc = dc_get(hdc);
    HPALETTE old;
    (void)bk;
    if (mf_dc(hdc))
        return obj_get(h, PAL_MAGIC) ? mf_select_palette(hdc, h) : 0;
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
    if (mf_dc(hdc))                     /* GD9: 1 through USER's RealizePalette */
        return mf_rec0(hdc, MR_REALIZEPALETTE);
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
        /* each square at most 65025: 16-bit unsigned products (inline MUL,
           no long multiplications), summed as a long */
        int dr = e[i].peRed - (BYTE)c, dg = e[i].peGreen - (BYTE)(c >> 8);
        int db = e[i].peBlue - (BYTE)(c >> 16);
        long d = (long)((unsigned)dr * (unsigned)dr) + (unsigned)dg * (unsigned)dg
                 + (unsigned)db * (unsigned)db;
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

/* GD8 (GD1's log) on 3.1: on a memory DC no Control call; QUERYESCSUPPORT
   and GETCOLORTABLE return 0 there (UNVERIFIED for the other escapes) */
int GDIAPI Escape(HDC hdc, int fn, int n, LPCSTR in, void __far *out) /* GDI.38 */
{
    LPDC dc = dc_get(hdc);
    if (mf_dc(hdc))                     /* GD9: any escape, recorded, 1 */
        return mf_escape(hdc, fn, n, in);
    (void)n;
    if (!dc || !dc->dev->fn[DRV_CONTROL] || (dc->flags & DCF_MEMORY))
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



WORD GDIAPI GetRasterizerCaps(void __far *p, int n)               /* GDI.313 */
{
    /* RASTERIZER_STATUS: nSize 6, wFlags, nLanguageID; GD5 on 3.1: nSize
       is 6 and the language 0409h, n bytes of it are written; wFlags
       TT_AVAILABLE | TT_ENABLED (3). w16's: TT_ENABLED only with WIN.INI
       [TrueType] TTEnable on (UNVERIFIED: 3.1's flags then) */
    short rs[3];
    rs[0] = 6;
    rs[1] = tt_enabled ? 3 : 1;
    rs[2] = 0x0409;
    if (p && n > 0)
        mem_copy(p, rs, n < 6 ? n : 6);
    return 0x0409;                      /* GD13 on 3.1: not 1 (the language?) */
}
