/*
 * dc.c - device contexts: creation, attributes, mapping, clipping, the
 * visible region and the other DC services USER uses, SaveDC/RestoreDC
 * and GetDCState/SetDCState.
 *
 * Observed on 3.1 (GD1/GD2 at boot, tests/gdi): a new DC gets the
 * physical colours of white (background) and black (text) from the
 * driver's ColorInfo; the default brush (white), pen (black) and font
 * (System) are realized for the device; USER gives each DC it caches a
 * visible region in screen coordinates (SelectVisRgn) and its origin
 * (SetDCOrg), and on ReleaseDC resets it with SetDCState from a state it
 * took with GetDCState at start-up (Schulman ch. 8). Output goes to the
 * driver at screen coordinates (logical -> device + DC origin), clipped to
 * the visible region and the program's clip region.
 */
#include "gdi.h"

#define DCHC_INVALIDVISRGN 1
#define DCHC_DELETEDC 2
#define DCHF_INVALIDATEVISRGN 1
#define DCHF_VALIDATEVISRGN 2


LPDC dc_get(HDC h)
{
    WORD m = obj_magic(h) & MAGIC_MASK;
    return m == DC_MAGIC || m == DISDC_MAGIC ? (LPDC)obj_get(h, 0) : (LPDC)0;
}

/* ---- the DC list (DeleteObject's "still selected" check,
   UnrealizeObject), linked through the DCs' hLink ---- */

static HDC dcs;

HDC dc_first(void)
{
    return dcs;
}

static void dc_link(HDC h)
{
    LPDC dc = dc_get(h);
    dc->hLink = dcs;
    dcs = h;
}

static void dc_unlink(HDC h)
{
    HDC cur = dcs;
    LPDC prev = 0, dc;
    while (cur && (dc = dc_get(cur)) != 0) {
        if (cur == h) {
            if (prev)
                prev->hLink = dc->hLink;
            else
                dcs = dc->hLink;
            return;
        }
        prev = dc;
        cur = dc->hLink;
    }
}

int dc_uses(HANDLE h)
{
    HDC l;
    LPDC dc;
    for (l = dcs; l && (dc = dc_get(l)) != 0; l = dc->hLink)
        if (dc->hPen == h || dc->hBrush == h || dc->hFont == h)
            return 1;
    return 0;
}

/* ---- realized objects: the driver's blocks, in GDI's heaps ---- */

static void phys_free(LPDC dc, XP __far *ph, int style)
{
    if (*ph) {
        BYTE __far *p = xptr(*ph);
        ((DRVREALIZE)dc->dev->fn[DRV_REALIZEOBJECT])(dc->lpPDevice, -style, p, p, 0);
        xfree(*ph);
        *ph = 0;
    }
}

static XP phys_realize(LPDC dc, int style, void __far *in, LPVOID xf)
{
    DRVREALIZE rz = (DRVREALIZE)dc->dev->fn[DRV_REALIZEOBJECT];
    WORD size = LOWORD(rz(dc->lpPDevice, style, in, 0, xf));
    XP x;
    if (!size || size == 0xFFFF)
        return 0;
    x = xalloc(size);
    if (!x)
        return 0;
    rz(dc->lpPDevice, style, in, xptr(x), xf);
    return x;
}

/* (the DC is a locked block: its pointer stays good over these
   allocations) */
LPVOID dc_ppen(LPDC dc)
{
    LPPENOBJ p = obj_get(dc->hPen, PEN_MAGIC);
    if (!p)
        return 0;
    if (dc->hPPenOf != dc->hPen || !dc->xPPen) {
        LOGPEN lp = p->lp;
        XP x;
        phys_free(dc, &dc->xPPen, OBJ_PEN);
        x = phys_realize(dc, OBJ_PEN, &lp, &lp);
        dc->xPPen = x;
        dc->hPPenOf = dc->hPen;
    }
    return dc->xPPen ? xptr(dc->xPPen) : 0;
}

/* LBRUSH as 3.1 passes it (GD1): lbColor the logical colour (for
   BS_PATTERN the far pointer to the pattern's PBITMAP, lbHatch its global
   handle), lbBkColor the DC's physical background colour */
#pragma pack(push, 1)
typedef struct { short lbStyle; DWORD lbColor; short lbHatch; DWORD lbBkColor; } LBRUSH;
#pragma pack(pop)

LPVOID dc_pbrush(LPDC dc)
{
    LPBRUSHOBJ b = obj_get(dc->hBrush, BRUSH_MAGIC);
    if (!b)
        return 0;
    if (dc->hPBrushOf != dc->hBrush || !dc->xPBrush || dc->lpPDevOfBrush != dc->lpPDevice) {
        LBRUSH lb;
        XP x;
        lb.lbStyle = b->lb.lbStyle;
        lb.lbColor = b->lb.lbColor;
        lb.lbHatch = b->lb.lbHatch;
        lb.lbBkColor = dc->dm.bkColor;
        if (lb.lbStyle == 3) {
            lb.lbColor = (DWORD)(LPVOID)GlobalLock(b->hPatBits);
            GlobalUnlock(b->hPatBits);
            lb.lbHatch = b->hPatBits;
        } else if (lb.lbStyle != 2)
            lb.lbHatch = 0;
        phys_free(dc, &dc->xPBrush, OBJ_BRUSH);
        x = phys_realize(dc, OBJ_BRUSH, &lb, MAKELP(dc->brushOrg.y, dc->brushOrg.x));
        dc->xPBrush = x;
        dc->hPBrushOf = dc->hBrush;
        dc->lpPDevOfBrush = dc->lpPDevice;
    }
    return dc->xPBrush ? xptr(dc->xPBrush) : 0;
}

/* UnrealizeObject or DeleteObject of a brush: DCs realize it again */
void dc_brush_changed(HBRUSH h)
{
    HDC l;
    LPDC dc;
    for (l = dcs; l && (dc = dc_get(l)) != 0; l = dc->hLink)
        if (dc->hPBrushOf == h)
            dc->hPBrushOf = 0;
}

void dc_set_device_colors(LPDC dc)
{
    dc->dm.bkColor = dev_colorinfo(dc, dc->dm.LbkColor);
    dc->dm.TextColor = dev_colorinfo(dc, dc->dm.LTextColor);
}

/* ---- creation ---- */

static void dc_defaults(LPDC dc)
{
    dc->hPen = stock[7];
    dc->hBrush = stock[0];
    dc->hFont = stock[13];
    dc->hPal = stock[15];
    dc->dm.Rop2 = 13;                   /* R2_COPYPEN */
    dc->dm.bkMode = 2;                  /* OPAQUE */
    dc->dm.LbkColor = RGB(255, 255, 255);
    dc->dm.LTextColor = 0;
    dc->wndExt.x = dc->wndExt.y = 1;
    dc->vpExt.x = dc->vpExt.y = 1;
    dc->mapMode = 1;                    /* MM_TEXT */
    dc->relAbs = 1;                     /* ABSOLUTE */
    dc->polyFill = 1;                   /* ALTERNATE */
    dc->stretchMode = 1;                /* BLACKONWHITE */
}

static HDC dc_new(NPDEVICE dev, LPVOID lpPDevice, WORD flags)
{
    HRGN vis = rgn_new(0, 0, dev->info.dpHorzRes, dev->info.dpVertRes);
    HRGN rao = vis ? rgn_new(0, 0, 0, 0) : 0;
    HDC h = rao ? obj_alloc(DC_MAGIC, sizeof(DC)) : 0;
    LPDC dc;
    if (!h) {
        if (vis)
            rgn_free(vis);
        if (rao)
            rgn_free(rao);
        return 0;
    }
    dc = dc_get(h);
    dc->flags = flags;
    dc->dev = dev;
    dc->lpPDevice = lpPDevice;
    dc->owner = GetCurrentTask();
    dc->planes = (BYTE)dev->info.dpPlanes;
    dc->bitsPixel = (BYTE)dev->info.dpBitsPixel;
    dc_defaults(dc);
    dc->hVisRgn = vis;
    dc->hRao = rao;
    dc_set_device_colors(dc);
    dc_pbrush(dc);
    dc = dc_get(h);
    dc_ppen(dc);
    dc = dc_get(h);
    font_realize(dc);
    dc_link(h);
    return h;
}

HDC GDIAPI CreateDC(LPCSTR driver, LPCSTR device, LPCSTR output, const void __far *init) /* GDI.53 */
{
    NPDEVICE d;
    (void)device;
    (void)output;
    (void)init;
    if (!driver)
        return 0;
    d = dev_open(driver);
    if (!d)
        return 0;
    return dc_new(d, d->lpPDevice, DCF_DISPLAY);
}

HDC GDIAPI CreateIC(LPCSTR driver, LPCSTR device, LPCSTR output, const void __far *init) /* GDI.153 */
{
    HDC h = CreateDC(driver, device, output, init);
    LPDC dc = dc_get(h);
    if (dc)
        dc->flags |= DCF_IC;
    return h;
}

HDC GDIAPI CreateCompatibleDC(HDC hdc)                             /* GDI.52 */
{
    LPDC src = hdc ? dc_get(hdc) : (LPDC)0;
    NPDEVICE d = src ? src->dev : display;
    HDC h;
    LPDC dc;
    if (!d)
        d = dev_open("DISPLAY");
    if (!d)
        return 0;
    d->refs++;
    h = dc_new(d, bm_pbitmap(stock_bitmap), DCF_MEMORY);
    dc = dc_get(h);
    if (dc)
        dc->hBitmap = stock_bitmap;
    else
        dev_close(d);
    return h;
}

static void saved_free(XP x);

BOOL GDIAPI DeleteDC(HDC hdc)                                      /* GDI.68 */
{
    LPDC dc = dc_get(hdc);
    LPBITMAPOBJ bm;
    HRGN vis;
    if (!dc)
        return FALSE;
    if (dc->hook)
        ((WORD (__far __pascal *)(HDC, WORD, DWORD, DWORD))dc->hook)(hdc, DCHC_DELETEDC, dc->hookData, 0);
    dc = dc_get(hdc);
    while (dc->xSaved) {                /* the SaveDC stack */
        XP x = dc->xSaved;
        dc->xSaved = ((SAVEDDC __far *)xptr(x))->dc.xSaved;
        saved_free(x);
    }
    vis = dc->hVisRgn;
    while (vis && ((LPOBJHDR)obj_get(vis, RGN_MAGIC))->hNext) {    /* SaveVisRgn's */
        LPOBJHDR v = obj_get(vis, RGN_MAGIC);
        HRGN top = v->hNext;
        v->hNext = ((LPOBJHDR)obj_get(top, RGN_MAGIC))->hNext;
        rgn_free(top);
    }
    phys_free(dc, &dc->xPPen, OBJ_PEN);
    phys_free(dc, &dc->xPBrush, OBJ_BRUSH);
    bm = obj_get(dc->hBitmap, BITMAP_MAGIC);
    if (bm && bm->hdcSel == hdc) {
        bm->hdcSel = 0;
        bm->bSelected = 0;
    }
    if (dc->hClipRgn)
        rgn_free(dc->hClipRgn);
    if (dc->hVisRgn)
        rgn_free(dc->hVisRgn);
    if (dc->hRao)
        rgn_free(dc->hRao);
    dev_close(dc->dev);
    dc_unlink(hdc);
    obj_free(hdc);
    return TRUE;
}

/* ---- the composite clip region (vis & clip): "Rao" (Schulman) ---- */

void dc_rao_invalidate(LPDC dc)
{
    dc->raoValid = 0;
}

/* call USER's DC hook when the visible region was invalidated */
static LPDC dc_validate(HDC hdc)
{
    LPDC dc = dc_get(hdc);
    if (dc && dc->hook && (dc->hookFlags & DCHF_INVALIDATEVISRGN)) {
        dc->hookFlags &= ~DCHF_INVALIDATEVISRGN;
        ((WORD (__far __pascal *)(HDC, WORD, DWORD, DWORD))dc->hook)(hdc, DCHC_INVALIDVISRGN,
                                                                      dc->hookData, 0);
        dc = dc_get(hdc);
    }
    return dc;
}

LPRGN dc_rao(LPDC dc)
{
    if (!dc->raoValid) {
        HRGN vis = dc->hVisRgn;
        if (dc->hClipRgn) {
            rgn_copy(dc->hRao, dc->hClipRgn);
            rgn_offset(dc->hRao, dc->dcOrg.x, dc->dcOrg.y);
            rgn_combine(dc->hRao, dc->hRao, vis, RGN_AND);
        } else
            rgn_copy(dc->hRao, vis);
        /* never past the device's surface (the screen, or the selected
           bitmap): drivers do not clip to it themselves, and output past
           x = 639 would wrap into the next scan line */
        {
            int w, h;
            if (dc->flags & DCF_MEMORY) {
                LPPBITMAP pb = dc->lpPDevice;
                w = pb->bmWidth;
                h = pb->bmHeight;
            } else {
                w = dc->dev->info.dpHorzRes;
                h = dc->dev->info.dpVertRes;
            }
            rgn_op_rect(dc->hRao, 0, 0, w, h, RGN_AND);
        }
        dc->raoValid = 1;
    }
    return rgn_ptr(dc->hRao);
}

/* the DC ready for output (hook called, composite region current) */
LPDC dc_out(HDC hdc)
{
    LPDC dc = dc_validate(hdc);
    if (dc)
        dc_rao(dc);
    return dc;
}

/* accumulate output bounds (screen coordinates; GetBoundsRect) */
void dc_update_bounds(LPDC dc, int l, int t, int r, int b)
{
    LPRGN rao = rgn_ptr(dc->hRao);
    if (rao) {
        if (l < rao->box.left) l = rao->box.left;
        if (t < rao->box.top) t = rao->box.top;
        if (r > rao->box.right) r = rao->box.right;
        if (b > rao->box.bottom) b = rao->box.bottom;
    }
    if (l >= r || t >= b)
        return;
    if (dc->bounds.left >= dc->bounds.right || dc->bounds.top >= dc->bounds.bottom) {
        dc->bounds.left = l;
        dc->bounds.top = t;
        dc->bounds.right = r;
        dc->bounds.bottom = b;
    } else {
        if (l < dc->bounds.left) dc->bounds.left = l;
        if (t < dc->bounds.top) dc->bounds.top = t;
        if (r > dc->bounds.right) dc->bounds.right = r;
        if (b > dc->bounds.bottom) dc->bounds.bottom = b;
    }
}

/* ---- coordinates ---- */

static int xform_x(LPDC dc, int x)
{
    if (dc->mapMode == 1)
        return x - dc->wndOrg.x + dc->vpOrg.x;
    return mul_div(x - dc->wndOrg.x, dc->vpExt.x, dc->wndExt.x) + dc->vpOrg.x;
}

static int xform_y(LPDC dc, int y)
{
    if (dc->mapMode == 1)
        return y - dc->wndOrg.y + dc->vpOrg.y;
    return mul_div(y - dc->wndOrg.y, dc->vpExt.y, dc->wndExt.y) + dc->vpOrg.y;
}

void dc_lp_to_dp(LPDC dc, int __far *p, int n)
{
    while (n-- > 0) {
        p[0] = xform_x(dc, p[0]);
        p[1] = xform_y(dc, p[1]);
        p += 2;
    }
}

void dc_dp_to_lp(LPDC dc, int __far *p, int n)
{
    while (n-- > 0) {
        if (dc->mapMode == 1) {
            p[0] = p[0] - dc->vpOrg.x + dc->wndOrg.x;
            p[1] = p[1] - dc->vpOrg.y + dc->wndOrg.y;
        } else {
            p[0] = mul_div(p[0] - dc->vpOrg.x, dc->wndExt.x, dc->vpExt.x) + dc->wndOrg.x;
            p[1] = mul_div(p[1] - dc->vpOrg.y, dc->wndExt.y, dc->vpExt.y) + dc->wndOrg.y;
        }
        p += 2;
    }
}

/* a logical width/height to device units (no origin) */
int dc_scale_x(LPDC dc, int x)
{
    return dc->mapMode == 1 ? x : mul_div(x, dc->vpExt.x, dc->wndExt.x);
}

int dc_scale_y(LPDC dc, int y)
{
    return dc->mapMode == 1 ? y : mul_div(y, dc->vpExt.y, dc->wndExt.y);
}

BOOL GDIAPI LPtoDP(HDC hdc, POINT __far *p, int n)                 /* GDI.99 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return FALSE;
    dc_lp_to_dp(dc, (int __far *)p, n);
    return TRUE;
}

BOOL GDIAPI DPtoLP(HDC hdc, POINT __far *p, int n)                 /* GDI.67 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return FALSE;
    dc_dp_to_lp(dc, (int __far *)p, n);
    return TRUE;
}

/* ---- simple attributes ---- */

#define ATTR(name, ord, field, type)                                     \
    type GDIAPI Get##name(HDC hdc)                                       \
    {                                                                    \
        LPDC dc = dc_get(hdc);                                           \
        return dc ? (type)dc->field : 0;                                 \
    }

ATTR(BkMode, 76, dm.bkMode, int)
ATTR(ROP2, 85, dm.Rop2, int)
ATTR(RelAbs, 86, relAbs, int)
ATTR(PolyFillMode, 84, polyFill, int)
ATTR(StretchBltMode, 88, stretchMode, int)
ATTR(MapMode, 81, mapMode, int)
ATTR(TextAlign, 345, textAlign, WORD)
ATTR(TextCharacterExtra, 89, charExtra, int)
ATTR(BkColor, 75, dm.LbkColor, COLORREF)
ATTR(TextColor, 90, dm.LTextColor, COLORREF)

int GDIAPI SetBkMode(HDC hdc, int m)                               /* GDI.2 */
{
    LPDC dc = dc_get(hdc);
    int old;
    if (!dc || m < 1 || m > 2)
        return 0;
    old = dc->dm.bkMode;
    dc->dm.bkMode = m;
    return old;
}

int GDIAPI SetROP2(HDC hdc, int r)                                 /* GDI.4 */
{
    LPDC dc = dc_get(hdc);
    int old;
    if (!dc || r < 1 || r > 16)
        return 0;
    old = dc->dm.Rop2;
    dc->dm.Rop2 = r;
    return old;
}

int GDIAPI SetRelAbs(HDC hdc, int m)                               /* GDI.5 */
{
    LPDC dc = dc_get(hdc);
    int old;
    if (!dc)
        return 0;
    old = dc->relAbs;
    dc->relAbs = m;
    return old;
}

int GDIAPI SetPolyFillMode(HDC hdc, int m)                         /* GDI.6 */
{
    LPDC dc = dc_get(hdc);
    int old;
    if (!dc || m < 1 || m > 2)
        return 0;
    old = dc->polyFill;
    dc->polyFill = m;
    return old;
}

int GDIAPI SetStretchBltMode(HDC hdc, int m)                       /* GDI.7 */
{
    LPDC dc = dc_get(hdc);
    int old;
    if (!dc || m < 1 || m > 3)
        return 0;
    old = dc->stretchMode;
    dc->stretchMode = m;
    return old;
}

int GDIAPI SetTextCharacterExtra(HDC hdc, int x)                   /* GDI.8 */
{
    LPDC dc = dc_get(hdc);
    int old;
    if (!dc)
        return 0x8000;
    old = dc->charExtra;
    dc->charExtra = x;
    dc->dm.CharExtra = dc_scale_x(dc, x);
    return old;
}

WORD GDIAPI SetTextAlign(HDC hdc, WORD a)                          /* GDI.346 */
{
    LPDC dc = dc_get(hdc);
    WORD old;
    if (!dc)
        return 0;
    old = dc->textAlign;
    dc->textAlign = a;
    return old;
}

int GDIAPI SetTextJustification(HDC hdc, int extra, int count)     /* GDI.10 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return 0;
    extra = dc_scale_x(dc, extra);
    dc->dm.TBreakExtra = extra;
    dc->dm.BreakCount = count;
    if (count > 0) {
        dc->dm.BreakExtra = extra / count;
        dc->dm.BreakRem = extra % count;
    } else {
        dc->dm.BreakExtra = 0;
        dc->dm.BreakRem = 0;
    }
    dc->dm.BreakErr = count / 2 + 1;    /* UNVERIFIED: initial error term */
    return 1;
}

COLORREF GDIAPI SetBkColor(HDC hdc, COLORREF c)                    /* GDI.1 */
{
    LPDC dc = dc_get(hdc);
    COLORREF old;
    if (!dc)
        return 0x80000000L;
    old = dc->dm.LbkColor;
    dc->dm.LbkColor = c;
    dc->dm.bkColor = dev_colorinfo(dc, c);
    return old;
}

COLORREF GDIAPI SetTextColor(HDC hdc, COLORREF c)                  /* GDI.9 */
{
    LPDC dc = dc_get(hdc);
    COLORREF old;
    if (!dc)
        return 0x80000000L;
    old = dc->dm.LTextColor;
    dc->dm.LTextColor = c;
    dc->dm.TextColor = dev_colorinfo(dc, c);
    return old;
}

COLORREF GDIAPI GetNearestColor(HDC hdc, COLORREF c)               /* GDI.154 */
{
    LPDC dc = dc_get(hdc);
    DWORD pc;
    if (!dc)
        return 0x80000000L;
    return ((DRVCOLORINFO)dc->dev->fn[DRV_COLORINFO])(dc->lpPDevice, c, &pc);
}

int GDIAPI GetDeviceCaps(HDC hdc, int i)                           /* GDI.80 */
{
    LPDC dc = dc_get(hdc);
    if (!dc || i < 0 || i > 0x6C || (i & 1))
        return 0;
    return *(short NEAR *)((BYTE NEAR *)&dc->dev->info + i);
}

/* ---- brush origin, DC origin ---- */

DWORD GDIAPI SetBrushOrg(HDC hdc, int x, int y)                    /* GDI.148 */
{
    LPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = MAKELONG(dc->brushOrg.x, dc->brushOrg.y);
    dc->brushOrg.x = x;
    dc->brushOrg.y = y;
    return old;
}

DWORD GDIAPI GetBrushOrg(HDC hdc)                                  /* GDI.149 */
{
    LPDC dc = dc_get(hdc);
    return dc ? MAKELONG(dc->brushOrg.x, dc->brushOrg.y) : 0;
}

/* SetDCOrg (USER): the screen position of device point (0, 0). GD2: 3.1
   returned 0 every time */
DWORD GDIAPI SetDCOrg(HDC hdc, int x, int y)                       /* GDI.117 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return 0;
    dc->dcOrg.x = x;
    dc->dcOrg.y = y;
    dc_rao_invalidate(dc);
    return 0;
}

DWORD GDIAPI GetDCOrg(HDC hdc)                                     /* GDI.79 */
{
    LPDC dc = dc_get(hdc);
    return dc ? MAKELONG(dc->dcOrg.x, dc->dcOrg.y) : 0;
}

/* ---- mapping ---- */

static void map_extents(LPDC dc)
{
    GDIINFO NEAR *gi = &dc->dev->info;
    switch (dc->mapMode) {
    case 2:                             /* MM_LOMETRIC */
        dc->wndExt = gi->dpMLoWin; dc->vpExt = gi->dpMLoVpt; break;
    case 3:                             /* MM_HIMETRIC */
        dc->wndExt = gi->dpMHiWin; dc->vpExt = gi->dpMHiVpt; break;
    case 4:                             /* MM_LOENGLISH */
        dc->wndExt = gi->dpELoWin; dc->vpExt = gi->dpELoVpt; break;
    case 5:                             /* MM_HIENGLISH */
        dc->wndExt = gi->dpEHiWin; dc->vpExt = gi->dpEHiVpt; break;
    case 6:                             /* MM_TWIPS */
        dc->wndExt = gi->dpTwpWin; dc->vpExt = gi->dpTwpVpt; break;
    case 7:                             /* MM_ISOTROPIC: starts as LOMETRIC */
        dc->wndExt = gi->dpMLoWin; dc->vpExt = gi->dpMLoVpt; break;
    case 8:                             /* MM_ANISOTROPIC: keeps the extents */
        break;
    default:
        dc->wndExt.x = dc->wndExt.y = dc->vpExt.x = dc->vpExt.y = 1;
    }
}

int GDIAPI SetMapMode(HDC hdc, int m)                              /* GDI.3 */
{
    LPDC dc = dc_get(hdc);
    int old;
    if (!dc || m < 1 || m > 8)
        return 0;
    old = dc->mapMode;
    dc->mapMode = m;
    map_extents(dc);
    return old;
}

static void iso_fix(LPDC dc)
{
    /* MM_ISOTROPIC: shrink the viewport extent so both axes scale alike
       UNVERIFIED: 3.1's exact rounding */
    long sx, sy;
    if (dc->mapMode != 7 || !dc->wndExt.x || !dc->wndExt.y)
        return;
    sx = (long)dc->vpExt.x * 1000 / dc->wndExt.x;
    sy = (long)dc->vpExt.y * 1000 / dc->wndExt.y;
    if ((sx < 0 ? -sx : sx) < (sy < 0 ? -sy : sy))
        dc->vpExt.y = (int)((long)dc->wndExt.y * (sx < 0 ? -sx : sx) * (sy < 0 ? -1 : 1) / 1000);
    else
        dc->vpExt.x = (int)((long)dc->wndExt.x * (sy < 0 ? -sy : sy) * (sx < 0 ? -1 : 1) / 1000);
}

#define ORGEXT(fn, ord, field, fix)                                      \
    DWORD GDIAPI fn(HDC hdc, int x, int y)                               \
    {                                                                    \
        LPDC dc = dc_get(hdc);                                           \
        DWORD old;                                                       \
        if (!dc)                                                         \
            return 0;                                                    \
        old = MAKELONG(dc->field.x, dc->field.y);                        \
        if (fix && dc->mapMode < 7)                                      \
            return old;                                                  \
        if (fix && (!x || !y))                                           \
            return 0;                                                    \
        dc->field.x = x;                                                 \
        dc->field.y = y;                                                 \
        if (fix)                                                         \
            iso_fix(dc);                                                 \
        return old;                                                      \
    }

ORGEXT(SetWindowOrg, 11, wndOrg, 0)
ORGEXT(SetWindowExt, 12, wndExt, 1)
ORGEXT(SetViewportOrg, 13, vpOrg, 0)
ORGEXT(SetViewportExt, 14, vpExt, 1)

#define GETPT(fn, ord, field)                                            \
    DWORD GDIAPI fn(HDC hdc)                                             \
    {                                                                    \
        LPDC dc = dc_get(hdc);                                           \
        return dc ? MAKELONG(dc->field.x, dc->field.y) : 0;              \
    }

GETPT(GetWindowOrg, 97, wndOrg)
GETPT(GetWindowExt, 96, wndExt)
GETPT(GetViewportOrg, 95, vpOrg)
GETPT(GetViewportExt, 94, vpExt)
GETPT(GetCurrentPosition, 78, cur)

DWORD GDIAPI OffsetWindowOrg(HDC hdc, int dx, int dy)              /* GDI.15 */
{
    LPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = MAKELONG(dc->wndOrg.x, dc->wndOrg.y);
    dc->wndOrg.x += dx;
    dc->wndOrg.y += dy;
    return old;
}

DWORD GDIAPI OffsetViewportOrg(HDC hdc, int dx, int dy)            /* GDI.17 */
{
    LPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = MAKELONG(dc->vpOrg.x, dc->vpOrg.y);
    dc->vpOrg.x += dx;
    dc->vpOrg.y += dy;
    return old;
}

DWORD GDIAPI ScaleWindowExt(HDC hdc, int xn, int xd, int yn, int yd) /* GDI.16 */
{
    LPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = MAKELONG(dc->wndExt.x, dc->wndExt.y);
    if (dc->mapMode < 7 || !xd || !yd)
        return old;
    dc->wndExt.x = (int)((long)dc->wndExt.x * xn / xd);
    dc->wndExt.y = (int)((long)dc->wndExt.y * yn / yd);
    iso_fix(dc);
    return old;
}

DWORD GDIAPI ScaleViewportExt(HDC hdc, int xn, int xd, int yn, int yd) /* GDI.18 */
{
    LPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = MAKELONG(dc->vpExt.x, dc->vpExt.y);
    if (dc->mapMode < 7 || !xd || !yd)
        return old;
    dc->vpExt.x = (int)((long)dc->vpExt.x * xn / xd);
    dc->vpExt.y = (int)((long)dc->vpExt.y * yn / yd);
    iso_fix(dc);
    return old;
}

DWORD GDIAPI MoveTo(HDC hdc, int x, int y)                         /* GDI.20 */
{
    LPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = MAKELONG(dc->cur.x, dc->cur.y);
    dc->cur.x = x;
    dc->cur.y = y;
    return old;
}

/* ---- selecting objects ---- */

HGDIOBJ GDIAPI SelectObject(HDC hdc, HGDIOBJ h)                    /* GDI.45 */
{
    LPDC dc = dc_get(hdc);
    WORD m = obj_magic(h);
    HANDLE old;
    if (!dc || !m)
        return 0;
    switch (m & MAGIC_MASK) {
    case PEN_MAGIC:
        old = dc->hPen;
        dc->hPen = h;
        dc_ppen(dc);
        return old;
    case BRUSH_MAGIC:
        old = dc->hBrush;
        dc->hBrush = h;
        dc_pbrush(dc);
        return old;
    case FONT_MAGIC:
        old = dc->hFont;
        dc->hFont = h;
        font_realize(dc);
        return old;
    case RGN_MAGIC: {
        extern int GDIAPI SelectClipRgn(HDC, HRGN);
        return SelectClipRgn(hdc, h);
    }
    case BITMAP_MAGIC: {
        LPBITMAPOBJ bm = obj_get(h, BITMAP_MAGIC), ob;
        LPPBITMAP pb;
        if (!(dc->flags & DCF_MEMORY))
            return 0;
        if (bm->hdcSel && bm->hdcSel != hdc && h != stock_bitmap)
            return 0;                   /* in another DC */
        pb = bm_pbitmap(h);
        if (!pb)
            return 0;
        old = dc->hBitmap;
        ob = obj_get(old, BITMAP_MAGIC);
        if (ob && ob->hdcSel == hdc) {
            ob->hdcSel = 0;
            ob->bSelected = 0;
        }
        if (h != stock_bitmap) {
            bm->hdcSel = hdc;
            bm->bSelected = 1;
        }
        dc->hBitmap = h;
        dc->lpPDevice = pb;
        rgn_set_rect(dc->hVisRgn, 0, 0, pb->bmWidth, pb->bmHeight);
        dc_rao_invalidate(dc);
        dc_set_device_colors(dc);
        return old;
    }
    }
    return 0;
}

HFONT GDIAPI GetCurLogFont(HDC hdc)                                /* GDI.411 */
{
    LPDC dc = dc_get(hdc);
    return dc ? dc->hFont : 0;
}

/* ---- clipping ---- */

int GDIAPI SelectClipRgn(HDC hdc, HRGN h)                          /* GDI.44 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return ERROR;
    if (!h) {
        if (dc->hClipRgn) {
            rgn_free(dc->hClipRgn);
            dc->hClipRgn = 0;
        }
    } else {
        if (!rgn_ptr(h))
            return ERROR;
        if (!dc->hClipRgn)
            dc->hClipRgn = rgn_new(0, 0, 0, 0);
        rgn_copy(dc->hClipRgn, h);
    }
    dc_rao_invalidate(dc);
    return rgn_complexity(dc_rao(dc));
}

static int clip_rect_op(LPDC dc, int l, int t, int r, int b, int mode)
{
    int p[4];
    p[0] = l; p[1] = t; p[2] = r; p[3] = b;
    dc_lp_to_dp(dc, p, 2);
    if (!dc->hClipRgn) {
        LPRGN vis;
        /* no clip region yet: start from the whole device surface */
        HRGN c = rgn_new(0, 0, dc->dev->info.dpHorzRes, dc->dev->info.dpVertRes);
        if (!c)
            return ERROR;
        dc->hClipRgn = c;
        vis = rgn_ptr(dc->hVisRgn);
        if (dc->flags & DCF_MEMORY && vis)
            rgn_set_rect(c, vis->box.left, vis->box.top, vis->box.right, vis->box.bottom);
    }
    rgn_op_rect(dc->hClipRgn, p[0], p[1], p[2], p[3], mode);
    dc_rao_invalidate(dc);
    return rgn_complexity(dc_rao(dc));
}

int GDIAPI IntersectClipRect(HDC hdc, int l, int t, int r, int b)  /* GDI.22 */
{
    LPDC dc = dc_get(hdc);
    return dc ? clip_rect_op(dc, l, t, r, b, RGN_AND) : ERROR;
}

int GDIAPI ExcludeClipRect(HDC hdc, int l, int t, int r, int b)    /* GDI.21 */
{
    LPDC dc = dc_get(hdc);
    return dc ? clip_rect_op(dc, l, t, r, b, RGN_DIFF) : ERROR;
}

int GDIAPI OffsetClipRgn(HDC hdc, int dx, int dy)                  /* GDI.32 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return ERROR;
    if (dc->hClipRgn) {
        rgn_offset(dc->hClipRgn, dc_scale_x(dc, dx), dc_scale_y(dc, dy));
        dc_rao_invalidate(dc);
    }
    return rgn_complexity(dc_rao(dc));
}

/* the box of the composite clip region, in logical coordinates */
int GDIAPI GetClipBox(HDC hdc, RECT __far *rc)                     /* GDI.77 */
{
    LPDC dc = dc_out(hdc);
    LPRGN r;
    int p[4];
    if (!dc)
        return ERROR;
    r = dc_rao(dc);
    p[0] = r->box.left - dc->dcOrg.x;
    p[1] = r->box.top - dc->dcOrg.y;
    p[2] = r->box.right - dc->dcOrg.x;
    p[3] = r->box.bottom - dc->dcOrg.y;
    if (!r->n)
        p[0] = p[1] = p[2] = p[3] = 0;
    dc_dp_to_lp(dc, p, 2);
    rc->left = min(p[0], p[2]);
    rc->right = max(p[0], p[2]);
    rc->top = min(p[1], p[3]);
    rc->bottom = max(p[1], p[3]);
    return rgn_complexity(r);
}

HRGN GDIAPI GetClipRgn(HDC hdc)                                    /* GDI.173 */
{
    LPDC dc = dc_get(hdc);
    return dc ? dc->hClipRgn : 0;
}

BOOL GDIAPI PtVisible(HDC hdc, int x, int y)                       /* GDI.103 */
{
    LPDC dc = dc_out(hdc);
    int p[2];
    if (!dc)
        return FALSE;
    p[0] = x;
    p[1] = y;
    dc_lp_to_dp(dc, p, 1);
    return PtInRegion(dc->hRao, p[0] + dc->dcOrg.x, p[1] + dc->dcOrg.y);
}

BOOL GDIAPI RectVisible(HDC hdc, const RECT __far *rc)             /* GDI.465 */
{
    LPDC dc = dc_out(hdc);
    int p[4];
    RECT r;
    if (!dc)
        return FALSE;
    p[0] = rc->left; p[1] = rc->top; p[2] = rc->right; p[3] = rc->bottom;
    dc_lp_to_dp(dc, p, 2);
    r.left = min(p[0], p[2]) + dc->dcOrg.x;
    r.right = max(p[0], p[2]) + dc->dcOrg.x;
    r.top = min(p[1], p[3]) + dc->dcOrg.y;
    r.bottom = max(p[1], p[3]) + dc->dcOrg.y;
    return RectInRegion(dc->hRao, &r);
}

BOOL GDIAPI RectVisibleOld(HDC hdc, const RECT __far *rc)          /* GDI.104 */
{
    return RectVisible(hdc, rc);
}

/* ---- the visible region (USER) ---- */

int GDIAPI SelectVisRgn(HDC hdc, HRGN h)                           /* GDI.105 */
{
    LPDC dc = dc_get(hdc);
    int r;
    if (!dc || !rgn_ptr(h))
        return ERROR;
    r = rgn_copy(dc->hVisRgn, h);
    dc_rao_invalidate(dc);
    return r;
}

HRGN GDIAPI InquireVisRgn(HDC hdc)                                 /* GDI.131 */
{
    LPDC dc = dc_get(hdc);
    return dc ? dc->hVisRgn : 0;
}

/* UNVERIFIED: the rectangle is in device coordinates of the DC (made
   screen coordinates with the DC origin), as IntersectClipRect's is in
   logical ones */
static int vis_rect_op(HDC hdc, int l, int t, int r, int b, int mode)
{
    LPDC dc = dc_get(hdc);
    int res;
    if (!dc)
        return ERROR;
    res = rgn_op_rect(dc->hVisRgn, l + dc->dcOrg.x, t + dc->dcOrg.y, r + dc->dcOrg.x,
                      b + dc->dcOrg.y, mode);
    dc = dc_get(hdc);
    dc_rao_invalidate(dc);
    return res;
}

int GDIAPI IntersectVisRect(HDC hdc, int l, int t, int r, int b)   /* GDI.98 */
{
    return vis_rect_op(hdc, l, t, r, b, RGN_AND);
}

int GDIAPI ExcludeVisRect(HDC hdc, int l, int t, int r, int b)     /* GDI.73 */
{
    return vis_rect_op(hdc, l, t, r, b, RGN_DIFF);
}

int GDIAPI OffsetVisRgn(HDC hdc, int dx, int dy)                   /* GDI.102 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return ERROR;
    rgn_offset(dc->hVisRgn, dx, dy);
    dc_rao_invalidate(dc);
    return rgn_complexity(rgn_ptr(dc->hVisRgn));
}

/* SaveVisRgn/RestoreVisRgn: a stack of copies kept in the DC (we use the
   region object's hNext field, as the header has one) */
HRGN GDIAPI SaveVisRgn(HDC hdc)                                    /* GDI.129 */
{
    LPDC dc = dc_get(hdc);
    HRGN copy;
    if (!dc)
        return 0;
    copy = rgn_new(0, 0, 0, 0);
    if (!copy)
        return 0;
    dc = dc_get(hdc);
    rgn_copy(copy, dc->hVisRgn);
    ((LPOBJHDR)obj_get(copy, 0))->hNext = ((LPOBJHDR)obj_get(dc->hVisRgn, 0))->hNext;
    ((LPOBJHDR)obj_get(dc->hVisRgn, 0))->hNext = copy;
    return copy;
}

int GDIAPI RestoreVisRgn(HDC hdc)                                  /* GDI.130 */
{
    LPDC dc = dc_get(hdc);
    HRGN top;
    int r;
    if (!dc)
        return ERROR;
    top = ((LPOBJHDR)obj_get(dc->hVisRgn, 0))->hNext;
    if (!top)
        return ERROR;
    ((LPOBJHDR)obj_get(dc->hVisRgn, 0))->hNext = ((LPOBJHDR)obj_get(top, 0))->hNext;
    r = rgn_copy(dc->hVisRgn, top);
    rgn_free(top);
    dc_rao_invalidate(dc_get(hdc));
    return r;
}

/* ---- the DC hook (USER; Schulman ch. 8) ---- */

BOOL GDIAPI SetDCHook(HDC hdc, FARPROC proc, DWORD data)           /* GDI.190 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return FALSE;
    dc->hook = proc;
    dc->hookData = data;
    return TRUE;
}

DWORD GDIAPI GetDCHook(HDC hdc, FARPROC __far *proc)               /* GDI.191 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return 0;
    *proc = dc->hook;
    return dc->hookData;
}

WORD GDIAPI SetHookFlags(HDC hdc, WORD flags)                      /* GDI.192 */
{
    LPDC dc = dc_get(hdc);
    WORD old;
    if (!dc)
        return 0;
    old = dc->hookFlags & DCHF_INVALIDATEVISRGN;
    if (flags & DCHF_INVALIDATEVISRGN)
        dc->hookFlags |= DCHF_INVALIDATEVISRGN;
    else if (flags & DCHF_VALIDATEVISRGN)
        dc->hookFlags &= ~DCHF_INVALIDATEVISRGN;
    return old;
}

/* ---- bounds (3.1 SDK: DCB_RESET 1, DCB_ACCUMULATE 2, DCB_SET 3,
   DCB_ENABLE 4, DCB_DISABLE 8). GD2: USER calls GetBoundsRect(hdc, rc,
   8001h) and got 3 with the rectangle in screen coordinates, or 1 when
   nothing was drawn; 3.1 accumulated for USER's DCs without SetBoundsRect.
   8000h is taken here as "screen coordinates" (UNVERIFIED). ---- */

WORD GDIAPI GetBoundsRect(HDC hdc, RECT __far *rc, WORD flags)     /* GDI.194 */
{
    LPDC dc = dc_get(hdc);
    WORD r;
    if (!dc)
        return 0;
    if (dc->bounds.left >= dc->bounds.right || dc->bounds.top >= dc->bounds.bottom)
        r = 1;
    else {
        r = 3;
        *rc = dc->bounds;
        if (!(flags & 0x8000)) {
            rc->left -= dc->dcOrg.x;
            rc->right -= dc->dcOrg.x;
            rc->top -= dc->dcOrg.y;
            rc->bottom -= dc->dcOrg.y;
        }
    }
    if (flags & 1)
        dc->bounds.left = dc->bounds.top = dc->bounds.right = dc->bounds.bottom = 0;
    return r;
}

WORD GDIAPI SetBoundsRect(HDC hdc, const RECT __far *rc, WORD flags) /* GDI.193 */
{
    LPDC dc = dc_get(hdc);
    WORD r;
    if (!dc)
        return 0;
    /* GD2: USER's SetBoundsRect(hdc, NULL, 8003h) returned 8005h (empty)
       or 8007h: its 8000h, DCB_ENABLE and DCB_RESET/DCB_SET */
    r = ((dc->bounds.left >= dc->bounds.right) ? 1 : 3) | 4 | (flags & 0x8000);
    if (flags & 1)
        dc->bounds.left = dc->bounds.top = dc->bounds.right = dc->bounds.bottom = 0;
    if ((flags & 2) && rc) {
        int ox = (flags & 0x8000) ? 0 : dc->dcOrg.x, oy = (flags & 0x8000) ? 0 : dc->dcOrg.y;
        dc_update_bounds(dc, rc->left + ox, rc->top + oy, rc->right + ox, rc->bottom + oy);
    }
    dc->boundsFlags = flags & 0x0C;
    return r;
}

/* ---- saving state ---- */

/* copy the state of src into dst: everything but the device, the clip,
   visible and composite regions, the DC origin, the hook, the realized
   objects and the save stack */
static void state_copy(LPDC dst, LPDC src, int bitmap)
{
    dst->hPen = src->hPen;
    dst->hBrush = src->hBrush;
    dst->hFont = src->hFont;
    dst->hPal = src->hPal;
    dst->dm.Rop2 = src->dm.Rop2;
    dst->dm.bkMode = src->dm.bkMode;
    dst->dm.LbkColor = src->dm.LbkColor;
    dst->dm.LTextColor = src->dm.LTextColor;
    dst->dm.TBreakExtra = src->dm.TBreakExtra;
    dst->dm.BreakExtra = src->dm.BreakExtra;
    dst->dm.BreakErr = src->dm.BreakErr;
    dst->dm.BreakRem = src->dm.BreakRem;
    dst->dm.BreakCount = src->dm.BreakCount;
    dst->dm.CharExtra = src->dm.CharExtra;
    dst->cur = src->cur;
    dst->wndOrg = src->wndOrg;
    dst->wndExt = src->wndExt;
    dst->vpOrg = src->vpOrg;
    dst->vpExt = src->vpExt;
    dst->mapMode = src->mapMode;
    dst->relAbs = src->relAbs;
    dst->polyFill = src->polyFill;
    dst->stretchMode = src->stretchMode;
    dst->textAlign = src->textAlign;
    dst->brushOrg = src->brushOrg;
    dst->charExtra = src->charExtra;
    dst->mapperFlags = src->mapperFlags;
    dst->raoValid = 0;
    if (bitmap)
        dst->hBitmap = src->hBitmap;
}

/* the DC's clip region becomes the rectangles x (taken; 0: no clip
   region) */
static void clip_take(HDC hdc, XP x)
{
    LPDC dc = dc_get(hdc);
    if (!x) {
        if (dc->hClipRgn) {
            rgn_free(dc->hClipRgn);
            dc->hClipRgn = 0;
        }
    } else {
        if (!dc->hClipRgn) {
            HRGN c = rgn_new(0, 0, 0, 0);
            dc = dc_get(hdc);
            dc->hClipRgn = c;
        }
        if (dc->hClipRgn)
            rgn_set_data(dc->hClipRgn, x);
        else
            xfree(x);
    }
    dc_rao_invalidate(dc_get(hdc));
}

/* GetDCState's copy: a DC object (Schulman: the same signature as a DC)
   with no device of its own, its clip region a region object */
static HANDLE state_new(HDC hdc)
{
    HANDLE h = obj_alloc(DC_MAGIC, sizeof(DC));
    LPDC s, src;
    HRGN clip = 0;
    if (!h)
        return 0;
    src = dc_get(hdc);
    if (src->hClipRgn) {
        clip = rgn_new(0, 0, 0, 0);
        if (!clip) {
            obj_free(h);
            return 0;
        }
        rgn_copy(clip, dc_get(hdc)->hClipRgn);
    }
    s = dc_get(h);
    src = dc_get(hdc);
    s->flags = DCF_STATE;
    s->dev = src->dev;
    s->lpPDevice = src->lpPDevice;
    state_copy(s, src, 1);
    s->hClipRgn = clip;
    return h;
}

/* after a state change: colours and realized objects for the new state */
static void state_refresh(HDC hdc)
{
    LPDC dc = dc_get(hdc);
    dc_set_device_colors(dc);
    dc_ppen(dc);
    dc = dc_get(hdc);
    dc_pbrush(dc);
    dc = dc_get(hdc);
    font_realize(dc);
}

HANDLE GDIAPI GetDCState(HDC hdc)                                  /* GDI.179 */
{
    return dc_get(hdc) ? state_new(hdc) : 0;
}

void GDIAPI SetDCState(HDC hdc, HANDLE hs)                         /* GDI.180 */
{
    LPDC dc = dc_get(hdc), s = dc_get(hs);
    XP clip = 0;
    if (!dc || !s)
        return;
    if (s->hClipRgn)
        clip = rgn_dup_data(s->hClipRgn);
    dc = dc_get(hdc);
    s = dc_get(hs);
    state_copy(dc, s, 0);
    clip_take(hdc, clip);
    state_refresh(hdc);
}

/* SaveDC's states are SAVEDDCs in GDI's heaps, the DC's xSaved the top
   (3.1 kept DC copies in DGROUP linked through hNext, Schulman ch. 8;
   GD4: D0h bytes each, 253 deep) */
static void saved_free(XP x)
{
    SAVEDDC __far *sv = xptr(x);
    if (sv->xClip)
        xfree(sv->xClip);
    xfree(x);
}

int GDIAPI SaveDC(HDC hdc)                                         /* GDI.30 */
{
    LPDC dc = dc_get(hdc);
    SAVEDDC __far *sv;
    XP x, clip = 0;
    if (!dc)
        return 0;
    if (dc->hClipRgn) {
        clip = rgn_dup_data(dc->hClipRgn);
        if (!clip)
            return 0;
    }
    x = xalloc(sizeof(SAVEDDC));
    if (!x) {
        xfree(clip);
        return 0;
    }
    dc = dc_get(hdc);
    sv = xptr(x);
    state_copy(&sv->dc, dc, 1);
    sv->xClip = clip;
    sv->dc.xSaved = dc->xSaved;
    dc->xSaved = x;
    return ++dc->saveLevel;
}

BOOL GDIAPI RestoreDC(HDC hdc, int level)                          /* GDI.39 */
{
    LPDC dc = dc_get(hdc);
    if (!dc)
        return FALSE;
    if (level < 0)
        level = dc->saveLevel + level + 1;
    if (level <= 0 || level > (int)dc->saveLevel)
        return FALSE;
    while ((int)dc->saveLevel >= level) {
        XP x = dc->xSaved;
        SAVEDDC __far *sv;
        if (!x)
            return FALSE;
        sv = xptr(x);
        dc->xSaved = sv->dc.xSaved;
        dc->saveLevel--;
        if ((int)dc->saveLevel == level - 1) {
            XP clip;
            if (dc->flags & DCF_MEMORY && sv->dc.hBitmap != dc->hBitmap)
                SelectObject(hdc, sv->dc.hBitmap);
            dc = dc_get(hdc);
            sv = xptr(x);
            state_copy(dc, &sv->dc, 0);
            clip = sv->xClip;
            sv->xClip = 0;
            clip_take(hdc, clip);
        }
        saved_free(x);
        dc = dc_get(hdc);
    }
    state_refresh(hdc);
    return TRUE;
}

/* ---- tasks: Death/Resurrection (screen switches) and task
   termination (USER) ---- */

void GDIAPI Death(HDC hdc)                                         /* GDI.121 */
{
    LPDC dc = dc_get(hdc);
    if (dc && dc->dev->fn[DRV_DISABLE])
        ((DRVDISABLE)dc->dev->fn[DRV_DISABLE])(dc->dev->lpPDevice);
}

void GDIAPI Resurrection(HDC hdc, WORD a, WORD b, WORD c, WORD d, WORD e, WORD f) /* GDI.122 */
{
    LPDC dc = dc_get(hdc);
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    if (dc)
        ((DRVENABLE)dc->dev->fn[DRV_ENABLE])(dc->dev->lpPDevice, 0, 0, 0, 0);
}

void GDIAPI GdiTaskTermination(HTASK t)                            /* GDI.460 */
{
    (void)t;                            /* UNVERIFIED: 3.1 frees the task's objects? */
}
