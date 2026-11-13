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
WORD gdi_extended;

/* the limit of segment sel (LSL), 0 if it is not valid */
WORD seg_limit(WORD sel);
#pragma aux seg_limit = "xor ax, ax" "lsl ax, dx" parm [dx] value [ax];
WORD get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax];

/* ---- ExtendedGdiHeap: the object table ----
   A stub is a fixed 4-byte block in DGROUP (its address is the handle):
   the number of the object's entry in this table, then the signature
   where TOOLHELP and IsGDIObject find it (offset 2, Schulman ch. 8). The
   table's entries (the bodies' XPs) are in pages in GDI's heaps; a free
   entry has a zero high word and the next free entry + 1 in its low. */
#define BT_PAGE 2048
#define BT_PAGES 32
static XP btdir[BT_PAGES];
static WORD bt_n, bt_free;

static XP __far *bt_slot(WORD i)
{
    XP d = i / BT_PAGE < BT_PAGES ? btdir[i / BT_PAGE] : 0;
    return d ? (XP __far *)xptr(d) + i % BT_PAGE : (XP __far *)0;
}

static int bt_new(XP body, WORD *idx)
{
    WORD i;
    XP __far *s;
    if (bt_free) {
        i = bt_free - 1;
        bt_free = LOWORD(*bt_slot(i));
    } else {
        i = bt_n;
        if (i / BT_PAGE >= BT_PAGES)
            return 0;
        if (!btdir[i / BT_PAGE]) {
            btdir[i / BT_PAGE] = xalloc(BT_PAGE * sizeof(XP));
            if (!btdir[i / BT_PAGE])
                return 0;
        }
        bt_n++;
    }
    s = bt_slot(i);
    *s = body;
    *idx = i;
    return 1;
}

static void bt_del(WORD i)
{
    *bt_slot(i) = bt_free;
    bt_free = i + 1;
}

/* the object's stub (ExtendedGdiHeap) and the body's XP, if h is one */
static OBJSTUB NEAR *stub_of(HANDLE h, XP *body)
{
    OBJSTUB NEAR *o;
    XP __far *s;
    WORD sz;
    if (!h || (h & 1) || h > seg_limit(get_ds()) - sizeof(OBJSTUB) + 1)
        return 0;
    /* (KERNEL may give a 4-byte request a bigger free block whole) */
    sz = LocalSize(h);
    if (sz < sizeof(OBJSTUB))
        return 0;
    o = (OBJSTUB NEAR *)h;
    if (o->idx >= bt_n || !(s = bt_slot(o->idx)) || !HIWORD(*s))
        return 0;
    /* the body names its stub (in wMetaList, unused by w16) */
    if (((LPOBJHDR)xptr(*s))->wMagic != o->wMagic || ((LPOBJHDR)xptr(*s))->wMetaList != h)
        return 0;
    *body = *s;
    return o;
}

/* An object's block in DGROUP is moveable and locked (objects never
   move; GD2: handles 4 apart, as the local handle table's entries are).
   With ExtendedGdiHeap it is an OBJSTUB and the object is in GDI's heaps,
   its header's wMetaList the stub's handle. */
HANDLE obj_alloc(WORD magic, WORD size)
{
    HANDLE h;
    LPOBJHDR o;
    XP body;
    WORD idx;
    if (gdi_extended) {
        body = xalloc(size);
        if (!body)
            return 0;
        if (!bt_new(body, &idx)) {
            xfree(body);
            return 0;
        }
        h = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(OBJSTUB));
        if (!h) {
            bt_del(idx);
            xfree(body);
            return 0;
        }
        ((OBJSTUB NEAR *)h)->idx = idx;
        ((OBJSTUB NEAR *)h)->wMagic = magic;
        o = xptr(body);
        o->wMetaList = h;
    } else {
        h = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, size);
        if (!h)
            return 0;
        LocalLock(h);
        o = (OBJHDR NEAR *)DEREF(h);
    }
    o->wMagic = magic;
    o->dwCount = ++obj_seq;
    return h;
}

/* the signature (with its flags) of a valid object handle, 0 if none */
WORD obj_magic(HANDLE h)
{
    WORD m;
    if (gdi_extended) {
        XP body;
        OBJSTUB NEAR *s = stub_of(h, &body);
        if (!s)
            return 0;
        m = s->wMagic;
    } else {
        OBJHDR NEAR *o;
        if (!h || (h & 3) != 2 || h > seg_limit(get_ds()) - 1)
            return 0;
        if (LocalSize(h) < sizeof(OBJHDR))
            return 0;
        o = DEREF(h);
        if (!o)
            return 0;
        m = o->wMagic;
    }
    if ((m & MAGIC_MASK) < PEN_MAGIC || (m & MAGIC_MASK) > METAFILEDC_MAGIC)
        return 0;
    return m;
}

/* the whole object of a handle if it has that signature (0: any) */
void __far *obj_get(HANDLE h, WORD magic)
{
    WORD m = obj_magic(h);
    XP body;
    if (!m || (magic && (m & MAGIC_MASK) != magic))
        return 0;
    if (gdi_extended)
        return stub_of(h, &body) ? xptr(body) : 0;
    return (OBJHDR NEAR *)DEREF(h);
}

void obj_set_magic(HANDLE h, WORD m)
{
    LPOBJHDR o = obj_get(h, 0);
    if (!o)
        return;
    o->wMagic = m;
    if (gdi_extended)
        ((OBJSTUB NEAR *)h)->wMagic = m;
}

int obj_type(HANDLE h)
{
    WORD m = obj_magic(h);
    return m ? (m & MAGIC_MASK) - MAGIC_BASE : 0;
}

void obj_free(HANDLE h)
{
    if (gdi_extended) {
        XP body;
        OBJSTUB NEAR *s = stub_of(h, &body);
        if (!s)
            return;
        s->wMagic = 0;
        bt_del(s->idx);
        xfree(body);
        LocalFree(h);
        return;
    }
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
    LPPENOBJ p;
    if (!h)
        return 0;
    p = obj_get(h, PEN_MAGIC);
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

/* BS_PATTERN: GetObject gives back the program's bitmap (GD4 on 3.1), and
   the brush keeps its own copy of the 8 x 8 pattern (the program may
   delete the bitmap), a global block made as a bitmap's is, PBITMAP and
   bits, in what Schulman's BRUSHOBJ calls crHatchBk (GD4: 3.1's pattern
   brush took no more of DGROUP than a solid one) */
HBRUSH GDIAPI CreateBrushIndirect(const LOGBRUSH __far *lb)        /* GDI.50 */
{
    HANDLE h;
    HGLOBAL pat = 0;
    LPBRUSHOBJ b;
    if (lb->lbStyle == 3) {
        pat = bm_pattern_copy(lb->lbHatch);
        if (!pat)
            return 0;
    }
    h = obj_alloc(BRUSH_MAGIC, sizeof(BRUSHOBJ));
    if (!h) {
        if (pat) {
            GlobalUnlock(pat);
            GlobalFree(pat);
        }
        return 0;
    }
    b = obj_get(h, BRUSH_MAGIC);
    b->lb = *lb;
    b->hPatBits = pat;
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
    HANDLE h;
    LPFONTOBJ f;
    int i, n = 0;
    while (n < LF_FACESIZE - 1 && lf->lfFaceName[n])
        n++;
    h = obj_alloc(FONT_MAGIC, sizeof(OBJHDR) + LF_FIXEDPART + max(n + 1, FONT_MINFACE));
    if (!h)
        return 0;
    f = obj_get(h, FONT_MAGIC);
    mem_copy(&f->lf, lf, LF_FIXEDPART);
    for (i = 0; i < n; i++)
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
    LPOBJHDR o = obj_get(h, 0);
    int size;
    void __far *src;
    BITMAP bm;
    LOGFONT lf;
    if (!o || n < 0)
        return 0;
    switch (o->wMagic & MAGIC_MASK) {
    case PEN_MAGIC:
        src = &((LPPENOBJ)o)->lp;
        size = sizeof(LOGPEN);
        break;
    case BRUSH_MAGIC:
        src = &((LPBRUSHOBJ)o)->lb;
        size = sizeof(LOGBRUSH);
        break;
    case FONT_MAGIC: {
        /* the block ends after the face name: the rest is zeros */
        LPFONTOBJ f = (LPFONTOBJ)o;
        int i;
        mem_set(&lf, 0, sizeof lf);
        mem_copy(&lf, &f->lf, LF_FIXEDPART);
        for (i = 0; i < LF_FACESIZE - 1 && f->lf.lfFaceName[i]; i++)
            lf.lfFaceName[i] = f->lf.lfFaceName[i];
        src = &lf;
        size = sizeof(LOGFONT);
        break;
    }
    case PAL_MAGIC:
        src = &((LPPALOBJ)o)->nEntries;
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
        src = &bm;
        size = sizeof(BITMAP);
        break;
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
    WORD m = obj_magic(h);
    if (!m)
        return 0;
    obj_set_magic(h, priv ? m | OBJ_PRIVATE : m & ~OBJ_PRIVATE);
    return (m & OBJ_PRIVATE) != 0;
}

/* the brush's realizations are made again (with the DC's brush origin
   when next used) */
BOOL GDIAPI UnrealizeObject(HGDIOBJ h)                              /* GDI.150 */
{
    WORD m = obj_magic(h);
    if (!m)
        return FALSE;
    if ((m & MAGIC_MASK) == BRUSH_MAGIC)
        dc_brush_changed(h);
    return TRUE;
}

/* SetObjectOwner (GDI.461): the retail 3.1 does nothing (Schulman) */
HANDLE GDIAPI SetObjectOwner(HANDLE h, HANDLE owner)                /* GDI.461 */
{
    (void)owner;
    return h;
}

BOOL GDIAPI DeleteObject(HGDIOBJ h)                                 /* GDI.69 */
{
    WORD m = obj_magic(h);
    if (!m)
        return FALSE;
    if (is_stock(h))
        return TRUE;
    switch (m & MAGIC_MASK) {
    case DC_MAGIC:
    case DISDC_MAGIC:
    case METADC_MAGIC:
        return FALSE;
    case BITMAP_MAGIC:
        if (((LPBITMAPOBJ)obj_get(h, BITMAP_MAGIC))->hdcSel)
            return FALSE;
        bm_free(h);
        return TRUE;
    case RGN_MAGIC:
        rgn_free(h);
        return TRUE;
    case PAL_MAGIC:
        pal_free(h);
        return TRUE;
    case BRUSH_MAGIC: {
        HGLOBAL pat;
        if (dc_uses(h))
            return FALSE;
        dc_brush_changed(h);
        pat = ((LPBRUSHOBJ)obj_get(h, BRUSH_MAGIC))->hPatBits;
        if (pat) {
            GlobalUnlock(pat);
            GlobalFree(pat);
        }
        obj_free(h);
        return TRUE;
    }
    default:
        if (dc_uses(h))
            return FALSE;
        obj_free(h);
        return TRUE;
    }
}
