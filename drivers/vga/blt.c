/*
 * blt.c - BitBlt (all 256 ternary raster operations, screen and memory
 * bitmaps, monochrome <-> colour conversion) and FastBorder.
 *
 * The DDK's rules (BitBlt reference page): a monochrome source on a colour
 * destination gives its 1 bits the DRAWMODE's background colour and its 0
 * bits the text colour; a colour source on a monochrome destination gives
 * pixels of the background colour 1 and all others 0; overlapping source
 * and destination on one surface are copied so that no source pixel is
 * overwritten before it is read.
 *
 * Each destination line is done in three steps: the source line, moved to
 * the destination's bit positions and converted to its format, into a
 * buffer; the brush's row for the line; then each plane of the
 * destination combined byte by byte (rop.c).
 */
#include "vga.h"

#define ROWB 130                        /* bytes of a line done at once (1032 pixels) */

static BYTE srow[4][ROWB];
static BYTE tmp[4][ROWB];

/* nb bytes of source plane p of line y, bit 7 of byte i being pixel
   off + 8 i (pixels outside the line are 0) */
static void fetch(SURF *s, int y, int p, int off, int nb, BYTE *out)
{
    BYTE __far *l;
    int i, idx, sh, wb = s->wbytes;
    BYTE b0, b1;
    if (s->screen)
        scr_plane(p);
    l = surf_line(s, y, p);
    idx = off >= 0 ? off >> 3 : -((-off + 7) >> 3);
    sh = off - idx * 8;
    for (i = 0; i < nb; i++, idx++) {
        b0 = idx >= 0 && idx < wb ? l[idx] : 0;
        if (!sh) {
            out[i] = b0;
            continue;
        }
        b1 = idx + 1 >= 0 && idx + 1 < wb ? l[idx + 1] : 0;
        out[i] = (BYTE)((b0 << sh) | (b1 >> (8 - sh)));
    }
}

/* the brush's pattern for a destination: pat[plane][row], rows by the
   device's y, bits by its x (the brush was aligned when realized) */
void pattern_planes(LPPBRUSH b, LPDRAWMODE dm, int mono, BYTE pat[4][8], BYTE *mask, BYTE *transp)
{
    int p, y;
    if (transp)
        *transp = 0;
    if (!b || (b->flags & PBF_HOLLOW)) {
        for (p = 0; p < 4; p++)
            for (y = 0; y < 8; y++)
                pat[p][y] = 0;
        if (mask)
            for (y = 0; y < 8; y++)
                mask[y] = 0xFF;
        return;
    }
    if (b->flags & PBF_MONOPAT) {
        /* a monochrome pattern: 0 bits the text colour, 1 bits the
           background colour (DDK, RealizeObject) */
        int fg = dm ? (mono ? PC_MONO(dm->TextColor) * 15 : PC_INDEX(dm->TextColor)) : 0;
        int bg = dm ? (mono ? PC_MONO(dm->bkColor) * 15 : PC_INDEX(dm->bkColor)) : 15;
        for (p = 0; p < 4; p++)
            for (y = 0; y < 8; y++)
                pat[p][y] = (BYTE)(((bg >> p) & 1 ? b->mono[y] : 0) |
                                   ((fg >> p) & 1 ? ~b->mono[y] : 0));
    } else if (mono) {
        for (y = 0; y < 8; y++)
            pat[0][y] = b->mono[y];
    } else {
        for (p = 0; p < 4; p++)
            for (y = 0; y < 8; y++)
                pat[p][y] = b->color[p][y];
    }
    if (mask)
        for (y = 0; y < 8; y++)
            mask[y] = (b->flags & PBF_HATCH) ? b->mask[y] : 0xFF;
    if (transp && (b->flags & PBF_HATCH) && dm && dm->bkMode == TRANSPARENT)
        *transp = 1;
}

/* the general transfer: w x h pixels from S (sx, sy) to D (dx, dy); S may
   be NULL (no source). Rectangles already clipped. */
static void blt(SURF *D, int dx, int dy, SURF *S, int sx, int sy, int w, int h,
                BYTE rop, BYTE pat[4][8], LPDRAWMODE dm)
{
    int nb, x0, y, yi, ystep, p, i, maxw;
    BYTE lmask, rmask;
    int fgi = dm ? PC_INDEX(dm->TextColor) : 0, bgi = dm ? PC_INDEX(dm->bkColor) : 15;
    if (D->planes == 1 && dm) {
        fgi = PC_MONO(dm->TextColor) * 15;
        bgi = PC_MONO(dm->bkColor) * 15;
    }
    /* wider than the buffers: in pieces, right to left when a source
       to the left on the same surface would be overwritten first */
    maxw = (ROWB - 1) * 8 - (dx & 7);
    if (w > maxw) {
        int same = S && S->sel == D->sel && S->off == D->off && S->screen == D->screen;
        if (same && dx > sx) {
            blt(D, dx + maxw, dy, S, sx + maxw, sy, w - maxw, h, rop, pat, dm);
            blt(D, dx, dy, S, sx, sy, maxw, h, rop, pat, dm);
        } else {
            blt(D, dx, dy, S, sx, sy, maxw, h, rop, pat, dm);
            blt(D, dx + maxw, dy, S, sx + maxw, sy, w - maxw, h, rop, pat, dm);
        }
        return;
    }
    x0 = dx >> 3;
    nb = ((dx + w - 1) >> 3) - x0 + 1;
    lmask = (BYTE)(0xFF >> (dx & 7));
    rmask = (BYTE)(0xFF << (7 - ((dx + w - 1) & 7)));
    y = dy;
    ystep = 1;
    if (S && S->sel == D->sel && S->off == D->off && S->screen == D->screen && dy > sy) {
        y = dy + h - 1;
        ystep = -1;
    }
    for (yi = 0; yi < h; yi++, y += ystep) {
        int srcy = sy + (y - dy);
        if (S) {
            int off = sx - (dx & 7);
            if (S->planes == D->planes) {
                for (p = 0; p < D->planes; p++)
                    fetch(S, srcy, p, off, nb, srow[p]);
            } else if (S->planes == 1) {
                /* mono -> colour: 1 bits background, 0 bits text colour */
                fetch(S, srcy, 0, off, nb, tmp[0]);
                for (p = 0; p < 4; p++)
                    for (i = 0; i < nb; i++)
                        srow[p][i] = (BYTE)(((bgi >> p) & 1 ? tmp[0][i] : 0) |
                                            ((fgi >> p) & 1 ? ~tmp[0][i] : 0));
            } else {
                /* colour -> mono: the background colour's pixels 1 */
                int bk = dm ? PC_INDEX(dm->bkColor) : 15;
                for (p = 0; p < 4; p++)
                    fetch(S, srcy, p, off, nb, tmp[p]);
                for (i = 0; i < nb; i++) {
                    BYTE diff = 0;
                    for (p = 0; p < 4; p++)
                        diff |= (BYTE)(tmp[p][i] ^ ((bk >> p) & 1 ? 0xFF : 0));
                    srow[0][i] = (BYTE)~diff;
                }
            }
        }
        for (p = 0; p < D->planes; p++) {
            if (D->screen)
                scr_plane(p);
            rop_run(rop, surf_line(D, y, p) + x0, S ? srow[p] : NULL, pat ? pat[p][y & 7] : 0,
                    nb, lmask, rmask);
        }
    }
}

/* a rectangle filled through rop with a brush (PatBlt), already clipped
   to the surface */
void fill_rect(SURF *s, int x0, int y0, int x1, int y1, BYTE rop, LPPBRUSH b, LPDRAWMODE dm)
{
    BYTE pat[4][8];
    pattern_planes(b, dm, s->planes == 1, pat, NULL, NULL);
    blt(s, x0, y0, NULL, 0, 0, x1 - x0, y1 - y0, rop, pat, dm);
}

/* a rectangle in one colour (index idx, or mono value on a mono bitmap) */
void fill_solid(SURF *s, int x0, int y0, int x1, int y1, BYTE rop, int idx, int mono)
{
    BYTE pat[4][8];
    int p, y;
    if (s->planes == 1)
        idx = mono ? 15 : 0;
    for (p = 0; p < 4; p++)
        for (y = 0; y < 8; y++)
            pat[p][y] = (BYTE)((idx >> p) & 1 ? 0xFF : 0);
    blt(s, x0, y0, NULL, 0, 0, x1 - x0, y1 - y0, rop, pat, NULL);
}

static int uses_src(BYTE rop)
{
    return ((rop >> 2) ^ rop) & 0x33;
}

static int uses_pat(BYTE rop)
{
    return ((rop >> 4) ^ rop) & 0x0F;
}

/* BOOL BitBlt(lpDestDev, wDestX, wDestY, lpSrcDev, wSrcX, wSrcY, wXext,
   wYext, Rop3, lpPBrush, lpDrawMode) - VGREC: pops 32 bytes */
WORD EXPORT BitBlt(LPVOID lpDst, short dx, short dy, LPVOID lpSrc, short sx, short sy,
                   short w, short h, DWORD rop3, LPPBRUSH brush, LPDRAWMODE dm)
{
    SURF D, S;
    BYTE rop = (BYTE)(rop3 >> 16), pat[4][8];
    int hasS = uses_src(rop) != 0, scr;
    RECT r;
    if (!surf_get(&D, lpDst))
        return 0;
    if (hasS && !surf_get(&S, lpSrc))
        return 0;
    if (w <= 0 || h <= 0)
        return 1;
    /* cut to the destination surface (and the source's), moving the
       source with the destination */
    r.left = dx;
    r.top = dy;
    r.right = dx + w;
    r.bottom = dy + h;
    if (hasS) {
        if (sx < 0) {
            r.left -= sx;
            sx = 0;
        }
        if (sy < 0) {
            r.top -= sy;
            sy = 0;
        }
    }
    {
        int ox = r.left, oy = r.top;
        if (!clip_rect(&r, NULL, &D))
            return 1;
        if (hasS) {
            sx += r.left - ox;
            sy += r.top - oy;
            if (r.right - r.left > S.width - sx)
                r.right = r.left + S.width - sx;
            if (r.bottom - r.top > S.height - sy)
                r.bottom = r.top + S.height - sy;
            if (r.right <= r.left || r.bottom <= r.top)
                return 1;
        }
    }
    scr = D.screen || (hasS && S.screen);
    if (scr) {
        RECT u = r;
        if (hasS && S.screen) {
            if (sx < u.left)
                u.left = sx;
            if (sy < u.top)
                u.top = sy;
            if (sx + (r.right - r.left) > u.right)
                u.right = sx + (r.right - r.left);
            if (sy + (r.bottom - r.top) > u.bottom)
                u.bottom = sy + (r.bottom - r.top);
        }
        scr_begin(u.left, u.top, u.right, u.bottom);
    }
    if (uses_pat(rop))
        pattern_planes(brush, dm, D.planes == 1, pat, NULL, NULL);
    blt(&D, r.left, r.top, hasS ? &S : NULL, sx, sy, r.right - r.left, r.bottom - r.top,
        rop, uses_pat(rop) ? pat : NULL, dm);
    if (scr)
        scr_end();
    return 1;
}

/* WORD FastBorder(lpRect, wHorizBorderThick, wVertBorderThick, dwRasterOp,
   lpDestDev, lpPBrush, lpDrawMode, lpClipRect): a frame inside lpRect,
   the left and right edges wHoriz wide, the top and bottom wVert high, no
   pixel done twice (USER's tracking frames use PATINVERT). VGREC: pops
   28 bytes, returns FFFFh. */
WORD EXPORT FastBorder(LPRECT rc, short hw, short vh, DWORD rop3, LPVOID lpDev,
                       LPPBRUSH brush, LPDRAWMODE dm, LPRECT clip)
{
    SURF D;
    BYTE rop = (BYTE)(rop3 >> 16), pat[4][8];
    RECT parts[4], all;
    int i;
    if (!surf_get(&D, lpDev) || !rc)
        return 0;
    parts[0].left = rc->left; parts[0].right = rc->right;
    parts[0].top = rc->top; parts[0].bottom = rc->top + vh;
    parts[1].left = rc->left; parts[1].right = rc->right;
    parts[1].top = rc->bottom - vh; parts[1].bottom = rc->bottom;
    parts[2].left = rc->left; parts[2].right = rc->left + hw;
    parts[2].top = rc->top + vh; parts[2].bottom = rc->bottom - vh;
    parts[3].left = rc->right - hw; parts[3].right = rc->right;
    parts[3].top = rc->top + vh; parts[3].bottom = rc->bottom - vh;
    all = *rc;
    if (!clip_rect(&all, clip, &D))
        return 0xFFFF;
    if (D.screen)
        scr_begin(all.left, all.top, all.right, all.bottom);
    pattern_planes(brush, dm, D.planes == 1, pat, NULL, NULL);
    for (i = 0; i < 4; i++) {
        RECT r = parts[i];
        if (parts[1].top < parts[0].bottom && i == 1)
            r.top = parts[0].bottom;        /* thinner than two edges */
        if (clip_rect(&r, clip, &D))
            blt(&D, r.left, r.top, NULL, 0, 0, r.right - r.left, r.bottom - r.top, rop,
                uses_pat(rop) ? pat : NULL, dm);
    }
    if (D.screen)
        scr_end();
    return 0xFFFF;
}

/* pixels [x0, x1) of line y through rop with the pattern; smask >= 0: the
   source is that byte in every column (scan lines with a transparent
   hatch put the hatch's row there) */
void row_op(SURF *D, int y, int x0, int x1, BYTE rop, BYTE pat[4][8], int smask)
{
    int nb, b0, p, i;
    BYTE lmask, rmask;
    if (x1 <= x0)
        return;
    b0 = x0 >> 3;
    nb = ((x1 - 1) >> 3) - b0 + 1;
    lmask = (BYTE)(0xFF >> (x0 & 7));
    rmask = (BYTE)(0xFF << (7 - ((x1 - 1) & 7)));
    if (smask >= 0)
        for (i = 0; i < nb && i < ROWB; i++)
            srow[0][i] = (BYTE)smask;
    for (p = 0; p < D->planes; p++) {
        if (D->screen)
            scr_plane(p);
        if (nb > ROWB && smask >= 0) {
            /* longer than the buffer: in pieces */
            int a = x0, b;
            while (a < x1) {
                b = ((a >> 3) + ROWB - 1) << 3;
                if (b > x1)
                    b = x1;
                {
                    int nb2 = ((b - 1) >> 3) - (a >> 3) + 1;
                    rop_run(rop, surf_line(D, y, p) + (a >> 3), srow[0], pat[p][y & 7], nb2,
                            (BYTE)(0xFF >> (a & 7)), (BYTE)(0xFF << (7 - ((b - 1) & 7))));
                }
                a = b;
            }
            continue;
        }
        rop_run(rop, surf_line(D, y, p) + b0, smask >= 0 ? srow[0] : NULL, pat[p][y & 7], nb,
                lmask, rmask);
    }
}
