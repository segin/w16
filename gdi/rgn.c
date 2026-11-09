/*
 * rgn.c - regions: y-x banded rectangle lists and the region API.
 *
 * A region is a list of non-overlapping rectangles in bands: rectangles
 * of a band share top and bottom and are sorted by left; bands are sorted
 * by top and adjacent bands with the same rectangles are merged. Every
 * result of an operation is in that form, so equal regions compare equal
 * rectangle by rectangle. The number of rectangles is limited only by
 * GDI's heap (ZOI); 3.1's own representation is not visible to programs
 * (no GetRegionData in 3.1). Return values follow the SDK (NULLREGION 1,
 * SIMPLEREGION 2, COMPLEXREGION 3, ERROR 0); GD2 saw them on 3.1.
 */
#include "gdi.h"

NPRGN rgn_ptr(HRGN h)
{
    return obj_get(h, RGN_MAGIC);
}

int rgn_complexity(NPRGN r)
{
    return !r ? ERROR : !r->n ? NULLREGION : r->n == 1 ? SIMPLEREGION : COMPLEXREGION;
}

/* make room for n rectangles; the region may move */
static NPRGN rgn_reserve(HRGN h, WORD n)
{
    NPRGN r = rgn_ptr(h);
    if (!r)
        return 0;
    if (n > r->cap) {
        WORD cap = n + 4;
        LocalUnlock(h);
        if (!LocalReAlloc(h, sizeof(RGNOBJ) + (cap - 1) * sizeof(RECT), LMEM_MOVEABLE)) {
            LocalLock(h);
            return 0;
        }
        LocalLock(h);
        r = rgn_ptr(h);
        r->cap = cap;
    }
    return r;
}

static void rgn_fix_box(NPRGN r)
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
    NPRGN r;
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
    HRGN h = obj_alloc(RGN_MAGIC, sizeof(RGNOBJ));
    if (!h)
        return 0;
    ((NPRGN)DEREF(h))->cap = 1;
    rgn_set_rect(h, l, t, r, b);
    return h;
}

int rgn_copy(HRGN dst, HRGN src)
{
    NPRGN s = rgn_ptr(src), d;
    WORD n;
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
    NPRGN r = rgn_ptr(h);
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

/* ---- combining ---- */

/* the x intervals of region r in the band [y0, y1) (r's rectangles that
   cover it); returns the count, stored as pairs in out */
static int band_of(NPRGN r, int y0, WORD *pos, int *out)
{
    int n = 0;
    WORD i = *pos;
    while (i < r->n && r->r[i].bottom <= y0)
        i++;
    *pos = i;
    for (; i < r->n && r->r[i].top <= y0; i++) {
        if (r->r[i].bottom > y0) {
            out[n++] = r->r[i].left;
            out[n++] = r->r[i].right;
        }
    }
    return n;
}

static int inside(int *iv, int n, int x)
{
    int i;
    for (i = 0; i < n; i += 2)
        if (x >= iv[i] && x < iv[i + 1])
            return 1;
    return 0;
}

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

/* combine intervals: walk every edge x of both lists */
static int combine_iv(int mode, int *a, int na, int *b, int nb, int *out)
{
    int n = 0, i = 0, j = 0, cur = 0, x;
    for (;;) {
        int xa = i < na ? a[i] : 0x7FFF, xb = j < nb ? b[j] : 0x7FFF, in;
        if (i >= na && j >= nb)
            break;
        x = xa < xb ? xa : xb;
        while (i < na && a[i] == x)
            i++;
        while (j < nb && b[j] == x)
            j++;
        in = op_in(mode, inside(a, na, x), inside(b, nb, x));
        if (in != cur) {
            out[n++] = x;
            cur = in;
        }
    }
    return n;
}

/* collect the distinct y edges of both regions, sorted */
static int y_edges(NPRGN a, NPRGN b, int *ys)
{
    int n = 0, i, j, k;
    NPRGN rr[2];
    rr[0] = a;
    rr[1] = b;
    for (k = 0; k < 2; k++) {
        if (!rr[k])
            continue;
        for (i = 0; i < (int)rr[k]->n; i++) {
            int v[2];
            v[0] = rr[k]->r[i].top;
            v[1] = rr[k]->r[i].bottom;
            for (j = 0; j < 2; j++) {
                int p = n;
                int q;
                for (q = 0; q < n; q++)
                    if (ys[q] >= v[j])
                        break;
                if (q < n && ys[q] == v[j])
                    continue;
                for (p = n; p > q; p--)
                    ys[p] = ys[p - 1];
                ys[q] = v[j];
                n++;
            }
        }
    }
    return n;
}

int rgn_combine(HRGN dst, HRGN ha, HRGN hb, int mode)
{
    NPRGN a = rgn_ptr(ha), b = mode == RGN_COPY ? 0 : rgn_ptr(hb), d;
    HLOCAL hy, hiv, hout;
    int *ys, *iva, *ivb, *ivc, *prev;
    RECT NEAR *out;
    WORD maxiv, nout = 0, cap, pa = 0, pb = 0, k;
    int ny, i, nprev = -1, prevStart = 0;
    if (!a || (mode != RGN_COPY && !b))
        return ERROR;
    if (mode == RGN_COPY)
        return rgn_copy(dst, ha);
    maxiv = 2 * (a->n + b->n) + 4;
    hy = LocalAlloc(LMEM_FIXED, 2 * (2 * (a->n + b->n) + 2));
    hiv = LocalAlloc(LMEM_FIXED, 2 * 4 * maxiv);
    cap = 2 * (a->n + b->n) * (a->n + b->n + 1) + 4;
    if (cap > 2000)
        cap = 2000;
    hout = LocalAlloc(LMEM_FIXED, cap * sizeof(RECT));
    if (!hy || !hiv || !hout) {
        if (hy) LocalFree(hy);
        if (hiv) LocalFree(hiv);
        if (hout) LocalFree(hout);
        return ERROR;
    }
    a = rgn_ptr(ha);
    b = rgn_ptr(hb);
    ys = (int *)(int NEAR *)hy;
    iva = (int *)(int NEAR *)hiv;
    ivb = iva + maxiv;
    ivc = ivb + maxiv;
    prev = ivc + maxiv;
    out = (RECT NEAR *)hout;
    ny = y_edges(a, b, ys);
    for (i = 0; i + 1 < ny; i++) {
        int y0 = ys[i], y1 = ys[i + 1];
        int na = band_of(a, y0, &pa, iva), nb = band_of(b, y0, &pb, ivb);
        int nc = combine_iv(mode, iva, na, ivb, nb, ivc), j, same;
        if (!nc) {
            nprev = -1;
            continue;
        }
        same = nprev == nc && out[nout - 1].bottom == y0;
        for (j = 0; same && j < nc; j++)
            if (prev[j] != ivc[j])
                same = 0;
        if (same) {
            for (k = prevStart; k < nout; k++)
                out[k].bottom = y1;
            continue;
        }
        if (nout + nc / 2 > cap) {
            HLOCAL nh;
            cap = nout + nc / 2 + cap;
            nh = LocalReAlloc(hout, cap * sizeof(RECT), LMEM_MOVEABLE);
            if (!nh) {
                LocalFree(hy);
                LocalFree(hiv);
                LocalFree(hout);
                return ERROR;
            }
            hout = nh;
            out = (RECT NEAR *)hout;
            a = rgn_ptr(ha);
            b = rgn_ptr(hb);
            ys = (int *)(int NEAR *)hy;
        }
        prevStart = nout;
        for (j = 0; j < nc; j += 2) {
            out[nout].left = ivc[j];
            out[nout].right = ivc[j + 1];
            out[nout].top = y0;
            out[nout].bottom = y1;
            nout++;
        }
        for (j = 0; j < nc; j++)
            prev[j] = ivc[j];
        nprev = nc;
    }
    d = rgn_reserve(dst, nout ? nout : 1);
    if (d) {
        out = (RECT NEAR *)hout;
        mem_copy(d->r, out, nout * sizeof(RECT));
        d->n = nout;
        rgn_fix_box(d);
    }
    LocalFree(hy);
    LocalFree(hiv);
    LocalFree(hout);
    return d ? rgn_complexity(d) : ERROR;
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
    NPRGN r = rgn_ptr(h);
    if (!r)
        return ERROR;
    rgn_offset(h, dx, dy);
    return rgn_complexity(rgn_ptr(h));
}

int GDIAPI GetRgnBox(HRGN h, RECT __far *rc)                        /* GDI.134 */
{
    NPRGN r = rgn_ptr(h);
    if (!r)
        return ERROR;
    *rc = r->box;
    return rgn_complexity(r);
}

BOOL GDIAPI PtInRegion(HRGN h, int x, int y)                        /* GDI.161 */
{
    NPRGN r = rgn_ptr(h);
    WORD i;
    if (!r)
        return FALSE;
    for (i = 0; i < r->n; i++)
        if (x >= r->r[i].left && x < r->r[i].right && y >= r->r[i].top && y < r->r[i].bottom)
            return TRUE;
    return FALSE;
}

static BOOL rect_in(NPRGN r, const RECT __far *rc)
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
    NPRGN ra = rgn_ptr(a), rb = rgn_ptr(b);
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
