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

static int wide_polyline(LPDC dc, POINT __far *p, int n, int w);
static int pen_width(LPDC dc);

static int out_polyline(LPDC dc, POINT __far *p, int n)
{
    DRVOUTPUT fn = (DRVOUTPUT)dc->dev->fn[DRV_OUTPUT];
    LPVOID pen = dc_ppen(dc);
    LPRGN rao = dc_rao(dc);
    RECT box;
    WORD k;
    int i;
    if (n < 2 || !pen)
        return 0;
    if (pen_width(dc) > 1)
        return wide_polyline(dc, p, n, pen_width(dc));
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
    LPDC dc;
    int usePen;
    int begun;
    POINT pts[1 + 2 * 64];
    int npts;
} SPANS;

static void spans_begin(SPANS *s, LPDC dc, int usePen)
{
    s->dc = dc;
    s->usePen = usePen;
    s->begun = 0;
    s->npts = 0;
}

static void spans_flush_row(SPANS *s)
{
    LPDC dc = s->dc;
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
    LPDC dc = s->dc;
    LPRGN rao = rgn_ptr(dc->hRao);
    WORD k;
    int i;
    s->npts = 0;
    for (k = 0; k < rao->n; k++) {
        RECT __far * c = &rao->r[k];
        if (y < c->top || y >= c->bottom)
            continue;
        for (i = 0; i + 1 < n; i += 2) {
            int a = max(xs[i], c->left), b = min(xs[i + 1], c->right);
            /* a zero-width span is passed on only inside the clip
               rectangle: outside it, clipping inverts it (a > b) */
            if (a >= b && !(a == b && xs[i] == xs[i + 1] && n == 2 && rao->n == 1))
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
    LPDC dc = s->dc;
    if (s->begun)
        ((DRVOUTPUT)dc->dev->fn[DRV_OUTPUT])(dc->lpPDevice, OS_ENDNSCAN, 0, 0, 0, 0, &dc->dm, 0);
}

static int brush_hollow(LPDC dc)
{
    LPBRUSHOBJ b = obj_get(dc->hBrush, BRUSH_MAGIC);
    return !b || b->lb.lbStyle == 1;
}

static LPPENOBJ pen_of(LPDC dc)
{
    return obj_get(dc->hPen, PEN_MAGIC);
}

static int pen_null(LPDC dc)
{
    LPPENOBJ p = pen_of(dc);
    return !p || p->lp.lopnStyle == 5;
}

static int pen_width(LPDC dc)
{
    LPPENOBJ p = pen_of(dc);
    int w;
    if (!p)
        return 1;
    w = dc_scale_x(dc, p->lp.lopnWidth.x);
    if (w < 0)
        w = -w;
    return w <= 1 ? 1 : w;
}

/* a buffer of n points in GDI's heaps; 0 when n is negative or more than
   one block holds (the size was counted in a word and wrapped) */
#define PTS_MAX 0x3FC0
static XP pts_alloc(long n)
{
    if (n < 0 || n > PTS_MAX)
        return 0;
    return xalloc((WORD)n * sizeof(POINT));
}

/* an outline polyline built point by point, drawn in pieces of at most
   PTS_MAX points (each piece starts at the last one's end, which the
   driver leaves undrawn, so every pixel is drawn once) */
typedef struct {
    LPDC dc;
    XP h;
    WORD n, cap, started;
    POINT first;
} OUTLINE;

static int ol_begin(OUTLINE *o, LPDC dc, long n)
{
    o->dc = dc;
    o->n = 0;
    o->started = 0;
    o->cap = n > PTS_MAX ? PTS_MAX : n < 2 ? 2 : (WORD)n;
    o->h = pts_alloc(o->cap);
    return o->h != 0;
}

static void ol_add(OUTLINE *o, int x, int y)
{
    POINT __far *p = xptr(o->h);
    if (o->n == o->cap) {
        out_polyline(o->dc, p, o->n);
        p = xptr(o->h);
        p[0] = p[o->n - 1];
        o->n = 1;
    }
    if (!o->started) {
        o->started = 1;
        o->first.x = x;
        o->first.y = y;
    }
    p[o->n].x = x;
    p[o->n].y = y;
    o->n++;
}

/* closes the outline (back to its first point) and draws the rest */
static void ol_end(OUTLINE *o)
{
    ol_add(o, o->first.x, o->first.y);
    out_polyline(o->dc, (POINT __far *)xptr(o->h), o->n);
    xfree(o->h);
}

/* points from logical to screen coordinates, in a local buffer */
static XP to_screen(LPDC dc, const POINT __far *p, int n, int close)
{
    XP h = pts_alloc((long)n + 1);
    POINT __far *q;
    int i;
    if (!h)
        return 0;
    q = (POINT __far *)xptr(h);
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

/* the x where edge a-b (a.y < b.y) crosses row y (a.y <= y < b.y),
   rounded to the nearest integer (halves away from a.x), in 32 bits:
   points up to 65535 apart (16-bit differences wrapped); off the int
   range: its end */
static int edge_x(POINT a, POINT b, int y)
{
    long dx = (long)b.x - a.x, q;
    unsigned long den = (unsigned long)((long)b.y - a.y);
    unsigned long m = (unsigned long)((long)y - a.y) * (unsigned long)(dx < 0 ? -dx : dx);
    q = (long)((m + den / 2) / den);
    q = dx < 0 ? a.x - q : a.x + q;
    return q < -32768L ? -32768 : q > 32767L ? 32767 : (int)q;
}

/* fill a polygon (screen coordinates), rows top..bottom-1 */
/* polygons together (PolyPolygon: one fill of all their edges), each of
   cnt[k] points; closed: with the edge from the last point to the first */
static void fill_polys(SPANS *s, POINT __far *p, const int __far *cnt, int npoly, int winding,
                       int closed)
{
    int ymin = p[0].y, ymax = p[0].y, y, i, j, k, base, n, total = 0;
    int xs[2 * 64], dirs[2 * 64];
    for (k = 0; k < npoly; k++)
        total += cnt[k];
    for (i = 1; i < total; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    {
        /* only the rows of the composite region's box: spans_row draws
           nothing on the others (every caller came through dc_out) */
        LPRGN rao = rgn_ptr(s->dc->hRao);
        if (rao) {
            if (ymin < rao->box.top)
                ymin = rao->box.top;
            if (ymax > rao->box.bottom)
                ymax = rao->box.bottom;
        }
    }
    for (y = ymin; y < ymax; y++) {
        int m = 0, out[2 * 64], no = 0;
        for (k = 0, base = 0; k < npoly; base += cnt[k], k++)
        for (n = cnt[k], i = 0; i < (closed ? n : n - 1) && m < 2 * 64; i++) {
            POINT a = p[base + i], b = p[base + (i + 1) % n];
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
            xs[m] = edge_x(a, b, y);
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

static void fill_polygon(SPANS *s, POINT __far *p, int n, int winding)
{
    fill_polys(s, p, (const int __far *)&n, 1, winding, 1);
}

/* ---- ellipses: the inside and the outline ---- */

/* The pixels of an ellipse in the box l, t, r, b (right and bottom
   exclusive), row y: [*x0, *x1] inclusive. Fitted to 3.1's outlines (GD3:
   the 60 x 30, 7 x 5 and 20 x 20 ellipses): pixel (x, y) is in when
   ((x - cx) / a)^2 + ((y - cy) / b)^2 <= 1.02 with cx, cy the box's centre
   pixel position ((l + r - 1) / 2) and a, b half its width and height. This
   reproduces the 7 x 5 and 20 x 20 ones exactly and the 60 x 30 one but
   for its top and bottom rows (one pixel wider each side). UNVERIFIED: 3.1
   seems to step through the quadrant incrementally. */
static void ellipse_row(int l, int t, int r, int b, int y, int *x0, int *x1)
{
    long A = (long)r - l, B = (long)b - t;  /* 2a, 2b */
    long Y = 2L * y - ((long)t + b - 1);   /* 2 (y - cy) */
    long num, lim, lo, hi, mid, c, X0, X1;
    int k = 0;
    if (A <= 0 || B <= 0) {
        *x0 = l;
        *x1 = r - 1;
        return;
    }
    /* the squares below fit in 32 bits up to 45000 (1.02 x 45000^2 <
       2^31); a wider box is computed at half size (B with Y; A, then the
       result doubled: a pixel or two at that size) */
    if (B > 45000L) {
        B /= 2;
        Y /= 2;
    }
    if (A > 45000L) {
        A /= 2;
        k = 1;
    }
    /* (X / A)^2 + (Y / B)^2 <= 1.02, X = 2 (x - cx): X^2 <= A^2 (1.02 B^2 - Y^2) / B^2 */
    num = B * B + B * B / 50 - Y * Y;     /* 1.02 B^2 (102 B^2 overflowed past B = 4588) */
    if (num < 0) {
        *x0 = 1;
        *x1 = 0;
        return;
    }
    lim = num / B * A + num % B * A / B;
    lim = lim / B * A + lim % B * A / B;
    lo = 0;
    hi = A + 1;
    while (lo < hi) {
        mid = (lo + hi + 1) / 2;
        if ((unsigned long)mid * mid <= (unsigned long)lim)
            lo = mid;
        else
            hi = mid - 1;
    }
    lo <<= k;
    /* x with |2x - (l + r - 1)| <= lo (in 32 bits: l + r - 1 is not an int) */
    c = (long)l + r - 1;
    X0 = (c - lo + 1) >> 1;
    X1 = (c + lo) >> 1;
    *x0 = X0 < l ? l : (int)X0;
    *x1 = X1 > r - 1 ? r - 1 : (int)X1;
}

/* an ellipse's rows, each computed once: row y and row t + b - 1 - y
   are the same (ellipse_row uses Y^2 only). The table holds (x0, x1) of
   rows t .. t + (b - t - 1) / 2; without it (no room) each row is
   computed when asked for. */
typedef struct {
    int l, t, r, b;
    XP h;
} EROWS;

static void er_row(const EROWS *e, int y, int *x0, int *x1)
{
    long m = (long)y - e->t, mm = (long)e->b - 1 - y;
    if (e->h && m >= 0 && mm >= 0) {
        POINT __far *q = (POINT __far *)xptr(e->h) + (WORD)(m < mm ? m : mm);
        *x0 = q->x;
        *x1 = q->y;
    } else
        ellipse_row(e->l, e->t, e->r, e->b, y, x0, x1);
}

static void er_begin(EROWS *e, int l, int t, int r, int b)
{
    long n = ((long)b - t + 1) / 2, i;
    e->l = l;
    e->t = t;
    e->r = r;
    e->b = b;
    e->h = n > 0 ? pts_alloc(n) : 0;
    for (i = 0; e->h && i < n; i++) {
        int x0, x1;
        POINT __far *q;
        ellipse_row(l, t, r, b, (int)(t + i), &x0, &x1);
        q = (POINT __far *)xptr(e->h) + (WORD)i;
        q->x = x0;
        q->y = x1;
    }
}

static void er_end(EROWS *e)
{
    if (e->h)
        xfree(e->h);
}

/* the inside, as 3.1 fills it: rows t .. b-2, spans [x0, x1) */
static void ellipse_fill(SPANS *s, const EROWS *e)
{
    int y, t = e->t, b = e->b;
    for (y = t; y < b - 1; y++) {
        int xs[2];
        er_row(e, y, &xs[0], &xs[1]);
        if (xs[0] < xs[1])
            spans_row(s, y, xs, 2);
    }
}

/* the outline: per row the pixels between this row's end and the
   neighbouring row's, as runs joined diagonally (3.1's outline polyline
   is made of such runs) */
static void ellipse_outline(LPDC dc, const EROWS *e)
{
    int t = e->t, b = e->b;
    long h = (long)b - t;
    int y, half = (int)(t + (h - 1) / 2);
    OUTLINE o;
    int x0, x1, px0, px1, nx0, nx1;
    if (h <= 0 || !ol_begin(&o, dc, 4 * h + 8))
        return;
    er_row(e, t, &x0, &x1);
    ol_add(&o, x0, t);
    ol_add(&o, x1, t);
    px1 = x1;
    for (y = t + 1; y < b - 1; y++) {              /* right side, down */
        er_row(e, y, &x0, &x1);
        if (y <= half) {
            int from = px1 + 1 <= x1 ? px1 + 1 : x1;
            ol_add(&o, from, y);
            if (x1 != from) ol_add(&o, x1, y);
        } else {
            er_row(e, y + 1, &nx0, &nx1);
            ol_add(&o, x1, y);
            if (nx1 + 1 < x1) ol_add(&o, nx1 + 1, y);
        }
        px1 = x1;
    }
    if (b - 1 > t) {
        er_row(e, b - 1, &x0, &x1);
        ol_add(&o, x1, b - 1);
        ol_add(&o, x0, b - 1);
        px0 = x0;
        for (y = b - 2; y > t; y--) {              /* left side, up */
            er_row(e, y, &x0, &x1);
            if (y > half) {
                int from = px0 - 1 >= x0 ? px0 - 1 : x0;
                ol_add(&o, from, y);
                if (x0 != from) ol_add(&o, x0, y);
            } else {
                er_row(e, y - 1, &nx0, &nx1);
                ol_add(&o, x0, y);
                if (nx0 - 1 > x0) ol_add(&o, nx0 - 1, y);
            }
            px0 = x0;
        }
    }
    ol_end(&o);
}

/* ---- wide pens: UNVERIFIED geometry (3.1's not observed in detail) ----
   each segment a filled quadrilateral w wide, each vertex a filled disc
   w across, drawn as scan lines with the pen */

static long isqrt(long v)
{
    long r = 0, b = 1L << 30;
    if (v <= 0)
        return 0;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

static int sat16(long v)
{
    return v < -32768L ? -32768 : v > 32767L ? 32767 : (int)v;
}

static int wide_polyline(LPDC dc, POINT __far *p, int n, int w)
{
    SPANS s;
    int i, y;
    spans_begin(&s, dc, 1);
    for (i = 0; i + 1 < n; i++) {
        /* in 32 bits (points up to 65535 apart); the squares' sum fits
           when each difference is at most 32767: a longer one halved
           (only its direction counts) */
        long dx = (long)p[i + 1].x - p[i].x, dy = (long)p[i + 1].y - p[i].y, len;
        POINT q[4];
        int ox, oy;
        while (dx > 32767L || dx < -32767L || dy > 32767L || dy < -32767L) {
            dx /= 2;
            dy /= 2;
        }
        len = isqrt(dx * dx + dy * dy);
        if (!len)
            continue;
        ox = (int)((-dy * w + len) / (2 * len));
        oy = (int)((dx * w + len) / (2 * len));
        q[0].x = sat16((long)p[i].x + ox);     q[0].y = sat16((long)p[i].y + oy);
        q[1].x = sat16((long)p[i + 1].x + ox); q[1].y = sat16((long)p[i + 1].y + oy);
        q[2].x = sat16((long)p[i + 1].x - ox); q[2].y = sat16((long)p[i + 1].y - oy);
        q[3].x = sat16((long)p[i].x - ox);     q[3].y = sat16((long)p[i].y - oy);
        fill_polygon(&s, q, 4, 1);
    }
    for (i = 0; i < n; i++) {
        int l = p[i].x - w / 2, t = p[i].y - w / 2;
        for (y = t; y < t + w; y++) {
            int xs[2];
            ellipse_row(l, t, l + w, t + w, y, &xs[0], &xs[1]);
            xs[1]++;
            if (xs[0] < xs[1])
                spans_row(&s, y, xs, 2);
        }
    }
    spans_end(&s);
    return 1;
}

/* ---- API ---- */

static void dev_box(LPDC dc, int l, int t, int r, int b, RECT *o)
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
    LPDC dc = dc_out(hdc);
    POINT p[2];
    if (mf_dc(hdc))
        return mf_rec2(hdc, MR_LINETO, y, x);
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
    LPDC dc = dc_out(hdc);
    XP h;
    int i, k;
    if (mf_dc(hdc))
        return mf_poly(hdc, MR_POLYLINE, pts, n);
    if (!dc || n < 2)
        return FALSE;
    if (pen_null(dc))
        return TRUE;
    /* in pieces of what one block holds, each from the last one's end
       (GD5 on 3.1: 16383 points are drawn) */
    for (i = 0; i < n - 1; i += k - 1) {
        k = n - i > PTS_MAX - 1 ? PTS_MAX - 1 : n - i;   /* (to_screen takes one more) */
        h = to_screen(dc, pts + i, k, 0);
        if (!h)
            return FALSE;
        dc = dc_get(hdc);
        out_polyline(dc, (POINT __far *)xptr(h), k);
        xfree(h);
    }
    return TRUE;
}

/* the frame of a box with a wide pen: four filled bands */
static void wide_frame(LPDC dc, RECT *r, int w)
{
    SPANS s;
    int y, in = w / 2, out_ = (w - 1) / 2;
    int L = r->left - in, T = r->top - in, R = r->right - 1 + out_ + 1, B = r->bottom - 1 + out_ + 1;
    spans_begin(&s, dc, 1);
    for (y = T; y < B; y++) {
        int xs[4];
        if (y < T + w || y >= B - w || R - L <= 2 * w) {    /* (the sides meet: one span) */
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
    LPDC dc = dc_out(hdc);
    RECT rc;
    int nopen, w;
    if (mf_dc(hdc))
        return mf_rec4(hdc, MR_RECTANGLE, b, r, t, l);
    if (!dc)
        return FALSE;
    dev_box(dc, l, t, r, b, &rc);
    if (rc.left == rc.right || rc.top == rc.bottom)
        return TRUE;                    /* GD5 on 3.1: nothing drawn */
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
                LPRGN rao = dc_rao(dc);
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
    LPDC dc = dc_out(hdc);
    XP h;
    POINT __far *q;
    if (!dc || n < 2)
        return FALSE;
    h = to_screen(dc, pts, n, 1);
    if (!h)
        return FALSE;
    dc = dc_get(hdc);
    q = (POINT __far *)xptr(h);
    if (!brush_hollow(dc) && n > 2) {
        SPANS s;
        spans_begin(&s, dc, 0);
        fill_polygon(&s, q, n, dc->polyFill == 2);
        spans_end(&s);
    }
    if (!pen_null(dc) && close_outline)
        out_polyline(dc, (POINT __far *)q, n + 1);
    xfree(h);
    return TRUE;
}

BOOL GDIAPI Polygon(HDC hdc, const POINT __far *pts, int n)       /* GDI.36 */
{
    if (mf_dc(hdc))
        return mf_poly(hdc, MR_POLYGON, pts, n);
    return polygon(hdc, pts, n, 1);
}

/* GD5 on 3.1: the polygons are filled together (one fill of all their
   edges, so a polygon inside another is a hole with ALTERNATE), and, as
   the SDK says, not closed: neither the outlines nor the fill have an
   edge back to a polygon's first point */
BOOL GDIAPI PolyPolygon(HDC hdc, const POINT __far *pts, const int __far *counts, int n) /* GDI.450 */
{
    LPDC dc = dc_out(hdc);
    long total = 0;
    int i, base;
    XP h;
    if (mf_dc(hdc))
        return mf_polypoly(hdc, pts, counts, n);
    if (!dc || n <= 0)
        return FALSE;
    for (i = 0; i < n; i++) {
        if (counts[i] < 0)
            return FALSE;
        total += counts[i];
    }
    if (total < 2 || total >= PTS_MAX)
        return FALSE;
    h = to_screen(dc, pts, (int)total, 0);
    if (!h)
        return FALSE;
    dc = dc_get(hdc);
    if (!brush_hollow(dc) && total > 2) {
        SPANS s;
        spans_begin(&s, dc, 0);
        fill_polys(&s, (POINT __far *)xptr(h), counts, n, dc->polyFill == 2, 0);
        spans_end(&s);
    }
    if (!pen_null(dc))
        for (i = 0, base = 0; i < n; base += counts[i], i++)
            if (counts[i] >= 2)
                out_polyline(dc, (POINT __far *)xptr(h) + base, counts[i]);
    xfree(h);
    return TRUE;
}

BOOL GDIAPI Ellipse(HDC hdc, int l, int t, int r, int b)          /* GDI.24 */
{
    LPDC dc = dc_out(hdc);
    RECT rc;
    int nopen;
    if (mf_dc(hdc))
        return mf_rec4(hdc, MR_ELLIPSE, b, r, t, l);
    if (!dc)
        return FALSE;
    dev_box(dc, l, t, r, b, &rc);
    nopen = pen_null(dc);
    if (rc.left >= rc.right || rc.top >= rc.bottom)
        return TRUE;
    /* GD5 on 3.1: an 8000 x 8000 ellipse is drawn, 10000 x 10000 fails,
       10000 x 64 is drawn. UNVERIFIED: the limit; here the product of the
       half axes up to 2^24 */
    if ((((long)rc.right - rc.left) / 2) * (((long)rc.bottom - rc.top) / 2) > 0x1000000L)
        return FALSE;
    {
        EROWS e;
        er_begin(&e, rc.left, rc.top, rc.right, rc.bottom);
        dc = dc_get(hdc);
        if (!brush_hollow(dc)) {
            SPANS s;
            spans_begin(&s, dc, 0);
            ellipse_fill(&s, &e);
            spans_end(&s);
        }
        if (!nopen)
            ellipse_outline(dc, &e);
        er_end(&e);
    }
    return TRUE;
}

/* UNVERIFIED: round rectangles as a rectangle with elliptic corners */
BOOL GDIAPI RoundRect(HDC hdc, int l, int t, int r, int b, int ew, int eh) /* GDI.28 */
{
    LPDC dc = dc_out(hdc);
    RECT rc;
    int nopen, y, w, h;
    SPANS s;
    EROWS e;
    if (mf_dc(hdc))
        return mf_rec6(hdc, MR_ROUNDRECT, eh, ew, b, r, t, l);
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
    /* row extents: an ellipse of w x h split in its middle (its rows
       computed once: every middle row is its middle one) */
    er_begin(&e, rc.left, rc.top, rc.left + w, rc.top + h);
    dc = dc_get(hdc);
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
            er_row(&e, ey, &x0, &x1);
            xs[0] = rc.left + (x0 - rc.left) + !nopen;
            xs[1] = rc.right - (rc.left + w - (x1 + 1)) - !nopen;
            if (xs[0] < xs[1])
                spans_row(&s, y, xs, 2);
        }
        spans_end(&s);
    }
    if (!nopen) {
        OUTLINE o;
        if (!ol_begin(&o, dc, 2 * ((long)rc.bottom - rc.top) + 4)) {
            er_end(&e);
            return FALSE;
        }
        for (y = rc.top; y < rc.bottom; y++) {
            int ey, x0, x1;
            if (y < rc.top + h / 2)
                ey = y;
            else if (y >= rc.bottom - (h - h / 2))
                ey = rc.top + h - (rc.bottom - y);
            else
                ey = rc.top + h / 2;
            er_row(&e, ey, &x0, &x1);
            ol_add(&o, rc.right - 1 - (rc.left + w - (x1 + 1)), y);
        }
        for (y = rc.bottom - 1; y >= rc.top; y--) {
            int ey, x0, x1;
            if (y < rc.top + h / 2)
                ey = y;
            else if (y >= rc.bottom - (h - h / 2))
                ey = rc.top + h - (rc.bottom - y);
            else
                ey = rc.top + h / 2;
            er_row(&e, ey, &x0, &x1);
            ol_add(&o, x0, y);
        }
        ol_end(&o);
    }
    er_end(&e);
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
    if (!se && (long)sx * ex + (long)sy * ey > 0)
        return 1;                /* the same radial: the whole ellipse (GD5 on 3.1) */
    if (se <= 0)                 /* the arc spans less than half a turn */
        return a <= 0 && b <= 0;
    return a <= 0 || b <= 0;
}

static BOOL arcs(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4, int kind)
{
    LPDC dc = dc_out(hdc);
    RECT rc;
    int p3[2], p4[2], cx, cy, y, n = 0, nopen;
    XP hp;
    POINT __far *p;
    EROWS e;
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
    hp = pts_alloc(2 * ((long)rc.bottom - rc.top) + 8);  /* UNVERIFIED: 3.1 past 8000 rows */
    if (!hp)
        return FALSE;
    er_begin(&e, rc.left, rc.top, rc.right, rc.bottom);
    dc = dc_get(hdc);
    p = (POINT __far *)xptr(hp);
    /* walk the outline counter-clockwise from the right-hand side's top */
    {
        int start = -1, i, m = 0, total;
        POINT __far *o = p;
        for (y = rc.bottom - 1; y >= rc.top; y--) {     /* right side, up */
            int x0, x1;
            er_row(&e, y, &x0, &x1);
            o[m].x = x1; o[m].y = y; m++;
        }
        for (y = rc.top; y < rc.bottom; y++) {          /* left side, down */
            int x0, x1;
            er_row(&e, y, &x0, &x1);
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
            XP hq = pts_alloc((long)total + 4);
            POINT __far *q;
            if (!hq) {
                er_end(&e);
                xfree(hp);
                return FALSE;
            }
            q = (POINT __far *)xptr(hq);
            p = (POINT __far *)xptr(hp);
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
            xfree(hq);
        }
    }
    er_end(&e);
    xfree(hp);
    return TRUE;
}

/* Arc, Pie, Chord on a metafile DC: the eight words reversed */
static BOOL mf_arc(HDC hdc, WORD fn, int l, int t, int r, int b, int x3, int y3, int x4, int y4)
{
    WORD w[8];
    w[0] = y4; w[1] = x4; w[2] = y3; w[3] = x3; w[4] = b; w[5] = r; w[6] = t; w[7] = l;
    return mf_recw(hdc, fn, 8, w);
}

/* ---- flood fill ---- */

extern WORD __cdecl gdi_ahincr;         /* entry.asm: KERNEL's __AHINCR */

typedef struct {
    LPVOID dev;
    DRVSCANLR scan;
    DWORD pc;
    WORD out;                           /* ScanLR's style for a pixel not filled */
    RECT box;
    WORD rowb;                          /* bytes a row of the marks */
    HGLOBAL hmark, hstack;
    BYTE __far *mark;                   /* a bit a pixel of the box: filled */
    POINT __far *stack;
    WORD nstack, capstack;
} FILL;

static BYTE __far *ff_at(FILL *f, int x, int y)
{
    DWORD off = (DWORD)(y - f->box.top) * f->rowb + ((WORD)(x - f->box.left) >> 3);
    return MAKELP(SELECTOROF(f->mark) + (WORD)(off >> 16) * gdi_ahincr, (WORD)off);
}

static int ff_marked(FILL *f, int x, int y)
{
    return *ff_at(f, x, y) & (0x80 >> ((x - f->box.left) & 7));
}

static void ff_push(FILL *f, int x, int y)
{
    if (f->nstack == f->capstack) {
        HGLOBAL g;
        if (f->capstack >= 0x4000)
            return;                     /* (the fill stops short) */
        GlobalUnlock(f->hstack);
        g = GlobalReAlloc(f->hstack, (DWORD)f->capstack * 2 * sizeof(POINT), GMEM_MOVEABLE);
        if (g) {
            f->hstack = g;
            f->capstack *= 2;
        }
        f->stack = GlobalLock(f->hstack);
        if (!g)
            return;
    }
    f->stack[f->nstack].x = x;
    f->stack[f->nstack].y = y;
    f->nstack++;
}

/* the first x from x (rightwards, or leftwards with 2 in style) that
   ScanLR finds with style (1: a pixel of the colour, 0: not), -1 none */
static int ff_scan(FILL *f, int x, int y, WORD style)
{
    return (int)f->scan(f->dev, x, y, f->pc, style);
}

/* the area around (x, y) (device coordinates, in the box): its spans
   marked, each run of the area on the rows above and below pushed */
static void ff_search(FILL *f, int x, int y)
{
    ff_push(f, x, y);
    while (f->nstack) {
        int l, r, k, ny, d;
        f->nstack--;
        x = f->stack[f->nstack].x;
        y = f->stack[f->nstack].y;
        if (ff_marked(f, x, y))
            continue;
        k = ff_scan(f, x, y, f->out | 2);
        l = k < f->box.left ? f->box.left : k + 1;
        k = ff_scan(f, x, y, f->out);
        r = k < 0 || k > f->box.right ? f->box.right : k;
        for (k = l; k < r; k++)
            *ff_at(f, k, y) |= 0x80 >> ((k - f->box.left) & 7);
        for (d = -1; d <= 1; d += 2) {
            ny = y + d;
            if (ny < f->box.top || ny >= f->box.bottom)
                continue;
            for (k = l; k < r; ) {
                int e = ff_scan(f, k, ny, f->out);
                if (e == k) {           /* not in the area: the next that is */
                    e = ff_scan(f, k, ny, f->out ^ 1);
                    if (e < 0)
                        break;
                    k = e;
                    continue;
                }
                if (!ff_marked(f, k, ny))
                    ff_push(f, k, ny);
                if (e < 0)
                    break;
                k = e;
            }
        }
    }
}

/* FloodFill and ExtFloodFill (GD9): on a metafile DC both are
   META_EXTFLOODFILL (FloodFill with FLOODFILLBORDER, 0) and answer 0.
   On a real DC (as the DDK describes it, GDI scans with the driver's
   ScanLR): the pixels 4-connected to (x, y) up to the colour
   (FLOODFILLBORDER) or of the colour (FLOODFILLSURFACE, 1), filled with
   the brush; FALSE when (x, y) is outside the clip region or not in such
   an area. The whole area is found before any of it is painted (the
   brush's own pixels never stop or extend it). UNVERIFIED: 3.1's
   connectivity, and how its search treats the clip region (here it
   searches the region's box and paints only the region). */
BOOL GDIAPI ExtFloodFill(HDC hdc, int x, int y, COLORREF c, WORD type) /* GDI.372 */
{
    LPDC dc;
    LPRGN rao;
    FILL f;
    SPANS s;
    int p[2], row, k;
    BOOL any = FALSE;
    if (mf_dc(hdc)) {
        WORD w[5];
        w[0] = type; w[1] = LOWORD(c); w[2] = HIWORD(c); w[3] = y; w[4] = x;
        mf_recw(hdc, MR_EXTFLOODFILL, 5, w);
        return FALSE;
    }
    dc = dc_out(hdc);
    if (!dc)
        return FALSE;
    f.scan = (DRVSCANLR)dc->dev->fn[DRV_SCANLR];
    if (!f.scan || brush_hollow(dc))
        return FALSE;
    p[0] = x;
    p[1] = y;
    dc_lp_to_dp(dc, p, 1);
    x = p[0] + dc->dcOrg.x;
    y = p[1] + dc->dcOrg.y;
    if (!PtInRegion(dc->hRao, x, y))
        return FALSE;
    dc = dc_get(hdc);
    f.dev = dc->lpPDevice;
    ((DRVCOLORINFO)dc->dev->fn[DRV_COLORINFO])(dc->lpPDevice, c, &f.pc);
    f.out = type == 1 ? 0 : 1;
    if (ff_scan(&f, x, y, f.out) == x)
        return FALSE;                   /* (x, y) is not in such an area */
    rao = rgn_ptr(dc->hRao);
    f.box = rao->box;
    f.rowb = (WORD)(((long)f.box.right - f.box.left + 7) >> 3);
    f.hmark = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                          (DWORD)f.rowb * ((long)f.box.bottom - f.box.top));
    f.hstack = GlobalAlloc(GMEM_MOVEABLE, 256 * sizeof(POINT));
    if (!f.hmark || !f.hstack) {
        if (f.hmark)
            GlobalFree(f.hmark);
        if (f.hstack)
            GlobalFree(f.hstack);
        return FALSE;
    }
    f.mark = GlobalLock(f.hmark);
    f.stack = GlobalLock(f.hstack);
    f.nstack = 0;
    f.capstack = 256;
    ff_search(&f, x, y);
    GlobalUnlock(f.hstack);
    GlobalFree(f.hstack);
    /* the marked runs, row by row, as scan lines with the brush */
    dc = dc_get(hdc);
    spans_begin(&s, dc, 0);
    for (row = f.box.top; row < f.box.bottom; row++) {
        int xs[2 * 64], n = 0, in = 0;
        for (k = f.box.left; k <= f.box.right; k++) {
            int m = k < f.box.right && ff_marked(&f, k, row);
            if (m != in) {
                xs[n++] = k;
                in = m;
                if (!in && n == 2 * 64) {
                    spans_row(&s, row, xs, n);
                    n = 0;
                    any = TRUE;
                }
            }
        }
        if (n) {
            spans_row(&s, row, xs, n);
            any = TRUE;
        }
    }
    spans_end(&s);
    GlobalUnlock(f.hmark);
    GlobalFree(f.hmark);
    return any;
}

BOOL GDIAPI FloodFill(HDC hdc, int x, int y, COLORREF c)          /* GDI.25 */
{
    return ExtFloodFill(hdc, x, y, c, 0);
}

BOOL GDIAPI Arc(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4) /* GDI.23 */
{
    if (mf_dc(hdc))
        return mf_arc(hdc, MR_ARC, l, t, r, b, x3, y3, x4, y4);
    return arcs(hdc, l, t, r, b, x3, y3, x4, y4, 0);
}

BOOL GDIAPI Pie(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4) /* GDI.26 */
{
    if (mf_dc(hdc))
        return mf_arc(hdc, MR_PIE, l, t, r, b, x3, y3, x4, y4);
    return arcs(hdc, l, t, r, b, x3, y3, x4, y4, 1);
}

BOOL GDIAPI Chord(HDC hdc, int l, int t, int r, int b, int x3, int y3, int x4, int y4) /* GDI.348 */
{
    if (mf_dc(hdc))
        return mf_arc(hdc, MR_CHORD, l, t, r, b, x3, y3, x4, y4);
    return arcs(hdc, l, t, r, b, x3, y3, x4, y4, 2);
}

/* ---- regions from shapes ---- */

/* A region built row by row downwards: the rectangles as OR'ing each
   row's spans into the region one by one gives them (rgn.c's banded
   form: a row's touching spans one rectangle; a row the same as the
   band ending at its top makes that band taller), appended without
   merging the whole region again at every row. Full (one block's
   rectangles, or no room): the rows after are left out. */
#define RB_MAX ((0xFF00 - sizeof(RGNDATA)) / sizeof(RECT))
typedef struct {
    XP x;
    WORD n, cap, prev, prevn;           /* prev, prevn: the last band (0: none) */
    int full;
} RBUILD;

static int rb_begin(RBUILD *b)
{
    b->n = b->prevn = b->prev = 0;
    b->cap = 64;
    b->full = 0;
    b->x = xalloc(RGNDATA_SIZE(b->cap));
    return b->x != 0;
}

/* row y's spans [xs[2i], xs[2i + 1]): sorted by left, none empty */
static void rb_row(RBUILD *b, int y, const int *xs, int n)
{
    LPRGN r;
    WORD start, cnt, k;
    int i;
    if (b->full)
        return;
    if (b->n + n / 2 > b->cap) {
        WORD cap = b->cap * 2;
        XP x;
        if (cap < b->n + n / 2)
            cap = b->n + n / 2;
        if (cap > RB_MAX)
            cap = RB_MAX;
        if (b->n + n / 2 > cap || !(x = xrealloc(b->x, RGNDATA_SIZE(cap)))) {
            b->full = 1;
            return;
        }
        b->x = x;
        b->cap = cap;
    }
    r = xptr(b->x);
    start = b->n;
    for (i = 0; i + 1 < n; i += 2) {
        if (b->n > start && xs[i] <= r->r[b->n - 1].right) {
            if (xs[i + 1] > r->r[b->n - 1].right)
                r->r[b->n - 1].right = xs[i + 1];
            continue;
        }
        r->r[b->n].left = xs[i];
        r->r[b->n].top = y;
        r->r[b->n].right = xs[i + 1];
        r->r[b->n].bottom = y + 1;
        b->n++;
    }
    cnt = b->n - start;
    if (!cnt) {
        b->prevn = 0;
        return;
    }
    if (b->prevn == cnt && r->r[b->prev].bottom == y) {
        for (k = 0; k < cnt; k++)
            if (r->r[b->prev + k].left != r->r[start + k].left ||
                r->r[b->prev + k].right != r->r[start + k].right)
                break;
        if (k == cnt) {
            for (k = 0; k < cnt; k++)
                r->r[b->prev + k].bottom = y + 1;
            b->n = start;
            return;
        }
    }
    b->prev = start;
    b->prevn = cnt;
}

/* the rectangles become region h's (its box as rgn.c makes it) */
static void rb_end(RBUILD *b, HRGN h)
{
    LPRGN r = xptr(b->x);
    WORD i;
    r->n = b->n;
    r->cap = b->cap;
    if (!r->n) {
        r->box.left = r->box.top = r->box.right = r->box.bottom = 0;
    } else {
        r->box = r->r[0];
        r->box.bottom = r->r[r->n - 1].bottom;
        for (i = 1; i < r->n; i++) {
            if (r->r[i].left < r->box.left)
                r->box.left = r->r[i].left;
            if (r->r[i].right > r->box.right)
                r->box.right = r->r[i].right;
        }
    }
    rgn_set_data(h, b->x);
}

static HRGN rgn_from_rows(int t, int b, int l, int r, int kind)
{
    HRGN h = rgn_new(0, 0, 0, 0);
    RBUILD rb;
    EROWS e;
    int y;
    (void)kind;
    if (!h)
        return 0;
    if (!rb_begin(&rb))
        return h;
    er_begin(&e, l, t, r, b);
    for (y = t; y < b; y++) {
        int xs[2];
        er_row(&e, y, &xs[0], &xs[1]);
        if (xs[0] > xs[1])
            continue;
        xs[1]++;
        rb_row(&rb, y, xs, 2);
    }
    er_end(&e);
    rb_end(&rb, h);
    return h;
}

/* UNVERIFIED: 3.1's elliptic region rows */
HRGN GDIAPI CreateEllipticRgn(int l, int t, int r, int b)        /* GDI.54 */
{
    return rgn_from_rows(min(t, b), max(t, b), min(l, r), max(l, r), 0);
}

HRGN GDIAPI CreateEllipticRgnIndirect(const RECT __far *rc)       /* GDI.55 */
{
    return rc ? CreateEllipticRgn(rc->left, rc->top, rc->right, rc->bottom) : 0;
}

HRGN GDIAPI CreatePolygonRgn(const POINT __far *pts, int n, int mode) /* GDI.63 */
{
    HRGN h = rgn_new(0, 0, 0, 0);
    POINT __far *p;
    XP hp;
    RBUILD rb;
    int i, y, ymin, ymax;
    if (!h || n < 3)
        return h;
    hp = pts_alloc(n);
    if (!hp) {                          /* GD5 on 3.1: 16383 points fail */
        rgn_free(h);
        return 0;
    }
    p = (POINT __far *)xptr(hp);
    for (i = 0; i < n; i++)
        p[i] = pts[i];
    ymin = ymax = p[0].y;
    for (i = 1; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    if (!rb_begin(&rb)) {
        xfree(hp);
        return h;
    }
    for (y = ymin; y < ymax; y++) {
        int xs[128], d[128], out[128], m = 0, j, w = 0, no = 0;
        p = (POINT __far *)xptr(hp);
        for (i = 0; i < n && m < 128; i++) {
            POINT a = p[i], b = p[(i + 1) % n];
            int dir = 1;
            if (a.y == b.y)
                continue;
            if (a.y > b.y) { POINT t = a; a = b; b = t; dir = -1; }
            if (y < a.y || y >= b.y)
                continue;
            xs[m] = edge_x(a, b, y);
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
                out[no++] = xs[i];
                out[no++] = xs[i + 1];
            }
        }
        rb_row(&rb, y, out, no);
    }
    rb_end(&rb, h);
    xfree(hp);
    return h;
}
