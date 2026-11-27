"""ttgeom - outline geometry for w16's TrueType fonts (tools/ttgen.py).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).

Glyphs are built from pieces (stems, bars, polygons, superelliptic bowls
and arcs, stroked spline paths), each a closed polygon densely sampled
along its curves, with corner flags. union() merges overlapping pieces
into the outline of their union (nonzero winding), shear() slants it for
italics, fit() turns each dense contour into TrueType quadratic splines
(on-curve points at corners and at horizontal/vertical extrema, control
points where the tangents meet, split until the curve is within a
tolerance of the samples).

Coordinates are font units, y up. A contour is (pts, corners): pts a list
of (x, y), corners a list of bools; pieces are counter-clockwise (positive
area) for ink and clockwise for holes. fit() returns TrueType's clockwise
orientation.
"""
import math

STEP = 14.0          # sampling step along curves, font units


class C:
    """a contour: points and corner flags"""
    __slots__ = ('p', 'k')

    def __init__(self, p, k=None):
        self.p = list(p)
        self.k = list(k) if k is not None else [True] * len(self.p)

    def area(self):
        p = self.p
        a = 0.0
        for i in range(len(p)):
            x0, y0 = p[i - 1]
            x1, y1 = p[i]
            a += x0 * y1 - x1 * y0
        return a / 2

    def reversed(self):
        return C(self.p[::-1], self.k[::-1])

    def ccw(self):
        return self if self.area() > 0 else self.reversed()

    def cw(self):
        return self if self.area() < 0 else self.reversed()

    def mapped(self, f):
        return C([f(x, y) for x, y in self.p], self.k)


def xform(shape, f):
    return [c.mapped(f) for c in shape]


def move(shape, dx, dy):
    return xform(shape, lambda x, y: (x + dx, y + dy))


def scale(shape, sx, sy, ox=0.0, oy=0.0):
    out = xform(shape, lambda x, y: (ox + (x - ox) * sx, oy + (y - oy) * sy))
    if sx * sy < 0:
        out = [c.reversed() for c in out]
    return out


def mirror_x(shape, cx):
    return scale(shape, -1, 1, cx, 0)


def mirror_y(shape, cy):
    return scale(shape, 1, -1, 0, cy)


def rotate(shape, deg, ox, oy):
    a = math.radians(deg)
    ca, sa = math.cos(a), math.sin(a)
    return xform(shape, lambda x, y: (ox + (x - ox) * ca - (y - oy) * sa,
                                      oy + (x - ox) * sa + (y - oy) * ca))


def shear(shape, slant, y0=0.0):
    """x += (y - y0) * slant  (slant = tan of the italic angle)"""
    return xform(shape, lambda x, y: (x + (y - y0) * slant, y))


def bbox(shape):
    xs = [x for c in shape for x, _ in c.p]
    ys = [y for c in shape for _, y in c.p]
    if not xs:
        return (0, 0, 0, 0)
    return (min(xs), min(ys), max(xs), max(ys))


# ------------------------------------------------------------ primitives


def poly(pts, corners=None):
    return [C(pts, corners).ccw()]


def rect(x0, y0, x1, y1):
    x0, x1 = min(x0, x1), max(x0, x1)
    y0, y1 = min(y0, y1), max(y0, y1)
    return [C([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])]


def quad(p0, p1, p2, n=None):
    """points of a quadratic Bezier (excluding p0)"""
    if n is None:
        ln = math.dist(p0, p1) + math.dist(p1, p2)
        n = max(2, int(ln / STEP))
    out = []
    for i in range(1, n + 1):
        t = i / n
        a = (1 - t) * (1 - t)
        b = 2 * t * (1 - t)
        c = t * t
        out.append((a * p0[0] + b * p1[0] + c * p2[0], a * p0[1] + b * p1[1] + c * p2[1]))
    return out


def cubic(p0, p1, p2, p3, n=None):
    if n is None:
        ln = math.dist(p0, p1) + math.dist(p1, p2) + math.dist(p2, p3)
        n = max(3, int(ln / STEP))
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return out


class Path:
    """a contour built from lines and curves; corners where segments meet
    unless a segment is added as smooth"""

    def __init__(self, x, y):
        self.p = [(x, y)]
        self.k = [True]

    def line(self, x, y, corner=True):
        self.p.append((x, y))
        self.k.append(corner)
        return self

    def qto(self, cx, cy, x, y, corner=True):
        pts = quad(self.p[-1], (cx, cy), (x, y))
        for q in pts[:-1]:
            self.p.append(q)
            self.k.append(False)
        self.p.append(pts[-1])
        self.k.append(corner)
        return self

    def cto(self, c1x, c1y, c2x, c2y, x, y, corner=True):
        pts = cubic(self.p[-1], (c1x, c1y), (c2x, c2y), (x, y))
        for q in pts[:-1]:
            self.p.append(q)
            self.k.append(False)
        self.p.append(pts[-1])
        self.k.append(corner)
        return self

    def pts(self, pts, corner_last=True):
        for q in pts[:-1]:
            self.p.append(q)
            self.k.append(False)
        self.p.append(pts[-1])
        self.k.append(corner_last)
        return self

    def smooth_last(self):
        self.k[-1] = False
        return self

    def close(self, smooth_start=False):
        p, k = self.p, self.k
        if math.dist(p[0], p[-1]) < 1e-6:
            k[0] = k[0] and k[-1]
            p, k = p[:-1], k[:-1]
        if smooth_start:
            k[0] = False
        return [C(p, k).ccw()]


# superellipse: x = a sgn(c)|c|^(2/n), y = b sgn(s)|s|^(2/n)


def se_point(cx, cy, a, b, n, t):
    c, s = math.cos(t), math.sin(t)
    e = 2.0 / n
    return (cx + a * math.copysign(abs(c) ** e, c), cy + b * math.copysign(abs(s) ** e, s))


def se_pts(cx, cy, a, b, n, t0, t1, rot=0.0):
    """samples from parameter t0 to t1 (radians), both included; rot turns
    the curve about its centre (degrees)"""
    ln = abs(t1 - t0) * (a + b) / 2
    m = max(8, int(ln / STEP))
    out = []
    ca, sa = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    for i in range(m + 1):
        x, y = se_point(0, 0, a, b, n, t0 + (t1 - t0) * i / m)
        out.append((cx + x * ca - y * sa, cy + x * sa + y * ca))
    return out


def ellipse(cx, cy, a, b, n=2.0, rot=0.0):
    pts = se_pts(cx, cy, a, b, n, 0, 2 * math.pi, rot)[:-1]
    return [C(pts, [False] * len(pts)).ccw()]


def ring(cx, cy, a, b, ta, tb, n=2.0, ni=None, rot=0.0, dx=0.0, dy=0.0):
    """a closed bowl: outer superellipse a x b, inner (a - ta) x (b - tb),
    the inner turned by rot degrees and moved by dx, dy (stress)"""
    if ni is None:
        ni = n
    o = ellipse(cx, cy, a, b, n)[0]
    i = ellipse(cx + dx, cy + dy, a - ta, b - tb, ni, rot)[0].cw()
    return [o, i]


def _line_hit(curve, p, d, t_lo, t_hi, steps=64):
    """the parameter in [t_lo, t_hi] where curve(t) meets the line p + s d
    (bisection on a sign change of the cross product, nearest to the middle)"""
    def f(t):
        x, y = curve(t)
        return (x - p[0]) * d[1] - (y - p[1]) * d[0]
    ts = [t_lo + (t_hi - t_lo) * i / steps for i in range(steps + 1)]
    vs = [f(t) for t in ts]
    best = None
    mid = (t_lo + t_hi) / 2
    for i in range(steps):
        if vs[i] == 0 or vs[i] * vs[i + 1] < 0:
            a, b = ts[i], ts[i + 1]
            fa = vs[i]
            for _ in range(50):
                m = (a + b) / 2
                fm = f(m)
                if fa * fm <= 0:
                    b = m
                else:
                    a, fa = m, fm
            r = (a + b) / 2
            if best is None or abs(r - mid) < abs(best - mid):
                best = r
    return best


def arc(cx, cy, a, b, ta, tb, a0, a1, n=2.0, ni=None, cut0=None, cut1=None,
        rot=0.0, dx=0.0, dy=0.0):
    """part of a bowl from parameter angle a0 to a1 (degrees, counter-
    clockwise, a1 > a0). The ends are cut along a line through the outer
    curve's end point at the angle cut0/cut1 (degrees from horizontal;
    None: through the inner curve's point at the same parameter)."""
    if ni is None:
        ni = n
    t0, t1 = math.radians(a0), math.radians(a1)
    ca, sa = math.cos(math.radians(rot)), math.sin(math.radians(rot))

    def inner(t):
        x, y = se_point(0, 0, a - ta, b - tb, ni, t)
        return (cx + dx + x * ca - y * sa, cy + dy + x * sa + y * ca)

    def ends(t, cut):
        po = se_point(cx, cy, a, b, n, t)
        if cut is None:
            return po, t
        d = (math.cos(math.radians(cut)), math.sin(math.radians(cut)))
        ti = _line_hit(inner, po, d, t - 1.2, t + 1.2)
        if ti is None:
            ti = t
        return po, ti

    po0, ti0 = ends(t0, cut0)
    po1, ti1 = ends(t1, cut1)
    outer = se_pts(cx, cy, a, b, n, t0, t1)
    m = max(8, int(abs(ti1 - ti0) * (a + b) / 2 / STEP))
    inn = [inner(ti1 + (ti0 - ti1) * i / m) for i in range(m + 1)]
    pts = outer + inn
    k = [True] + [False] * (len(outer) - 2) + [True, True] + [False] * (len(inn) - 2) + [True]
    return [C(pts, k).ccw()]


def stem(x0, y0, x1, y1, w):
    """a straight stroke between two points, w wide measured across x, ends
    cut horizontally"""
    h = w / 2
    return poly([(x0 - h, y0), (x0 + h, y0), (x1 + h, y1), (x1 - h, y1)])


def diag(x0, y0, x1, y1, w):
    """a straight stroke with true thickness w (perpendicular), ends cut
    horizontally"""
    ang = math.atan2(x1 - x0, y1 - y0)
    return stem(x0, y0, x1, y1, w / max(0.2, math.cos(ang)))


def hstroke(x0, y0, x1, y1, w):
    """a straight stroke, w thick measured across y, ends cut vertically"""
    h = w / 2
    return poly([(x0, y0 - h), (x1, y1 - h), (x1, y1 + h), (x0, y0 + h)])


def nstroke(x0, y0, x1, y1, w):
    """a straight stroke, ends cut square"""
    d = math.dist((x0, y0), (x1, y1)) or 1
    nx, ny = -(y1 - y0) / d * w / 2, (x1 - x0) / d * w / 2
    return poly([(x0 - nx, y0 - ny), (x1 - nx, y1 - ny), (x1 + nx, y1 + ny), (x0 + nx, y0 + ny)])


# ------------------------------------------------------- stroked splines


def hermite(pts, tans=None, tension=0.5):
    """a smooth curve through pts; tans[i] a direction (dx, dy) or None
    (Catmull-Rom). Returns a function t in [0, len(pts) - 1] -> (x, y)."""
    n = len(pts)
    T = []
    for i in range(n):
        t = tans[i] if tans and tans[i] is not None else None
        if t is None:
            if i == 0:
                t = (pts[1][0] - pts[0][0], pts[1][1] - pts[0][1])
            elif i == n - 1:
                t = (pts[-1][0] - pts[-2][0], pts[-1][1] - pts[-2][1])
            else:
                t = ((pts[i + 1][0] - pts[i - 1][0]) / 2, (pts[i + 1][1] - pts[i - 1][1]) / 2)
            T.append(t)
        else:
            T.append(t)
    # scale explicit tangents to the neighbouring chord lengths
    S = []
    for i in range(n):
        d0 = math.dist(pts[i], pts[i - 1]) if i > 0 else None
        d1 = math.dist(pts[i], pts[i + 1]) if i < n - 1 else None
        L = [d for d in (d0, d1) if d is not None]
        ln = sum(L) / len(L)
        tl = math.hypot(*T[i]) or 1
        S.append((T[i][0] / tl * ln * 2 * tension, T[i][1] / tl * ln * 2 * tension))

    def f(t):
        i = min(int(t), n - 2)
        u = t - i
        p0, p1 = pts[i], pts[i + 1]
        m0, m1 = S[i], S[i + 1]
        h00 = 2 * u ** 3 - 3 * u ** 2 + 1
        h10 = u ** 3 - 2 * u ** 2 + u
        h01 = -2 * u ** 3 + 3 * u ** 2
        h11 = u ** 3 - u ** 2
        return (h00 * p0[0] + h10 * m0[0] + h01 * p1[0] + h11 * m1[0],
                h00 * p0[1] + h10 * m0[1] + h01 * p1[1] + h11 * m1[1])
    f.n = n
    return f


def _cut_side(side, p, d, at_start, within):
    """trim a polyline where it first crosses the line p + s d, looking
    from its start (at_start) or from its end, at most `within` units of
    length in"""
    def cr(q):
        return (q[0] - p[0]) * d[1] - (q[1] - p[1]) * d[0]
    n = len(side)
    idx = list(range(n - 1)) if at_start else list(range(n - 2, -1, -1))
    walked = 0.0
    for i in idx:
        a, b = side[i], side[i + 1]
        walked += math.dist(a, b)
        if walked > within:
            break
        ca, cb = cr(a), cr(b)
        if ca == 0 or ca * cb < 0:
            t = ca / (ca - cb) if ca != cb else 0
            x = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
            if at_start:
                return [x] + side[i + 1:]
            return side[:i + 1] + [x]
    return side


def stroke(pts, tans=None, wv=100, wh=None, widths=None, cap0=None, cap1=None,
           stress=0.0, tension=0.5, ext=0.0):
    """a stroke along a smooth path through pts. Its thickness follows the
    direction (an elliptical pen: wv across a vertical stroke, wh across a
    horizontal one, the axis turned by stress degrees), times widths[i] at
    pts[i] if given (interpolated). cap0/cap1: the end cut's angle
    (degrees from horizontal), None for square ends. ext lengthens the
    centre line at both ends before a cut."""
    if wh is None:
        wh = wv
    f = hermite(pts, tans, tension)
    n = len(pts)
    total = sum(math.dist(pts[i], pts[i + 1]) for i in range(n - 1))
    m = max(16, int(total / (STEP * 0.7)))
    sa = math.radians(stress)
    left, right = [], []
    ts = [(n - 1) * i / m for i in range(m + 1)]
    cen = [f(t) for t in ts]
    for i, t in enumerate(ts):
        a = cen[max(0, i - 1)]
        b = cen[min(m, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        dl = math.hypot(dx, dy) or 1
        dx, dy = dx / dl, dy / dl
        th = math.atan2(dy, dx) - sa
        w = math.hypot(wv * math.sin(th), wh * math.cos(th))
        if widths:
            j = min(int(t), n - 2)
            u = t - j
            w *= widths[j] * (1 - u) + widths[j + 1] * u
        x, y = cen[i]
        left.append((x - dy * w / 2, y + dx * w / 2))
        right.append((x + dy * w / 2, y - dx * w / 2))
    # extend for cuts
    def extend(side, at_start, length):
        if at_start:
            a, b = side[0], side[1]
        else:
            a, b = side[-1], side[-2]
        d = math.dist(a, b) or 1
        q = (a[0] + (a[0] - b[0]) / d * length, a[1] + (a[1] - b[1]) / d * length)
        return [q] + side if at_start else side + [q]
    if cap0 is not None:
        e = max(ext, wv, wh)
        left, right = extend(left, True, e), extend(right, True, e)
        d = (math.cos(math.radians(cap0)), math.sin(math.radians(cap0)))
        left = _cut_side(left, cen[0], d, True, 4 * e)
        right = _cut_side(right, cen[0], d, True, 4 * e)
    if cap1 is not None:
        e = max(ext, wv, wh)
        left, right = extend(left, False, e), extend(right, False, e)
        d = (math.cos(math.radians(cap1)), math.sin(math.radians(cap1)))
        left = _cut_side(left, cen[-1], d, False, 4 * e)
        right = _cut_side(right, cen[-1], d, False, 4 * e)
    pts_ = left + right[::-1]
    k = [True] + [False] * (len(left) - 2) + [True, True] + [False] * (len(right) - 2) + [True]
    return [C(pts_, k).ccw()]


def blob(cx, cy, r, rx=None):
    """a round dot / ball terminal"""
    return ellipse(cx, cy, rx if rx is not None else r, r, 2.0)


# --------------------------------------------------------------- union


def _segs_intersect(a, b, c, d):
    """intersection of segments a-b and c-d: (t, u, point) with t, u in
    [0, 1) or None"""
    rx, ry = b[0] - a[0], b[1] - a[1]
    sx, sy = d[0] - c[0], d[1] - c[1]
    den = rx * sy - ry * sx
    if den == 0:
        return None
    qx, qy = c[0] - a[0], c[1] - a[1]
    t = (qx * sy - qy * sx) / den
    u = (qx * ry - qy * rx) / den
    if 0 <= t < 1 and 0 <= u < 1:
        return t, u, (a[0] + t * rx, a[1] + t * ry)
    return None


def _winding(contours, px, py):
    w = 0
    for c in contours:
        p = c.p
        n = len(p)
        for i in range(n):
            x0, y0 = p[i]
            x1, y1 = p[(i + 1) % n]
            if y0 <= py:
                if y1 > py and (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0) > 0:
                    w += 1
            elif y1 <= py and (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0) < 0:
                w -= 1
    return w


def _chunks(c, size=8):
    p = c.p
    n = len(p)
    out = []
    for s in range(0, n, size):
        e = min(n, s + size)
        xs = [p[i % n][0] for i in range(s, e + 1)]
        ys = [p[i % n][1] for i in range(s, e + 1)]
        out.append((s, e, min(xs), min(ys), max(xs), max(ys)))
    return out


def union(shape):
    """the outline of the union of the contours (nonzero winding); holes
    (clockwise contours) take part as such"""
    # break exact coincidences: move each contour by a tiny distinct amount
    cs = []
    for i, c in enumerate(shape):
        if len(c.p) < 3 or abs(c.area()) < 1e-6:
            continue
        dx, dy = 0.00731 * (i + 1), 0.00419 * (i + 1) * (1 if i % 2 else -1)
        cs.append(C([(x + dx, y + dy) for x, y in c.p], c.k))
    nc = len(cs)
    if nc <= 1:
        return cs
    boxes = [bbox([c]) for c in cs]
    chunks = [_chunks(c) for c in cs]
    hits = [[] for _ in range(nc)]      # per contour: (edge, t, node id)
    nodes = []
    for i in range(nc):
        for j in range(i + 1, nc):
            bi, bj = boxes[i], boxes[j]
            if bi[2] < bj[0] or bj[2] < bi[0] or bi[3] < bj[1] or bj[3] < bi[1]:
                continue
            pi, pj = cs[i].p, cs[j].p
            ni, nj = len(pi), len(pj)
            for (s0, e0, x0, y0, x1, y1) in chunks[i]:
                for (s1, e1, u0, v0, u1, v1) in chunks[j]:
                    if x1 < u0 or u1 < x0 or y1 < v0 or v1 < y0:
                        continue
                    for a in range(s0, e0):
                        A, B = pi[a], pi[(a + 1) % ni]
                        for b in range(s1, e1):
                            r = _segs_intersect(A, B, pj[b], pj[(b + 1) % nj])
                            if r:
                                nid = len(nodes)
                                nodes.append(r[2])
                                hits[i].append((a, r[0], nid))
                                hits[j].append((b, r[1], nid))
    runs = []       # (start node, end node, points, corners)
    whole = []
    for i, c in enumerate(cs):
        others = cs[:i] + cs[i + 1:]
        sign = 1 if c.area() > 0 else -1
        p, k = c.p, c.k
        n = len(p)

        def keep(pt):
            w = _winding(others, pt[0], pt[1])
            return (w == 0) != (w + sign == 0)
        if not hits[i]:
            m = ((p[0][0] + p[1][0]) / 2, (p[0][1] + p[1][1]) / 2)
            if keep(m):
                whole.append(c)
            continue
        hs = sorted(hits[i])
        for h in range(len(hs)):
            ea, ta, na = hs[h]
            eb, tb, nb = hs[(h + 1) % len(hs)]
            pts = [nodes[na]]
            ks = [True]
            e = ea
            wrap = (eb, tb) <= (ea, ta)
            # vertices after edge ea up to edge eb's start
            cnt = (eb - ea) % n
            if wrap and cnt == 0:
                cnt = n
            for s in range(cnt):
                v = (ea + 1 + s) % n
                pts.append(p[v])
                ks.append(k[v])
            pts.append(nodes[nb])
            ks.append(True)
            # a point on the run to test
            if len(pts) > 2:
                mi = len(pts) // 2
                q0, q1 = pts[mi - 1], pts[mi]
            else:
                q0, q1 = pts[0], pts[1]
            mpt = ((q0[0] + q1[0]) / 2, (q0[1] + q1[1]) / 2)
            if math.dist(q0, q1) < 1e-9:
                continue
            if keep(mpt):
                runs.append((na, nb, pts, ks))
    # chain the runs
    by_start = {}
    for r in runs:
        by_start.setdefault(r[0], []).append(r)
    used = set()
    out = list(whole)
    for r in runs:
        if id(r) in used:
            continue
        pts, ks = [], []
        cur = r
        start = r[0]
        ok = False
        for _ in range(len(runs) + 1):
            used.add(id(cur))
            pts.extend(cur[2][:-1])
            ks.extend(cur[3][:-1])
            if cur[1] == start:
                ok = True
                break
            nxt = [q for q in by_start.get(cur[1], []) if id(q) not in used]
            if not nxt:
                break
            cur = nxt[0]
        if ok and len(pts) >= 3:
            cc = C(pts, ks)
            if abs(cc.area()) > 4:
                out.append(cc)
    return out


# ----------------------------------------------------------------- fit


def _dedupe(c):
    p, k = [], []
    for (x, y), f in zip(c.p, c.k):
        if p and math.dist(p[-1], (x, y)) < 0.05:
            k[-1] = k[-1] or f
            continue
        p.append((x, y))
        k.append(f)
    while len(p) > 1 and math.dist(p[0], p[-1]) < 0.05:
        k[0] = k[0] or k[-1]
        p.pop()
        k.pop()
    return p, k


def _unit(dx, dy):
    d = math.hypot(dx, dy)
    return (dx / d, dy / d) if d > 1e-12 else (0.0, 0.0)


def _qpt(p0, c, p2, t):
    u = 1 - t
    return (u * u * p0[0] + 2 * u * t * c[0] + t * t * p2[0],
            u * u * p0[1] + 2 * u * t * c[1] + t * t * p2[1])


def _dist_to_poly(pt, poly_):
    best = 1e18
    for i in range(len(poly_) - 1):
        a, b = poly_[i], poly_[i + 1]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = dx * dx + dy * dy
        t = 0 if L == 0 else max(0, min(1, ((pt[0] - a[0]) * dx + (pt[1] - a[1]) * dy) / L))
        q = (a[0] + t * dx, a[1] + t * dy)
        d = (pt[0] - q[0]) ** 2 + (pt[1] - q[1]) ** 2
        if d < best:
            best = d
    return math.sqrt(best)


def _fit_run(pts, t0, t1, tol, out):
    """append quadratic segments for the samples pts (first and last are
    on-curve, t0/t1 their tangent directions, None at a sharp end: the
    one-sided direction is used). out gets (point, on) pairs after pts[0]."""
    n = len(pts)
    if n == 2:
        out.append((pts[1], True))
        return
    p0, p2 = pts[0], pts[-1]
    # straight?
    chord = math.dist(p0, p2)
    if chord > 0:
        mx = max(abs((q[0] - p0[0]) * (p2[1] - p0[1]) - (q[1] - p0[1]) * (p2[0] - p0[0])) / chord
                 for q in pts[1:-1])
        if mx < tol * 0.5:
            out.append((p2, True))
            return
    d0 = t0 or _unit(pts[1][0] - p0[0], pts[1][1] - p0[1])
    d1 = t1 or _unit(p2[0] - pts[-2][0], p2[1] - pts[-2][1])
    den = d0[0] * d1[1] - d0[1] * d1[0]
    ctrl = None
    if abs(den) > 1e-6:
        qx, qy = p2[0] - p0[0], p2[1] - p0[1]
        s = (qx * d1[1] - qy * d1[0]) / den
        u = (d0[0] * qy - d0[1] * qx) / den
        if s > 0 and u > 0 and s < chord * 2 and u < chord * 2:
            ctrl = (p0[0] + s * d0[0], p0[1] + s * d0[1])
    if ctrl is not None:
        curve = [_qpt(p0, ctrl, p2, i / 24) for i in range(25)]
        err = max(_dist_to_poly(q, curve) for q in pts[1:-1])
        if err <= tol or n <= 3:
            out.append((ctrl, False))
            out.append((p2, True))
            return
    if n <= 3:
        out.append((pts[1], True))
        out.append((p2, True))
        return
    m = n // 2
    a, b = pts[m - 1], pts[m + 1]
    tm = _unit(b[0] - a[0], b[1] - a[1])
    _fit_run(pts[:m + 1], t0, tm, tol, out)
    _fit_run(pts[m:], tm, t1, tol, out)


def fit(shape, tol=1.2):
    """TrueType contours (lists of (x, y, on), integers, clockwise ink)"""
    result = []
    for c in shape:
        p, k = _dedupe(c)
        n = len(p)
        if n < 3:
            continue
        # sharp turns are corners too
        for i in range(n):
            a, b, cc = p[i - 1], p[i], p[(i + 1) % n]
            u, v = _unit(b[0] - a[0], b[1] - a[1]), _unit(cc[0] - b[0], cc[1] - b[1])
            if u[0] * v[0] + u[1] * v[1] < math.cos(math.radians(28)):
                k[i] = True
        # split points: corners, and extrema in x or y on smooth stretches
        split = set(i for i in range(n) if k[i])
        for i in range(n):
            if k[i]:
                continue
            a, b, cc = p[i - 1], p[i], p[(i + 1) % n]
            for ax in (0, 1):
                d0 = b[ax] - a[ax]
                d1 = cc[ax] - b[ax]
                if d0 * d1 < 0 or (d0 == 0 and d1 != 0):
                    split.add(i)
        if not split:
            split.add(0)
        # merge split points that are neighbours on smooth stretches
        sp = sorted(split)
        tan = {}
        for i in sp:
            if k[i]:
                tan[i] = None
            else:
                a, b = p[i - 1], p[(i + 1) % n]
                d = _unit(b[0] - a[0], b[1] - a[1])
                # snap near-axis tangents at extrema
                if abs(d[0]) < 0.08:
                    d = (0.0, math.copysign(1.0, d[1]))
                elif abs(d[1]) < 0.08:
                    d = (math.copysign(1.0, d[0]), 0.0)
                tan[i] = d
        out = []
        first = sp[0]
        pts_out = [(p[first], True)]
        for j in range(len(sp)):
            s = sp[j]
            e = sp[(j + 1) % len(sp)]
            cnt = (e - s) % n or n
            run = [p[(s + q) % n] for q in range(cnt + 1)]
            seg = []
            _fit_run(run, tan[s], tan[e], tol, seg)
            pts_out.extend(seg)
        pts_out.pop()       # the first point again
        # round, drop duplicates and on-curve points between collinear lines
        r = []
        for (x, y), on in pts_out:
            q = (int(round(x)), int(round(y)), on)
            if r and r[-1][:2] == q[:2]:
                if on and not r[-1][2]:
                    r[-1] = q
                continue
            r.append(q)
        while len(r) > 1 and r[0][:2] == r[-1][:2]:
            r.pop()
        changed = True
        while changed and len(r) > 3:
            changed = False
            for i in range(len(r)):
                a, b, cc = r[i - 1], r[i], r[(i + 1) % len(r)]
                if a[2] and b[2] and cc[2]:
                    cr = (b[0] - a[0]) * (cc[1] - a[1]) - (b[1] - a[1]) * (cc[0] - a[0])
                    if cr == 0 and (b[0] - a[0]) * (cc[0] - b[0]) + (b[1] - a[1]) * (cc[1] - b[1]) >= 0:
                        del r[i]
                        changed = True
                        break
        if len(r) < 3:
            continue
        # TrueType: clockwise ink
        a = 0
        for i in range(len(r)):
            a += r[i - 1][0] * r[i][1] - r[i][0] * r[i - 1][1]
        if abs(a) < 2:
            continue
        r = r[::-1]
        # start on an on-curve point
        while not r[0][2]:
            r = r[1:] + r[:1]
        result.append(r)
    return result
