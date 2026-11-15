/*
 * rgn.c - regions: y-x banded rectangle lists and the region API.
 *
 * A region is a list of non-overlapping rectangles in bands: rectangles
 * of a band share top and bottom and are sorted by left; bands are sorted
 * by top and adjacent bands with the same rectangles are merged. Every
 * result of an operation is in that form, so equal regions compare equal
 * rectangle by rectangle. 3.1's own representation is not visible to
 * programs (no GetRegionData in 3.1). Return values follow the SDK
 * (NULLREGION 1, SIMPLEREGION 2, COMPLEXREGION 3, ERROR 0); GD2 saw them
 * on 3.1.
 *
 * The region object in GDI's DGROUP is its header and the XP of its
 * rectangles (RGNDATA), which are in GDI's own heaps (heap.c): a region
 * takes 0Eh bytes of DGROUP however many rectangles it has (GD4: 3.1's
 * rectangle region took 2Ah, its rectangles were in DGROUP too; 3.1
 * refused a region of more than 384 rectangles in bands of 128, or of 128
 * one-rectangle bands). One region's rectangles are one block, so at
 * most about 8000 of them.
 */
#include "gdi.h"

/* the region's rectangles (0: not a region) */
LPRGN rgn_ptr(HRGN h)
{
    LPRGNOBJ o = obj_get(h, RGN_MAGIC);
    return o ? xptr(o->xData) : 0;
}

int rgn_complexity(LPRGN r)
{
    return !r ? ERROR : !r->n ? NULLREGION : r->n == 1 ? SIMPLEREGION : COMPLEXREGION;
}

/* make room for n rectangles; the rectangles may move */
static LPRGN rgn_reserve(HRGN h, WORD n)
{
    LPRGNOBJ o = obj_get(h, RGN_MAGIC);
    LPRGN r;
    XP x;
    if (!o)
        return 0;
    r = xptr(o->xData);
    if (n > r->cap) {
        WORD cap = n + 4;
        if (cap > (0xFF00 - sizeof(RGNDATA)) / sizeof(RECT))
            return 0;
        x = xrealloc(o->xData, RGNDATA_SIZE(cap));
        if (!x)
            return 0;
        o = obj_get(h, RGN_MAGIC);
        o->xData = x;
        r = xptr(x);
        r->cap = cap;
    }
    return r;
}

static void rgn_fix_box(LPRGN r)
{
    WORD i;
    if (!r->n) {
        r->box.left = r->box.top = r->box.right = r->box.bottom = 0;
        return;
    }
    r->box = r->r[0];
    r->box.bottom = r->r[r->n - 1].bottom;
    for (i = 1; i < r->n; i++) {
        if (r->r[i].left < r->box.left)
            r->box.left = r->r[i].left;
        if (r->r[i].right > r->box.right)
            r->box.right = r->r[i].right;
    }
}

void rgn_set_rect(HRGN h, int l, int t, int r_, int b)
{
    LPRGN r;
    int x;
    if (l > r_)
        x = l, l = r_, r_ = x;
    if (t > b)
        x = t, t = b, b = x;
    r = rgn_reserve(h, 1);
    if (!r)
        return;
    if (l == r_ || t == b) {
        r->n = 0;
    } else {
        r->n = 1;
        r->r[0].left = l;
        r->r[0].top = t;
        r->r[0].right = r_;
        r->r[0].bottom = b;
    }
    rgn_fix_box(r);
}

HRGN rgn_new(int l, int t, int r, int b)
{
    XP x = xalloc(RGNDATA_SIZE(1));
    HRGN h;
    if (!x)
        return 0;
    h = obj_alloc(RGN_MAGIC, sizeof(RGNOBJ));
    if (!h) {
        xfree(x);
        return 0;
    }
    ((LPRGNOBJ)obj_get(h, RGN_MAGIC))->xData = x;
    ((LPRGN)xptr(x))->cap = 1;
    rgn_set_rect(h, l, t, r, b);
    return h;
}

void rgn_free(HRGN h)
{
    LPRGNOBJ o = obj_get(h, RGN_MAGIC);
    if (!o)
        return;
    xfree(o->xData);
    obj_free(h);
}

/* a copy of the rectangles (SaveDC keeps the clip region so) */
XP rgn_dup_data(HRGN h)
{
    LPRGN s = rgn_ptr(h);
    WORD n;
    XP x;
    if (!s)
        return 0;
    n = s->n;
    x = xalloc(RGNDATA_SIZE(n ? n : 1));
    if (!x)
        return 0;
    s = rgn_ptr(h);
    mem_copy(xptr(x), s, RGNDATA_SIZE(n));
    ((LPRGN)xptr(x))->cap = n ? n : 1;
    return x;
}

/* the region's rectangles become x's (which is the region's then) */
int rgn_set_data(HRGN h, XP x)
{
    LPRGNOBJ o = obj_get(h, RGN_MAGIC);
    if (!o || !x)
        return ERROR;
    xfree(o->xData);
    o->xData = x;
    return rgn_complexity(xptr(x));
}

int rgn_copy(HRGN dst, HRGN src)
{
    LPRGN s, d;
    WORD n;
    if (dst == src)
        return rgn_complexity(rgn_ptr(src));
    s = rgn_ptr(src);
    if (!s)
        return ERROR;
    n = s->n;
    d = rgn_reserve(dst, n ? n : 1);
    if (!d)
        return ERROR;
    s = rgn_ptr(src);
    mem_copy(d->r, s->r, n * sizeof(RECT));
    d->n = n;
    d->box = s->box;
    return rgn_complexity(d);
}

void rgn_offset(HRGN h, int dx, int dy)
{
    LPRGN r = rgn_ptr(h);
    WORD i;
    if (!r)
        return;
    for (i = 0; i < r->n; i++) {
        r->r[i].left += dx;
        r->r[i].right += dx;
        r->r[i].top += dy;
        r->r[i].bottom += dy;
    }
    rgn_fix_box(r);
}

/* ---- combining: a sweep over the bands of both regions ---- */

static int op_in(int mode, int a, int b)
{
    switch (mode) {
    case RGN_AND: return a && b;
    case RGN_OR: return a || b;
    case RGN_XOR: return a != b;
    case RGN_DIFF: return a && !b;
    default: return a;
    }
}

/* the band of r covering y (rectangles i .. i + count - 1), after
   skipping the bands above y; returns the count (0: none covers y) and
   sets *next to the next y where r changes (0x7FFF: none) */
static WORD band_at(LPRGN r, WORD *pos, int y, int *next)
{
    WORD i = *pos, k;
    while (i < r->n && r->r[i].bottom <= y)
        i++;
    *pos = i;
    if (i >= r->n) {
        *next = 0x7FFF;
        return 0;
    }
    if (r->r[i].top > y) {
        *next = r->r[i].top;
        return 0;
    }
    *next = r->r[i].bottom;
    for (k = i; k < r->n && r->r[k].top == r->r[i].top; k++)
        ;
    return k - i;
}

#define EDGE(rg, i, e) ((e) & 1 ? (rg)->r[(i) + ((e) >> 1)].right : (rg)->r[(i) + ((e) >> 1)].left)

/* dst = a op b, b being region hb or, with hb 0, the rectangles at rb
   (on the stack) */
static int combine(HRGN dst, HRGN ha, HRGN hb, LPRGN rb, int mode)
{
    LPRGN a, b, out;
    XP xo;
    WORD cap, nout = 0, pa = 0, pb = 0, prevStart = 0, prevCount = 0;
    int y, have = 0, prevBottom = 0;
    a = rgn_ptr(ha);
    b = hb ? rgn_ptr(hb) : rb;
    if (!a || !b)
        return ERROR;
    cap = a->n + b->n + 8;
    if (cap > 0x1000)
        cap = 0x1000;
    xo = xalloc(RGNDATA_SIZE(cap));
    if (!xo)
        return ERROR;
    a = rgn_ptr(ha);
    b = hb ? rgn_ptr(hb) : rb;
    out = xptr(xo);
    y = -0x7FFF;
    for (;;) {
        int na_, nb_, ya, yb, y1;
        WORD ia, ib, na, nb, ea, eb, start, cnt, k;
        int inA = 0, inB = 0, cur = 0;
        na = band_at(a, &pa, y, &ya);
        nb = band_at(b, &pb, y, &yb);
        y1 = min(ya, yb);
        if (!na && !nb) {
            if (y1 == 0x7FFF)
                break;
            y = y1;
            continue;
        }
        ia = pa;
        ib = pb;
        /* room for this band's rectangles */
        if (nout + na + nb + 1 > cap) {
            XP x2;
            WORD ncap = nout + na + nb + 1 + cap / 2;
            if (ncap > (0xFF00 - sizeof(RGNDATA)) / sizeof(RECT))
                ncap = (0xFF00 - sizeof(RGNDATA)) / sizeof(RECT);
            if (nout + na + nb + 1 > ncap)
                goto fail;
            x2 = xrealloc(xo, RGNDATA_SIZE(ncap));
            if (!x2)
                goto fail;
            xo = x2;
            cap = ncap;
            a = rgn_ptr(ha);
            b = hb ? rgn_ptr(hb) : rb;
            out = xptr(xo);
        }
        /* merge the band's edges: 2 per rectangle, sorted */
        start = nout;
        ea = eb = 0;
        na_ = 2 * na;
        nb_ = 2 * nb;
        while (ea < na_ || eb < nb_) {
            int xa = ea < na_ ? EDGE(a, ia, ea) : 0x7FFF;
            int xb = eb < nb_ ? EDGE(b, ib, eb) : 0x7FFF;
            int x = min(xa, xb), in;
            while (ea < na_ && EDGE(a, ia, ea) == x)
                inA ^= 1, ea++;
            while (eb < nb_ && EDGE(b, ib, eb) == x)
                inB ^= 1, eb++;
            in = op_in(mode, inA, inB);
            if (in != cur) {
                if (in) {
                    out->r[nout].left = x;
                    out->r[nout].top = y;
                    out->r[nout].bottom = y1;
                } else
                    out->r[nout++].right = x;
                cur = in;
            }
        }
        cnt = nout - start;
        if (!cnt) {
            have = 0;
        } else if (have && prevBottom == y && prevCount == cnt) {
            /* the same as the band above: make that one taller */
            for (k = 0; k < cnt; k++)
                if (out->r[prevStart + k].left != out->r[start + k].left ||
                    out->r[prevStart + k].right != out->r[start + k].right)
                    break;
            if (k == cnt) {
                for (k = 0; k < cnt; k++)
                    out->r[prevStart + k].bottom = y1;
                nout = start;
            } else {
                prevStart = start;
                prevCount = cnt;
            }
            prevBottom = y1;
        } else {
            have = 1;
            prevStart = start;
            prevCount = cnt;
            prevBottom = y1;
        }
        if (y1 == 0x7FFF)
            break;
        y = y1;
    }
    out->n = nout;
    out->cap = cap;
    rgn_fix_box(out);
    return rgn_set_data(dst, xo);
fail:
    xfree(xo);
    return ERROR;
}

int rgn_combine(HRGN dst, HRGN ha, HRGN hb, int mode)
{
    if (mode == RGN_COPY)
        return rgn_copy(dst, ha);
    if (!hb)
        return ERROR;
    return combine(dst, ha, hb, 0, mode);
}

/* h = h op the rectangle l, t, r, b (no region object needed) */
int rgn_op_rect(HRGN h, int l, int t, int r, int b, int mode)
{
    RGNDATA one;
    int x;
    if (l > r)
        x = l, l = r, r = x;
    if (t > b)
        x = t, t = b, b = x;
    one.cap = 1;
    one.n = l < r && t < b;
    one.r[0].left = l;
    one.r[0].top = t;
    one.r[0].right = r;
    one.r[0].bottom = b;
    one.box = one.r[0];
    return combine(h, h, 0, &one, mode);
}

/* ---- API ---- */

HRGN GDIAPI CreateRectRgn(int l, int t, int r, int b)              /* GDI.64 */
{
    return rgn_new(l, t, r, b);
}

HRGN GDIAPI CreateRectRgnIndirect(const RECT __far *rc)            /* GDI.65 */
{
    return rgn_new(rc->left, rc->top, rc->right, rc->bottom);
}

HRGN GDIAPI SetRectRgn(HRGN h, int l, int t, int r, int b)         /* GDI.172 */
{
    if (!rgn_ptr(h))
        return 0;
    rgn_set_rect(h, l, t, r, b);
    return h;
}

int GDIAPI CombineRgn(HRGN d, HRGN a, HRGN b, int mode)            /* GDI.47 */
{
    if (!rgn_ptr(d) || mode < RGN_AND || mode > RGN_COPY)
        return ERROR;
    return rgn_combine(d, a, b, mode);
}

int GDIAPI OffsetRgn(HRGN h, int dx, int dy)                        /* GDI.101 */
{
    LPRGN r = rgn_ptr(h);
    if (!r)
        return ERROR;
    rgn_offset(h, dx, dy);
    return rgn_complexity(rgn_ptr(h));
}

int GDIAPI GetRgnBox(HRGN h, RECT __far *rc)                        /* GDI.134 */
{
    LPRGN r = rgn_ptr(h);
    if (!r)
        return ERROR;
    *rc = r->box;
    return rgn_complexity(r);
}

BOOL GDIAPI PtInRegion(HRGN h, int x, int y)                        /* GDI.161 */
{
    LPRGN r = rgn_ptr(h);
    WORD i;
    if (!r)
        return FALSE;
    for (i = 0; i < r->n; i++)
        if (x >= r->r[i].left && x < r->r[i].right && y >= r->r[i].top && y < r->r[i].bottom)
            return TRUE;
    return FALSE;
}

static BOOL rect_in(LPRGN r, const RECT __far *rc)
{
    WORD i;
    if (!r)
        return FALSE;
    for (i = 0; i < r->n; i++)
        if (rc->left < r->r[i].right && rc->right > r->r[i].left &&
            rc->top < r->r[i].bottom && rc->bottom > r->r[i].top)
            return TRUE;
    return FALSE;
}

/* GD2: 3.1 returned 0101h for a rectangle in the region (USER's calls) */
BOOL GDIAPI RectInRegionOld(HRGN h, const RECT __far *rc)           /* GDI.181 */
{
    return rect_in(rgn_ptr(h), rc) ? 0x0101 : 0;
}

BOOL GDIAPI RectInRegion(HRGN h, const RECT __far *rc)              /* GDI.466 */
{
    return rect_in(rgn_ptr(h), rc);
}

BOOL GDIAPI EqualRgn(HRGN a, HRGN b)                                /* GDI.72 */
{
    LPRGN ra = rgn_ptr(a), rb = rgn_ptr(b);
    WORD i;
    if (!ra || !rb)
        return ERROR;
    if (ra->n != rb->n)
        return FALSE;
    for (i = 0; i < ra->n; i++)
        if (ra->r[i].left != rb->r[i].left || ra->r[i].right != rb->r[i].right ||
            ra->r[i].top != rb->r[i].top || ra->r[i].bottom != rb->r[i].bottom)
            return FALSE;
    return TRUE;
}
