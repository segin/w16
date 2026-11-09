/*
 * draw.c - lines and shapes.
 *
 * VGA.DRV draws polylines (also styled ones: dpLines LC_POLYLINE |
 * LC_STYLED) and scan lines (dpPolygonals PC_SCANLINE) only, so GDI breaks
 * every figure into those. Observed on 3.1 (GD3 under GD1, tests/gdi):
 *  - LineTo: one OS_POLYLINE of 2 points, lpClipRect = the line's box
 *    with right and bottom one further (the driver does not draw the last
 *    point); Polyline likewise with all points.
 *  - Rectangle with a pen and R2_COPYPEN: the inside (left+1, top+1 to
 *    right-1, bottom-1) is a driver BitBlt PATCOPY with the brush, then the
 *    border is a closed 5-point OS_POLYLINE (left,top)-(right-1,top)-
 *    (right-1,bottom-1)-(left,bottom-1)-(left,top), clipped to the box. With
 *    the null pen the inside is left, top to right-1, bottom-1. With another
 *    ROP2 the inside is scan lines.
 *  - Polygon: OS_BEGINNSCAN, one OS_SCANLINES per row from the top vertex
 *    to the row above the bottom one (pen NULL, the brush; spans as pairs,
 *    lpClipRect NULL: GDI clips), OS_ENDNSCAN, then the closed outline as a
 *    polyline clipped to the box. Span ends are the edges' x at the row's
 *    y rounded to the nearest integer, [left, right) (checked against the
 *    ALTERNATE and WINDING star of GD3).
 *  - Ellipse: scan lines for the inside, then the outline as one polyline
 *    of many points. UNVERIFIED: 3.1's ellipse points; here the ellipse
 *    equation, so ellipses, round rectangles, arcs, pies and chords differ
 *    from 3.1 by a few pixels.
 *  - Wide pens: drawn as filled shapes with the pen (scan lines with the
 *    physical pen, no brush), UNVERIFIED geometry.
 */
#include "gdi.h"

#define R2_COPYPEN 13

/* ---- output helpers (screen coordinates) ---- */

static int out_polyline(NPDC dc, POINT __far *p, int n)
{
    DRVOUTPUT fn = (DRVOUTPUT)dc->dev->fn[DRV_OUTPUT];
    LPVOID pen = dc_ppen(dc);
    NPRGN rao = dc_rao(dc);
    RECT box;
    WORD k;
    int i;
    if (n < 2 || !pen)
        return 0;
    box.left = box.right = p[0].x;
    box.top = box.bottom = p[0].y;
    for (i = 1; i < n; i++) {
        if (p[i].x < box.left) box.left = p[i].x;
        if (p[i].x > box.right) box.right = p[i].x;
        if (p[i].y < box.top) box.top = p[i].y;
        if (p[i].y > box.bottom) box.bottom = p[i].y;
    }
    box.right++;
    box.bottom++;
    for (k = 0; k < rao->n; k++) {
        RECT c = rao->r[k];
        if (c.left < box.left) c.left = box.left;
        if (c.top < box.top) c.top = box.top;
        if (c.right > box.right) c.right = box.right;
        if (c.bottom > box.bottom) c.bottom = box.bottom;
        if (c.left >= c.right || c.top >= c.bottom)
            continue;
        fn(dc->lpPDevice, OS_POLYLINE, n, p, pen, 0, &dc->dm, &c);
        rao = rgn_ptr(dc->hRao);
    }
    dc_update_bounds(dc, box.left, box.top, box.right, box.bottom);
    return 1;
}

/* a span list: rows of [x0, x1) pairs, filled with the brush (or the pen) */
typedef struct {
    NPDC dc;
    int usePen;
    int begun;
    POINT pts[1 + 2 * 64];
    int npts;
} SPANS;

static void spans_begin(SPANS *s, NPDC dc, int usePen)
{
    s->dc = dc;
    s->usePen = usePen;
    s->begun = 0;
    s->npts = 0;
}

static void spans_flush_row(SPANS *s)
{
    NPDC dc = s->dc;
    DRVOUTPUT fn = (DRVOUTPUT)dc->dev->fn[DRV_OUTPUT];
    LPVOID pen = s->usePen ? dc_ppen(dc) : 0, br = s->usePen ? 0 : dc_pbrush(dc);
    if (s->npts <= 1)
        return;
    if (!s->begun) {
        fn(dc->lpPDevice, OS_BEGINNSCAN, 0, 0, pen, br, &dc->dm, 0);
        s->begun = 1;
    }
    fn(dc->lpPDevice, OS_SCANLINES, s->npts, s->pts, pen, br, &dc->dm, 0);
    s->npts = 0;
}

/* one row's spans, clipped to the composite region */
static void spans_row(SPANS *s, int y, int *xs, int n)
{
    NPDC dc = s->dc;
    NPRGN rao = rgn_ptr(dc->hRao);
    WORD k;
    int i;
    s->npts = 0;
    for (k = 0; k < rao->n; k++) {
        NPRECT c = &rao->r[k];
        if (y < c->top || y >= c->bottom)
            continue;
        for (i = 0; i + 1 < n; i += 2) {
            int a = max(xs[i], c->left), b = min(xs[i + 1], c->right);
            if (a >= b && !(xs[i] == xs[i + 1] && n == 2 && rao->n == 1))
                continue;
            if (s->npts >= 1 + 2 * 64 - 1)
                break;
            if (!s->npts) {
                s->pts[0].x = 0;
                s->pts[0].y = y;
                s->npts = 1;
            }
            s->pts[s->npts].x = a;
            s->pts[s->npts].y = b;
            s->npts++;
            dc_update_bounds(dc, a, y, b, y + 1);
        }
    }
    spans_flush_row(s);
}

static void spans_end(SPANS *s)
{
    NPDC dc = s->dc;
    if (s->begun)
        ((DRVOUTPUT)dc->dev->fn[DRV_OUTPUT])(dc->lpPDevice, OS_ENDNSCAN, 0, 0, 0, 0, &dc->dm, 0);
}

static int brush_hollow(NPDC dc)
{
    BRUSHOBJ NEAR *b = obj_get(dc->hBrush, BRUSH_MAGIC);
    return !b || b->lb.lbStyle == 1;
}

static PENOBJ NEAR *pen_of(NPDC dc)
{
    return obj_get(dc->hPen, PEN_MAGIC);
}

static int pen_null(NPDC dc)
{
    PENOBJ NEAR *p = pen_of(dc);
    return !p || p->lp.lopnStyle == 5;
}

static int pen_width(NPDC dc)
{
    PENOBJ NEAR *p = pen_of(dc);
    int w;
    if (!p)
        return 1;
    w = dc_scale_x(dc, p->lp.lopnWidth.x);
    if (w < 0)
        w = -w;
    return w <= 1 ? 1 : w;
}

/* points from logical to screen coordinates, in a local buffer */
static HLOCAL to_screen(NPDC dc, const POINT __far *p, int n, int close)
{
    HLOCAL h = LocalAlloc(LMEM_FIXED, (n + 1) * sizeof(POINT));
    POINT NEAR *q;
    int i;
    if (!h)
        return 0;
    q = (POINT NEAR *)h;
    for (i = 0; i < n; i++)
        q[i] = p[i];
    dc_lp_to_dp(dc, (int __far *)(POINT __far *)q, n);
    for (i = 0; i < n; i++) {
        q[i].x += dc->dcOrg.x;
        q[i].y += dc->dcOrg.y;
    }
    if (close)
        q[n] = q[0];
    return h;
}

/* ---- polygons: spans by edge crossings at each row ---- */

static int round_div(long num, long den)
{
    /* nearest integer of num / den (den > 0) */
    long q = num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
    return (int)q;
}

/* fill a polygon (screen coordinates), rows top..bottom-1 */
static void fill_polygon(SPANS *s, POINT NEAR *p, int n, int winding)
{
    int ymin = p[0].y, ymax = p[0].y, y, i, j;
    int xs[2 * 64], dirs[2 * 64];
    for (i = 1; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    for (y = ymin; y < ymax; y++) {
        int m = 0, out[2 * 64], no = 0;
        for (i = 0; i < n && m < 2 * 64; i++) {
            POINT a = p[i], b = p[(i + 1) % n];
            int d = 1;
            if (a.y == b.y)
                continue;
            if (a.y > b.y) {
                POINT t = a;
                a = b;
                b = t;
                d = -1;
            }
            if (y < a.y || y >= b.y)
                continue;
            xs[m] = a.x + round_div((long)(y - a.y) * (b.x - a.x), b.y - a.y);
            dirs[m] = d;
            m++;
        }
        /* sort by x */
        for (i = 1; i < m; i++)
            for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t;
                t = dirs[j]; dirs[j] = dirs[j - 1]; dirs[j - 1] = t;
            }
        if (!winding) {
            for (i = 0; i + 1 < m; i += 2) {
                out[no++] = xs[i];
                out[no++] = xs[i + 1];
            }
        } else {
            int w = 0;
            for (i = 0; i < m; i++) {
                int was = w;
                w += dirs[i];
                if (!was && w)
                    out[no++] = xs[i];
                else if (was && !w)
                    out[no++] = xs[i];
            }
        }
        spans_row(s, y, out, no);
    }
}

/* ---- ellipses: the inside and the outline ---- */

/* x half-width of the ellipse in box l..r (exclusive) at row y */
static void ellipse_row(int l, int t, int r, int b, int y, int *x0, int *x1)
{
    /* centre and radii in half pixels: UNVERIFIED (3.1's own points) */
    long a2 = (long)(r - l - 1), b2 = (long)(b - t - 1);
    long cy2 = (long)(t + b - 1);          /* 2 * centre y */
    long dy = 2L * y - cy2;                /* 2 * (y - cy) */
    long num, lo, hi, mid;
    if (b2 <= 0 || a2 <= 0) {
        *x0 = l;
        *x1 = r;
        return;
    }
    /* largest dx (in half pixels) with (dx/a2)^2 + (dy/b2)^2 <= 1 */
    num = b2 * b2 - dy * dy;
    if (num < 0) {
        *x0 = *x1 = (l + r) / 2;
        return;
    }
    /* lo = floor(a2 * sqrt(num) / b2), in 32-bit integers */
    {
        unsigned long lim = (unsigned long)(num / b2 * a2 + num % b2 * a2 / b2);
        lim = lim / b2 * a2 + lim % b2 * a2 / b2;
        lo = 0;
        hi = a2;
        while (lo < hi) {
            mid = (lo + hi + 1) / 2;
            if ((unsigned long)mid * mid <= lim)
                lo = mid;
            else
                hi = mid - 1;
        }
    }
    *x0 = (int)((l + r - 1 - lo + 1) / 2);
    *x1 = (int)((l + r - 1 + lo) / 2 + 1);
}

static void ellipse_fill(SPANS *s, int l, int t, int r, int b, int inset)
{
    int y;
    for (y = t + inset; y < b - inset; y++) {
        int xs[2];
        ellipse_row(l, t, r, b, y, &xs[0], &xs[1]);
        xs[0] += inset;
        xs[1] -= inset;
        if (xs[0] < xs[1])
            spans_row(s, y, xs, 2);
    }
}

/* the outline as a closed polyline through the edge pixels */
static void ellipse_outline(NPDC dc, int l, int t, int r, int b)
{
    int h = b - t, y, n = 0;
    HLOCAL hp = LocalAlloc(LMEM_FIXED, (4 * h + 8) * sizeof(POINT));
    POINT NEAR *p;
    if (!hp)
        return;
    p = (POINT NEAR *)hp;
    for (y = t; y < b; y++) {                  /* right side, down */
        int x0, x1;
        ellipse_row(l, t, r, b, y, &x0, &x1);
        p[n].x = x1 - 1;
        p[n].y = y;
        n++;
    }
    for (y = b - 1; y >= t; y--) {             /* left side, up */
        int x0, x1;
        ellipse_row(l, t, r, b, y, &x0, &x1);
        p[n].x = x0;
        p[n].y = y;
        n++;
    }
    p[n++] = p[0];
    out_polyline(dc, (POINT __far *)p, n);
    LocalFree(hp);
}

/* ---- API ---- */

static void dev_box(NPDC dc, int l, int t, int r, int b, RECT *o)
{
    int p[4];
    p[0] = l;
    p[1] = t;
    p[2] = r;
    p[3] = b;
    dc_lp_to_dp(dc, p, 2);
    o->left = min(p[0], p[2]) + dc->dcOrg.x;
    o->right = max(p[0], p[2]) + dc->dcOrg.x;
    o->top = min(p[1], p[3]) + dc->dcOrg.y;
    o->bottom = max(p[1], p[3]) + dc->dcOrg.y;
}

BOOL GDIAPI LineTo(HDC hdc, int x, int y)                         /* GDI.19 */
{
    NPDC dc = dc_out(hdc);
    POINT p[2];
    if (!dc)
        return FALSE;
    p[0] = dc->cur;
    p[1].x = x;
    p[1].y = y;
    dc->cur = p[1];
    if (pen_null(dc))
        return TRUE;
    dc_lp_to_dp(dc, (int __far *)p, 2);
    p[0].x += dc->dcOrg.x;
    p[0].y += dc->dcOrg.y;
    p[1].x += dc->dcOrg.x;
    p[1].y += dc->dcOrg.y;
    out_polyline(dc, p, 2);
    return TRUE;
}

BOOL GDIAPI Polyline(HDC hdc, const POINT __far *pts, int n)      /* GDI.37 */
{
    NPDC dc = dc_out(hdc);
    HLOCAL h;
    if (!dc || n < 2)
        return FALSE;
    if (pen_null(dc))
        return TRUE;
    h = to_screen(dc, pts, n, 0);
    if (!h)
        return FALSE;
    dc = dc_get(hdc);
    out_polyline(dc, (POINT __far *)(POINT NEAR *)h, n);
    LocalFree(h);
    return TRUE;
}

/* the frame of a box with a wide pen: four filled bands */
static void wide_frame(NPDC dc, RECT *r, int w)
{
    SPANS s;
    int y, in = w / 2, out_ = (w - 1) / 2;
    int L = r->left - in, T = r->top - in, R = r->right - 1 + out_ + 1, B = r->bottom - 1 + out_ + 1;
    spans_begin(&s, dc, 1);
    for (y = T; y < B; y++) {
        int xs[4];
        if (y < T + w || y >= B - w) {
            xs[0] = L;
            xs[1] = R;
            spans_row(&s, y, xs, 2);
        } else {
            xs[0] = L;
            xs[1] = L + w;
            xs[2] = R - w;
            xs[3] = R;
            spans_row(&s, y, xs, 4);
        }
    }
    spans_end(&s);
}

BOOL GDIAPI Rectangle(HDC hdc, int l, int t, int r, int b)        /* GDI.27 */
{
    NPDC dc = dc_out(hdc);
    RECT rc;
    int nopen, w;
    if (!dc)
        return FALSE;
    dev_box(dc, l, t, r, b, &rc);
    nopen = pen_null(dc);
    w = nopen ? 0 : pen_width(dc);
    if (!nopen && pen_of(dc)->lp.lopnStyle == 6 && w > 1) {   /* PS_INSIDEFRAME */
        rc.left += w / 2;
        rc.top += w / 2;
        rc.right -= (w - 1) / 2;
        rc.bottom -= (w - 1) / 2;
    }
    if (!brush_hollow(dc)) {
        RECT in = rc;
        if (!nopen) {
            in.left++;
            in.top++;
        }
        in.right--;
        in.bottom--;
        if (in.left < in.right && in.top < in.bottom) {
            if (dc->dm.Rop2 == R2_COPYPEN) {
                NPRGN rao = dc_rao(dc);
                WORD k;
                DRVBITBLT fn = (DRVBITBLT)dc->dev->fn[DRV_BITBLT];
                LPVOID br = dc_pbrush(dc);
                for (k = 0; k < rao->n; k++) {
                    RECT c = rao->r[k];
                    if (c.left < in.left) c.left = in.left;
                    if (c.top < in.top) c.top = in.top;
                    if (c.right > in.right) c.right = in.right;
                    if (c.bottom > in.bottom) c.bottom = in.bottom;
                    if (c.left < c.right && c.top < c.bottom)
                        fn(dc->lpPDevice, c.left, c.top, 0, 0, 0, c.right - c.left,
                           c.bottom - c.top, 0x00F00021L, br, &dc->dm);
                    rao = rgn_ptr(dc->hRao);
                }
                dc_update_bounds(dc, in.left, in.top, in.right, in.bottom);
            } else {
                SPANS s;
                int y;
                spans_begin(&s, dc, 0);
                for (y = in.top; y < in.bottom; y++) {
                    int xs[2];
                    xs[0] = in.left;
                    xs[1] = in.right;
                    spans_row(&s, y, xs, 2);
                }
                spans_end(&s);
            }
        }
    }
    if (!nopen) {
        if (w > 1)
            wide_frame(dc, &rc, w);
        else {
            POINT p[5];
            p[0].x = rc.left;      p[0].y = rc.top;
            p[1].x = rc.right - 1; p[1].y = rc.top;
            p[2].x = rc.right - 1; p[2].y = rc.bottom - 1;
            p[3].x = rc.left;      p[3].y = rc.bottom - 1;
            p[4] = p[0];
            out_polyline(dc, p, 5);
        }
    }
    return TRUE;
}

static BOOL polygon(HDC hdc, const POINT __far *pts, int n, int close_outline)
{
    NPDC dc = dc_out(hdc);
    HLOCAL h;
    POINT NEAR *q;
    if (!dc || n < 2)
        return FALSE;
    h = to_screen(dc, pts, n, 1);
    if (!h)
        return FALSE;
    dc = dc_get(hdc);
    q = (POINT NEAR *)h;
    if (!brush_hollow(dc) && n > 2) {
        SPANS s;
        spans_begin(&s, dc, 0);
        fill_polygon(&s, q, n, dc->polyFill == 2);
        spans_end(&s);
    }
    if (!pen_null(dc) && close_outline)
        out_polyline(dc, (POINT __far *)q, n + 1);
    LocalFree(h);
    return TRUE;
}

BOOL GDIAPI Polygon(HDC hdc, const POINT __far *pts, int n)       /* GDI.36 */
{
    return polygon(hdc, pts, n, 1);
}

BOOL GDIAPI PolyPolygon(HDC hdc, const POINT __far *pts, const int __far *counts, int n) /* GDI.450 */
{
    int i;
    /* UNVERIFIED: 3.1 fills the polygons together (one fill rule) */
    for (i = 0; i < n; i++) {
        polygon(hdc, pts, counts[i], 1);
        pts += counts[i];
    }
    return TRUE;
}

BOOL GDIAPI Ellipse(HDC hdc, int l, int t, int r, int b)          /* GDI.24 */
{
    NPDC dc = dc_out(hdc);
    RECT rc;
    int nopen;
    if (!dc)
        return FALSE;
    dev_box(dc, l, t, r, b, &rc);
    nopen = pen_null(dc);
    if (nopen) {
        rc.right--;
        rc.bottom--;
    }
    if (rc.left >= rc.right || rc.top >= rc.bottom)
        return TRUE;
    if (!brush_hollow(dc)) {
        SPANS s;
        spans_begin(&s, dc, 0);
        ellipse_fill(&s, rc.left, rc.top, rc.right, rc.bottom, nopen ? 0 : 1);
        spans_end(&s);
    }
    if (!nopen)
        ellipse_outline(dc, rc.left, rc.top, rc.right, rc.bottom);
    return TRUE;
}

/* UNVERIFIED: round rectangles as a rectangle with elliptic corners */
BOOL GDIAPI RoundRect(HDC hdc, int l, int t, int r, int b, int ew, int eh) /* GDI.28 */
{
    NPDC dc = dc_out(hdc);
    RECT rc;
    int nopen, y, w, h, n = 0;
    HLOCAL hp;
    POINT NEAR *p;
    SPANS s;
    if (!dc)
        return FALSE;
    dev_box(dc, l, t, r, b, &rc);
    w = dc_scale_x(dc, ew);
    h = dc_scale_y(dc, eh);
    if (w < 0) w = -w;
    if (h < 0) h = -h;
    if (w > rc.right - rc.left) w = rc.right - rc.left;
    if (h > rc.bottom - rc.top) h = rc.bottom - rc.top;
    nopen = pen_null(dc);
    if (nopen) {
        rc.right--;
        rc.bottom--;
    }
    if (rc.left >= rc.right || rc.top >= rc.bottom)
        return TRUE;
    /* row extents: an ellipse of w x h split in its middle */
    if (!brush_hollow(dc)) {
        spans_begin(&s, dc, 0);
        for (y = rc.top + !nopen; y < rc.bottom - !nopen; y++) {
            int xs[2], ey, x0, x1;
            if (y < rc.top + h / 2)
                ey = rc.top + (y - rc.top);
            else if (y >= rc.bottom - (h - h / 2))
                ey = rc.top + h - (rc.bottom - y);
            else
                ey = rc.top + h / 2;
            ellipse_row(rc.left, rc.top, rc.left + w, rc.top + h, ey, &x0, &x1);
            xs[0] = rc.left + (x0 - rc.left) + !nopen;
            xs[1] = rc.right - (rc.left + w - x1) - !nopen;
            if (xs[0] < xs[1])
                spans_row(&s, y, xs, 2);
        }
        spans_end(&s);
    }
    if (!nopen) {
        hp = LocalAlloc(LMEM_FIXED, (2 * (rc.bottom - rc.top) + 4) * sizeof(POINT));
        if (!hp)
            return FALSE;
        p = (POINT NEAR *)hp;
        for (y = rc.top; y < rc.bottom; y++) {
            int ey, x0, x1;
            if (y < rc.top + h / 2)
                ey = y;
            else if (y >= rc.bottom - (h - h / 2))
                ey = rc.top + h - (rc.bottom - y);
            else
                ey = rc.top + h / 2;
            ellipse_row(rc.left, rc.top, rc.left + w, rc.top + h, ey, &x0, &x1);
            p[n].x = rc.right - 1 - (rc.left + w - x1);
            p[n].y = y;
            n++;
        }
        for (y = rc.bottom - 1; y >= rc.top; y--) {
            int ey, x0, x1;
            if (y < rc.top + h / 2)
                ey = y;
            else if (y >= rc.bottom - (h - h / 2))
                ey = rc.top + h - (rc.bottom - y);
            else
                ey = rc.top + h / 2;
            ellipse_row(rc.left, rc.top, rc.left + w, rc.top + h, ey, &x0, &x1);
            p[n].x = x0;
            p[n].y = y;
            n++;
        }
        p[n++] = p[0];
        out_polyline(dc, (POINT __far *)p, n);
        LocalFree(hp);
    }
    return TRUE;
}

/* ---- arcs: UNVERIFIED approximations (the outline points between the
   two radials, counter-clockwise from the start radial) ---- */

static long cross(int ax, int ay, int bx, int by)
{
    return (long)ax * by - (long)ay * bx;
}

/* is point (x, y) (relative to the centre) counter-clockwise from s and
   clockwise from e (screen y downwards: counter-clockwise on screen) */
static int in_arc(int x, int y, int sx, int sy, int ex, int ey)
{
    long a = cross(sx, sy, x, y), b = cross(x, y, ex, ey), se = cross(sx, sy, ex, ey);
    if (se <= 0)                 /* the arc spans less than half a turn */
        return a <= 0 && b <= 0;
    return a <= 0 || b <= 0;
}

static BOOL arcs(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4, int kind)
{
    NPDC dc = dc_out(hdc);
    RECT rc;
    int p3[2], p4[2], cx, cy, y, n = 0, nopen;
    HLOCAL hp;
    POINT NEAR *p;
    if (!dc)
        return FALSE;
    dev_box(dc, l, t, r, b, &rc);
    p3[0] = x3; p3[1] = y3; p4[0] = x4; p4[1] = y4;
    dc_lp_to_dp(dc, p3, 1);
    dc_lp_to_dp(dc, p4, 1);
    cx = (rc.left + rc.right - 1) / 2;
    cy = (rc.top + rc.bottom - 1) / 2;
    p3[0] += dc->dcOrg.x - cx; p3[1] += dc->dcOrg.y - cy;
    p4[0] += dc->dcOrg.x - cx; p4[1] += dc->dcOrg.y - cy;
    nopen = pen_null(dc);
    hp = LocalAlloc(LMEM_FIXED, (2 * (rc.bottom - rc.top) + 8) * sizeof(POINT));
    if (!hp)
        return FALSE;
    p = (POINT NEAR *)hp;
    /* walk the outline counter-clockwise from the right-hand side's top */
    {
        int start = -1, i, m = 0, total;
        POINT NEAR *o = p;
        for (y = rc.bottom - 1; y >= rc.top; y--) {     /* right side, up */
            int x0, x1;
            ellipse_row(rc.left, rc.top, rc.right, rc.bottom, y, &x0, &x1);
            o[m].x = x1 - 1; o[m].y = y; m++;
        }
        for (y = rc.top; y < rc.bottom; y++) {          /* left side, down */
            int x0, x1;
            ellipse_row(rc.left, rc.top, rc.right, rc.bottom, y, &x0, &x1);
            o[m].x = x0; o[m].y = y; m++;
        }
        total = m;
        /* the points inside the arc, in order, starting after a gap */
        for (i = 0; i < total; i++)
            if (!in_arc(o[i].x - cx, o[i].y - cy, p3[0], p3[1], p4[0], p4[1])) {
                start = i;
                break;
            }
        if (start < 0)
            start = 0;
        {
            HLOCAL hq = LocalAlloc(LMEM_FIXED, (total + 4) * sizeof(POINT));
            POINT NEAR *q;
            if (!hq) {
                LocalFree(hp);
                return FALSE;
            }
            q = (POINT NEAR *)hq;
            p = (POINT NEAR *)hp;
            for (i = 0; i < total; i++) {
                POINT pt = p[(start + i) % total];
                if (in_arc(pt.x - cx, pt.y - cy, p3[0], p3[1], p4[0], p4[1]))
                    q[n++] = pt;
            }
            if (kind == 1 && n) {                /* pie: back to the centre */
                q[n].x = cx; q[n].y = cy; n++;
                q[n] = q[0]; n++;
            } else if (kind == 2 && n)           /* chord: close */
                q[n++] = q[0];
            if (kind && !brush_hollow(dc) && n > 2) {
                SPANS s;
                spans_begin(&s, dc, 0);
                fill_polygon(&s, q, n - 1, 0);
                spans_end(&s);
            }
            if (!nopen && n > 1)
                out_polyline(dc, (POINT __far *)q, n);
            LocalFree(hq);
        }
    }
    LocalFree(hp);
    return TRUE;
}

BOOL GDIAPI Arc(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4) /* GDI.23 */
{
    return arcs(hdc, l, t, r, b, x3, y3, x4, y4, 0);
}

BOOL GDIAPI Pie(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4) /* GDI.26 */
{
    return arcs(hdc, l, t, r, b, x3, y3, x4, y4, 1);
}

BOOL GDIAPI Chord(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4) /* GDI.348 */
{
    return arcs(hdc, l, t, r, b, x3, y3, x4, y4, 2);
}

/* ---- regions from shapes ---- */

static HRGN rgn_from_rows(int t, int b, int l, int r, int kind)
{
    HRGN h = rgn_new(0, 0, 0, 0), row;
    int y;
    if (!h)
        return 0;
    row = rgn_new(0, 0, 0, 0);
    for (y = t; y < b; y++) {
        int x0, x1;
        ellipse_row(l, t, r, b, y, &x0, &x1);
        (void)kind;
        if (x0 >= x1)
            continue;
        rgn_set_rect(row, x0, y, x1, y + 1);
        rgn_combine(h, h, row, RGN_OR);
    }
    obj_free(row);
    return h;
}

/* UNVERIFIED: 3.1's elliptic region rows */
HRGN GDIAPI CreateEllipticRgn(int l, int t, int r, int b)        /* GDI.54 */
{
    return rgn_from_rows(min(t, b), max(t, b), min(l, r), max(l, r), 0);
}

HRGN GDIAPI CreateEllipticRgnIndirect(const RECT __far *rc)       /* GDI.55 */
{
    return CreateEllipticRgn(rc->left, rc->top, rc->right, rc->bottom);
}

HRGN GDIAPI CreatePolygonRgn(const POINT __far *pts, int n, int mode) /* GDI.63 */
{
    HRGN h = rgn_new(0, 0, 0, 0), row;
    POINT NEAR *p;
    HLOCAL hp;
    int i, y, ymin, ymax;
    if (!h || n < 3)
        return h;
    hp = LocalAlloc(LMEM_FIXED, n * sizeof(POINT));
    if (!hp)
        return h;
    p = (POINT NEAR *)hp;
    for (i = 0; i < n; i++)
        p[i] = pts[i];
    ymin = ymax = p[0].y;
    for (i = 1; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    row = rgn_new(0, 0, 0, 0);
    for (y = ymin; y < ymax; y++) {
        int xs[128], d[128], m = 0, j, w = 0;
        for (i = 0; i < n && m < 128; i++) {
            POINT a = p[i], b = p[(i + 1) % n];
            int dir = 1;
            if (a.y == b.y)
                continue;
            if (a.y > b.y) { POINT t = a; a = b; b = t; dir = -1; }
            if (y < a.y || y >= b.y)
                continue;
            xs[m] = a.x + round_div((long)(y - a.y) * (b.x - a.x), b.y - a.y);
            d[m++] = dir;
        }
        for (i = 1; i < m; i++)
            for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t;
                t = d[j]; d[j] = d[j - 1]; d[j - 1] = t;
            }
        for (i = 0; i + 1 < m; i++) {
            int in;
            w += d[i];
            in = mode == 2 ? w != 0 : (i & 1) == 0;
            if (in && xs[i] < xs[i + 1]) {
                rgn_set_rect(row, xs[i], y, xs[i + 1], y + 1);
                rgn_combine(h, h, row, RGN_OR);
            }
        }
    }
    obj_free(row);
    LocalFree(hp);
    return h;
}
