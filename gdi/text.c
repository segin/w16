/*
 * text.c - TextOut, ExtTextOut, extents and metrics.
 *
 * Observed on 3.1 (GD1/GD2): text goes to the driver's ExtTextOut at
 * screen coordinates with the composite clip box as lpClipRect, the
 * realized font (FONTINFO), the DC's DRAWMODE and TEXTXFORM; TextOut
 * passes no widths, no opaque rectangle and options 0; ExtTextOut passes
 * its options and its rectangle (made screen coordinates, as a copy) as
 * the opaque rectangle. GetTextExtent asks the driver: ExtTextOut with
 * the count negated, at 0,0, no clip rectangle; the driver returns the
 * height in DX and the width in AX; a zero count returns 0 without a
 * driver call. GetTextMetrics is computed from the font: tmDefaultChar
 * and tmBreakChar are the .FNT values plus dfFirstChar (System font: 80h
 * and 20h), tmDigitizedAspect = dfHorizRes/dfVertRes (GD2).
 */
#include "gdi.h"

#define TA_UPDATECP 1
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_BOTTOM 8
#define TA_BASELINE 24
#define ETO_OPAQUE 2
#define ETO_CLIPPED 4

/* device widths and heights in logical units (GD5 on 3.1: positive) */
static int log_x(LPDC dc, int v)
{
    if (dc->mapMode != 1)
        v = mul_div(v, dc->wndExt.x, dc->vpExt.x);
    return v < 0 ? -v : v;
}

static int log_y(LPDC dc, int v)
{
    if (dc->mapMode != 1)
        v = mul_div(v, dc->wndExt.y, dc->vpExt.y);
    return v < 0 ? -v : v;
}

/* the driver's extent of the text; the justification error term
   (DRAWMODE BreakErr) as it was before (GD8, GD1's log on 3.1: only
   output moves it on; GetTextExtent and the extents TextOut takes
   leave it) */
static DWORD drv_extent(LPDC dc, LPCSTR s, int n)
{
    LPFONT f = dc_font(dc);
    TEXTXFORM tx;
    short err = dc->dm.BreakErr;
    DWORD r;
    if (!f)
        return 0;
    font_xform(dc, f, &tx);
    r = ((DRVEXTTEXTOUT)dc->dev->fn[DRV_EXTTEXTOUT])(dc->lpPDevice, 0, 0, 0, s, -n,
                                                     f->fi, &dc->dm, &tx, 0, 0, 0);
    dc->dm.BreakErr = err;
    return r;
}

/* the device width ExtTextOut's text takes */
static int text_width(LPDC dc, LPCSTR s, int n, const short __far *dx)
{
    int w = 0, i;
    if (n <= 0)
        return 0;
    if (!dx)
        return LOWORD(drv_extent(dc, s, n));
    for (i = 0; i < n - 1; i++)
        w += dc_scale_x(dc, dx[i]);
    return w + LOWORD(drv_extent(dc, s + n - 1, 1));
}

DWORD GDIAPI GetTextExtent(HDC hdc, LPCSTR s, int n)               /* GDI.91 */
{
    LPDC dc = dc_get(hdc);
    DWORD e;
    int w, h;
    if (!dc || n <= 0 || !s)
        return 0;
    font_realize(dc);
    e = drv_extent(dc, s, n);
    w = LOWORD(e);
    h = HIWORD(e);
    if (dc->mapMode != 1) {
        w = mul_div(w, dc->wndExt.x, dc->vpExt.x);
        h = mul_div(h, dc->wndExt.y, dc->vpExt.y);
        if (w < 0) w = -w;
        if (h < 0) h = -h;
    }
    return MAKELONG(w, h);
}

BOOL GDIAPI GetTextExtentPoint(HDC hdc, LPCSTR s, int n, POINT __far *sz) /* GDI.471 */
{
    DWORD e;
    if (!sz)
        return FALSE;
    e = GetTextExtent(hdc, s, n);
    sz->x = LOWORD(e);
    sz->y = HIWORD(e);
    return TRUE;
}

BOOL GDIAPI ExtTextOut(HDC hdc, int x, int y, WORD opt, const RECT __far *rc,
                       LPCSTR s, int n, const short __far *dx)    /* GDI.351 */
{
    LPDC dc = dc_out(hdc);
    LPFONTINFO fi;
    DRVEXTTEXTOUT fn;
    int p[4];
    RECT orc, clip;
    LPRGN rao;
    WORD k, nrects;
    short __far *ddx = 0;
    XP xdx = 0;
    TEXTXFORM tx;
    int tw;
    if (!dc)
        return FALSE;
    if (!font_realize(dc))
        return FALSE;
    dc = dc_get(hdc);
    fi = dc_font(dc)->fi;
    font_xform(dc, dc_font(dc), &tx);
    fn = (DRVEXTTEXTOUT)dc->dev->fn[DRV_EXTTEXTOUT];
    if (n < 0 || !s)
        n = 0;
    if (dc->textAlign & TA_UPDATECP) {
        x = dc->cur.x;
        y = dc->cur.y;
    }
    p[0] = x;
    p[1] = y;
    dc_lp_to_dp(dc, p, 1);
    x = p[0];
    y = p[1];
    tw = text_width(dc, s, n, dx);
    if (dc->textAlign & TA_CENTER) {
        if ((dc->textAlign & TA_CENTER) == TA_CENTER)
            x -= tw / 2;
        else
            x -= tw;
    }
    if ((dc->textAlign & TA_BASELINE) == TA_BASELINE)
        y -= fi->dfAscent;
    else if (dc->textAlign & TA_BOTTOM)
        y -= fi->dfPixHeight;
    x += dc->dcOrg.x;
    y += dc->dcOrg.y;
    if (rc && (opt & (ETO_OPAQUE | ETO_CLIPPED))) {
        p[0] = rc->left;
        p[1] = rc->top;
        p[2] = rc->right;
        p[3] = rc->bottom;
        dc_lp_to_dp(dc, p, 2);
        orc.left = min(p[0], p[2]) + dc->dcOrg.x;
        orc.right = max(p[0], p[2]) + dc->dcOrg.x;
        orc.top = min(p[1], p[3]) + dc->dcOrg.y;
        orc.bottom = max(p[1], p[3]) + dc->dcOrg.y;
    } else
        opt &= ~(ETO_OPAQUE | ETO_CLIPPED);
    if (dx && n) {
        int i;
        xdx = xalloc(n * 2);
        if (xdx) {
            ddx = xptr(xdx);
            for (i = 0; i < n; i++)
                ddx[i] = dc_scale_x(dc, dx[i]);
        }
        dc = dc_get(hdc);
    }
    rao = dc_rao(dc);
    nrects = rao->n;
    for (k = 0; k < nrects; k++) {
        rao = rgn_ptr(dc->hRao);
        clip = nrects == 1 ? rao->box : rao->r[k];
        fn(dc->lpPDevice, x, y, &clip, s, n, fi, &dc->dm, &tx,
           ddx ? xptr(xdx) : (short __far *)0, opt ? &orc : 0, opt);
    }
    if (n && !dx && dc->dm.BreakCount > 0) {
        /* GD8 (GD1's log) on 3.1: the next output goes on with the
           justification's error term where this text left it, one
           BreakRem off per break character, BreakCount back on when it
           reaches 0 or less (the driver keeps its own term); with dx
           widths, which take no justification, left (UNVERIFIED) */
        BYTE brk = (BYTE)(fi->dfBreakChar + fi->dfFirstChar);
        int i;
        for (i = 0; i < n; i++)
            if ((BYTE)s[i] == brk) {
                dc->dm.BreakErr -= dc->dm.BreakRem;
                if (dc->dm.BreakErr <= 0)
                    dc->dm.BreakErr += dc->dm.BreakCount;
            }
    }
    if (n) {
        DWORD e = drv_extent(dc, s, n);
        dc_update_bounds(dc, x, y, x + LOWORD(e), y + HIWORD(e));
        /* GD5 on 3.1: the position moves by the text's width (with dx,
           the widths before the last character and its own), to the
           left for TA_RIGHT, not at all for TA_CENTER */
        if (dc->textAlign & TA_UPDATECP) {
            int lw = mul_div(tw, dc->wndExt.x, dc->vpExt.x);
            if ((dc->textAlign & TA_CENTER) == TA_RIGHT)
                dc->cur.x -= lw;
            else if (!(dc->textAlign & TA_CENTER))
                dc->cur.x += lw;
        }
    }
    if (opt & ETO_OPAQUE)
        dc_update_bounds(dc, orc.left, orc.top, orc.right, orc.bottom);
    xfree(xdx);
    return TRUE;
}

BOOL GDIAPI TextOut(HDC hdc, int x, int y, LPCSTR s, int n)        /* GDI.33 */
{
    if (n <= 0)
        return TRUE;
    return ExtTextOut(hdc, x, y, 0, 0, s, n, 0);
}

BOOL GDIAPI GetTextMetrics(HDC hdc, TEXTMETRIC __far *tm)          /* GDI.93 */
{
    LPDC dc = dc_get(hdc);
    LPFONTINFO fi;
    LPFONTOBJ fo;
    TEXTXFORM tx;
    if (!dc || !tm || !font_realize(dc))
        return FALSE;
    dc = dc_get(hdc);
    fi = dc_font(dc)->fi;
    font_xform(dc, dc_font(dc), &tx);
    fo = obj_get(dc->hFont, FONT_MAGIC);
    tm->tmHeight = fi->dfPixHeight;
    tm->tmAscent = fi->dfAscent;
    tm->tmDescent = fi->dfPixHeight - fi->dfAscent;
    tm->tmInternalLeading = fi->dfInternalLeading;
    tm->tmExternalLeading = fi->dfExternalLeading;
    tm->tmAveCharWidth = fi->dfAvgWidth;
    tm->tmMaxCharWidth = fi->dfMaxWidth;
    tm->tmWeight = fi->dfWeight;
    tm->tmItalic = fi->dfItalic;
    tm->tmUnderlined = fo && fo->lf.lfUnderline ? 1 : fi->dfUnderline;
    tm->tmStruckOut = fo && fo->lf.lfStrikeOut ? 1 : fi->dfStrikeOut;
    tm->tmFirstChar = fi->dfFirstChar;
    tm->tmLastChar = fi->dfLastChar;
    tm->tmDefaultChar = fi->dfDefaultChar + fi->dfFirstChar;
    tm->tmBreakChar = fi->dfBreakChar + fi->dfFirstChar;
    tm->tmPitchAndFamily = fi->dfPitchAndFamily;
    tm->tmCharSet = fi->dfCharSet;
    tm->tmOverhang = tx.txfOverhang;
    if (tx.txfOverhang) {          /* emboldened: UNVERIFIED metrics */
        tm->tmWeight = tx.txfWeight;
        tm->tmAveCharWidth += tx.txfOverhang;
        tm->tmMaxCharWidth += tx.txfOverhang;
    }
    tm->tmDigitizedAspectX = fi->dfHorizRes;
    tm->tmDigitizedAspectY = fi->dfVertRes;
    if (dc->mapMode != 1) {
        /* GD5 on 3.1 (MM_LOMETRIC, anisotropic 2:1): heights and widths
           in logical units, the descent what the ascent leaves */
        tm->tmHeight = log_y(dc, tm->tmHeight);
        tm->tmAscent = log_y(dc, tm->tmAscent);
        tm->tmDescent = tm->tmHeight - tm->tmAscent;
        tm->tmInternalLeading = log_y(dc, tm->tmInternalLeading);
        tm->tmExternalLeading = log_y(dc, tm->tmExternalLeading);
        tm->tmAveCharWidth = log_x(dc, tm->tmAveCharWidth);
        tm->tmMaxCharWidth = log_x(dc, tm->tmMaxCharWidth);
        tm->tmOverhang = log_x(dc, tm->tmOverhang);
    }
    return TRUE;
}

int GDIAPI GetTextFace(HDC hdc, int n, LPSTR buf)                  /* GDI.92 */
{
    LPDC dc = dc_get(hdc);
    int i;
    if (!dc || !buf || !font_realize(dc) || n <= 0)
        return 0;
    dc = dc_get(hdc);
    for (i = 0; i < n - 1 && dc_font(dc)->face[i]; i++)
        buf[i] = dc_font(dc)->face[i];
    buf[i] = 0;
    return i;
}

BOOL GDIAPI GetCharWidth(HDC hdc, WORD first, WORD last, short __far *buf) /* GDI.350 */
{
    LPDC dc = dc_get(hdc);
    DRVGETCHARWIDTH fn;
    TEXTXFORM tx;
    if (!dc || !buf || !font_realize(dc) || last < first)
        return FALSE;
    dc = dc_get(hdc);
    font_xform(dc, dc_font(dc), &tx);
    fn = (DRVGETCHARWIDTH)dc->dev->fn[DRV_GETCHARWIDTH];
    if (!fn || !fn(dc->lpPDevice, buf, first, last, dc_font(dc)->fi, &dc->dm, &tx))
        return FALSE;
    if (dc->mapMode != 1) {             /* logical units (GD5 on 3.1) */
        WORD i;
        dc = dc_get(hdc);
        for (i = 0; i <= last - first; i++)
            buf[i] = log_x(dc, buf[i]);
    }
    return TRUE;
}

DWORD GDIAPI SetMapperFlags(HDC hdc, DWORD f)                      /* GDI.349 */
{
    LPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = dc->mapperFlags;
    dc->mapperFlags = f;                /* GD5: 3.1 keeps the whole DWORD */
    return old;
}
