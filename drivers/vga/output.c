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
    for (p = 0; p < D.planes; p++) {
        if (D.screen)
            scr_plane(p);
        l = surf_line(&D, y, p) + (x >> 3);
        v = *l;
        *l = (BYTE)((rop3_apply(rop, (idx >> p) & 1 ? 0xFF : 0, 0, v) & m) | (v & ~m));
    }
}

/* the styles' on/off runs in pixels, starting "on" (UNVERIFIED: 3.1's
   own lengths and how they follow the line's direction) */
static const BYTE style_runs[5][6] = {
    { 0 },
    { 18, 6, 0 },                       /* dashed */
    { 3, 3, 0 },                        /* dotted */
    { 9, 6, 3, 6, 0 },                  /* dash dot */
    { 9, 3, 3, 3, 3, 3 }                /* dash dot dot */
};
static int style, style_i, style_left, style_on;

static void style_start(int s)
{
    style = s >= LS_DASHED && s <= LS_DASHDOTDOT ? s : 0;
    style_i = 0;
    style_left = style ? style_runs[style][0] : 0;
    style_on = 1;
}

/* the next pixel of a styled line: 1 on, 0 off */
static int style_next(void)
{
    int on;
    if (!style)
        return 1;
    on = style_on;
    if (--style_left <= 0) {
        style_i++;
        if (style_i >= 6 || !style_runs[style][style_i])
            style_i = 0;
        style_left = style_runs[style][style_i];
        style_on = !(style_i & 1);
    }
    return on;
}

/* a line from (x0, y0) up to, not including, (x1, y1): Bresenham
   (UNVERIFIED: 3.1's choice of pixel where the line passes between two) */
static void line(int x0, int y0, int x1, int y1, BYTE rop, int fg, BYTE bgrop, int bg)
{
    int dx = x1 - x0, dy = y1 - y0, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, i, n;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    if (dx >= dy) {
        n = dx;
        err = 2 * dy - dx;
        for (i = 0; i < n; i++) {
            if (style_next())
                put_pixel(x0, y0, rop, fg);
            else if (bg >= 0)
                put_pixel(x0, y0, bgrop, bg);
            if (err > 0) {
                y0 += sy;
                err -= 2 * dx;
            }
            err += 2 * dy;
            x0 += sx;
        }
    } else {
        n = dy;
        err = 2 * dx - dy;
        for (i = 0; i < n; i++) {
            if (style_next())
                put_pixel(x0, y0, rop, fg);
            else if (bg >= 0)
                put_pixel(x0, y0, bgrop, bg);
            if (err > 0) {
                x0 += sx;
                err -= 2 * dy;
            }
            err += 2 * dx;
            y0 += sy;
        }
    }
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
            int c = D.planes == 1 ? pen->mono * 15 : pen->color;
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
