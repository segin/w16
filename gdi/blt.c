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

/* a logical rectangle's corner and signed extents in device units */
static void dev_ext(LPDC dc, int x, int y, int w, int h, int *px, int *py, int *pw, int *ph)
{
    int p[4];
    p[0] = x;
    p[1] = y;
    p[2] = x + w;
    p[3] = y + h;
    dc_lp_to_dp(dc, p, 2);
    *px = p[0];
    *py = p[1];
    *pw = p[2] - p[0];
    *ph = p[3] - p[1];
}

/* a corner and a signed extent made the lower corner and a positive one */
static void dev_norm(int *p, int *e)
{
    if (*e < 0) {
        *p += *e;
        *e = -*e;
    }
}

BOOL GDIAPI PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop) /* GDI.29 */
{
    LPDC dc = dc_out(hdc);
    int dx, dy, dw, dh;
    if (mf_dc(hdc))
        return mf_rec6(hdc, MR_PATBLT, LOWORD(rop), HIWORD(rop), h, w, y, x);
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
    if (mf_dc(hdc))
        return mf_blt(hdc, x, y, w, h, hsrc, xs, ys, 0, 0, rop, 0);
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

/* stretch one 1bpp-per-plane scanline from src[sx..sx+sw) into dst[0..dw)
   using exact nearest-neighbor DDA: floor((flipx ? dw-1-i : i) * sw / dw) */
static void stretch_row_1bpp(BYTE __far *dst, int dw, const BYTE __far *src,
                             int sx, int sw, int slim, int flipx)
{
    int xq = sw / dw, xr = sw % dw;
    int pos, i, bi = 0, bit;
    WORD acc;
    if (!flipx) {
        pos = 0;
        acc = 0;
        for (i = 0; i < dw; ) {
            BYTE out = 0;
            int lim = dw - i < 8 ? dw - i : 8;
            for (bit = 0; bit < lim; bit++, i++) {
                int sxi = sx + pos;
                if (sxi >= 0 && sxi < slim) {
                    if (src[sxi >> 3] & (0x80 >> (sxi & 7)))
                        out |= (BYTE)(0x80 >> bit);
                }
                pos += xq;
                acc += (WORD)xr;
                if (acc >= (WORD)dw) {
                    acc -= (WORD)dw;
                    pos++;
                }
            }
            dst[bi++] = out;
        }
    } else {
        long num = (long)(dw - 1) * sw;
        pos = (int)(num / dw);
        acc = (WORD)(num % dw);
        for (i = 0; i < dw; ) {
            BYTE out = 0;
            int lim = dw - i < 8 ? dw - i : 8;
            for (bit = 0; bit < lim; bit++, i++) {
                int sxi = sx + pos;
                if (sxi >= 0 && sxi < slim) {
                    if (src[sxi >> 3] & (0x80 >> (sxi & 7)))
                        out |= (BYTE)(0x80 >> bit);
                }
                pos -= xq;
                if (acc < (WORD)xr) {
                    acc += (WORD)(dw - xr);
                    pos--;
                } else {
                    acc -= (WORD)xr;
                }
            }
            dst[bi++] = out;
        }
    }
}

/* StretchBlt with different sizes: VGA.DRV has no StretchBlt, so GDI
   stretches. UNVERIFIED: 3.1's sampling; here each destination pixel
   takes the source pixel at floor(i * src / dst), through a temporary
   bitmap of the source's format, then one BitBlt. */
BOOL stretch_blt(LPDC dc, int dx, int dy, int dw, int dh, LPDC sdc, int sx, int sy,
                 int sw, int sh, DWORD rop)
{
    DRVBITBLT fn = (DRVBITBLT)dc->dev->fn[DRV_BITBLT];
    DRAWMODE dm = sdc->dm;
    LPPBITMAP srcdev = sdc->lpPDevice, pt;
    HBITMAP tmp, rowbm = 0;
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
    if (pt->bmBitsPixel == 1 && (pt->bmPlanes == 1 || pt->bmPlanes == 4)) {
        int src_is_mem = srcdev->bmType == 0;
        LPPBITMAP prow = 0;
        int planes = pt->bmPlanes;
        WORD dwb = pt->bmWidthBytes, swb = 0;
        int slim = 0, shlim = 0, need_zero = 0;
        int yq = sh / dh, yr = sh % dh, ypos = 0;
        WORD yacc = 0;
        int prev_syj = -1, prev_dj = -1;
        if (src_is_mem) {
            swb = srcdev->bmWidthBytes;
            slim = srcdev->bmWidth;
            shlim = srcdev->bmHeight;
        } else {
            rowbm = bm_create(sw, 1, planes, 1, 0, 0);
            if (rowbm) {
                prow = bm_pbitmap(rowbm);
                swb = prow->bmWidthBytes;
                slim = sw;
                shlim = dc->dev->info.dpVertRes;
                need_zero = sx < 0 || sx + sw > dc->dev->info.dpHorzRes;
            }
        }
        if (src_is_mem || prow) {
            for (j = 0; j < dh; j++) {
                int dj = flipy ? dh - 1 - j : j;
                int syj = sy + ypos;
                if (syj == prev_syj && prev_dj >= 0) {
                    mem_copy(bm_scan(pt, dj), bm_scan(pt, prev_dj), dwb * planes);
                } else if (syj >= 0 && syj < shlim) {
                    BYTE __far *sscan, __far *dscan;
                    int p;
                    if (src_is_mem) {
                        sscan = bm_scan(srcdev, syj);
                    } else {
                        if (need_zero)
                            mem_set(bm_scan(prow, 0), 0, swb * planes);
                        fn(prow, 0, 0, srcdev, sx, syj, sw, 1, 0x00CC0020L, 0, &dm);
                        prow = bm_pbitmap(rowbm);
                        pt = bm_pbitmap(tmp);
                        sscan = bm_scan(prow, 0);
                    }
                    dscan = bm_scan(pt, dj);
                    for (p = 0; p < planes; p++)
                        stretch_row_1bpp(dscan + p * dwb, dw, sscan + p * swb,
                                         src_is_mem ? sx : 0, sw, slim, flipx);
                    prev_syj = syj;
                    prev_dj = dj;
                }
                ypos += yq;
                yacc += (WORD)yr;
                if (yacc >= (WORD)dh) {
                    yacc -= (WORD)dh;
                    ypos++;
                }
            }
            if (rowbm)
                bm_free(rowbm);
            goto done;
        }
    }
    for (j = 0; j < dh; j++) {
        int syj = sy + (int)((long)(flipy ? dh - 1 - j : j) * sh / dh);
        for (i = 0; i < dw; i++) {
            int sxi = sx + (int)((long)(flipx ? dw - 1 - i : i) * sw / dw);
            fn(pt, i, j, srcdev, sxi, syj, 1, 1, 0x00CC0020L, 0, &dm);
        }
    }
done:
    t = *sdc;
    t.lpPDevice = bm_pbitmap(tmp);
    blt_out(dc, dx + dc->dcOrg.x, dy + dc->dcOrg.y, dw, dh, &t, 0, 0, rop);
    bm_free(tmp);
    return TRUE;
}

BOOL GDIAPI StretchBlt(HDC hdc, int x, int y, int w, int h, HDC hsrc, int xs, int ys,
                       int ws, int hs, DWORD rop)                  /* GDI.35 */
{
    LPDC dc = dc_out(hdc), sdc;
    int dx, dy, dw, dh, sx, sy, sw, sh;
    if (mf_dc(hdc))
        return mf_blt(hdc, x, y, w, h, hsrc, xs, ys, ws, hs, rop, 1);
    if (!dc)
        return FALSE;
    if (!NEEDS_SRC(rop))
        return PatBlt(hdc, x, y, w, h, rop);
    sdc = dc_out(hsrc);
    if (!sdc)
        return FALSE;
    dc = dc_get(hdc);
    /* both rectangles in device units, each DC with its own mapping: a
       mirror is where the device extents' signs differ (a negative
       extent or an axis the mapping turns round) */
    {
        int fx, fy;
        dev_ext(dc, x, y, w, h, &dx, &dy, &dw, &dh);
        dev_ext(sdc, xs, ys, ws, hs, &sx, &sy, &sw, &sh);
        fx = (dw < 0) != (sw < 0);
        fy = (dh < 0) != (sh < 0);
        dev_norm(&dx, &dw);
        dev_norm(&dy, &dh);
        dev_norm(&sx, &sw);
        dev_norm(&sy, &sh);
        if (!dw || !dh)
            return TRUE;
        sx += sdc->dcOrg.x;
        sy += sdc->dcOrg.y;
        if (!fx && !fy && dw == sw && dh == sh) {
            blt_out(dc, dx + dc->dcOrg.x, dy + dc->dcOrg.y, dw, dh, sdc, sx, sy, rop);
            return TRUE;
        }
        return stretch_blt(dc, dx, dy, fx ? -dw : dw, fy ? -dh : dh, sdc, sx, sy, sw, sh, rop);
    }
}

/* USER's window frames (Schulman ch. 8: the driver's FastBorder) */
BOOL GDIAPI FastWindowFrame(HDC hdc, const RECT __far *rc, int xw, int yw, DWORD rop) /* GDI.400 */
{
    LPDC dc = rc ? dc_out(hdc) : 0;
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
    if (mf_dc(hdc))
        return mf_rec4(hdc, MR_SETPIXEL, LOWORD(c), HIWORD(c), y, x);
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
    if (mf_dc(hdc))                     /* GD9 */
        return 0;
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

/* GD9: a region call on a metafile DC records the region (and brush)
   in its object table, then the call with their slots */
static BOOL mf_rgn(HDC hdc, WORD fn, HRGN hrgn, HBRUSH hbr)
{
    int r = mf_slot(hdc, hrgn), b = hbr ? mf_slot(hdc, hbr) : 0;
    if (r < 0 || b < 0)
        return FALSE;
    return hbr ? mf_rec2(hdc, fn, r, b) : mf_rec1(hdc, fn, r);
}

BOOL GDIAPI FillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr)                /* GDI.40 */
{
    if (mf_dc(hdc))
        return mf_rgn(hdc, MR_FILLREGION, hrgn, hbr);
    return rgn_blt(hdc, hrgn, hbr, 0x00F00021L);
}

BOOL GDIAPI PaintRgn(HDC hdc, HRGN hrgn)                           /* GDI.43 */
{
    if (mf_dc(hdc))
        return mf_rgn(hdc, MR_PAINTREGION, hrgn, 0);
    return rgn_blt(hdc, hrgn, 0, 0x00F00021L);
}

BOOL GDIAPI InvertRgn(HDC hdc, HRGN hrgn)                          /* GDI.42 */
{
    if (mf_dc(hdc))
        return mf_rgn(hdc, MR_INVERTREGION, hrgn, 0);
    return rgn_blt(hdc, hrgn, 0, 0x00550009L);
}

/* UNVERIFIED: 3.1's FrameRgn; here the region minus itself shrunk */
BOOL GDIAPI FrameRgn(HDC hdc, HRGN hrgn, HBRUSH hbr, int w, int h) /* GDI.41 */
{
    HRGN a, b, frame;
    BOOL r;
    if (mf_dc(hdc))                     /* GD9: 3.1 records nothing, 0 */
        return FALSE;
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
