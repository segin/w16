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

/* the driver's extent of the text (with the caller's device widths, as
   3.1 asks it for ExtTextOut's: GD1's log of GD10); the justification
   error term (DRAWMODE BreakErr) as it was before (GD8, GD1's log on 3.1:
   only output moves it on; GetTextExtent and the extents TextOut takes
   leave it) */
static DWORD drv_extent_w(LPDC dc, LPCSTR s, int n, short __far *widths)
{
    LPFONT f = dc_font(dc);
    TEXTXFORM tx;
    short err = dc->dm.BreakErr;
    DWORD r;
    if (!f)
        return 0;
    font_xform(dc, f, &tx);
    r = ((DRVEXTTEXTOUT)dc->dev->fn[DRV_EXTTEXTOUT])(dc->lpPDevice, 0, 0, 0, s, -n,
                                                     f->fi, &dc->dm, &tx, widths, 0, 0);
    dc->dm.BreakErr = err;
    return r;
}

static DWORD drv_extent(LPDC dc, LPCSTR s, int n)
{
    return drv_extent_w(dc, s, n, 0);
}

/* the italic overhang of the DC's realized font (0: not simulated) */
static int italic_overhang(LPDC dc)
{
    LPFONT f = dc_font(dc);
    TEXTXFORM tx;
    if (!f)
        return 0;
    font_xform(dc, f, &tx);
    return font_italic_overhang(f->fi, &tx);
}

DWORD GDIAPI GetTextExtent(HDC hdc, LPCSTR s, int n)               /* GDI.91 */
{
    LPDC dc = dc_get(hdc);
    DWORD e;
    int w, h;
    if (!dc || n <= 0 || !s)
        return 0;
    font_realize(dc);
    dc = dc_get(hdc);
    e = drv_extent(dc, s, n);
    /* GD10 on 3.1: a simulated italic's extent has its overhang */
    w = LOWORD(e) + italic_overhang(dc);
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

/* one row of an underline or strike-out: an OS_POLYLINE of two points
   (the end point not drawn) with the clip rectangle its box (GD1's log
   on 3.1), cut to the ETO_CLIPPED rectangle */
static void text_hline(LPDC dc, LPVOID pen, int x0, int x1, int y, const RECT *cr)
{
    DRVOUTPUT fn = (DRVOUTPUT)dc->dev->fn[DRV_OUTPUT];
    LPRGN rao = dc_rao(dc);
    POINT pt[2];
    RECT box;
    WORD k;
    DRAWMODE dm = dc->dm;
    dm.Rop2 = 13;                       /* R2_COPYPEN, whatever the DC's (GD10) */
    if (cr) {
        if (y < cr->top || y >= cr->bottom)
            return;
        if (x0 < cr->left)
            x0 = cr->left;
        if (x1 > cr->right)
            x1 = cr->right;
    }
    if (x0 >= x1)
        return;
    pt[0].x = x0;
    pt[0].y = y;
    pt[1].x = x1;
    pt[1].y = y;
    box.left = x0;
    box.top = y;
    box.right = x1 + 1;
    box.bottom = y + 1;
    if (cr && box.right > cr->right)
        box.right = cr->right;
    for (k = 0; k < rao->n; k++) {
        RECT c = rao->r[k];
        if (c.left < box.left) c.left = box.left;
        if (c.top < box.top) c.top = box.top;
        if (c.right > box.right) c.right = box.right;
        if (c.bottom > box.bottom) c.bottom = box.bottom;
        if (c.left >= c.right || c.top >= c.bottom)
            continue;
        fn(dc->lpPDevice, OS_POLYLINE, 2, pt, pen, 0, &dm, &c);
        rao = rgn_ptr(dc->hRao);
    }
}

/* The simulated underline and strike-out (GD10, GD1's log on 3.1): after
   the text, the underline then the strike-out, each as many rows as the
   font's height / 12 (at least one), one polyline a row in the text
   colour (a solid pen realized for it), R2_COPYPEN whatever the DC's
   ROP2; as long as the driver's extent (without the italic overhang).
   The underline's top row is one below the baseline (the cell's last rows
   when there is no room); the strike-out's is a third of the way from the
   baseline up to the internal leading, (2 ascent + internal leading) / 3,
   less half the thickness. With italic each line starts (height - its
   top row) / 2 to the right. */
static void text_lines(HDC hdc, int x, int y, int lw, int io, const TEXTXFORM *tx,
                       const RECT *cr)
{
    LPDC dc = dc_get(hdc);
    LPFONTINFO fi = dc_font(dc)->fi;
    DRVREALIZE rz = (DRVREALIZE)dc->dev->fn[DRV_REALIZEOBJECT];
    int h = fi->dfPixHeight, t = h / 12, pass, top, x0, r;
    LOGPEN lp;
    WORD size;
    XP xpen;
    if (t < 1)
        t = 1;
    lp.lopnStyle = 0;
    lp.lopnWidth.x = 0;
    lp.lopnWidth.y = 0;
    lp.lopnColor = dc->dm.LTextColor;
    size = LOWORD(rz(dc->lpPDevice, OBJ_PEN, &lp, 0, 0));
    if (!size || size == 0xFFFF || !(xpen = xalloc(size)))
        return;
    dc = dc_get(hdc);
    rz(dc->lpPDevice, OBJ_PEN, &lp, xptr(xpen), xptr(xpen));
    for (pass = 0; pass < 2; pass++) {
        if (!(tx->txfAccelerator & (pass ? 0x1000 : 0x0800)))
            continue;
        if (pass)
            top = (2 * fi->dfAscent + fi->dfInternalLeading) / 3 - t / 2;
        else {
            top = fi->dfAscent + 1;
            if (top + t > h)
                top = h - t;
        }
        x0 = x + (io ? (h - top) / 2 : 0);
        for (r = 0; r < t; r++)
            text_hline(dc, xptr(xpen), x0, x0 + lw, y + top + r, cr);
    }
    xfree(xpen);
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
    int tw, lw, io;
    if (mf_dc(hdc))
        return mf_exttext(hdc, x, y, opt, rc, s, n, dx);
    if (!dc)
        return FALSE;
    if (!font_realize(dc))
        return FALSE;
    dc = dc_get(hdc);
    fi = dc_font(dc)->fi;
    font_xform(dc, dc_font(dc), &tx);
    fn = (DRVEXTTEXTOUT)dc->dev->fn[DRV_EXTTEXTOUT];
    io = font_italic_overhang(fi, &tx);
    if (n < 0 || !s)
        n = 0;
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
    if (dc->textAlign & TA_UPDATECP) {
        x = dc->cur.x;
        y = dc->cur.y;
    }
    p[0] = x;
    p[1] = y;
    dc_lp_to_dp(dc, p, 1);
    x = p[0];
    y = p[1];
    /* the width: the driver's extent (GD1's log of GD10 on 3.1: with
       the caller's widths, if any), which the underline and strike-out
       take, and the simulated italic's overhang beyond it */
    lw = n > 0 ? LOWORD(drv_extent_w(dc, s, n, ddx ? xptr(xdx) : (short __far *)0)) : 0;
    tw = n > 0 ? lw + io : 0;
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
    rao = dc_rao(dc);
    nrects = rao->n;
    for (k = 0; k < nrects; k++) {
        rao = rgn_ptr(dc->hRao);
        clip = nrects == 1 ? rao->box : rao->r[k];
        if (!io || !n) {
            fn(dc->lpPDevice, x, y, &clip, s, n, fi, &dc->dm, &tx,
               ddx ? xptr(xdx) : (short __far *)0, opt ? &orc : 0, opt);
            continue;
        }
        /* italic (GD10, GD1's log on 3.1): the text again for each band
           of two rows from the top, each one pixel left of the one
           above, the top band moved right by the overhang and the bottom
           one not at all; each band's clip rectangle is its rows (the
           first band's from the clip's top, the last's to its bottom, so
           that the opaque rectangle, passed every time, is filled) */
        {
            int nb = (fi->dfPixHeight + 1) / 2, b;
            for (b = 0; b < nb; b++) {
                RECT bc = clip;
                if (b > 0 && bc.top < y + 2 * b)
                    bc.top = y + 2 * b;
                if (b < nb - 1 && bc.bottom > y + 2 * b + 2)
                    bc.bottom = y + 2 * b + 2;
                if (bc.top >= bc.bottom)
                    continue;
                fn(dc->lpPDevice, x + io - b, y, &bc, s, n, fi, &dc->dm, &tx,
                   ddx ? xptr(xdx) : (short __far *)0, opt ? &orc : 0, opt);
            }
        }
    }
    if (n && (tx.txfAccelerator & 0x1800))
        text_lines(hdc, x, y, lw, io, &tx, (opt & ETO_CLIPPED) ? &orc : (RECT *)0);
    dc = dc_get(hdc);
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
        dc_update_bounds(dc, x, y, x + tw, y + fi->dfPixHeight);
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
    if (mf_dc(hdc))
        return mf_text(hdc, x, y, s, n);
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
    /* GD10 on 3.1: simulated attributes as TEXTXFORM has them (italic
       1, underline and strike-out FFh) */
    tm->tmItalic = tx.txfItalic ? tx.txfItalic : fi->dfItalic;
    tm->tmUnderlined = tx.txfUnderline ? tx.txfUnderline : fi->dfUnderline;
    tm->tmStruckOut = tx.txfStrikeOut ? tx.txfStrikeOut : fi->dfStrikeOut;
    tm->tmFirstChar = fi->dfFirstChar;
    tm->tmLastChar = fi->dfLastChar;
    tm->tmDefaultChar = fi->dfDefaultChar + fi->dfFirstChar;
    tm->tmBreakChar = fi->dfBreakChar + fi->dfFirstChar;
    tm->tmPitchAndFamily = fi->dfPitchAndFamily;
    tm->tmCharSet = fi->dfCharSet;
    /* GD10 on 3.1: the overhang is the emboldening's 1 and the italic's;
       emboldening makes the widths one more, italic does not */
    tm->tmOverhang = tx.txfOverhang;
    if (tx.txfAccelerator & 0x0200) {
        tm->tmWeight = tx.txfWeight;
        tm->tmAveCharWidth++;
        tm->tmMaxCharWidth++;
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
    {
        /* GD10 on 3.1: a simulated italic's widths have its overhang */
        int io = font_italic_overhang(dc_font(dc)->fi, &tx);
        WORD i;
        if (io)
            for (i = 0; i <= last - first; i++)
                buf[i] += io;
    }
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
    if (mf_dc(hdc))
        return mf_rec2(hdc, MR_SETMAPPERFLAGS, LOWORD(f), HIWORD(f));
    if (!dc)
        return 0;
    old = dc->mapperFlags;
    dc->mapperFlags = f;                /* GD5: 3.1 keeps the whole DWORD */
    return old;
}
