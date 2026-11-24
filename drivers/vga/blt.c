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
   off + 8 i (pixels outside the line are 0); caller selects VGA plane */
static void fetch_raw(SURF *s, int y, int p, int off, int nb, BYTE *out)
{
    BYTE __far *l = surf_line(s, y, p);
    int i, idx, sh, wb = s->wbytes;
    BYTE b0, b1;
    idx = off >= 0 ? off >> 3 : -((-off + 7) >> 3);
    sh = off - idx * 8;
    if (!sh) {
        if (idx >= 0 && idx + nb <= wb) {
            memcpyf(out, l + idx, (WORD)nb);
            return;
        }
        for (i = 0; i < nb; i++, idx++)
            out[i] = idx >= 0 && idx < wb ? l[idx] : 0;
        return;
    }
    for (i = 0; i < nb; i++, idx++) {
        b0 = idx >= 0 && idx < wb ? l[idx] : 0;
        b1 = idx + 1 >= 0 && idx + 1 < wb ? l[idx + 1] : 0;
        out[i] = (BYTE)((b0 << sh) | (b1 >> (8 - sh)));
    }
}

/* solid colour fill on the VGA screen using Write Mode 0 Set/Reset + Map Mask 0x0F;
   [x0, x1) x [y0, y1) already clipped to 640x480 */
void scr_fill_solid(int x0, int y0, int x1, int y1, int idx)
{
    int b0, b1, nb, ib0, ib1, y;
    BYTE lmask, rmask;
    volatile BYTE __far *vl;
    if (x0 >= x1 || y0 >= y1)
        return;
    b0 = x0 >> 3;
    b1 = (x1 - 1) >> 3;
    nb = b1 - b0 + 1;
    lmask = (BYTE)(0xFF >> (x0 & 7));
    rmask = (BYTE)(0xFF << (7 - ((x1 - 1) & 7)));
    if (nb == 1) {
        lmask &= rmask;
        rmask = 0xFF;
    }
    outw(0x3C4, 0x0F02);                            /* map mask: all 4 planes */
    outw(0x3CE, (WORD)((idx & 15) << 8) | 0x00);    /* set/reset: idx */
    outw(0x3CE, 0x0F01);                            /* enable set/reset: all 4 */
    outw(0x3CE, 0x0003);                            /* rotate 0, replace */
    outw(0x3CE, 0x0005);                            /* write mode 0, read mode 0 */
    if (lmask != 0xFF) {
        outw(0x3CE, (WORD)(lmask << 8) | 0x08);
        vl = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)y0 * 80 + b0);
        for (y = y0; y < y1; y++, vl += 80) {
            vga_latch(vl);
            *vl = 0xFF;
        }
    }
    ib0 = lmask == 0xFF ? b0 : b0 + 1;
    ib1 = rmask == 0xFF ? b1 : b1 - 1;
    if (ib0 <= ib1) {
        WORD cnt = (WORD)(ib1 - ib0 + 1);
        BYTE __far *dl = (BYTE __far *)MK_FP(vga_sel, (WORD)y0 * 80 + ib0);
        outw(0x3CE, 0xFF08);
        for (y = y0; y < y1; y++, dl += 80)
            memsetf(dl, 0xFF, cnt);
    }
    if (nb > 1 && rmask != 0xFF) {
        outw(0x3CE, (WORD)(rmask << 8) | 0x08);
        vl = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)y0 * 80 + b1);
        for (y = y0; y < y1; y++, vl += 80) {
            vga_latch(vl);
            *vl = 0xFF;
        }
    }
    scr_reset();
}

static int solid_pat_idx(BYTE rop, BYTE pat[4][8], int *idx)
{
    int p, y, c = 0;
    if (rop == 0x00) {
        *idx = 0;
        return 1;
    }
    if (rop == 0xFF) {
        *idx = 15;
        return 1;
    }
    if (rop != 0xF0)
        return 0;
    if (!pat) {
        *idx = 0;
        return 1;
    }
    for (p = 0; p < 4; p++) {
        BYTE v = pat[p][0];
        if (v != 0 && v != 0xFF)
            return 0;
        for (y = 1; y < 8; y++)
            if (pat[p][y] != v)
                return 0;
        if (v)
            c |= 1 << p;
    }
    *idx = c;
    return 1;
}

/* byte-aligned screen-to-screen SRCCOPY via VGA Write Mode 1 (32-bit latch copy) */
static int scr_copy_aligned(int dx, int dy, int sx, int sy, int w, int h)
{
    int x0 = dx >> 3, sx0 = sx >> 3;
    int nb = ((dx + w - 1) >> 3) - x0 + 1;
    BYTE lmask = (BYTE)(0xFF >> (dx & 7));
    BYTE rmask = (BYTE)(0xFF << (7 - ((dx + w - 1) & 7)));
    int ib0, ib1, icnt, overlap, ystart, ystep, yi, y, i, p;
    if (nb == 1) {
        lmask &= rmask;
        rmask = 0xFF;
    }
    ib0 = lmask == 0xFF ? 0 : 1;
    ib1 = rmask == 0xFF ? nb : nb - 1;
    icnt = ib1 > ib0 ? ib1 - ib0 : 0;
    overlap = !(dy + h <= sy || sy + h <= dy || dx + w <= sx || sx + w <= dx);
    if (!icnt || (overlap && sx != dx && (lmask != 0xFF || rmask != 0xFF)))
        return 0;
    if (dy > sy) {
        ystart = dy + h - 1;
        ystep = -1;
    } else {
        ystart = dy;
        ystep = 1;
    }
    outw(0x3C4, 0x0F02);                /* map mask: all 4 planes */
    outw(0x3CE, 0x0105);                /* write mode 1: 32-bit latch copy */
    y = ystart;
    for (yi = 0; yi < h; yi++, y += ystep) {
        int srcy = sy + (y - dy);
        volatile BYTE __far *d = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)y * 80 + x0 + ib0);
        volatile BYTE __far *s = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)srcy * 80 + sx0 + ib0);
        if (dx > sx) {
            for (i = icnt - 1; i >= 0; i--) {
                BYTE t = s[i];
                d[i] = t;
            }
        } else {
            for (i = 0; i < icnt; i++) {
                BYTE t = s[i];
                d[i] = t;
            }
        }
    }
    scr_reset();
    if (lmask != 0xFF) {
        for (p = 0; p < 4; p++) {
            scr_plane(p);
            y = ystart;
            for (yi = 0; yi < h; yi++, y += ystep) {
                int srcy = sy + (y - dy);
                BYTE __far *d = (BYTE __far *)MK_FP(vga_sel, (WORD)y * 80 + x0);
                BYTE __far *s = (BYTE __far *)MK_FP(vga_sel, (WORD)srcy * 80 + sx0);
                *d = (BYTE)((*s & lmask) | (*d & ~lmask));
            }
        }
    }
    if (nb > 1 && rmask != 0xFF) {
        int rx = x0 + nb - 1, srx = sx0 + nb - 1;
        for (p = 0; p < 4; p++) {
            scr_plane(p);
            y = ystart;
            for (yi = 0; yi < h; yi++, y += ystep) {
                int srcy = sy + (y - dy);
                BYTE __far *d = (BYTE __far *)MK_FP(vga_sel, (WORD)y * 80 + rx);
                BYTE __far *s = (BYTE __far *)MK_FP(vga_sel, (WORD)srcy * 80 + srx);
                *d = (BYTE)((*s & rmask) | (*d & ~rmask));
            }
        }
    }
    return 1;
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
    int nb, x0, y, ystart, yi, ystep, p, i, maxw, c;
    BYTE lmask, rmask;
    int fgi = dm ? PC_INDEX(dm->TextColor) : 0, bgi = dm ? PC_INDEX(dm->bkColor) : 15;
    if (D->planes == 1 && dm) {
        fgi = PC_MONO(dm->TextColor) * 15;
        bgi = PC_MONO(dm->bkColor) * 15;
    }
    /* fast path 1: solid fill on the VGA screen (Write Mode 0 Set/Reset) */
    if (D->screen && !S && solid_pat_idx(rop, pat, &c)) {
        scr_fill_solid(dx, dy, dx + w, dy + h, c);
        return;
    }
    /* fast path 2: byte-aligned screen-to-screen SRCCOPY (Write Mode 1 latch copy) */
    if (D->screen && S && S->screen && rop == 0xCC && (dx & 7) == (sx & 7)) {
        if (scr_copy_aligned(dx, dy, sx, sy, w, h))
            return;
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
    ystart = dy;
    ystep = 1;
    if (S && S->sel == D->sel && S->off == D->off && S->screen == D->screen && dy > sy) {
        ystart = dy + h - 1;
        ystep = -1;
    }
    if (!S) {
        /* no source: hoist VGA plane loop outside scanlines */
        for (p = 0; p < D->planes; p++) {
            if (D->screen)
                scr_plane(p);
            for (y = dy; y < dy + h; y++)
                rop_run(rop, surf_line(D, y, p) + x0, NULL, pat ? pat[p][y & 7] : 0,
                        nb, lmask, rmask);
        }
        return;
    }
    if (S->planes == D->planes) {
        /* matching planes (including overlapping screen-to-screen): planes are
           independent, so hoist the plane loop outside y */
        int off = sx - (dx & 7);
        for (p = 0; p < D->planes; p++) {
            if (D->screen || S->screen)
                scr_plane(p);
            y = ystart;
            for (yi = 0; yi < h; yi++, y += ystep) {
                int srcy = sy + (y - dy);
                fetch_raw(S, srcy, p, off, nb, srow[0]);
                rop_run(rop, surf_line(D, y, p) + x0, srow[0], pat ? pat[p][y & 7] : 0,
                        nb, lmask, rmask);
            }
        }
        return;
    }
    if (S->planes == 1) {
        /* mono -> colour: 1 bits background, 0 bits text colour; hoist plane loop */
        int off = sx - (dx & 7);
        for (p = 0; p < 4; p++) {
            int bg1 = (bgi >> p) & 1, fg1 = (fgi >> p) & 1;
            if (D->screen)
                scr_plane(p);
            if (bg1 == fg1) {
                memsetf(srow[0], bg1 ? 0xFF : 0, (WORD)nb);
                for (y = dy; y < dy + h; y++)
                    rop_run(rop, surf_line(D, y, p) + x0, srow[0], pat ? pat[p][y & 7] : 0,
                            nb, lmask, rmask);
            } else {
                for (y = dy; y < dy + h; y++) {
                    int srcy = sy + (y - dy);
                    fetch_raw(S, srcy, 0, off, nb, tmp[0]);
                    if (!bg1) {
                        for (i = 0; i < nb; i++)
                            srow[0][i] = (BYTE)~tmp[0][i];
                        rop_run(rop, surf_line(D, y, p) + x0, srow[0], pat ? pat[p][y & 7] : 0,
                                nb, lmask, rmask);
                    } else {
                        rop_run(rop, surf_line(D, y, p) + x0, tmp[0], pat ? pat[p][y & 7] : 0,
                                nb, lmask, rmask);
                    }
                }
            }
        }
        return;
    }
    /* colour -> mono: the background colour's pixels 1 */
    {
        int bk = dm ? PC_INDEX(dm->bkColor) : 15;
        int off = sx - (dx & 7);
        for (y = dy; y < dy + h; y++) {
            int srcy = sy + (y - dy);
            for (p = 0; p < 4; p++) {
                if (S->screen)
                    scr_plane(p);
                fetch_raw(S, srcy, p, off, nb, tmp[p]);
            }
            for (i = 0; i < nb; i++) {
                BYTE diff = 0;
                for (p = 0; p < 4; p++)
                    diff |= (BYTE)(tmp[p][i] ^ ((bk >> p) & 1 ? 0xFF : 0));
                srow[0][i] = (BYTE)~diff;
            }
            rop_run(rop, surf_line(D, y, 0) + x0, srow[0], pat ? pat[0][y & 7] : 0,
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
    if (s->screen && (rop == 0xF0 || rop == 0x00 || rop == 0xFF)) {
        scr_fill_solid(x0, y0, x1, y1, rop == 0x00 ? 0 : rop == 0xFF ? 15 : idx);
        return;
    }
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
    {
        long dl = dx, dt = dy, dr = (long)dx + w, db = (long)dy + h;
        long sl = sx, st = sy;
        if (hasS) {
            if (sl < 0) {
                dl -= sl;
                sl = 0;
            }
            if (st < 0) {
                dt -= st;
                st = 0;
            }
        }
        if (dl < 0) {
            sl -= dl;
            dl = 0;
        }
        if (dt < 0) {
            st -= dt;
            dt = 0;
        }
        if (dr > D.width)
            dr = D.width;
        if (db > D.height)
            db = D.height;
        if (hasS) {
            if (sl >= S.width || st >= S.height)
                return 1;
            if (dr - dl > S.width - sl)
                dr = dl + (S.width - sl);
            if (db - dt > S.height - st)
                db = dt + (S.height - st);
        }
        if (dl >= dr || dt >= db)
            return 1;
        r.left = (short)dl;
        r.top = (short)dt;
        r.right = (short)dr;
        r.bottom = (short)db;
        sx = (short)sl;
        sy = (short)st;
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
    all = *rc;
    if (!clip_rect(&all, clip, &D))
        return 0xFFFF;
    parts[0].left = rc->left; parts[0].right = rc->right;
    parts[0].top = rc->top; parts[0].bottom = rc->top + vh;
    if (parts[0].bottom > rc->bottom)
        parts[0].bottom = rc->bottom;
    parts[1].left = rc->left; parts[1].right = rc->right;
    parts[1].top = rc->bottom - vh; parts[1].bottom = rc->bottom;
    parts[2].left = rc->left; parts[2].right = rc->left + hw;
    if (parts[2].right > rc->right)
        parts[2].right = rc->right;
    parts[2].top = rc->top + vh; parts[2].bottom = rc->bottom - vh;
    parts[3].left = rc->right - hw; parts[3].right = rc->right;
    parts[3].top = rc->top + vh; parts[3].bottom = rc->bottom - vh;
    if (D.screen)
        scr_begin(all.left, all.top, all.right, all.bottom);
    pattern_planes(brush, dm, D.planes == 1, pat, NULL, NULL);
    for (i = 0; i < 4; i++) {
        RECT r = parts[i];
        if (i == 1 && r.top < parts[0].bottom)
            r.top = parts[0].bottom;        /* thinner than two edges */
        if (i == 3 && r.left < parts[2].right)
            r.left = parts[2].right;
        if (clip_rect(&r, &all, &D))
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
    if (D->screen && smask < 0) {
        if (rop == 0x00) {
            scr_fill_solid(x0, y, x1, y + 1, 0);
            return;
        }
        if (rop == 0xFF) {
            scr_fill_solid(x0, y, x1, y + 1, 15);
            return;
        }
        if (rop == 0xF0) {
            int c = 0, solid = 1, ry = y & 7;
            for (p = 0; p < 4; p++) {
                BYTE v = pat[p][ry];
                if (v != 0 && v != 0xFF) {
                    solid = 0;
                    break;
                }
                if (v)
                    c |= 1 << p;
            }
            if (solid) {
                scr_fill_solid(x0, y, x1, y + 1, c);
                return;
            }
        }
    }
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
            long a = x0, b;
            while (a < x1) {
                b = ((a >> 3) + ROWB - 1) << 3;
                if (b > x1)
                    b = x1;
                {
                    int nb2 = (int)(((b - 1) >> 3) - (a >> 3) + 1);
                    rop_run(rop, surf_line(D, y, p) + (int)(a >> 3), srow[0], pat[p][y & 7],
                            (WORD)nb2, (BYTE)(0xFF >> (a & 7)),
                            (BYTE)(0xFF << (7 - ((b - 1) & 7))));
                }
                a = b;
            }
            continue;
        }
        rop_run(rop, surf_line(D, y, p) + b0, smask >= 0 ? srow[0] : NULL, pat[p][y & 7], nb,
                lmask, rmask);
    }
}
