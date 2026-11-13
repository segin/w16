/*
 * blt.c - BitBlt, PatBlt, StretchBlt, FastWindowFrame, pixels, region
 * painting.
 *
 * Observed on 3.1 (GD1/GD2 at boot): PatBlt and BitBlt become the
 * driver's BitBlt at screen coordinates (DC origin added), PatBlt with no
 * source device; the dest DC's physical brush and DRAWMODE are passed
 * with every call; a StretchBlt whose source and destination sizes are
 * equal is a BitBlt; FastWindowFrame becomes the driver's FastBorder with
 * the rectangle at screen coordinates and the clipping box. Output is cut
 * into one driver call per rectangle of the composite clip region (the
 * driver's BitBlt has no clip rectangle; DDK).
 */
#include "gdi.h"

#define NEEDS_SRC(rop) ((((rop) >> 2) ^ (rop)) & 0x330000L)

/* intersect [l,t,r,b) with rect k of the composite region; 0 if empty */
static int clip_to(LPRGN rao, WORD k, int *l, int *t, int *r, int *b)
{
    RECT __far * c = &rao->r[k];
    if (*l < c->left) *l = c->left;
    if (*t < c->top) *t = c->top;
    if (*r > c->right) *r = c->right;
    if (*b > c->bottom) *b = c->bottom;
    return *l < *r && *t < *b;
}

/* the driver BitBlt over the clip region; dest rect in screen coords */
static void blt_out(LPDC dc, int x, int y, int w, int h, DC __far *src, int sx, int sy, DWORD rop)
{
    LPRGN rao = dc_rao(dc);
    DRVBITBLT fn = (DRVBITBLT)dc->dev->fn[DRV_BITBLT];
    LPVOID pbr = dc_pbrush(dc);
    WORD k;
    rao = rgn_ptr(dc->hRao);
    for (k = 0; k < rao->n; k++) {
        int l = x, t = y, r = x + w, b = y + h;
        if (!clip_to(rao, k, &l, &t, &r, &b))
            continue;
        fn(dc->lpPDevice, l, t, src ? src->lpPDevice : 0, sx + (l - x), sy + (t - y),
           r - l, b - t, rop, pbr, &dc->dm);
        rao = rgn_ptr(dc->hRao);
    }
    dc_update_bounds(dc, x, y, x + w, y + h);
}

/* device rectangle of a logical one (normalised) */
static void dev_rect(LPDC dc, int x, int y, int w, int h, int *px, int *py, int *pw, int *ph)
{
    int p[4];
    p[0] = x;
    p[1] = y;
    p[2] = x + w;
    p[3] = y + h;
    dc_lp_to_dp(dc, p, 2);
    *px = min(p[0], p[2]);
    *py = min(p[1], p[3]);
    *pw = max(p[0], p[2]) - *px;
    *ph = max(p[1], p[3]) - *py;
}

BOOL GDIAPI PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop) /* GDI.29 */
{
    LPDC dc = dc_out(hdc);
    int dx, dy, dw, dh;
    if (!dc)
        return FALSE;
    dev_rect(dc, x, y, w, h, &dx, &dy, &dw, &dh);
    if (dw && dh)
        blt_out(dc, dx + dc->dcOrg.x, dy + dc->dcOrg.y, dw, dh, 0, 0, 0, rop);
    return TRUE;
}

BOOL stretch_blt(LPDC dc, int dx, int dy, int dw, int dh, LPDC sdc, int sx, int sy,
                 int sw, int sh, DWORD rop);

BOOL GDIAPI BitBlt(HDC hdc, int x, int y, int w, int h, HDC hsrc, int xs, int ys, DWORD rop) /* GDI.34 */
{
    LPDC dc = dc_out(hdc), sdc = 0;
    int dx, dy, dw, dh, sx = 0, sy = 0, sw, sh;
    if (!dc)
        return FALSE;
    if (NEEDS_SRC(rop)) {
        sdc = dc_out(hsrc);
        if (!sdc)
            return FALSE;
        dc = dc_get(hdc);
        dev_rect(sdc, xs, ys, w, h, &sx, &sy, &sw, &sh);
        sx += sdc->dcOrg.x;
        sy += sdc->dcOrg.y;
    }
    dev_rect(dc, x, y, w, h, &dx, &dy, &dw, &dh);
    if (!dw || !dh)
        return TRUE;
    if (sdc && (sw != dw || sh != dh))
        return stretch_blt(dc, dx, dy, dw, dh, sdc, sx, sy, sw, sh, rop);
    blt_out(dc, dx + dc->dcOrg.x, dy + dc->dcOrg.y, dw, dh, sdc, sx, sy, rop);
    return TRUE;
}

/* StretchBlt with different sizes: VGA.DRV has no StretchBlt, so GDI
   stretches. UNVERIFIED: 3.1's sampling; here each destination pixel
   takes the source pixel at floor(i * src / dst), through a temporary
   bitmap of the destination's format, then one BitBlt. */
BOOL stretch_blt(LPDC dc, int dx, int dy, int dw, int dh, LPDC sdc, int sx, int sy,
                 int sw, int sh, DWORD rop)
{
    DRVBITBLT fn = (DRVBITBLT)dc->dev->fn[DRV_BITBLT];
    DRAWMODE dm = sdc->dm;
    LPPBITMAP srcdev = sdc->lpPDevice, pt;
    HBITMAP tmp;
    DC t;
    int i, j, flipx = 0, flipy = 0;
    if (dw < 0) { dw = -dw; flipx = 1; }
    if (dh < 0) { dh = -dh; flipy = 1; }
    if (sw < 0) { sw = -sw; flipx ^= 1; }
    if (sh < 0) { sh = -sh; flipy ^= 1; }
    if (!dw || !dh || !sw || !sh)
        return TRUE;
    /* a bitmap in the source's format (a PDEVICE's first word is not 0) */
    if (srcdev->bmType)
        tmp = bm_create(dw, dh, dc->dev->info.dpPlanes, dc->dev->info.dpBitsPixel, 0, 0);
    else
        tmp = bm_create(dw, dh, srcdev->bmPlanes, srcdev->bmBitsPixel, 0, 0);
    if (!tmp)
        return FALSE;
    pt = bm_pbitmap(tmp);
    dm.Rop2 = 13;
    for (j = 0; j < dh; j++) {
        int syj = sy + (int)((long)(flipy ? dh - 1 - j : j) * sh / dh);
        for (i = 0; i < dw; i++) {
            int sxi = sx + (int)((long)(flipx ? dw - 1 - i : i) * sw / dw);
            fn(pt, i, j, srcdev, sxi, syj, 1, 1, 0x00CC0020L, 0, &dm);
        }
    }
    t = *sdc;
    t.lpPDevice = pt;
    blt_out(dc, dx + dc->dcOrg.x, dy + dc->dcOrg.y, dw, dh, &t, 0, 0, rop);
    bm_free(tmp);
    return TRUE;
}

BOOL GDIAPI StretchBlt(HDC hdc, int x, int y, int w, int h, HDC hsrc, int xs, int ys,
                       int ws, int hs, DWORD rop)                  /* GDI.35 */
{
    LPDC dc = dc_out(hdc), sdc;
    int dx, dy, dw, dh, sx, sy, sw, sh;
    if (!dc)
        return FALSE;
    if (!NEEDS_SRC(rop))
        return PatBlt(hdc, x, y, w, h, rop);
    sdc = dc_out(hsrc);
    if (!sdc)
        return FALSE;
    dc = dc_get(hdc);
    if (w == ws && h == hs)
        return BitBlt(hdc, x, y, w, h, hsrc, xs, ys, rop);
    dev_rect(dc, x, y, w, h, &dx, &dy, &dw, &dh);
    dev_rect(sdc, xs, ys, ws, hs, &sx, &sy, &sw, &sh);
    if ((w < 0) != (ws < 0))
        dw = -dw;
    if ((h < 0) != (hs < 0))
        dh = -dh;
    if (dw == sw && dh == sh)
        return BitBlt(hdc, x, y, w, h, hsrc, xs, ys, rop);
    return stretch_blt(dc, dx, dy, dw, dh, sdc, sx + sdc->dcOrg.x, sy + sdc->dcOrg.y, sw, sh, rop);
}

/* USER's window frames (Schulman ch. 8: the driver's FastBorder) */
BOOL GDIAPI FastWindowFrame(HDC hdc, const RECT __far *rc, int xw, int yw, DWORD rop) /* GDI.400 */
{
    LPDC dc = dc_out(hdc);
    DRVFASTBORDER fn;
    LPRGN rao;
    RECT r;
    WORD k;
    WORD res = 0;
    if (!dc)
        return FALSE;
    fn = (DRVFASTBORDER)dc->dev->fn[DRV_FASTBORDER];
    if (!fn)
        return FALSE;
    r.left = rc->left + dc->dcOrg.x;
    r.top = rc->top + dc->dcOrg.y;
    r.right = rc->right + dc->dcOrg.x;
    r.bottom = rc->bottom + dc->dcOrg.y;
    rao = dc_rao(dc);
    for (k = 0; k < rao->n; k++) {
        RECT c;
        rao = rgn_ptr(dc->hRao);
        c = rao->n == 1 ? rao->box : rao->r[k];
        res = fn(&r, xw, yw, rop, dc->lpPDevice, dc_pbrush(dc), &dc->dm, &c);
    }
    dc_update_bounds(dc, r.left, r.top, r.right, r.bottom);
    return res;
}

/* ---- pixels ---- */

COLORREF GDIAPI SetPixel(HDC hdc, int x, int y, COLORREF c)        /* GDI.31 */
{
    LPDC dc = dc_out(hdc);
    int p[2];
    DWORD pc, rgb;
    if (!dc)
        return (COLORREF)-1L;
    p[0] = x;
    p[1] = y;
    dc_lp_to_dp(dc, p, 1);
    p[0] += dc->dcOrg.x;
    p[1] += dc->dcOrg.y;
    rgb = ((DRVCOLORINFO)dc->dev->fn[DRV_COLORINFO])(dc->lpPDevice, c, &pc);
    if (!PtInRegion(dc->hRao, p[0], p[1]))
        return (COLORREF)-1L;
    ((DRVPIXEL)dc->dev->fn[DRV_PIXEL])(dc->lpPDevice, p[0], p[1], pc, &dc->dm);
    dc_update_bounds(dc, p[0], p[1], p[0] + 1, p[1] + 1);
    return rgb;
}

COLORREF GDIAPI GetPixel(HDC hdc, int x, int y)                    /* GDI.83 */
{
    LPDC dc = dc_out(hdc);
    int p[2];
    DWORD pc;
    if (!dc)
        return (COLORREF)-1L;
    p[0] = x;
    p[1] = y;
    dc_lp_to_dp(dc, p, 1);
    p[0] += dc->dcOrg.x;
    p[1] += dc->dcOrg.y;
    if (!PtInRegion(dc->hRao, p[0], p[1]))
        return (COLORREF)-1L;
    pc = ((DRVPIXEL)dc->dev->fn[DRV_PIXEL])(dc->lpPDevice, p[0], p[1], 0, 0);
    return ((DRVCOLORINFO)dc->dev->fn[DRV_COLORINFO])(dc->lpPDevice, pc, 0);
}

/* ---- painting regions (region in device coordinates of the DC) ---- */

static BOOL rgn_blt(HDC hdc, HRGN hrgn, HBRUSH hbr, DWORD rop)
{
    LPDC dc = dc_out(hdc);
    LPRGN r;
    HBRUSH old = 0;
    WORD k;
    if (!dc || !(r = rgn_ptr(hrgn)))
        return FALSE;
    if (hbr)
        old = SelectObject(hdc, hbr);
    for (k = 0;; k++) {
        RECT c;
        dc = dc_get(hdc);
        r = rgn_ptr(hrgn);
        if (k >= r->n)
            break;
        c = r->r[k];
        blt_out(dc, c.left + dc->dcOrg.x, c.top + dc->dcOrg.y, c.right - c.left,
                c.bottom - c.top, 0, 0, 0, rop);
    }
    if (hbr)
        SelectObject(hdc, old);
    return TRUE;
}

BOOL GDIAPI FillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr)                /* GDI.40 */
{
    return rgn_blt(hdc, hrgn, hbr, 0x00F00021L);
}

BOOL GDIAPI PaintRgn(HDC hdc, HRGN hrgn)                           /* GDI.43 */
{
    return rgn_blt(hdc, hrgn, 0, 0x00F00021L);
}

BOOL GDIAPI InvertRgn(HDC hdc, HRGN hrgn)                          /* GDI.42 */
{
    return rgn_blt(hdc, hrgn, 0, 0x00550009L);
}

/* UNVERIFIED: 3.1's FrameRgn; here the region minus itself shrunk */
BOOL GDIAPI FrameRgn(HDC hdc, HRGN hrgn, HBRUSH hbr, int w, int h) /* GDI.41 */
{
    HRGN a, b, frame;
    BOOL r;
    if (!rgn_ptr(hrgn))
        return FALSE;
    a = rgn_new(0, 0, 0, 0);
    b = rgn_new(0, 0, 0, 0);
    frame = rgn_new(0, 0, 0, 0);
    rgn_copy(a, hrgn);
    rgn_copy(b, hrgn);
    rgn_offset(b, w, 0);
    rgn_combine(a, a, b, RGN_AND);
    rgn_copy(b, hrgn);
    rgn_offset(b, -w, 0);
    rgn_combine(a, a, b, RGN_AND);
    rgn_copy(b, hrgn);
    rgn_offset(b, 0, h);
    rgn_combine(a, a, b, RGN_AND);
    rgn_copy(b, hrgn);
    rgn_offset(b, 0, -h);
    rgn_combine(a, a, b, RGN_AND);
    rgn_combine(frame, hrgn, a, RGN_DIFF);
    r = FillRgn(hdc, frame, hbr);
    rgn_free(a);
    rgn_free(b);
    rgn_free(frame);
    return r;
}
