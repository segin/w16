/*
 * output.c - Output (polylines, scan lines), Pixel and ScanLR.
 *
 * The GDIINFO says what GDI may ask Output for: dpLines LC_POLYLINE and
 * LC_STYLED, dpPolygonals PC_SCANLINE, dpCurves none, so GDI draws
 * rectangles, ellipses, polygons and the rest itself with BitBlt, scan
 * lines and polylines (VGREC; ARCHITECTURE.md 11.2, 11.4). Anything else
 * returns -1 (DDK: "GDI simulates").
 */
#include "vga.h"

static SURF D;
static RECT clipr;

/* one pixel through rop (a ROP3 with the colour as the pattern) */
static void put_pixel(int x, int y, BYTE rop, int idx)
{
    BYTE m, v;
    BYTE __far *l;
    int p;
    if (x < clipr.left || x >= clipr.right || y < clipr.top || y >= clipr.bottom)
        return;
    m = (BYTE)(0x80 >> (x & 7));
    if (D.screen && (rop == 0xF0 || rop == 0x00 || rop == 0xFF)) {
        int c = rop == 0x00 ? 0 : rop == 0xFF ? 15 : idx;
        volatile BYTE __far *vl = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)y * 80 + (x >> 3));
        outw(0x3C4, 0x0F02);
        outw(0x3CE, (WORD)((c & 15) << 8) | 0x00);
        outw(0x3CE, 0x0003);
        outw(0x3CE, 0x0305);
        outw(0x3CE, 0xFF08);
        vga_latch(vl);
        *vl = m;
        scr_reset();
        return;
    }
    for (p = 0; p < D.planes; p++) {
        if (D.screen)
            scr_plane(p);
        l = surf_line(&D, y, p) + (x >> 3);
        v = *l;
        *l = (BYTE)((rop3_apply(rop, (idx >> p) & 1 ? 0xFF : 0, 0, v) & m) | (v & ~m));
    }
}

/* Styled lines as Windows 3.1's VGA.DRV draws them (VG5: every style,
   direction and slope, opaque and transparent, single lines, polylines of
   mixed directions in one Output call and separate LineTo calls). Each
   style is an 8-bit mask, its first bit first. A counter moves through a
   bit in 12 units: 3 a pixel along a line in x (|dx| > |dy|), 4 along y
   or at 45 degrees, so a bit lasts 4 or 3 pixels. Each Output call starts
   the counter at 0 for a first segment in x, -1 in y (the first bit has 4
   pixels either way). Through a polyline the mask and counter go on; at a
   turn from a segment in x to one in y the pixels left of the bit are
   kept (the counter becomes (4 c - 12) / 3, rounded down: the fit is the
   same for any rounding), at a turn from y to x the counter is kept. */
static const BYTE style_mask[5] = { 0xFF, 0xE7, 0xAA, 0xE4, 0xEA };
static int style_bit, style_err, style_step, style_first, style_prevx;
static BYTE style_m;

static void style_start(int s)
{
    style_m = s >= LS_DASHED && s <= LS_DASHDOTDOT ? style_mask[s] : 0xFF;
    style_bit = 0;
    style_first = 1;
}

/* a segment along x (or not) starts */
static void style_segment(int xmajor)
{
    if (style_first) {
        style_err = xmajor ? 0 : -1;
        style_first = 0;
    } else if (style_prevx && !xmajor) {
        int v = 4 * style_err - 12;
        style_err = v >= 0 ? v / 3 : -((-v + 2) / 3);
    }
    style_prevx = xmajor;
    style_step = xmajor ? 3 : 4;
}

/* the next pixel of a styled line: 1 on, 0 off */
static int style_next(void)
{
    int on = (style_m >> (7 - style_bit)) & 1;
    style_err += style_step;
    if (style_err >= 12) {
        style_err -= 12;
        style_bit = (style_bit + 1) & 7;
    }
    return on;
}

/* A line from (x0, y0) up to, not including, (x1, y1). Its pixels, as
   Windows 3.1's VGA.DRV's (VG5, every direction and slope): one per step
   along the longer axis (the y axis at 45 degrees), the other coordinate
   rounded to the nearest; exactly half way it goes to the side of the
   line's upper end (the start when y0 <= y1, else the end). */
static void line(int x0, int y0, int x1, int y1, BYTE rop, int fg, BYTE bgrop, int bg)
{
    long dx, dy, sx, sy, i, n, dmaj, dmin, rem = 0, cx, cy, cbase;
    int up, xmaj, wm3 = 0, fgc = 0, bgc = 0, cur_c = 0;
    if (x0 == x1 && y0 == y1)
        return;
    /* fast path for solid horizontal and vertical segments */
    if (style_m == 0xFF) {
        if (y0 == y1) {
            int hx0 = x0 < x1 ? x0 : x1 + 1;
            int hx1 = x0 < x1 ? x1 : x0 + 1;
            if (y0 < clipr.top || y0 >= clipr.bottom)
                return;
            if (hx0 < clipr.left)
                hx0 = clipr.left;
            if (hx1 > clipr.right)
                hx1 = clipr.right;
            if (hx0 >= hx1)
                return;
            if (D.screen && (rop == 0xF0 || rop == 0x00 || rop == 0xFF)) {
                scr_fill_solid(hx0, y0, hx1, y0 + 1, rop == 0x00 ? 0 : rop == 0xFF ? 15 : fg);
            } else {
                int b0 = hx0 >> 3, nb = ((hx1 - 1) >> 3) - b0 + 1, p;
                BYTE lm = (BYTE)(0xFF >> (hx0 & 7)), rm = (BYTE)(0xFF << (7 - ((hx1 - 1) & 7)));
                for (p = 0; p < D.planes; p++) {
                    if (D.screen)
                        scr_plane(p);
                    rop_run(rop, surf_line(&D, y0, p) + b0, NULL,
                            (BYTE)((fg >> p) & 1 ? 0xFF : 0), (WORD)nb, lm, rm);
                }
            }
            return;
        }
        if (x0 == x1) {
            int vy0 = y0 < y1 ? y0 : y1 + 1;
            int vy1 = y0 < y1 ? y1 : y0 + 1;
            BYTE m;
            WORD bx;
            if (x0 < clipr.left || x0 >= clipr.right)
                return;
            if (vy0 < clipr.top)
                vy0 = clipr.top;
            if (vy1 > clipr.bottom)
                vy1 = clipr.bottom;
            if (vy0 >= vy1)
                return;
            m = (BYTE)(0x80 >> (x0 & 7));
            bx = (WORD)(x0 >> 3);
            if (D.screen && (rop == 0xF0 || rop == 0x00 || rop == 0xFF)) {
                int c = rop == 0x00 ? 0 : rop == 0xFF ? 15 : fg, yy;
                volatile BYTE __far *vl = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)vy0 * 80 + bx);
                outw(0x3C4, 0x0F02);
                outw(0x3CE, (WORD)((c & 15) << 8) | 0x00);
                outw(0x3CE, 0x0F01);
                outw(0x3CE, 0x0003);
                outw(0x3CE, 0x0005);
                outw(0x3CE, (WORD)(m << 8) | 0x08);
                for (yy = vy0; yy < vy1; yy++, vl += 80) {
                    vga_latch(vl);
                    *vl = 0xFF;
                }
                scr_reset();
            } else {
                int p, yy;
                for (p = 0; p < D.planes; p++) {
                    BYTE pv = (BYTE)((fg >> p) & 1 ? 0xFF : 0);
                    if (D.screen)
                        scr_plane(p);
                    for (yy = vy0; yy < vy1; yy++) {
                        BYTE __far *l = surf_line(&D, yy, p) + bx;
                        BYTE v = *l;
                        *l = (BYTE)((rop3_apply(rop, pv, 0, v) & m) | (v & ~m));
                    }
                }
            }
            return;
        }
    }
    dx = (long)x1 - x0;
    dy = (long)y1 - y0;
    sx = dx < 0 ? -1L : 1L;
    sy = dy < 0 ? -1L : 1L;
    up = y0 > y1;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    xmaj = dx > dy;
    style_segment(xmaj);
    if (xmaj) {
        dmaj = dx;
        dmin = dy;
        cx = x0;
        cbase = y0;
    } else {
        dmaj = dy;
        dmin = dx;
        cy = y0;
        cbase = x0;
    }
    n = dmaj;
    if (D.screen && (rop == 0xF0 || rop == 0x00 || rop == 0xFF) &&
        (bg < 0 || bgrop == 0xF0 || bgrop == 0x00 || bgrop == 0xFF)) {
        wm3 = 1;
        fgc = rop == 0x00 ? 0 : rop == 0xFF ? 15 : fg;
        bgc = bg >= 0 ? (bgrop == 0x00 ? 0 : bgrop == 0xFF ? 15 : bg) : 0;
        cur_c = fgc;
        outw(0x3C4, 0x0F02);
        outw(0x3CE, (WORD)((cur_c & 15) << 8) | 0x00);
        outw(0x3CE, 0x0003);
        outw(0x3CE, 0x0305);
        outw(0x3CE, 0xFF08);
    }
    for (i = 0; i < n; i++) {
        /* rem / dmaj: the fraction past base, of i * dmin / dmaj */
        int bump = 2 * rem > dmaj || (2 * rem == dmaj && up);
        if (xmaj) {
            cy = bump ? cbase + sy : cbase;
        } else {
            cx = bump ? cbase + sx : cbase;
        }
        if (wm3) {
            int on = style_next();
            if (on || bg >= 0) {
                int px = (int)cx, py = (int)cy;
                if (cx == px && cy == py &&
                    px >= clipr.left && px < clipr.right &&
                    py >= clipr.top && py < clipr.bottom) {
                    int want = on ? fgc : bgc;
                    volatile BYTE __far *vl;
                    if (want != cur_c) {
                        outw(0x3CE, (WORD)((want & 15) << 8) | 0x00);
                        cur_c = want;
                    }
                    vl = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)py * 80 + (px >> 3));
                    vga_latch(vl);
                    *vl = (BYTE)(0x80 >> (px & 7));
                }
            }
        } else if (style_next()) {
            put_pixel((int)cx, (int)cy, rop, fg);
        } else if (bg >= 0) {
            put_pixel((int)cx, (int)cy, bgrop, bg);
        }
        if (xmaj)
            cx += sx;
        else
            cy += sy;
        rem += dmin;
        if (rem >= dmaj) {
            rem -= dmaj;
            cbase += xmaj ? sy : sx;
        }
    }
    if (wm3)
        scr_reset();
}

static int set_clip(LPRECT clip)
{
    clipr.left = 0;
    clipr.top = 0;
    clipr.right = D.width;
    clipr.bottom = D.height;
    return clip_rect(&clipr, clip, &D);
}

/* WORD Output(lpDestDev, wStyle, wCount, lpPoints, lpPPen, lpPBrush,
   lpDrawMode, lpClipRect) */
WORD EXPORT Output(LPVOID lpDev, WORD ostyle, WORD count, LPPOINT pts, LPPPEN pen,
                   LPPBRUSH brush, LPDRAWMODE dm, LPRECT clip)
{
    int i;
    if (!surf_get(&D, lpDev))
        return 0;
    switch (ostyle) {
    case OS_BEGINNSCAN:
    case OS_ENDNSCAN:
        return 1;
    case OS_POLYLINE: {
        BYTE rop, bgrop;
        int fg, bg = -1;
        if (!pen || !dm || count < 2 || pen->style == LS_NOLINE)
            return 1;
        if (!set_clip(clip))
            return 1;
        rop = rop2_to_rop3(dm->Rop2);
        fg = D.planes == 1 ? pen->mono * 15 : pen->color;
        bgrop = rop;
        if (dm->bkMode == OPAQUE && pen->style >= LS_DASHED && pen->style <= LS_DASHDOTDOT)
            bg = D.planes == 1 ? PC_MONO(dm->bkColor) * 15 : PC_INDEX(dm->bkColor);
        if (D.screen)
            scr_begin(clipr.left, clipr.top, clipr.right, clipr.bottom);
        style_start(pen->style);
        for (i = 0; i + 1 < (int)count; i++)
            line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, rop, fg, bgrop, bg);
        if (D.screen)
            scr_end();
        return 1;
    }
    case OS_SCANLINES: {
        BYTE pat[4][8], mask[8], transp, rop;
        int y, x0, x1, p, k;
        if (!dm || count < 2)
            return 1;
        if (!set_clip(clip))
            return 1;
        y = pts[0].y;
        if (y < clipr.top || y >= clipr.bottom)
            return 1;
        if (brush) {
            if (brush->flags & PBF_HOLLOW)
                return 1;
            pattern_planes(brush, dm, D.planes == 1, pat, mask, &transp);
        } else if (pen) {
            int c;
            if (pen->style == LS_NOLINE)
                return 1;
            c = D.planes == 1 ? pen->mono * 15 : pen->color;
            for (p = 0; p < 4; p++)
                for (k = 0; k < 8; k++)
                    pat[p][k] = (BYTE)((c >> p) & 1 ? 0xFF : 0);
            transp = 0;
        } else
            return 1;
        rop = rop2_to_rop3(dm->Rop2);
        if (transp) {
            /* a hatch with a transparent background: where the source
               (the hatch's mask) is 0 the destination stays */
            BYTE r = 0;
            int b;
            for (b = 0; b < 8; b++) {
                int pp = (b >> 2) & 1, ss = (b >> 1) & 1, dd = b & 1;
                if (ss ? (rop >> ((pp << 2) | dd)) & 1 : dd)
                    r |= (BYTE)(1 << b);
            }
            rop = r;
        }
        if (D.screen)
            scr_begin(clipr.left, y, clipr.right, y + 1);
        for (i = 1; i < (int)count; i++) {
            x0 = pts[i].x;
            x1 = pts[i].y;
            if (x0 > x1) {
                int t = x0;
                x0 = x1;
                x1 = t;
            }
            if (x0 < clipr.left)
                x0 = clipr.left;
            if (x1 > clipr.right)
                x1 = clipr.right;
            row_op(&D, y, x0, x1, rop, pat, transp ? mask[y & 7] : -1);
        }
        if (D.screen)
            scr_end();
        return 1;
    }
    }
    return (WORD)-1;
}

/* DWORD Pixel(lpDestDev, X, Y, dwPhysColor, lpDrawMode): set (lpDrawMode
   given: through its Rop2) or get the pixel */
DWORD EXPORT Pixel(LPVOID lpDev, short x, short y, DWORD pc, LPDRAWMODE dm)
{
    int p, idx = 0;
    BYTE m;
    if (!surf_get(&D, lpDev))
        return 0x80000000L;
    if (x < 0 || y < 0 || x >= D.width || y >= D.height)
        return 0x80000000L;
    clipr.left = clipr.top = 0;
    clipr.right = D.width;
    clipr.bottom = D.height;
    if (D.screen)
        scr_begin(x, y, x + 1, y + 1);
    if (dm) {
        int c = D.planes == 1 ? PC_MONO(pc) * 15 : PC_INDEX(pc);
        put_pixel(x, y, rop2_to_rop3(dm->Rop2), c);
        if (D.screen)
            scr_end();
        return 1;
    }
    m = (BYTE)(0x80 >> (x & 7));
    for (p = 0; p < D.planes; p++) {
        if (D.screen)
            scr_plane(p);
        if (surf_line(&D, y, p)[x >> 3] & m)
            idx |= 1 << p;
    }
    if (D.screen)
        scr_end();
    if (D.planes == 1)
        return MKPC(idx ? 15 : 0, idx);
    return MKPC(idx, rgb_mono(index_rgb(idx)));
}

/* the colour (index, or mono value) of a pixel */
static int pixel_of(int x, int y)
{
    int p, idx = 0;
    BYTE m = (BYTE)(0x80 >> (x & 7));
    for (p = 0; p < D.planes; p++) {
        if (D.screen)
            scr_plane(p);
        if (surf_line(&D, y, p)[x >> 3] & m)
            idx |= 1 << p;
    }
    return idx;
}

/* WORD ScanLR(lpDestDev, X, Y, dwPhysColor, Style): from (X, Y) left
   (style bit 1) or right, the first pixel of the colour (bit 0) or not of
   it; -1 if none, 8000h if (X, Y) is off the surface. UNVERIFIED: whether
   the start pixel itself counts (here it does). */
WORD EXPORT ScanLR(LPVOID lpDev, short x, short y, DWORD pc, WORD sstyle)
{
    int want, step, c;
    if (!surf_get(&D, lpDev))
        return 0x8000;
    if (x < 0 || y < 0 || x >= D.width || y >= D.height)
        return 0x8000;
    want = D.planes == 1 ? (PC_MONO(pc) ? 1 : 0) : PC_INDEX(pc);
    step = sstyle & 2 ? -1 : 1;
    if (D.screen)
        scr_begin(0, y, 640, y + 1);
    for (; x >= 0 && x < D.width; x += step) {
        c = pixel_of(x, y);
        if ((c == want) == ((sstyle & 1) != 0))
            break;
    }
    if (D.screen)
        scr_end();
    return x >= 0 && x < D.width ? (WORD)x : (WORD)-1;
}
