/*
 * object.c - GDI objects: handles, stock objects, creation, GetObject,
 * DeleteObject, SelectObject, IsGDIObject, MakeObjectPrivate.
 *
 * Objects live in GDI's local heap as moveable blocks; a handle is the
 * local handle (Schulman ch. 8: handles index GDI's default heap; GD2 saw
 * handles 4 apart, as the local handle table's entries are). Each starts
 * with the object header (Schulman ch. 8), signature 'GO'..'QO' + flags.
 * IsGDIObject returns the type, the signature minus 4F46h (GD2 on 3.1:
 * 7 for a DC, 2 for a brush). MakeObjectPrivate sets or clears 2000h in
 * the signature and returned 0 for public objects (GD2).
 */
#include "gdi.h"

HANDLE stock[20];
DWORD obj_seq;
HBITMAP stock_bitmap;
NPDC dc_list[1];

HANDLE obj_alloc(WORD magic, WORD size)
{
    HANDLE h = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, size);
    OBJHDR NEAR *o;
    if (!h)
        return 0;
    LocalLock(h);                       /* objects never move */
    o = POBJ(h);
    o->wMagic = magic;
    o->dwCount = ++obj_seq;
    return h;
}

/* the object of a handle if it has that signature (0: any) */
void NEAR *obj_get(HANDLE h, WORD magic)
{
    OBJHDR NEAR *o;
    WORD sz;
    if (!h || (h & 3) != 2)
        return 0;
    sz = LocalSize(h);
    if (sz < sizeof(OBJHDR))
        return 0;
    o = POBJ(h);
    if (!o)
        return 0;
    if ((o->wMagic & MAGIC_MASK) < PEN_MAGIC || (o->wMagic & MAGIC_MASK) > METAFILEDC_MAGIC)
        return 0;
    if (magic && (o->wMagic & MAGIC_MASK) != magic)
        return 0;
    return o;
}

int obj_type(HANDLE h)
{
    OBJHDR NEAR *o = obj_get(h, 0);
    return o ? (o->wMagic & MAGIC_MASK) - MAGIC_BASE : 0;
}

void obj_free(HANDLE h)
{
    LocalUnlock(h);
    LocalFree(h);
}

static int is_stock(HANDLE h)
{
    int i;
    for (i = 0; i < 20; i++)
        if (stock[i] == h)
            return 1;
    return h == stock_bitmap;
}

/* ---- creation ---- */

HPEN GDIAPI CreatePenIndirect(const LOGPEN __far *lp)              /* GDI.62 */
{
    HANDLE h = obj_alloc(PEN_MAGIC, sizeof(PENOBJ));
    PENOBJ NEAR *p;
    if (!h)
        return 0;
    p = DEREF(h);
    p->lp = *lp;
    if (p->lp.lopnStyle > 6)            /* PS_INSIDEFRAME is the last */
        p->lp.lopnStyle = 0;
    return h;
}

HPEN GDIAPI CreatePen(int style, int width, COLORREF color)        /* GDI.61 */
{
    LOGPEN lp;
    lp.lopnStyle = style;
    lp.lopnWidth.x = width;
    lp.lopnWidth.y = 0;
    lp.lopnColor = color;
    return CreatePenIndirect(&lp);
}

HBRUSH GDIAPI CreateBrushIndirect(const LOGBRUSH __far *lb)        /* GDI.50 */
{
    HANDLE h = obj_alloc(BRUSH_MAGIC, sizeof(BRUSHOBJ));
    BRUSHOBJ NEAR *b;
    if (!h)
        return 0;
    b = DEREF(h);
    b->lb = *lb;
    b->crHatchBk = RGB(255, 255, 255);
    if (lb->lbStyle == 3) {             /* BS_PATTERN: keep a copy */
        LPPBITMAP src = bm_pbitmap(lb->lbHatch);
        if (!src) {
            obj_free(h);
            return 0;
        }
        b->hbmPattern = bm_create(8, 8, src->bmPlanes, src->bmBitsPixel, 0, 0);
        if (b->hbmPattern) {
            LPPBITMAP d = bm_pbitmap(b->hbmPattern);
            int y, w = min(src->bmWidthBytes, d->bmWidthBytes), p;
            for (p = 0; p < src->bmPlanes; p++)
                for (y = 0; y < 8 && y < src->bmHeight; y++)
                    mem_copy((char __far *)d->bmBits + p * d->bmWidthPlanes + y * d->bmWidthBytes,
                             (char __far *)src->bmBits + p * src->bmWidthPlanes + y * src->bmWidthBytes, w);
            b->lb.lbHatch = b->hbmPattern;
        }
    }
    return h;
}

HBRUSH GDIAPI CreateSolidBrush(COLORREF color)                     /* GDI.66 */
{
    LOGBRUSH lb;
    lb.lbStyle = 0;
    lb.lbColor = color;
    lb.lbHatch = 0;
    return CreateBrushIndirect(&lb);
}

HBRUSH GDIAPI CreateHatchBrush(int hatch, COLORREF color)          /* GDI.58 */
{
    LOGBRUSH lb;
    lb.lbStyle = 2;
    lb.lbColor = color;
    lb.lbHatch = hatch;
    return CreateBrushIndirect(&lb);
}

HBRUSH GDIAPI CreatePatternBrush(HBITMAP hbm)                      /* GDI.60 */
{
    LOGBRUSH lb;
    lb.lbStyle = 3;
    lb.lbColor = 0;
    lb.lbHatch = hbm;
    return CreateBrushIndirect(&lb);
}

HFONT GDIAPI CreateFontIndirect(const LOGFONT __far *lf)           /* GDI.57 */
{
    HANDLE h = obj_alloc(FONT_MAGIC, sizeof(FONTOBJ));
    FONTOBJ NEAR *f;
    int i;
    if (!h)
        return 0;
    f = DEREF(h);
    mem_copy(&f->lf, lf, sizeof(LOGFONT) - LF_FACESIZE);
    for (i = 0; i < LF_FACESIZE - 1 && lf->lfFaceName[i]; i++)
        f->lf.lfFaceName[i] = lf->lfFaceName[i];
    return h;
}

HFONT GDIAPI CreateFont(int h, int w, int esc, int orient, int weight, BYTE italic,
                        BYTE underline, BYTE strike, BYTE charset, BYTE outprec,
                        BYTE clipprec, BYTE quality, BYTE pitch, LPCSTR face) /* GDI.56 */
{
    LOGFONT lf;
    int i;
    lf.lfHeight = h;
    lf.lfWidth = w;
    lf.lfEscapement = esc;
    lf.lfOrientation = orient;
    lf.lfWeight = weight;
    lf.lfItalic = italic;
    lf.lfUnderline = underline;
    lf.lfStrikeOut = strike;
    lf.lfCharSet = charset;
    lf.lfOutPrecision = outprec;
    lf.lfClipPrecision = clipprec;
    lf.lfQuality = quality;
    lf.lfPitchAndFamily = pitch;
    for (i = 0; i < LF_FACESIZE; i++)
        lf.lfFaceName[i] = 0;
    for (i = 0; face && i < LF_FACESIZE - 1 && face[i]; i++)
        lf.lfFaceName[i] = face[i];
    return CreateFontIndirect(&lf);
}

/* ---- stock objects ---- */

static HANDLE stock_brush(int style, COLORREF c)
{
    LOGBRUSH lb;
    lb.lbStyle = style;
    lb.lbColor = c;
    lb.lbHatch = 0;
    return CreateBrushIndirect(&lb);
}

static HANDLE stock_font(const LOGFONT __far *lf)
{
    return CreateFontIndirect(lf);
}

/* the stock fonts OEM_FIXED, ANSI_FIXED and ANSI_VAR come from the
   display driver's OEMBIN resource "FONTS" (DDK: the stock-font resource;
   GDI's initialisation failed with a stand-in DISPLAY driver without the
   resources, GD1): three entries, each a LOGFONT whose face name is
   cut at its terminating zero */
static void driver_stock_fonts(void)
{
    HMODULE hd = GetModuleHandle("DISPLAY");
    HRSRC r = hd ? FindResource(hd, "FONTS", "OEMBIN") : 0;
    HGLOBAL g = r ? LoadResource(hd, r) : 0;
    BYTE __far *p = g ? LockResource(g) : 0;
    int i, k;
    for (i = 0; i < 3; i++) {
        LOGFONT lf;
        mem_set(&lf, 0, sizeof lf);
        if (p) {
            mem_copy(&lf, p, 18);
            p += 18;
            for (k = 0; *p && k < LF_FACESIZE - 1; k++)
                lf.lfFaceName[k] = *p++;
            p++;
        }
        stock[10 + i] = stock_font(&lf);
    }
    if (g)
        FreeResource(g);
}

void stock_init(void)
{
    static LOGFONT sysfont = { 16, 7, 0, 0, 700, 0, 0, 0, 0, 1, 2, 2, 0x22, "System" };
    static LOGFONT devfont = { 0 };
    static LOGFONT fixfont = { 15, 8, 0, 0, 400, 0, 0, 0, 0, 1, 2, 2, 0x31, "Fixedsys" };
    LOGPEN lp;
    stock[0] = stock_brush(0, RGB(255, 255, 255));       /* WHITE_BRUSH */
    stock[1] = stock_brush(0, RGB(192, 192, 192));       /* LTGRAY_BRUSH */
    stock[2] = stock_brush(0, RGB(128, 128, 128));       /* GRAY_BRUSH */
    stock[3] = stock_brush(0, RGB(64, 64, 64));          /* DKGRAY_BRUSH */
    stock[4] = stock_brush(0, RGB(0, 0, 0));             /* BLACK_BRUSH */
    stock[5] = stock_brush(1, 0);                        /* NULL_BRUSH */
    lp.lopnStyle = 0;
    lp.lopnWidth.x = 0;
    lp.lopnWidth.y = 0;
    lp.lopnColor = RGB(255, 255, 255);
    stock[6] = CreatePenIndirect(&lp);                   /* WHITE_PEN */
    lp.lopnColor = 0;
    stock[7] = CreatePenIndirect(&lp);                   /* BLACK_PEN */
    lp.lopnStyle = 5;
    stock[8] = CreatePenIndirect(&lp);                   /* NULL_PEN */
    driver_stock_fonts();                                /* 10, 11, 12 */
    /* UNVERIFIED: the LOGFONTs of SYSTEM_FONT (GD1 saw these values given
       to the driver's RealizeObject), DEVICE_DEFAULT_FONT and
       SYSTEM_FIXED_FONT */
    stock[13] = stock_font(&sysfont);                    /* SYSTEM_FONT */
    stock[14] = stock_font(&devfont);                    /* DEVICE_DEFAULT_FONT */
    stock[15] = pal_default();                           /* DEFAULT_PALETTE */
    stock[16] = stock_font(&fixfont);                    /* SYSTEM_FIXED_FONT */
    /* the bitmap a new memory DC has: 1 x 1 monochrome (GD1: the PBITMAP
       the driver gets for a fresh memory DC) */
    stock_bitmap = bm_create(1, 1, 1, 1, 0, BMF_STOCK);
}

HGDIOBJ GDIAPI GetStockObject(int i)                               /* GDI.87 */
{
    if (i < 0 || i >= 20)
        return 0;
    return stock[i];
}

/* ---- queries ---- */

int GDIAPI GetObject(HGDIOBJ h, int n, void __far *buf)            /* GDI.82 */
{
    OBJHDR NEAR *o = obj_get(h, 0);
    int size;
    void NEAR *src;
    BITMAP bm;
    if (!o || n < 0)
        return 0;
    switch (o->wMagic & MAGIC_MASK) {
    case PEN_MAGIC:
        src = &((PENOBJ NEAR *)o)->lp;
        size = sizeof(LOGPEN);
        break;
    case BRUSH_MAGIC:
        src = &((BRUSHOBJ NEAR *)o)->lb;
        size = sizeof(LOGBRUSH);
        break;
    case FONT_MAGIC:
        src = &((FONTOBJ NEAR *)o)->lf;
        size = sizeof(LOGFONT);
        break;
    case PAL_MAGIC:
        src = &((PALOBJ NEAR *)o)->nEntries;
        size = 2;
        break;
    case BITMAP_MAGIC: {
        LPPBITMAP pb = bm_pbitmap(h);
        bm.bmType = 0;
        bm.bmWidth = pb->bmWidth;
        bm.bmHeight = pb->bmHeight;
        bm.bmWidthBytes = pb->bmWidthBytes;
        bm.bmPlanes = pb->bmPlanes;
        bm.bmBitsPixel = pb->bmBitsPixel;
        bm.bmBits = 0;
        src = 0;
        size = sizeof(BITMAP);
        if (n > size)
            n = size;
        if (buf)
            mem_copy(buf, &bm, n);
        return n;
    }
    default:
        return 0;
    }
    if (!buf)
        return size;
    if (n > size)
        n = size;
    mem_copy(buf, src, n);
    return n;
}

int GDIAPI IsGDIObject(HANDLE h)                                    /* GDI.462 */
{
    return obj_type(h);
}

BOOL GDIAPI MakeObjectPrivate(HANDLE h, BOOL priv)                  /* GDI.463 */
{
    OBJHDR NEAR *o = obj_get(h, 0);
    BOOL old;
    if (!o)
        return 0;
    old = (o->wMagic & OBJ_PRIVATE) != 0;
    if (priv)
        o->wMagic |= OBJ_PRIVATE;
    else
        o->wMagic &= ~OBJ_PRIVATE;
    return old;
}

BOOL GDIAPI UnrealizeObject(HGDIOBJ h)                              /* GDI.150 */
{
    OBJHDR NEAR *o = obj_get(h, 0);
    if (!o)
        return FALSE;
    if ((o->wMagic & MAGIC_MASK) == BRUSH_MAGIC)
        ((BRUSHOBJ NEAR *)o)->gen++;
    return TRUE;
}

/* SetObjectOwner (GDI.461): the retail 3.1 does nothing (Schulman) */
HANDLE GDIAPI SetObjectOwner(HANDLE h, HANDLE owner)                /* GDI.461 */
{
    (void)owner;
    return h;
}

int dc_uses(HANDLE h);                  /* dc.c */
void bm_free(HBITMAP h);                /* bitmap.c */
void rgn_free(HRGN h);                  /* rgn.c */

BOOL GDIAPI DeleteObject(HGDIOBJ h)                                 /* GDI.69 */
{
    OBJHDR NEAR *o = obj_get(h, 0);
    if (!o)
        return FALSE;
    if (is_stock(h))
        return TRUE;
    switch (o->wMagic & MAGIC_MASK) {
    case DC_MAGIC:
    case DISDC_MAGIC:
    case METADC_MAGIC:
        return FALSE;
    case BITMAP_MAGIC:
        if (((BITMAPOBJ NEAR *)o)->hdcSel)
            return FALSE;
        bm_free(h);
        return TRUE;
    case RGN_MAGIC:
        obj_free(h);
        return TRUE;
    case BRUSH_MAGIC:
        if (dc_uses(h))
            return FALSE;
        if (((BRUSHOBJ NEAR *)o)->hbmPattern)
            bm_free(((BRUSHOBJ NEAR *)o)->hbmPattern);
        obj_free(h);
        return TRUE;
    default:
        if (dc_uses(h))
            return FALSE;
        obj_free(h);
        return TRUE;
    }
}
