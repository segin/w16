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

static DWORD drv_extent(NPDC dc, LPCSTR s, int n)
{
    if (!dc->pfont)
        return 0;
    return ((DRVEXTTEXTOUT)dc->dev->fn[DRV_EXTTEXTOUT])(dc->lpPDevice, 0, 0, 0, s, -n,
                                                        dc->pfont->fi, &dc->dm, &dc->xform,
                                                        0, 0, 0);
}

DWORD GDIAPI GetTextExtent(HDC hdc, LPCSTR s, int n)               /* GDI.91 */
{
    NPDC dc = dc_get(hdc);
    DWORD e;
    int w, h;
    if (!dc || n <= 0)
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
    DWORD e = GetTextExtent(hdc, s, n);
    sz->x = LOWORD(e);
    sz->y = HIWORD(e);
    return TRUE;
}

BOOL GDIAPI ExtTextOut(HDC hdc, int x, int y, WORD opt, const RECT __far *rc,
                       LPCSTR s, int n, const short __far *dx)    /* GDI.351 */
{
    NPDC dc = dc_out(hdc);
    LPFONTINFO fi;
    DRVEXTTEXTOUT fn;
    int p[4];
    RECT orc, clip;
    NPRGN rao;
    WORD k, nrects;
    short NEAR *ddx = 0;
    HLOCAL hdx = 0;
    if (!dc)
        return FALSE;
    if (!font_realize(dc))
        return FALSE;
    dc = dc_get(hdc);
    fi = dc->pfont->fi;
    fn = (DRVEXTTEXTOUT)dc->dev->fn[DRV_EXTTEXTOUT];
    if (n < 0)
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
    if ((dc->textAlign & TA_CENTER) && n) {
        int w = LOWORD(drv_extent(dc, s, n));
        if ((dc->textAlign & TA_CENTER) == TA_CENTER)
            x -= w / 2;
        else
            x -= w;
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
        hdx = LocalAlloc(LMEM_FIXED, n * 2);
        if (hdx) {
            ddx = (short NEAR *)hdx;
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
        fn(dc->lpPDevice, x, y, &clip, s, n, fi, &dc->dm, &dc->xform,
           ddx ? (short __far *)ddx : (short __far *)0, opt ? &orc : 0, opt);
    }
    if (n) {
        DWORD e = drv_extent(dc, s, n);
        dc_update_bounds(dc, x, y, x + LOWORD(e), y + HIWORD(e));
        if (dc->textAlign & TA_UPDATECP)
            dc->cur.x += mul_div(LOWORD(e), dc->wndExt.x, dc->vpExt.x);
    }
    if (opt & ETO_OPAQUE)
        dc_update_bounds(dc, orc.left, orc.top, orc.right, orc.bottom);
    if (hdx)
        LocalFree(hdx);
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
    NPDC dc = dc_get(hdc);
    LPFONTINFO fi;
    FONTOBJ NEAR *fo;
    if (!dc || !font_realize(dc))
        return FALSE;
    dc = dc_get(hdc);
    fi = dc->pfont->fi;
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
    tm->tmOverhang = 0;
    tm->tmDigitizedAspectX = fi->dfHorizRes;
    tm->tmDigitizedAspectY = fi->dfVertRes;
    if (dc->mapMode != 1) {
        /* UNVERIFIED: metrics in logical units */
        tm->tmHeight = dc_scale_y(dc, 0) + mul_div(tm->tmHeight, dc->wndExt.y, dc->vpExt.y);
    }
    return TRUE;
}

int GDIAPI GetTextFace(HDC hdc, int n, LPSTR buf)                  /* GDI.92 */
{
    NPDC dc = dc_get(hdc);
    int i;
    if (!dc || !font_realize(dc) || n <= 0)
        return 0;
    dc = dc_get(hdc);
    for (i = 0; i < n - 1 && dc->pfont->face[i]; i++)
        buf[i] = dc->pfont->face[i];
    buf[i] = 0;
    return i;
}

BOOL GDIAPI GetCharWidth(HDC hdc, WORD first, WORD last, short __far *buf) /* GDI.350 */
{
    NPDC dc = dc_get(hdc);
    DRVGETCHARWIDTH fn;
    if (!dc || !font_realize(dc) || last < first)
        return FALSE;
    dc = dc_get(hdc);
    fn = (DRVGETCHARWIDTH)dc->dev->fn[DRV_GETCHARWIDTH];
    if (fn)
        return fn(dc->lpPDevice, buf, first, last, dc->pfont->fi, &dc->dm, &dc->xform) != 0;
    return FALSE;
}

DWORD GDIAPI SetMapperFlags(HDC hdc, DWORD f)                      /* GDI.349 */
{
    NPDC dc = dc_get(hdc);
    DWORD old;
    if (!dc)
        return 0;
    old = dc->mapperFlags;
    dc->mapperFlags = (WORD)f;
    return old;
}
