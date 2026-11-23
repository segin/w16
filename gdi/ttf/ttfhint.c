/*
 * ttfhint.c - w16's autohinter: grid-fitting TrueType outlines at render
 * time, for the monochrome rasteriser of ttfx.c, without a bytecode
 * interpreter. Written for w16 from published descriptions of automatic
 * hinting (David Turner and Werner Lemberg's papers on FreeType's
 * auto-hinter: segments, edges, stems, blue zones, interpolation); no
 * other hinter's code was read. Public domain (CC0, LICENSE).
 *
 * The method, per glyph at the size asked for, in each direction (y for
 * horizontal features, x for vertical ones):
 *
 * - Segments: runs of the outline (the points and the control points, in
 *   contour order) that go along the direction within about 5 degrees,
 *   each with its position across (font units), its extent along, which
 *   side the ink is on (from the contour's direction: TrueType's outer
 *   contours are clockwise; a glyph whose contours turn the other way is
 *   flipped) and whether it is round (part of a curve).
 * - Stems: each segment is linked to the nearest segment across that faces
 *   it with ink between them and overlaps it along: the two sides of a
 *   stem, or a serif and the stem it belongs to.
 * - Edges: segments at about the same position (a quarter pixel) are one
 *   edge, the unit that is fitted; an edge is linked as its longest
 *   linked segment is.
 * - Blue zones (y, per font, computed once from reference glyphs: the
 *   flat and round bottoms of H E L Z x z / O C G o c e s, the x-height of
 *   x z u v / o e c s, the cap height of H E T Z / O C G S, the ascender
 *   of b d h k l, the descender of p q / g): an edge on the matching side
 *   of the ink within a zone goes to the zone's reference line rounded to
 *   the pixel grid, or to its overshoot line, which is the reference plus
 *   the overshoot rounded: under half a pixel it is suppressed.
 * - Stem widths: whole pixels, at least one; a stem within a fifth of the
 *   font's standard width (the stems of l or I across x, of o across y)
 *   takes the standard width first, so equal stems come out equal. Rounded
 *   up only from 0.66 of a pixel in a regular face (1.5-pixel stems made 2
 *   would set it in bold), from 0.375 in a bold one (OS/2 weight 600 or
 *   more), so that it stays bolder. A stem with one edge already placed
 *   has the other at that width from it; a free stem is centred where it
 *   was, its edges on the grid; across x a free stem after the first keeps
 *   its distance from the one before, rounded (so that a round glyph's two
 *   sides do not both go outward). Edges that are not linked are
 *   interpolated between the placed ones and rounded.
 * - The edges stay in order (an edge is never moved below the one under
 *   it), and inside the glyph's pixel box (the unhinted outline's, which
 *   is within ttfx_GetGlyphBitmapBox's): advance widths and boxes never
 *   change, the hinting only moves points within the box.
 * - The points: those of an edge's segments move with the edge; the rest
 *   of their contour is interpolated between them along it, as TrueType's
 *   IUP instruction does (in proportion between the two touched points on
 *   either side when between them, else moved with the nearer); a contour
 *   without any follows the piecewise-linear map that takes the edges'
 *   positions to their fitted ones. Interpolating along the contour keeps
 *   a round stroke's outer curve with its own extreme, where the map would
 *   pull it toward the inner edge's move.
 * - With TTFX_HINT_DROPOUT the scan converter (ttfx.c) adds rule 3 along
 *   rows: a stroke thinner than a pixel that holds no pixel centre turns
 *   on the pixel its middle is in.
 *
 * All integer: font units, 26.6 pixels and 8.24 scales; 80286 code
 * (tests/ttf/cpu286.sh); the loops avoid 32-bit multiplies and divides
 * (helper calls on the 80286). Memory, a glyph's, from ttfx_alloc: the
 * scaled points (16 bytes a vertex), the font units and contour order
 * (14), 10 while moving points, and about 5K of segments and edges;
 * nothing kept but
 * the font's zones and widths in ttfx_font. Not hinted: sizes over
 * HMAXPPEM pixels to the em (it would not show).
 */
#include "ttfx.h"

#define HMAXSEG 160                     /* segments (and edges) a direction */
#define HMAXPPEM 255
#define HEND 0xFFFFu                    /* the end of a contour in the order */
#define HNEXT(i, m) ((i) + 1 == (m) ? 0 : (i) + 1)  /* (i + 1) mod m, no division */

typedef struct {
    ttfx_i16 pos, lo, hi;               /* across; the extent along (font units) */
    ttfx_i16 link, edge;                /* the segment across, -1; its edge */
    ttfx_u16 cs, cm, first, npts;       /* its contour (ord index, points); its points there */
    ttfx_u8 top, round;                 /* top: the ink is below (on the low side) */
} hseg;

typedef struct {
    ttfx_i32 g, fit;                    /* 26.6 on the grid: as scaled, fitted */
    ttfx_i16 u, len;                    /* font units; its longest segment's length */
    ttfx_i16 link, llen;                /* the linked edge, -1; that segment's length */
    ttfx_u8 flags;
} hedge;

#define HE_TOP 1                        /* has a segment with the ink below */
#define HE_BOT 2                        /* ... with the ink above */
#define HE_SET 4                        /* placed */
#define HE_ANCHOR 8                     /* placed by a zone or a stem */

typedef struct {
    const ttfx_font *f;
    const ttfx_vertex TTFX_FAR *v;
    const ttfx_i16 TTFX_FAR *uc;        /* point p's font units: [2p] x, [2p + 1] y */
    ttfx_u16 TTFX_FAR *ord;             /* points in contour order: 2i vertex i's, 2i+1 its control */
    unsigned nord;
    hseg TTFX_FAR *seg;
    hedge TTFX_FAR *edge;
    int nseg, nedge, flip;
} hctx;

static ttfx_i32 habs(ttfx_i32 v)
{
    return v < 0 ? -v : v;
}

/* to the nearest pixel (26.6), halves up */
static ttfx_i32 hround(ttfx_i32 v)
{
    return (v + 32) & ~63L;
}

/* a coordinate of point p in font units: axis 0 x, 1 y */
#define HC(h, p, axis) ((ttfx_i32)(h)->uc[2 * (unsigned)(p) + (axis)])

/* the points' font units in one array (hctx.uc): 8 bytes a vertex */
static ttfx_i16 TTFX_FAR *hunits(const ttfx_font *f, const ttfx_vertex TTFX_FAR *v, int nv)
{
    ttfx_i16 TTFX_FAR *u = (ttfx_i16 TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)nv * 8);
    int i;
    if (u)
        for (i = 0; i < nv; i++) {
            u[4 * i] = v[i].x;
            u[4 * i + 1] = v[i].y;
            u[4 * i + 2] = v[i].type == TTFX_vcurve ? v[i].cx : v[i].x;
            u[4 * i + 3] = v[i].type == TTFX_vcurve ? v[i].cy : v[i].y;
        }
    return u;
}

/* a * b / c (c > 0) to nearest: in one multiply and one divide when the
   product is small (the 80286 has no 32-bit ones: each is a call) */
static ttfx_i32 hmuldiv(ttfx_i32 a, ttfx_i32 b, ttfx_i32 c)
{
    ttfx_i32 p;
    if (a > 0x7FFFL || a < -0x7FFFL || b > 0x7FFFL || b < -0x7FFFL)
        return ttfx_MulDivRound(a, b, c);
    p = a * b;
    return (p >= 0 ? p + (c >> 1) : p - (c >> 1)) / c;
}

/* the contours' points in order, each contour ended by HEND; the start
   point is left out (stb_truetype's contours come back to it) */
static unsigned horder(const ttfx_vertex TTFX_FAR *v, int nv, ttfx_u16 TTFX_FAR *ord)
{
    unsigned n = 0;
    int i, open = 0;
    for (i = 0; i < nv; i++) {
        if (v[i].type == TTFX_vmove) {
            if (open)
                ord[n++] = HEND;
            open = 1;
            continue;
        }
        if (v[i].type == TTFX_vcurve)
            ord[n++] = (ttfx_u16)(2 * i + 1);
        ord[n++] = (ttfx_u16)(2 * i);
    }
    if (open)
        ord[n++] = HEND;
    return n;
}

/* 1 if the contours turn counter-clockwise (y up) overall: the sum of the
   signed areas, the coordinates sixteenths (enough for the sign) */
static int hflip(const hctx *h)
{
    ttfx_i32 a = 0;
    unsigned k = 0, s;
    while (k < h->nord) {
        for (s = k; h->ord[k] != HEND; k++) {
            ttfx_u16 p = h->ord[k], q = h->ord[h->ord[k + 1] == HEND ? s : k + 1];
            a += ((HC(h, p, 0) + HC(h, q, 0)) >> 4) * ((HC(h, q, 1) - HC(h, p, 1)) >> 4);
        }
        k++;
    }
    /* the shoelace sum is twice the area, positive counter-clockwise */
    return a > 0;
}

/* the piece from p to q across axis a: 0 nothing (no length), +1 or -1
   along (its direction along), 2 across */
static int hpiece(const hctx *h, ttfx_u16 p, ttfx_u16 q, int a)
{
    ttfx_i32 da = HC(h, q, a) - HC(h, p, a), dl = HC(h, q, !a) - HC(h, p, !a);
    if (!da && !dl)
        return 0;
    da = habs(da);
    if ((da << 3) + (da << 2) <= habs(dl))      /* |da| * 12, no multiply */
        return dl > 0 ? 1 : -1;
    return 2;
}

/* the segments across axis a (a 1: horizontal segments, positions y);
   minlen the shortest kept */
static void hsegments(hctx *h, int a, ttfx_i32 minlen)
{
    unsigned k = 0, s, m, j, k0, kj, kfirst = 0, jfirst = 0, jlast = 0;
    h->nseg = 0;
    while (k < h->nord) {
        int dir = 0, cl, prev;
        ttfx_i32 amin = 0, amax = 0, lmin = 0, lmax = 0;
        int round = 0;
        for (s = k; h->ord[k] != HEND; k++)
            ;
        m = k - s;
        k++;
        if (m < 2)
            continue;
        /* start after a break: a piece across, or a change of direction */
        for (k0 = 0; k0 < m; k0++)
            if (hpiece(h, h->ord[s + k0], h->ord[s + HNEXT(k0, m)], a) == 2)
                break;
        if (k0 == m) {
            prev = 0;
            for (j = 0; j < m; j++) {
                cl = hpiece(h, h->ord[s + j], h->ord[s + HNEXT(j, m)], a);
                if (cl)
                    prev = cl;
            }
            for (k0 = 0; k0 < m; k0++) {
                cl = hpiece(h, h->ord[s + k0], h->ord[s + HNEXT(k0, m)], a);
                if (cl && cl != prev)
                    break;
                if (cl)
                    prev = cl;
            }
            if (k0 == m)
                continue;               /* all one way: nothing */
        }
        for (j = 0, kj = k0; j <= m; j++, kj = HNEXT(kj, m)) {
            ttfx_u16 p = 0, q = 0;
            cl = 2;
            if (j < m) {
                p = h->ord[s + kj];
                q = h->ord[s + HNEXT(kj, m)];
                cl = hpiece(h, p, q, a);
                if (!cl)
                    continue;
            }
            if (dir && cl == dir) {     /* the run goes on */
                ttfx_i32 qa = HC(h, q, a), ql = HC(h, q, !a);
                if (qa < amin) amin = qa;
                if (qa > amax) amax = qa;
                if (ql < lmin) lmin = ql;
                if (ql > lmax) lmax = ql;
                round |= (p | q) & 1;
                jlast = j;
                continue;
            }
            if (dir && lmax - lmin >= minlen && h->nseg < HMAXSEG) {
                hseg TTFX_FAR *g = &h->seg[h->nseg++];
                g->top = (ttfx_u8)(((dir > 0) != (a == 0)) ^ h->flip);
                g->pos = (ttfx_i16)(g->top ? amax : amin);
                g->lo = (ttfx_i16)lmin;
                g->hi = (ttfx_i16)lmax;
                g->round = (ttfx_u8)round;
                g->link = g->edge = -1;
                g->cs = (ttfx_u16)s;
                g->cm = (ttfx_u16)m;
                g->first = (ttfx_u16)kfirst;
                g->npts = (ttfx_u16)(jlast - jfirst + 2);
            }
            dir = 0;
            if (cl == 1 || cl == -1) {  /* a new run */
                ttfx_i32 pa = HC(h, p, a), qa = HC(h, q, a);
                ttfx_i32 pl = HC(h, p, !a), ql = HC(h, q, !a);
                dir = cl;
                jfirst = jlast = j;
                kfirst = kj;
                amin = pa < qa ? pa : qa;
                amax = pa < qa ? qa : pa;
                lmin = pl < ql ? pl : ql;
                lmax = pl < ql ? ql : pl;
                round = (p | q) & 1;
            }
        }
    }
}

/* sort the segments by position, then link each to the nearest segment
   facing it, with ink between, overlapping it along by half the shorter
   (and nearer than maxdist) */
static void hlink(hctx *h, ttfx_i32 maxdist)
{
    int i, j, n = h->nseg;
    hseg TTFX_FAR *sg = h->seg;
    for (i = 1; i < n; i++) {
        hseg t = sg[i];
        for (j = i; j > 0 && sg[j - 1].pos > t.pos; j--)
            sg[j] = sg[j - 1];
        sg[j] = t;
    }
    for (i = 0; i < n; i++) {
        hseg TTFX_FAR *s = &sg[i];
        ttfx_i32 best = maxdist + 1, ls = (ttfx_i32)s->hi - s->lo;
        int step = s->top ? -1 : 1;
        for (j = i + step; j >= 0 && j < n; j += step) {
            hseg TTFX_FAR *t = &sg[j];
            ttfx_i32 d = s->top ? (ttfx_i32)s->pos - t->pos : (ttfx_i32)t->pos - s->pos, ov, lt;
            if (d >= best)
                break;
            if (t->top == s->top || d <= 0)
                continue;
            lt = (ttfx_i32)t->hi - t->lo;
            ov = (ttfx_i32)(s->hi < t->hi ? s->hi : t->hi) - (s->lo > t->lo ? s->lo : t->lo);
            if (ov <= 0 || ov * 2 < (ls < lt ? ls : lt))
                continue;
            best = d;
            s->link = (ttfx_i16)j;
        }
    }
}

/* the segments at about the same position (within tol) as edges, in
   order; scale s and offset o take font units to the grid */
static void hedges(hctx *h, ttfx_i32 tol, ttfx_scale s, ttfx_i32 o)
{
    int i, n = 0;
    ttfx_i32 first = 0;
    for (i = 0; i < h->nseg; i++) {
        hseg TTFX_FAR *g = &h->seg[i];
        hedge TTFX_FAR *e;
        ttfx_i16 len = (ttfx_i16)(g->hi - g->lo);
        if (!n || g->pos - first > tol) {
            e = &h->edge[n++];
            first = g->pos;
            e->u = g->pos;
            e->len = len;
            e->link = -1;
            e->llen = 0;
            e->flags = 0;
        } else {
            e = &h->edge[n - 1];
            if (len > e->len) {
                e->len = len;
                e->u = g->pos;
            }
        }
        e->flags |= g->top ? HE_TOP : HE_BOT;
        g->edge = (ttfx_i16)(n - 1);
    }
    h->nedge = n;
    for (i = 0; i < n; i++)
        h->edge[i].g = ttfx_Scale26(h->edge[i].u, s) + o;
    /* an edge's link: its longest linked segment's */
    for (i = 0; i < h->nseg; i++) {
        hseg TTFX_FAR *g = &h->seg[i];
        hedge TTFX_FAR *e = &h->edge[g->edge];
        int l;
        if (g->link < 0)
            continue;
        l = h->seg[g->link].edge;
        if (l != g->edge && g->hi - g->lo > e->llen) {
            e->llen = (ttfx_i16)(g->hi - g->lo);
            e->link = (ttfx_i16)l;
        }
    }
}

/* a stem's width w (26.6) on the grid: the standard width sw if near it,
   in whole pixels, at least one, rounded up from up/64 of a pixel. In
   monochrome a stem a pixel wider is a third or a half darker: a regular
   face's 1.5-pixel stems made 2 would set it in bold, so a regular face
   rounds up only from 0.66 of a pixel; a bold face from 0.375, so that its
   1.5-pixel stems are 2 and it stays bolder than the regular */
#define HUP_REGULAR 42
#define HUP_BOLD 24
static ttfx_i32 hwidth(ttfx_i32 w, ttfx_i32 sw, int up)
{
    if (sw > 0 && habs(w - sw) * 5 < sw)
        w = sw;
    w = (w + 64 - up) & ~63L;
    return w < 64 ? 64 : w;
}

/* fit the edges: zones (y), stems, the rest; lo, hi the box */
static void hfit(hctx *h, int a, ttfx_scale s, ttfx_i32 o, ttfx_i32 lo, ttfx_i32 hi)
{
    const ttfx_font *f = h->f;
    hedge TTFX_FAR *e = h->edge;
    int n = h->nedge, i, z, more;
    ttfx_i32 shift = 0;                 /* (x) how far the last free stem moved */
    ttfx_i32 sw = f->stdw[a] ? ttfx_Scale26(f->stdw[a], s) : 0;
    ttfx_i32 fuzz = f->unitsPerEm / 40;
    /* the blue zones: an edge on the zone's side of the ink, from a little
       outside its reference line to a little past its overshoot line */
    if (a)
        for (i = 0; i < n; i++) {
            ttfx_i32 bd = 0x7FFFFFFFL, fit = 0;
            for (z = 0; z < f->nzones; z++) {
                const ttfx_i16 *zn = f->zone[z];
                ttfx_i32 r = zn[0], sh = zn[1], u = e[i].u, d, ds, gr, ov;
                if (zn[2] ? !(e[i].flags & HE_TOP) : !(e[i].flags & HE_BOT))
                    continue;
                if (zn[2] ? (u < r - fuzz || u > sh + fuzz) : (u > r + fuzz || u < sh - fuzz))
                    continue;
                d = habs(u - r);
                ds = habs(u - sh);
                if ((d < ds ? d : ds) >= bd)
                    continue;
                bd = d < ds ? d : ds;
                gr = ttfx_Scale26(r, s) + o;
                ov = ttfx_Scale26(sh, s) + o - gr;
                fit = hround(gr);
                if (ds < d)             /* the overshoot, rounded (under half a pixel: none) */
                    fit += ov < 0 ? -hround(-ov) : hround(ov);
            }
            if (bd != 0x7FFFFFFFL) {
                e[i].fit = fit;
                e[i].flags |= HE_SET | HE_ANCHOR;
            }
        }
    /* stems: from an edge placed to its link (either way), until none
       moves; then the lowest free stem centred, and again. Across x a
       free stem after the first keeps its distance from the one before,
       rounded (it moves by as much as that one did, then to the grid):
       rounding each on its own could widen a glyph by a pixel, its sides
       going out both ways */
    do {
        do {
            more = 0;
            for (i = 0; i < n; i++) {
                int l = e[i].link;
                hedge TTFX_FAR *p, TTFX_FAR *q;
                ttfx_i32 w;
                if (l < 0 || ((e[i].flags ^ e[l].flags) & HE_SET) == 0)
                    continue;
                p = e[i].flags & HE_SET ? &e[i] : &e[l];    /* placed */
                q = p == &e[i] ? &e[l] : &e[i];
                w = hwidth(habs(q->g - p->g), sw, f->hint_up);
                q->fit = q->g > p->g ? p->fit + w : p->fit - w;
                q->flags |= HE_SET | HE_ANCHOR;
                more = 1;
            }
        } while (more);
        for (i = 0; i < n; i++) {
            int l = e[i].link;
            if (l >= 0 && !(e[i].flags & HE_SET) && !(e[l].flags & HE_SET)) {
                hedge TTFX_FAR *p = e[i].g < e[l].g ? &e[i] : &e[l], TTFX_FAR *q = p == &e[i] ? &e[l] : &e[i];
                ttfx_i32 w = hwidth(q->g - p->g, sw, f->hint_up);
                p->fit = hround(((p->g + q->g - w) >> 1) + shift);
                q->fit = p->fit + w;
                if (!a)
                    shift = ((p->fit + q->fit) - (p->g + q->g)) >> 1;
                p->flags |= HE_SET | HE_ANCHOR;
                q->flags |= HE_SET | HE_ANCHOR;
                more = 1;
                break;
            }
        }
    } while (more);
    /* the rest: between the placed edges, rounded */
    {
        int prev = -1, next = 0;
        for (i = 0; i < n; i++) {
            ttfx_i32 fit;
            if (e[i].flags & HE_ANCHOR) {
                prev = i;
                continue;
            }
            if (next <= i)
                for (next = i + 1; next < n && !(e[next].flags & HE_ANCHOR); next++)
                    ;
            if (prev >= 0 && next < n && e[next].g != e[prev].g)
                fit = e[prev].fit + hmuldiv(e[i].g - e[prev].g, e[next].fit - e[prev].fit,
                                                     e[next].g - e[prev].g);
            else if (prev >= 0)
                fit = e[i].g + e[prev].fit - e[prev].g;
            else if (next < n)
                fit = e[i].g + e[next].fit - e[next].g;
            else
                fit = e[i].g;
            e[i].fit = hround(fit);
            e[i].flags |= HE_SET;
        }
    }
    /* in order, and in the box */
    for (i = 0; i < n; i++) {
        if (i && e[i].fit < e[i - 1].fit)
            e[i].fit = e[i - 1].fit;
        if (e[i].fit < lo)
            e[i].fit = lo;
        if (e[i].fit > hi)
            e[i].fit = hi;
    }
}

/* the map from the grid as scaled to the fitted grid */
static ttfx_i32 hmap(const hedge TTFX_FAR *e, int n, ttfx_i32 g)
{
    int l = 0, r = n - 1;
    if (g <= e[0].g)
        return g + e[0].fit - e[0].g;
    if (g >= e[r].g)
        return g + e[r].fit - e[r].g;
    while (r - l > 1) {                 /* e[l].g <= g < e[r].g */
        int m = (l + r) >> 1;
        if (e[m].g <= g)
            l = m;
        else
            r = m;
    }
    return e[l].fit + hmuldiv(g - e[l].g, e[r].fit - e[l].fit, e[r].g - e[l].g);
}

/* a point's coordinate a on the grid with y up (-y) */
#define hget(pt, i, a) ((a) ? -(pt)[i].y : (pt)[i].x)
#define hset(pt, i, a, c) ((a) ? ((pt)[i].y = -(c)) : ((pt)[i].x = (c)))

/* c, between points at a1 and a2 that moved to n1 and n2: in proportion
   between them, else with the nearer */
static ttfx_i32 hiup(ttfx_i32 c, ttfx_i32 a1, ttfx_i32 n1, ttfx_i32 a2, ttfx_i32 n2)
{
    if (a1 > a2) {
        ttfx_i32 t = a1;
        a1 = a2;
        a2 = t;
        t = n1;
        n1 = n2;
        n2 = t;
    }
    if (c <= a1)
        return c + n1 - a1;
    if (c >= a2)
        return c + n2 - a2;
    return n1 + hmuldiv(c - a1, n2 - n1, a2 - a1);
}

/* hint one direction: a 0 x (vertical stems), 1 y (horizontal ones and
   the zones). The points' coordinate a is the grid's with y up: -y */
static void hdim(hctx *h, ttfx_point TTFX_FAR *pt, int nv, int a, ttfx_scale s, ttfx_i32 o)
{
    const ttfx_font *f = h->f;
    ttfx_i32 upx = 0x01000000L / s, tol = upx / 4, lo = 0x7FFFFFFFL, hi = -0x7FFFFFFFL, c;
    ttfx_i32 TTFX_FAR *org;             /* the points' coordinates before */
    ttfx_u8 TTFX_FAR *tch;              /* 1: moved with an edge */
    unsigned k;
    int i;
    if (tol > f->unitsPerEm / 50)
        tol = f->unitsPerEm / 50;
    hsegments(h, a, f->unitsPerEm / 100);
    if (!h->nseg)
        return;
    hlink(h, (ttfx_i32)f->unitsPerEm * 3 / 10);
    hedges(h, tol, s, o);
    for (i = 0; i < 2 * nv; i++) {
        if ((i & 1) && h->v[i >> 1].type != TTFX_vcurve)
            continue;
        c = a ? -pt[i].y : pt[i].x;
        if (c < lo) lo = c;
        if (c > hi) hi = c;
    }
    lo &= ~63L;
    hi = (hi + 63) & ~63L;
    hfit(h, a, s, o, lo, hi);
    org = (ttfx_i32 TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)nv * 8);
    tch = org ? (ttfx_u8 TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)nv * 2) : 0;
    for (i = 0; i < 2 * nv; i++) {
        c = a ? -pt[i].y : pt[i].x;
        if (org) {
            org[i] = c;
            if (tch)
                tch[i] = 0;
        }
        if (!tch)                       /* (out of memory: the map for all) */
            hset(pt, i, a, hmap(h->edge, h->nedge, c));
    }
    if (tch) {
        /* the points of the edges' segments move with their edge */
        for (i = 0; i < h->nseg; i++) {
            const hseg TTFX_FAR *g = &h->seg[i];
            ttfx_i32 d = h->edge[g->edge].fit - h->edge[g->edge].g;
            unsigned t, u;
            for (t = 0, u = g->first; t < g->npts; t++, u = HNEXT(u, g->cm)) {
                ttfx_u16 p = h->ord[g->cs + u];
                hset(pt, p, a, org[p] + d);
                tch[p] = 1;
            }
        }
        /* the rest of each contour between them, as TrueType's IUP; a
           contour without any follows the map */
        k = 0;
        while (k < h->nord) {
            unsigned cs = k, m, t0, t, t2, u;
            for (; h->ord[k] != HEND; k++)
                ;
            m = k - cs;
            k++;
            for (t0 = 0; t0 < m && !tch[h->ord[cs + t0]]; t0++)
                ;
            if (t0 == m) {
                for (t = 0; t < m; t++) {
                    ttfx_u16 p = h->ord[cs + t];
                    hset(pt, p, a, hmap(h->edge, h->nedge, org[p]));
                }
                continue;
            }
            t = t0;
            do {
                ttfx_u16 p1 = h->ord[cs + t], p2;
                for (t2 = HNEXT(t, m); !tch[h->ord[cs + t2]]; t2 = HNEXT(t2, m))
                    ;
                p2 = h->ord[cs + t2];
                for (u = HNEXT(t, m); u != t2; u = HNEXT(u, m)) {
                    ttfx_u16 p = h->ord[cs + u];
                    hset(pt, p, a, hiup(org[p], org[p1], hget(pt, p1, a), org[p2], hget(pt, p2, a)));
                }
                t = t2;
            } while (t != t0);
        }
    }
    if (tch)
        ttfx_free(f->alloc, tch);
    if (org)
        ttfx_free(f->alloc, org);
    /* a contour's start point is its end point (not in the order) */
    for (i = 0; i < nv; i++)
        if (h->v[i].type == TTFX_vmove) {
            int e = i + 1;
            while (e < nv && h->v[e].type != TTFX_vmove)
                e++;
            if (e - 1 > i)
                hset(pt, 2 * i, a, hget(pt, 2 * (e - 1), a));
        }
    /* in the box */
    for (i = 0; i < 2 * nv; i++) {
        c = hget(pt, i, a);
        if (c < lo)
            hset(pt, i, a, lo);
        if (c > hi)
            hset(pt, i, a, hi);
    }
}

/* ---- the font: blue zones and standard widths, once ---- */

/* per zone: the characters with flat and with round extremes, top 1 */
static const char *const hzones[TTFX_HINT_ZONES][2] = {
    { "HELZxz", "OCGoces" },            /* the baseline */
    { "pq", "g" },                      /* the descender */
    { "xzuv", "oecs" },                 /* the x-height */
    { "HETZ", "OCGS" },                 /* the cap height */
    { "bdhkl", "" },                    /* the ascender */
};
static const ttfx_u8 hztop[TTFX_HINT_ZONES] = { 0, 0, 1, 1, 1 };

/* the average extreme (top: highest y, else lowest) of the characters' outlines */
static int hextreme(const ttfx_font *f, const char *cs, int top, ttfx_i32 *avg)
{
    ttfx_i32 sum = 0;
    int n = 0, i, nv;
    for (; *cs; cs++) {
        ttfx_vertex TTFX_FAR *v;
        ttfx_u16 g = ttfx_FindGlyphIndex(f, (unsigned char)*cs);
        ttfx_i32 ext;
        if (!g || (nv = ttfx_GetGlyphShape(f, g, &v)) == 0)
            continue;
        ext = top ? -0x8000L : 0x7FFFL;
        for (i = 0; i < nv; i++) {
            ttfx_i32 y = v[i].y;
            if (top ? y > ext : y < ext)
                ext = y;
            if (v[i].type == TTFX_vcurve && (top ? v[i].cy > ext : v[i].cy < ext))
                ext = v[i].cy;
        }
        ttfx_FreeShape(f, v);
        sum += ext;
        n++;
    }
    if (n)
        *avg = sum >= 0 ? (sum + n / 2) / n : -((-sum + n / 2) / n);
    return n;
}

/* the narrowest stem across axis a of the first of the characters there is */
static ttfx_i16 hstdw(ttfx_font *f, const char *cs, int a)
{
    hctx h;
    ttfx_vertex TTFX_FAR *v;
    int nv, i;
    ttfx_i32 w = 0;
    for (; *cs; cs++) {
        ttfx_u16 g = ttfx_FindGlyphIndex(f, (unsigned char)*cs);
        if (g && (nv = ttfx_GetGlyphShape(f, g, &v)) != 0)
            break;
    }
    if (!*cs)
        return 0;
    h.f = f;
    h.v = v;
    h.uc = hunits(f, v, nv);
    h.ord = (ttfx_u16 TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)nv * 6 + 2);
    h.seg = (hseg TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)HMAXSEG * sizeof(hseg));
    if (h.uc && h.ord && h.seg) {
        h.nord = horder(v, nv, h.ord);
        h.flip = hflip(&h);
        hsegments(&h, a, f->unitsPerEm / 100);
        hlink(&h, (ttfx_i32)f->unitsPerEm * 3 / 10);
        for (i = 0; i < h.nseg; i++)
            if (h.seg[i].link >= 0) {
                ttfx_i32 d = habs((ttfx_i32)h.seg[h.seg[i].link].pos - h.seg[i].pos);
                if (!w || d < w)
                    w = d;
            }
    }
    if (h.seg)
        ttfx_free(f->alloc, h.seg);
    if (h.ord)
        ttfx_free(f->alloc, h.ord);
    if (h.uc)
        ttfx_free(f->alloc, (void TTFX_FAR *)h.uc);
    ttfx_FreeShape(f, v);
    return (ttfx_i16)w;
}

static void hfont(ttfx_font *f)
{
    int z;
    f->hint_done = 1;
    f->nzones = 0;
    /* bold: OS/2's weight class 600 or more, else head's macStyle bit 0 */
    f->hint_up = (ttfx_u8)((f->os2 ? ttfx_U16(f, f->os2 + 4) >= 600 : ttfx_U16(f, f->head + 44) & 1)
                           ? HUP_BOLD : HUP_REGULAR);
    for (z = 0; z < TTFX_HINT_ZONES; z++) {
        ttfx_i32 r = 0, sh = 0;
        int top = hztop[z], nr = hextreme(f, hzones[z][0], top, &r), ns = hextreme(f, hzones[z][1], top, &sh);
        ttfx_i16 *zn = f->zone[f->nzones];
        if (!nr && !ns)
            continue;
        if (!nr)
            r = sh;
        if (!ns || (top ? sh < r : sh > r))
            sh = r;
        zn[0] = (ttfx_i16)r;
        zn[1] = (ttfx_i16)sh;
        zn[2] = (ttfx_i16)top;
        f->nzones++;
    }
    f->stdw[0] = hstdw(f, "lI", 0);
    f->stdw[1] = hstdw(f, "oO", 1);
}

ttfx_point TTFX_FAR *ttfx__HintPoints(const ttfx_font *cf, const ttfx_vertex TTFX_FAR *v, int nv,
                                      ttfx_scale sx, ttfx_scale sy, ttfx_i32 ox, ttfx_i32 oy)
{
    ttfx_font *f = (ttfx_font *)cf;     /* (the font's analysis is kept in it) */
    ttfx_point TTFX_FAR *pt;
    ttfx_i32 ppem;
    hctx h;
    int i;
    if (nv <= 0 || nv > 4000)
        return 0;
    pt = (ttfx_point TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)nv * 2 * sizeof(ttfx_point));
    if (!pt)
        return 0;
    for (i = 0; i < nv; i++) {
        pt[2 * i].x = ttfx_Scale26(v[i].x, sx) + ox;
        pt[2 * i].y = -ttfx_Scale26(v[i].y, sy) + oy;
        if (v[i].type == TTFX_vcurve) {
            pt[2 * i + 1].x = ttfx_Scale26(v[i].cx, sx) + ox;
            pt[2 * i + 1].y = -ttfx_Scale26(v[i].cy, sy) + oy;
        } else
            pt[2 * i + 1] = pt[2 * i];
    }
    ppem = ttfx_ScaleRound(f->unitsPerEm, sy > sx ? sy : sx);
    if (ppem < 2 || ppem > HMAXPPEM || sx <= 0 || sy <= 0)
        return pt;
    if (!f->hint_done)
        hfont(f);
    h.f = f;
    h.v = v;
    h.uc = hunits(f, v, nv);
    h.ord = (ttfx_u16 TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)nv * 6 + 2);
    h.seg = (hseg TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)HMAXSEG * sizeof(hseg));
    h.edge = (hedge TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)HMAXSEG * sizeof(hedge));
    if (h.uc && h.ord && h.seg && h.edge) {
        h.nord = horder(v, nv, h.ord);
        h.flip = hflip(&h);
        if (f->hint & TTFX_HINT_Y)
            hdim(&h, pt, nv, 1, sy, -oy);
        if (f->hint & TTFX_HINT_X)
            hdim(&h, pt, nv, 0, sx, ox);
    }
    if (h.edge)
        ttfx_free(f->alloc, h.edge);
    if (h.seg)
        ttfx_free(f->alloc, h.seg);
    if (h.ord)
        ttfx_free(f->alloc, h.ord);
    if (h.uc)
        ttfx_free(f->alloc, (void TTFX_FAR *)h.uc);
    return pt;
}
