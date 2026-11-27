"""w16's TrueType designs: w16 Sans, w16 Serif and w16 Mono.

w16's own work, dedicated to the public domain under CC0 1.0 (LICENSE).

Every glyph is drawn here from scratch as geometry: stems, bars, diagonal
strokes, superelliptic bowls and arcs, stroked spline paths, serifs and
dots (tools/ttgeom.py), in parameters of the face (Style). No outline of
any other font was looked at, traced or measured; only the advance widths,
the horizontal extent of each character's ink (GetCharABCWidths' A and B)
and the face's vertical metrics (x-height, cap height, ascender and
descender from GGO_METRICS black boxes of x, H, d, p) come from Windows
3.1's fonts, observed through the API by probe FN4 (tests/apps/fn4.c,
tests/fonts/fn4ref.txt), so that text sets the same.

A glyph function g(s, L, R) draws the character upright with its ink from
x = L to x = R (the reference's A and A + B); tools/ttgen.py shears italic
faces and fits the result to the ink box. Kinds:

  sans   a neo-grotesque: near-uniform strokes, squarish bowls
         (superellipses), horizontal terminals
  serif  a transitional roman: contrast (thick verticals, hairline
         horizontals), bracketed serifs, ball terminals; its italic has
         cursive lowercase (single-storey a, entry and exit strokes)
  mono   a typewriter slab: low contrast, unbracketed slab serifs, every
         character in the same advance
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import ttgeom as G  # noqa: E402

# ------------------------------------------------------------------ style


class Style:
    """the parameters of one face; metrics in font units (em 2048)"""

    def __init__(self, kind, bold, italic, vm):
        self.kind = kind
        self.bold = bold
        self.italic = italic
        self.serif = kind == 'serif'
        self.mono = kind == 'mono'
        self.sans = kind == 'sans'
        self.cap = vm['cap']
        self.xh = vm['xh']
        self.asc = vm['asc']
        self.desc = vm['desc']           # negative
        self.ov = vm.get('ov', 18)
        self.slant = math.tan(math.radians(vm.get('angle', 0))) if italic else 0.0
        b = bold
        if kind == 'sans':
            self.sv = 268 if b else 186          # lowercase stem
            self.svc = 282 if b else 196         # capital stem
            self.sh = 214 if b else 164          # horizontal strokes
            self.shc = 226 if b else 172
            self.hair = self.sh
            self.n, self.ni = 2.35, 2.6
            self.thin = self.sh                  # thin diagonals
        elif kind == 'serif':
            self.sv = 258 if b else 172
            self.svc = 290 if b else 190
            self.sh = 124 if b else 90
            self.shc = 132 if b else 98
            self.hair = 118 if b else 84
            self.n, self.ni = 2.05, 2.15
            self.thin = 142 if b else 94
            self.sl = 120 if b else 112          # serif length beyond the stem
            self.st = 92 if b else 74            # serif thickness
            self.sb = 56 if b else 50            # bracket
        else:
            self.sv = 232 if b else 140
            self.svc = 236 if b else 144
            self.sh = 210 if b else 124
            self.shc = 214 if b else 128
            self.hair = self.sh
            self.n, self.ni = 2.15, 2.3
            self.thin = self.sv * 0.92
            self.sl = 140                        # slab length beyond the stem
            self.st = self.sh                    # slab thickness
            self.sb = 0
        self.dot = self.sv * (1.18 if self.serif else 1.1)
        self.diag = {'sans': 0.86, 'serif': 0.9, 'mono': 0.86}[kind] + (0.04 if b else 0)
        # the counters' stress, degrees: none (an inclined stress put the
        # bowls' thick and thin parts between pixels and made w16 Serif's
        # round letters look slanted at 8-10 pt)
        self.stress = 0

    # vertical zones
    def top(self, caps):
        return self.cap if caps else self.xh


# ---------------------------------------------------------------- helpers


def U(*shapes):
    out = []
    for s in shapes:
        out.extend(s)
    return out


def ring_box(s, o, i, rot=0.0, n=None, ni=None):
    """a closed bowl: outer box o, inner box i, (x0, y0, x1, y1)"""
    n = n or s.n
    ni = ni or s.ni
    cxo, cyo, ao, bo = (o[0] + o[2]) / 2, (o[1] + o[3]) / 2, (o[2] - o[0]) / 2, (o[3] - o[1]) / 2
    cxi, cyi, ai, bi = (i[0] + i[2]) / 2, (i[1] + i[3]) / 2, (i[2] - i[0]) / 2, (i[3] - i[1]) / 2
    out = G.ellipse(cxo, cyo, ao, bo, n)
    out += [G.ellipse(cxi, cyi, ai, bi, ni, rot)[0].cw()]
    return out


def arc_box(s, o, i, a0, a1, cut0=None, cut1=None, rot=0.0, n=None, ni=None):
    """part of a bowl (outer box o, inner box i) from angle a0 to a1
    (degrees, counter-clockwise; 0 is the right side, 90 the top)"""
    n = n or s.n
    ni = ni or s.ni
    cxo, cyo, ao, bo = (o[0] + o[2]) / 2, (o[1] + o[3]) / 2, (o[2] - o[0]) / 2, (o[3] - o[1]) / 2
    cxi, cyi, ai, bi = (i[0] + i[2]) / 2, (i[1] + i[3]) / 2, (i[2] - i[0]) / 2, (i[3] - i[1]) / 2
    return G.arc(cxo, cyo, ao, bo, ao - ai, bo - bi, a0, a1, n, ni, cut0, cut1,
                 rot, cxi - cxo, cyi - cyo)


def bowl(s, x0, x1, y0, y1, tv=None, th=None, rot=None):
    """an o-like ring with its ink in [x0, x1] x [y0, y1]"""
    tv = tv if tv is not None else s.sv
    th = th if th is not None else s.hair
    if rot is None:
        rot = s.stress
    return ring_box(s, (x0, y0, x1, y1), (x0 + tv, y0 + th, x1 - tv, y1 - th), rot)


def vstem(x0, x1, y0, y1):
    return G.rect(x0, y0, x1, y1)


def hbar(x0, x1, yc, t):
    return G.rect(x0, yc - t / 2, x1, yc + t / 2)


def dot(s, cx, cy, r=None):
    r = r or s.dot / 2
    if s.sans:
        # a square-ish dot for the grotesque
        return G.ellipse(cx, cy, r, r, 3.2)
    return G.ellipse(cx, cy, r, r * 1.02, 2.0)


# ---- serifs: no-ops for the sans


def _bracket_poly(x_stem, y, sl, st, sb, side, up):
    """one side of a bracketed serif: a slab from the stem edge outwards
    with a concave fillet up the stem. side -1 left, +1 right; up +1 for a
    foot (serif at the bottom), -1 for a head"""
    sgn = side
    y0 = y
    y1 = y + up * st
    tip = x_stem + sgn * sl
    p = G.Path(x_stem - sgn * 26, y0)
    # a square tip, as thick as the serif (a thinner tip than serif
    # rounds away on the screen: serifs went missing at 8-10 pt)
    p.line(tip, y0)
    p.line(tip, y1)
    if sb > 0:
        p.qto(x_stem + sgn * sb * 0.15, y1, x_stem, y1 + up * sb)
    else:
        p.line(x_stem, y1)
    p.line(x_stem - sgn * 26, y1 + up * max(sb, 1))
    return p.close()


def serif_at(s, xl, xr, y, up, l=1.0, r=1.0, bracket=True):
    """serifs at the end of a stem from xl to xr: at y (the stem's end),
    pointing away from the stem (up = +1 at a foot, -1 at a head); l, r
    scale the left and right serif (0: none)"""
    if s.sans:
        return []
    out = []
    sb = s.sb if bracket else 0
    if l:
        out += _bracket_poly(xl, y, s.sl * l, s.st, sb, -1, up)
    if r:
        out += _bracket_poly(xr, y, s.sl * r, s.st, sb, +1, up)
    if s.mono and l and r:
        out += G.rect(xl - s.sl * l, y, xr + s.sl * r, y + up * s.st)
    return out


def foot(s, xl, xr, y=0, l=1.0, r=1.0):
    return serif_at(s, xl, xr, y, +1, l, r)


def head(s, xl, xr, y, l=1.0, r=1.0):
    return serif_at(s, xl, xr, y, -1, l, r)


def dfoot(s, xl, xr, y=0, l=1.0, r=1.0):
    """serifs at the end of a diagonal: unbracketed"""
    return serif_at(s, xl, xr, y, +1, l, r, False)


def dhead(s, xl, xr, y, l=1.0, r=1.0):
    return serif_at(s, xl, xr, y, -1, l, r, False)


def flag(s, xl, xr, ytop):
    """the head serif of a lowercase stem (b, d, h, i, j, k, l, m, n, p, r,
    u): an oblique wedge to the left (serif), a slab to the left (mono)"""
    if s.sans:
        return []
    if s.mono:
        return G.rect(xl - s.sl * 0.95, ytop - s.st, xr - 2, ytop)
    sl = s.sl * 1.25
    t = s.st * 1.1
    p = G.Path(xr, ytop)
    p.line(xl - sl, ytop - t * 1.55)
    p.line(xl - sl, ytop - t * 2.4)
    p.qto(xl - sl * 0.2, ytop - t * 2.3, xl, ytop - t * 2.3 - s.sb * 1.2)
    p.line(xr, ytop - t * 3.5)
    return p.close()


def beak(s, x, y, down, length=None):
    """the vertical serif at the end of an arm (E, F, L, T, Z): from the
    arm at x (its end) extending up or down"""
    if s.sans:
        return []
    length = length or (s.sl * 1.25 if s.serif else s.sl * 0.75)
    w = s.st * (1.35 if s.serif else 1.0)
    d = -1 if down else 1
    if s.serif:
        p = G.Path(x, y)
        p.line(x, y + d * length)
        p.line(x - w * 0.5, y + d * length)
        p.qto(x - w * 1.1, y + d * length * 0.3, x - w * 3.0, y)
        return p.close()
    return G.rect(x - w, y, x, y + d * length)


def ball(s, cx, cy, r=None):
    """a ball terminal (serif) / round end (mono)"""
    r = r or s.sv * 0.62
    return G.ellipse(cx, cy, r, r * 1.05, 2.0)


# --------------------------------------------------------- glyph helpers


def arch(s, xl, xr, top, join=None, sv=None, th=None):
    """the shoulder of n, h, m: from the stem at xl (its left edge) to the
    right stem ending at xr, top of the arch at top (+ overshoot); join is
    where the right side turns vertical"""
    sv = sv or s.sv
    th = th or s.sh
    join = join if join is not None else top * (0.52 if s.sans else 0.5)
    outer_top = top + s.ov * 0.6
    b = outer_top - join
    o = (xl + sv * 0.5, join - b, xr, outer_top)
    i = (xl + sv - 6, join - b + th, xr - sv, outer_top - th)
    if s.serif:
        # the arch springs thinly from the stem
        i = (xl + sv * 0.86, join - b + th, xr - sv, outer_top - th)
    a = arc_box(s, o, i, 0, 180, None, None)
    return a + vstem(xr - sv, xr, 0, join + 2)


def open_bowl(s, x0, x1, y0, y1, a0, a1, cut0=0, cut1=0, tv=None, th=None, rot=None):
    tv = tv if tv is not None else s.sv
    th = th if th is not None else s.hair
    if rot is None:
        rot = s.stress
    return arc_box(s, (x0, y0, x1, y1), (x0 + tv, y0 + th, x1 - tv, y1 - th), a0, a1, cut0, cut1, rot)


def side_bowl(s, xstem_l, xstem_r, xr, y0, y1, tv=None, th=None, right=True):
    """the bowl of b, d, p, q: joined to the stem whose edges are
    xstem_l, xstem_r, its far side at xr; right: the bowl is on the right"""
    tv = tv if tv is not None else s.sv
    th = th if th is not None else s.hair
    if right:
        o = (xstem_l + (xstem_r - xstem_l) * 0.5, y0, xr, y1)
        i = (xstem_r - 8, y0 + th, xr - tv, y1 - th)
        if s.serif:
            i = (xstem_r - 2, y0 + th, xr - tv, y1 - th)
    else:
        o = (xr, y0, xstem_l + (xstem_r - xstem_l) * 0.5, y1)
        i = (xr + tv, y0 + th, xstem_l + 8, y1 - th)
        if s.serif:
            i = (xr + tv, y0 + th, xstem_l + 2, y1 - th)
    return ring_box(s, o, i, 8 if s.serif else 0)


def dbowl(s, xs, x1, y0, y1, tv=None, th=None):
    """the bowl of B, D, P, R: horizontal strokes from the stem (inner edge
    xs) out to a half-round right side at x1, between y0 and y1"""
    tv = tv if tv is not None else s.svc
    th = th if th is not None else s.shc
    h = y1 - y0
    r = min(h / 2, (x1 - xs) * 0.62)
    cx = x1 - r
    o = (cx - r, y0, x1, y1)
    i = (cx - r + tv, y0 + th, x1 - tv, y1 - th)
    out = arc_box(s, o, i, -90, 90, None, None, n=s.n + 0.15, ni=s.ni + 0.15)
    out += G.rect(xs - 10, y0, cx + 2, y0 + th)
    out += G.rect(xs - 10, y1 - th, cx + 2, y1)
    return out


def xline(p0, p1, q0, q1):
    """where line p0-p1 meets line q0-q1"""
    rx, ry = p1[0] - p0[0], p1[1] - p0[1]
    sx, sy = q1[0] - q0[0], q1[1] - q0[1]
    den = rx * sy - ry * sx
    if abs(den) < 1e-9:
        return p1
    t = ((q0[0] - p0[0]) * sy - (q0[1] - p0[1]) * sx) / den
    return (p0[0] + t * rx, p0[1] + t * ry)


def diag_pair(s, x0, x1, apex_x, top, w_l, w_r, flat=None, base=0):
    """two diagonals of true thickness w_l, w_r meeting at the top (A),
    their outer edges from x0 and x1 at the base to the apex, which is
    flat wide"""
    flat = flat if flat is not None else (w_l + w_r) * 0.18
    flat = max(flat, 6)
    al, ar = (apex_x - flat / 2, top), (apex_x + flat / 2, top)
    hl = dw(w_l, al[0] - x0, top - base)
    hr = dw(w_r, x1 - ar[0], top - base)
    # inner edges
    il0, il1 = (x0 + hl, base), (al[0] + hl, top)
    ir0, ir1 = (x1 - hr, base), (ar[0] - hr, top)
    P = xline(il0, il1, (x1, base), ar)
    Q = xline(ir0, ir1, (x0, base), al)
    out = G.poly([(x0, base), il0, P, ar, al])
    out += G.poly([ir0, (x1, base), ar, al, Q])
    return out


def vee(s, x0, x1, top, w_l, w_r, flat=None, bottom=0):
    """two diagonals meeting at the bottom (V, v)"""
    sh = mirror_v(diag_pair(s, x0, x1, (x0 + x1) / 2, top - bottom, w_l, w_r, flat), (top - bottom) / 2)
    return G.move(sh, 0, bottom)


def mirror_v(shape, cy):
    return G.mirror_y(shape, cy)


def slanted(x_top, x_bot, y_top, y_bot, w):
    """a stroke between (x_top, y_top) and (x_bot, y_bot), w measured
    horizontally, cut horizontally"""
    return G.stem(x_bot, y_bot, x_top, y_top, w)


# diagonal strokes are drawn this much lighter than the stems they are
# given as (Style.diag; set by draw()): a diagonal as thick as a stem
# covers more pixels across a row, and v, w, x, y came out a pixel
# heavier than the stems at 10-14 pt
_DIAG = [1.0]


def dw(w, dx, dy):
    """horizontal width of a stroke of thickness w with run dx over rise dy"""
    return w * _DIAG[0] / math.cos(math.atan2(abs(dx), abs(dy)))


def tail_hook(s, xc, xl, ybot, sv, th, side=-1, depth=None, cut=0):
    """a hook at the bottom of a stem (j, J, t, f's top mirrored): from the
    stem whose centre is xc bending towards side (-1 left) to the end x xl;
    the bottom of the hook at ybot"""
    depth = depth or (abs(xc - xl) * 0.9)
    if side < 0:
        o = (xl, ybot, xc + sv / 2, ybot + 2 * depth)
        i = (xl + sv * 0.1, ybot + th, xc - sv / 2, ybot + 2 * depth - th)
        return arc_box(s, o, i, 180 + 12, 360, cut, None)
    o = (xc - sv / 2, ybot, xl, ybot + 2 * depth)
    i = (xc + sv / 2, ybot + th, xl - sv * 0.1, ybot + 2 * depth - th)
    return arc_box(s, o, i, 180, 360 - 12, None, cut)


# ================================================================ capitals


def g_A(s, L, R):
    top = s.cap
    w_thick = s.svc * (1.08 if s.serif else 1.0)
    w_thin = s.thin if not s.sans else s.svc * 0.96
    flat = 0 if s.serif else (s.svc * 0.35 if s.sans else s.sv * 0.5)
    inset = (s.sl * 0.85) if not s.sans else 0
    x0, x1 = L + inset, R - inset
    cx = (x0 + x1) / 2 + (s.svc * 0.06 if s.serif else 0)
    out = diag_pair(s, x0, x1, cx, top + (s.ov if s.serif else 0), w_thin, w_thick, flat)
    # crossbar
    yb = top * (0.30 if s.sans else 0.33)
    t = s.shc
    out += G.rect(x0 + (x1 - x0) * 0.2, yb - t / 2, x1 - (x1 - x0) * 0.2, yb + t / 2)
    if not s.sans:
        wl = dw(w_thin, cx - x0, top)
        wr = dw(w_thick, x1 - cx, top)
        out += dfoot(s, x0, x0 + wl, 0, 1.0, 0.9)
        out += dfoot(s, x1 - wr, x1, 0, 1.0, 1.0)
    return out


def g_B(s, L, R):
    top = s.cap
    sv, th = s.svc, s.shc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    ym = top * 0.54
    out += dbowl(s, xs + sv, R - (R - xs) * 0.06, ym - th / 2, top, sv * 0.96, th)
    out += dbowl(s, xs + sv, R, 0, ym + th / 2, sv, th)
    if not s.sans:
        out += head(s, xs, xs + sv, top, 1, 0) + foot(s, xs, xs + sv, 0, 1, 0)
    return out


def g_C(s, L, R, caps=True, top=None):
    top = top or (s.cap if caps else s.xh)
    tv = s.svc * (1.04 if s.sans else 1.0) if caps else s.sv
    th = s.shc if s.sans else s.hair
    y0, y1 = -s.ov, top + s.ov
    if s.sans:
        return open_bowl(s, L, R, y0, y1, 44, 316, 0, 0, tv, th)
    out = open_bowl(s, L, R, y0, y1, 38, 322, 90, 90, tv, th)
    if caps:
        # vertical beak at the top terminal
        pt = G.se_point((L + R) / 2, (y0 + y1) / 2, (R - L) / 2, (y1 - y0) / 2, s.n, math.radians(38))
        out += G.rect(pt[0] - s.sv * 0.22, pt[1] - s.sv * 0.6, pt[0] + 1, top * 0.96 if s.serif else pt[1] + s.sv * 0.4)
    return out


def g_D(s, L, R):
    top = s.cap
    sv, th = s.svc, s.shc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    # one big bowl, rounder than B's
    r = top / 2
    cx = R - min(r, (R - xs) * 0.55)
    o = (2 * cx - R, -0.0, R, top)
    i = (2 * cx - R + sv, th, R - sv * 1.03, top - th)
    out += arc_box(s, o, i, -90, 90, None, None, n=s.n + 0.2, ni=s.ni + 0.2)
    out += G.rect(xs + sv - 10, 0, cx + 2, th) + G.rect(xs + sv - 10, top - th, cx + 2, top)
    if not s.sans:
        out += head(s, xs, xs + sv, top, 1, 0) + foot(s, xs, xs + sv, 0, 1, 0)
    return out


def _arms(s, L, R, xs, sv, arms):
    """E/F/L arms: list of (y_center, length factor, kind)"""
    out = []
    th = s.shc
    for (y, f, which) in arms:
        x1 = xs + (R - xs) * f
        if which == 'top':
            out += G.rect(xs, y - th, x1, y)
            out += beak(s, x1, y, True)
        elif which == 'bot':
            out += G.rect(xs, y, x1, y + th)
            out += beak(s, x1, y, False, (s.sl * 1.5) if s.serif else None)
        else:
            out += G.rect(xs, y - th / 2, x1, y + th / 2)
            if not s.sans:
                out += G.rect(x1 - s.st * 1.1, y - th / 2 - s.sl * 0.45, x1, y + th / 2 + s.sl * 0.45)
    return out


def g_E(s, L, R):
    top = s.cap
    sv = s.svc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    out += _arms(s, L, R, xs, sv, [(top, 0.95 if not s.sans else 0.97, 'top'),
                                     (top * 0.53, 0.86 if not s.sans else 0.9, 'mid'),
                                     (0, 1.0, 'bot')])
    if not s.sans:
        out += head(s, xs, xs + sv, top, 1, 0) + foot(s, xs, xs + sv, 0, 1, 0)
    return out


def g_F(s, L, R):
    top = s.cap
    sv = s.svc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    out += _arms(s, L, R, xs, sv, [(top, 1.0, 'top'), (top * (0.52 if s.sans else 0.5), 0.88, 'mid')])
    if not s.sans:
        out += head(s, xs, xs + sv, top, 1, 0) + foot(s, xs, xs + sv, 0, 1, 1.1)
    return out


def g_G(s, L, R):
    top = s.cap
    tv, th = s.svc, (s.shc if s.sans else s.hair)
    y0, y1 = -s.ov, top + s.ov
    out = open_bowl(s, L, R, y0, y1, 40 if s.sans else 36, 360 - 2, 0 if s.sans else 90, None, tv, th)
    yb = top * (0.5 if s.sans else 0.46)
    # the bar and the right stem
    out += G.rect(R - tv, 0 if s.sans else top * 0.06, R, yb)
    out += G.rect(L + (R - L) * 0.52, yb - th * (1.0 if s.sans else 1.2), R, yb)
    if s.sans:
        out += G.rect(R - tv, -s.ov * 0.2, R, top * 0.06)
    if not s.sans:
        pt = G.se_point((L + R) / 2, (y0 + y1) / 2, (R - L) / 2, (y1 - y0) / 2, s.n, math.radians(36))
        out += G.rect(pt[0] - s.sv * 0.22, pt[1] - s.sv * 0.6, pt[0] + 1, top * 0.96)
        out += G.rect(L + (R - L) * 0.45, yb - s.st, R, yb)
    return out


def g_H(s, L, R):
    top = s.cap
    sv = s.svc
    i = s.sl if not s.sans else 0
    x0, x1 = L + i, R - i
    out = vstem(x0, x0 + sv, 0, top) + vstem(x1 - sv, x1, 0, top)
    yb = top * (0.53 if s.sans else 0.52)
    out += hbar(x0 + sv / 2, x1 - sv / 2, yb, s.shc)
    for xa, xb in ((x0, x0 + sv), (x1 - sv, x1)):
        out += head(s, xa, xb, top) + foot(s, xa, xb, 0)
    return out


def g_I(s, L, R):
    top = s.cap
    sv = s.svc
    if s.mono:
        cx = (L + R) / 2
        out = vstem(cx - sv / 2, cx + sv / 2, 0, top)
        out += G.rect(L, 0, R, s.st) + G.rect(L, top - s.st, R, top)
        return out
    if s.sans:
        return vstem(L, R, 0, top)
    cx = (L + R) / 2
    x0, x1 = cx - sv / 2, cx + sv / 2
    f = max(0.3, (x0 - L) / s.sl)
    return vstem(x0, x1, 0, top) + head(s, x0, x1, top, f, f) + foot(s, x0, x1, 0, f, f)


def g_J(s, L, R):
    top = s.cap
    sv = s.svc
    th = s.shc if s.sans else s.hair
    xr = R - (s.sl if not s.sans else 0)
    xs = xr - sv
    depth = top * 0.3
    out = vstem(xs, xr, depth, top)
    o = (L, -s.ov, xr, -s.ov + 2 * depth + 2 * s.ov)
    i = (L + sv * 0.9, -s.ov + th, xs, -s.ov + 2 * depth + 2 * s.ov - th)
    out += arc_box(s, o, i, 180 + (10 if s.sans else 0), 360, 0 if s.sans else 90, None)
    if s.serif:
        out += ball(s, L + sv * 0.55, depth * 0.85, sv * 0.55)
    if not s.sans:
        out += head(s, xs, xr, top, 1.0 if s.mono else 1.2, 0.9)
    return out


def arm(xj, yj, xa, top, ha):
    """the upper arm of K and k: from the stem's edge at (xj, yj) (its
    centre line there) up to the top, its right edge ending at xa; ha wide
    measured across x; it runs on into the stem below yj"""
    xt = xa - ha / 2
    k = (xt - xj) / (top - yj)
    yb = yj - ha * 0.55
    return G.stem(xj - (yj - yb) * k, yb, xt, top, ha)


def g_K(s, L, R):
    top = s.cap
    sv = s.svc
    i = s.sl if not s.sans else 0
    xs = L + i
    out = vstem(xs, xs + sv, 0, top)
    xj = xs + sv
    yj = top * (0.40 if s.sans else 0.42)
    # upper arm: from the stem to the top right; leg: from the arm to bottom right
    wa = s.svc * (0.98 if s.sans else 0.62 if s.serif else 0.95)
    wl = s.svc * (1.04 if not s.serif else 1.12)
    xa = R - i * 0.6
    ha = dw(wa, xa - xj, top - yj)
    out += arm(xj, yj, xa, top, ha)
    # the leg starts on the arm
    lx = xj + (xa - xj) * 0.28
    ly = yj + (top - yj) * 0.28
    xl = R - (i * 0.4 if not s.sans else 0)
    wlh = dw(wl, xl - lx, ly)
    out += G.poly([(lx - wlh * 0.55, ly), (xl - wlh, 0), (xl, 0), (lx + wlh * 0.55, ly + 40)])
    if not s.sans:
        out += head(s, xs, xs + sv) if False else []
        out += head(s, xs, xs + sv, top) + foot(s, xs, xs + sv, 0)
        out += dhead(s, xa - dw(wa, xa - xj, top - yj), xa, top, 0.9, 0.7)
        out += dfoot(s, xl - wlh, xl, 0, 0.6, 0.9)
    return out


def g_L(s, L, R):
    top = s.cap
    sv = s.svc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    out += _arms(s, L, R, xs, sv, [(0, 1.0, 'bot')])
    if not s.sans:
        out += head(s, xs, xs + sv, top) + foot(s, xs, xs + sv, 0, 1, 0)
    return out


def g_M(s, L, R):
    top = s.cap
    sv = s.svc
    i = s.sl * 0.8 if not s.sans else 0
    x0, x1 = L + i, R - i
    wl = s.thin if s.serif else sv * (0.92 if s.sans else 0.9)
    wr = sv
    out = vstem(x0, x0 + wl, 0, top) + vstem(x1 - wr, x1, 0, top)
    cx = (x0 + x1) / 2
    wd1 = s.svc * (1.0 if s.serif else 0.86 if s.sans else 0.72)
    wd2 = s.thin if s.serif else s.svc * (0.86 if s.sans else 0.72)
    vb = 0 if not s.sans else 0
    out += G.poly([(x0, top), (x0 + dw(wd1, cx - x0, top) , top), (cx + dw(wd1, cx - x0, top) * 0.5, vb),
                   (cx - dw(wd1, cx - x0, top) * 0.5, vb)])
    out += G.poly([(x1 - dw(wd2, x1 - cx, top), top), (x1 - 2, top), (cx + dw(wd2, x1 - cx, top) * 0.5, vb),
                   (cx - dw(wd2, x1 - cx, top) * 0.5, vb)])
    if not s.sans:
        out += foot(s, x0, x0 + wl, 0) + foot(s, x1 - wr, x1, 0)
        out += head(s, x0, x0 + wl, top, 1, 0) + head(s, x1 - wr, x1, top, 0, 1)
    return out


def g_N(s, L, R):
    top = s.cap
    sv = s.svc
    i = s.sl * 0.8 if not s.sans else 0
    x0, x1 = L + i, R - i
    w = s.thin if s.serif else sv * (0.95 if s.sans else 0.9)
    out = vstem(x0, x0 + w, 0 if s.sans else 0, top) + vstem(x1 - w, x1, 0, top)
    wd = sv * (1.05 if s.serif else 0.92)
    h = dw(wd, x1 - x0, top)
    out += G.poly([(x0, top), (x0 + h, top), (x1, 0), (x1 - h, 0)])
    if not s.sans:
        out += foot(s, x0, x0 + w, 0) + head(s, x0, x0 + w, top, 1, 0) + head(s, x1 - w, x1, top)
    return out


def g_O(s, L, R):
    top = s.cap
    return bowl(s, L, R, -s.ov, top + s.ov, s.svc * (1.04 if s.serif else 1.0), s.shc if s.sans else s.hair)


def g_P(s, L, R):
    top = s.cap
    sv = s.svc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    yb = top * (0.42 if s.sans else 0.44)
    out += dbowl(s, xs + sv, R, yb, top, sv, s.shc)
    if not s.sans:
        out += head(s, xs, xs + sv, top, 1, 0) + foot(s, xs, xs + sv, 0)
    return out


def g_Q(s, L, R):
    top = s.cap
    out = g_O(s, L, R)
    cx = (L + R) / 2
    w = s.svc * (1.0 if s.sans else 1.1)
    if s.sans:
        out += G.nstroke(cx + (R - L) * 0.12, top * 0.2, R + (R - L) * 0.0, -s.ov * 2.5, w * 0.95)
    else:
        out += G.stroke([(cx - (R - L) * 0.05, s.hair * 0.5), (cx + (R - L) * 0.25, -top * 0.12),
                         (R + (R - L) * 0.05, -top * 0.2)], [(1, -0.2), (1, -0.5), (1, -0.2)],
                        wv=w * 1.1, wh=s.hair * 1.2, widths=[0.6, 1.0, 0.5])
    return out


def g_R(s, L, R):
    top = s.cap
    sv = s.svc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    yb = top * (0.45 if s.sans else 0.47)
    xb = R - (R - xs) * (0.06 if s.sans else 0.1)
    out += dbowl(s, xs + sv, xb, yb, top, sv, s.shc)
    # leg
    xl0 = xs + sv + (xb - xs - sv) * (0.42 if s.sans else 0.38)
    wl = sv * (1.02 if not s.serif else 1.1)
    xr = R - (s.sl * 0.4 if not s.sans else 0)
    h = dw(wl, xr - xl0, yb)
    out += G.poly([(xl0 - h * 0.5, yb + s.shc * 0.5), (xr - h, 0), (xr, 0), (xl0 + h * 0.6, yb + s.shc * 0.5)])
    if not s.sans:
        out += head(s, xs, xs + sv, top, 1, 0) + foot(s, xs, xs + sv, 0)
        out += foot(s, xr - h, xr, 0, 0.3, 0.8)
    return out


def s_curve(s, L, R, y0, y1, wv, wh, caps=True, cut=0):
    """the S spine: ink from L to R, y0 (bottom incl. overshoot) to y1"""
    w = R - L
    h = y1 - y0
    cx = L + w * 0.5
    pts = [(R - wv * 0.45, y0 + h * 0.75),
           (cx + w * 0.02, y1 - wh / 2),
           (L + wv / 2 + w * 0.01, y0 + h * 0.73),
           (cx, y0 + h * 0.5),
           (R - wv / 2 - w * 0.0, y0 + h * 0.265),
           (cx - w * 0.02, y0 + wh / 2),
           (L + wv * 0.45, y0 + h * 0.24)]
    tans = [(-0.55, 1), (-1, 0), (0, -1), (1, -0.45 * h / w), (0, -1), (-1, 0), (-0.55, 1)]
    if s.sans:
        c0, c1 = cut, cut
    else:
        c0, c1 = None, None
    return G.stroke(pts, tans, wv=wv, wh=wh, cap0=c0, cap1=c1, stress=0, tension=0.55)


def g_S(s, L, R):
    top = s.cap
    out = s_curve(s, L, R, -s.ov, top + s.ov, s.svc * (1.02 if s.sans else 1.1), s.shc if s.sans else s.hair * 1.1)
    return out


def g_T(s, L, R):
    top = s.cap
    sv = s.svc
    cx = (L + R) / 2
    th = s.shc
    out = G.rect(L, top - th, R, top) + vstem(cx - sv / 2, cx + sv / 2, 0, top)
    if not s.sans:
        out += beak(s, L + 0.5, top, True) + G.mirror_x(beak(s, L + 0.5, top, True), cx)
        out += foot(s, cx - sv / 2, cx + sv / 2, 0)
    return out


def g_U(s, L, R):
    top = s.cap
    sv = s.svc
    i = s.sl * 0.8 if not s.sans else 0
    x0, x1 = L + i, R - i
    wr = s.thin * 1.2 if s.serif else sv
    yc = top * 0.38
    out = vstem(x0, x0 + sv, yc, top) + vstem(x1 - wr, x1, yc, top)
    th = s.shc if s.sans else s.hair * 1.2
    o = (x0, -s.ov, x1, 2 * yc + s.ov)
    ii = (x0 + sv, -s.ov + th, x1 - wr, 2 * yc + s.ov - th)
    out += arc_box(s, o, ii, 180, 360, None, None)
    if not s.sans:
        out += head(s, x0, x0 + sv, top) + head(s, x1 - wr, x1, top)
    return out


def g_V(s, L, R):
    top = s.cap
    i = s.sl * 0.85 if not s.sans else 0
    x0, x1 = L + i, R - i
    wl = s.svc * (1.1 if s.serif else 1.0)
    wr = s.thin if s.serif else s.svc * 0.96
    flat = s.svc * 0.3 if s.sans else (0 if s.serif else s.sv * 0.4)
    out = vee(s, x0, x1, top, wl, wr, flat, -s.ov if s.serif else 0)
    if not s.sans:
        out += dhead(s, x0, x0 + dw(wl, (x1 - x0) / 2, top), top, 1, 0.8)
        out += dhead(s, x1 - dw(wr, (x1 - x0) / 2, top), x1, top, 0.7, 1)
    return out


def mono_w(s, x0, x1, top, sv):
    """w and W of w16 Mono: four strokes of one weight, the middle apex
    well below the top (meeting at the top, in a glyph this narrow, the
    inner strokes ran into one blot at 8-10 pt), serifs on the outer two"""
    q = (x1 - x0) / 4
    t = sv * 0.82
    ha = top * 0.7
    b1, b2, m = x0 + q * 1.05, x1 - q * 1.05, (x0 + x1) / 2
    h1 = dw(t, b1 - x0, top)
    h2 = dw(t, m - b1, ha)
    out = slanted(x0 + h1 / 2, b1, top, 0, h1) + slanted(x1 - h1 / 2, b2, top, 0, h1)
    out += slanted(m, b1 + h2 * 0.3, ha, 0, h2) + slanted(m, b2 - h2 * 0.3, ha, 0, h2)
    out += dhead(s, x0, x0 + h1, top, 1, 0.6) + dhead(s, x1 - h1, x1, top, 0.6, 1)
    return out


def g_W(s, L, R):
    top = s.cap
    i = s.sl * 0.6 if not s.sans else 0
    x0, x1 = L + i, R - i
    w = x1 - x0
    if s.mono:
        return mono_w(s, x0, x1, top, s.svc)
    thick = s.svc * (1.05 if s.serif else 0.92 if s.sans else 0.74)
    thin = s.thin if s.serif else s.svc * (0.86 if s.sans else 0.68)
    q = w / 4
    flat = s.svc * 0.25 if s.sans else 0
    out = vee(s, x0, x0 + 2 * q + q * 0.05, top, thick, thin, flat)
    out += vee(s, x0 + 2 * q - q * 0.05, x1, top * (1.0 if not s.sans else 1.0), thick, thin, flat)
    if not s.sans:
        out += dhead(s, x0, x0 + dw(thick, q, top), top, 1, 0.6)
        out += dhead(s, x1 - dw(thin, q, top), x1, top, 0.6, 1)
        if s.mono:
            out += dhead(s, x0 + 2 * q - dw(thin, q, top) / 2, x0 + 2 * q + dw(thin, q, top) / 2, top, 0.4, 0.4)
    return out


def g_X(s, L, R):
    top = s.cap
    i = s.sl * 0.7 if not s.sans else 0
    x0, x1 = L + i, R - i
    w1 = s.svc * (1.08 if s.serif else 0.98)
    w2 = s.thin if s.serif else s.svc * 0.94
    h1 = dw(w1, x1 - x0, top)
    h2 = dw(w2, x1 - x0, top)
    out = G.poly([(x0, top), (x0 + h1, top), (x1, 0), (x1 - h1, 0)])
    out += G.poly([(x1 - h2, top), (x1, top), (x0 + h2, 0), (x0, 0)])
    if not s.sans:
        out += dhead(s, x0, x0 + h1, top, 1, 0.7) + dhead(s, x1 - h2, x1, top, 0.7, 1)
        out += dfoot(s, x0, x0 + h2, 0, 1, 0.7) + dfoot(s, x1 - h1, x1, 0, 0.7, 1)
    return out


def g_Y(s, L, R):
    top = s.cap
    i = s.sl * 0.75 if not s.sans else 0
    x0, x1 = L + i, R - i
    cx = (x0 + x1) / 2
    sv = s.svc
    yj = top * (0.44 if s.sans else 0.45)
    wl = s.svc * (1.08 if s.serif else 0.98)
    wr = s.thin if s.serif else s.svc * 0.96
    out = vstem(cx - sv / 2, cx + sv / 2, 0, yj + 20)
    hl = dw(wl, cx - x0, top - yj)
    hr = dw(wr, x1 - cx, top - yj)
    out += G.poly([(x0, top), (x0 + hl, top), (cx + sv / 2, yj), (cx - sv / 2, yj)])
    out += G.poly([(x1 - hr, top), (x1, top), (cx + sv / 2 if s.sans else cx + sv * 0.2, yj),
                   (cx - sv / 2 if s.sans else cx - sv * 0.2, yj)])
    if not s.sans:
        out += dhead(s, x0, x0 + hl, top, 1, 0.7) + dhead(s, x1 - hr, x1, top, 0.7, 1)
        out += foot(s, cx - sv / 2, cx + sv / 2, 0)
    return out


def g_Z(s, L, R):
    top = s.cap
    th = s.shc
    i = s.sl * 0.2 if not s.sans else 0
    x0, x1 = L + i, R
    wd = s.svc * (1.05 if s.serif else 0.98)
    h = dw(wd, x1 - x0, top)
    out = G.rect(x0 + (R - L) * 0.04, top - th, x1 - (R - L) * 0.02, top)
    out += G.rect(L, 0, R, th)
    out += G.poly([(x1 - h, top - th + 1), (x1 - (R - L) * 0.02, top - th + 1), (L + h, th - 1), (L, th - 1)])
    if not s.sans:
        out += beak(s, x0 + (R - L) * 0.04 + 0.5, top, True, s.sl * 1.1)
        out += beak(s, R - 0.5, 0, False, s.sl * 1.4)
    return out


# ================================================================ lowercase


def _xs(s, L):
    """the left stem's left edge for a lowercase with a flag serif"""
    return L + (s.sl * (1.25 if s.serif else 0.95) if not s.sans else 0)


def g_a(s, L, R):
    if s.italic and s.serif:
        return gi_a(s, L, R)
    top = s.xh
    sv, th = s.sv, s.sh if s.sans else s.hair
    xr = R - (s.sl * 0.55 if s.mono else (s.sl * 0.3 if s.serif else 0))
    xs0 = xr - sv
    ytop = top * (0.62 if s.sans else 0.6)
    # stem
    out = vstem(xs0, xr, 0, ytop)
    # top hook: from the stem round to the upper left terminal
    o = (L + (R - L) * 0.05, ytop - (top + s.ov - ytop), xr, top + s.ov)
    i = (L + (R - L) * 0.05 + sv * 0.9, ytop - (top + s.ov - ytop) + th, xs0, top + s.ov - th)
    out += arc_box(s, o, i, 0, 152 if s.sans else 140, None, 0 if s.sans else 90)
    if s.serif:
        pt = G.se_point((o[0] + o[2]) / 2, (o[1] + o[3]) / 2, (o[2] - o[0]) / 2, (o[3] - o[1]) / 2, s.n, math.radians(140))
        out += ball(s, pt[0] + sv * 0.35, pt[1] - sv * 0.3, sv * 0.58)
    # lower bowl
    yb = top * (0.56 if s.sans else 0.54)
    bo = (L, -s.ov, xs0 + sv * 0.5, yb)
    bi = (L + sv * 0.95, -s.ov + th, xs0 + 6, yb - s.sh * 0.9)
    out += ring_box(s, bo, bi, 0 if not s.serif else 12)
    if s.serif:
        out += G.rect(xs0, 0, xr + s.sl * 0.35, s.st) if False else []
        out += tail_out(s, xs0, xr, 0)
    if s.mono:
        out += G.rect(xs0, 0, R, s.st)
    return out


def tail_out(s, xl, xr, y):
    """a small exit spur at the foot of a stem (serif a, d, u)"""
    p = G.Path(xl, y + s.sv * 0.6)
    p.qto(xl + 4, y - 4, xr + s.sl * 0.25, y - 4)
    p.line(xr + s.sl * 0.45, y + s.st * 0.9)
    p.line(xr + s.sl * 0.25, y + s.st * 0.9)
    p.qto(xr, y + s.st * 0.9, xr, y + s.sv * 0.8)
    return p.close()


def g_b(s, L, R):
    top = s.xh
    sv = s.sv
    xs = _xs(s, L)
    out = vstem(xs, xs + sv, 0, s.asc)
    th = s.sh if s.sans else s.hair
    out += side_bowl(s, xs, xs + sv, R, -s.ov, top + s.ov, sv, th, True)
    out += flag(s, xs, xs + sv, s.asc)
    if s.mono:
        out += G.rect(xs - s.sl * 0.95, 0, xs + sv, s.st)
    return out


def g_c(s, L, R):
    if s.sans:
        return open_bowl(s, L, R, -s.ov, s.xh + s.ov, 44, 316, 0, 0, s.sv, s.sh)
    top = s.xh
    out = open_bowl(s, L, R, -s.ov, top + s.ov, 40 if s.serif else 42, 322 if s.serif else 318,
                    90 if not s.mono else 0, 0 if s.mono else 60, s.sv, s.hair)
    if s.serif:
        pt = G.se_point((L + R) / 2, top / 2, (R - L) / 2, top / 2 + s.ov, s.n, math.radians(40))
        out += ball(s, pt[0] - s.sv * 0.5, pt[1] - s.sv * 0.45, s.sv * 0.6)
    return out


def g_d(s, L, R):
    top = s.xh
    sv = s.sv
    xr = R - (s.sl * (0.4 if s.serif else 0.55) if not s.sans else 0)
    xs = xr - sv
    out = vstem(xs, xr, 0, s.asc)
    th = s.sh if s.sans else s.hair
    out += side_bowl(s, xs, xr, L, -s.ov, top + s.ov, sv, th, False)
    out += flag(s, xs, xr, s.asc)
    if s.serif:
        out += tail_out(s, xs, xr, 0)
    if s.mono:
        out += G.rect(xs, 0, R, s.st)
    return out


def g_e(s, L, R):
    top = s.xh
    sv = s.sv
    th = s.sh if s.sans else s.hair
    yb = top * (0.53 if s.sans else 0.56)
    tb = s.sh if s.sans else s.hair * 1.15
    o = (L, -s.ov, R, top + s.ov)
    cy = (o[1] + o[3]) / 2
    i = (L + sv, -s.ov + th, R - sv * (0.95 if s.sans else 0.75), top + s.ov - th)
    # the bowl starts inside the bar (its right end cut level, hidden)
    b = (o[3] - o[1]) / 2
    yc = yb - tb / 2 - cy
    a0 = math.degrees(math.asin(math.copysign(min(0.99, abs(yc / b)) ** (s.n / 2), yc)))
    out = arc_box(s, o, i, a0, 325 if s.sans else 330, 0, 0 if s.sans else 60, s.stress)
    # bar: its top is the bowl's floor
    out += G.rect(L + sv * 0.5, yb - tb, R, yb)
    return out


def g_f(s, L, R):
    if s.italic and s.serif:
        return gi_f(s, L, R)
    top = s.xh
    sv = s.sv
    th = s.sh if s.sans else s.hair
    if s.mono:
        xs = (L + R) / 2 - sv * 0.75
    else:
        xs = L + (R - L) * (0.2 if s.sans else 0.24)
    hook_top = s.asc + s.ov * 0.5
    r = (R - xs) * 0.5
    yh = hook_top - r * 1.05
    out = vstem(xs, xs + sv, 0, yh + 4)
    o = (xs, yh - (hook_top - yh), R + (sv * 0.2 if s.serif else 0), hook_top)
    i = (xs + sv, yh - (hook_top - yh) + th, R - sv * 0.3, hook_top - th)
    out += arc_box(s, o, i, 30 if s.sans else 40, 180, 0 if s.sans else 90, None)
    if s.serif:
        pt = G.se_point((o[0] + o[2]) / 2, (o[1] + o[3]) / 2, (o[2] - o[0]) / 2, (o[3] - o[1]) / 2, s.n, math.radians(40))
        out += ball(s, pt[0] - sv * 0.45, pt[1] - sv * 0.4, sv * 0.55)
    # crossbar
    bl = L if not s.mono else L + (R - L) * 0.04
    br = xs + sv + (R - xs - sv) * (0.55 if s.sans else 0.5)
    out += G.rect(bl, top - (s.sh if s.sans else s.hair * 1.1), br, top)
    if not s.sans:
        out += foot(s, xs, xs + sv, 0, 1.0 if s.serif else 1.2, 1.2 if s.serif else 1.6)
    return out


def g_g(s, L, R):
    if s.serif and not s.italic:
        return g_g_two(s, L, R)
    top = s.xh
    sv = s.sv
    th = s.sh if s.sans else s.hair
    xr = R - (s.sl * 0.55 if s.mono else 0)
    xs = xr - sv
    ybot = s.desc * 0.9
    depth = -ybot * 0.72
    out = vstem(xs, xr, ybot - s.ov + depth - 6, top)
    out += side_bowl(s, xs, xr, L, -s.ov + top * 0.02, top + s.ov, sv, th, False)
    # the hook below
    th = th * 0.9
    o = (L + (R - L) * 0.06, ybot - s.ov, xr, ybot - s.ov + 2 * depth)
    i = (L + (R - L) * 0.06 + sv, ybot - s.ov + th, xs, ybot - s.ov + 2 * depth - th)
    out += arc_box(s, o, i, 200 if s.sans else 205, 360, 0 if s.sans else 90, None)
    if s.mono:
        out += G.rect(xs, top - s.st, R, top)
    return out


def g_g_two(s, L, R):
    """the double-storey g of the roman"""
    top = s.xh
    sv = s.sv
    th = s.hair
    w = R - L
    # upper bowl
    ub = (L + w * 0.06, top * 0.36, L + w * 0.06 + w * 0.68, top + s.ov)
    out = ring_box(s, ub, (ub[0] + sv, ub[1] + th, ub[2] - sv, ub[3] - th), s.stress)
    # ear
    out += G.rect(ub[2] - sv * 0.6, top - s.hair * 1.2, R, top)
    out += G.rect(R - s.st * 1.0, top - s.hair * 1.2 - s.sl * 0.35, R, top)
    # link from the upper bowl down to the loop
    lo_top = top * 0.08
    lb = (L, s.desc * 0.98, R - w * 0.02, lo_top + s.hair * 1.2)
    out += ring_box(s, lb, (lb[0] + sv * 0.6, lb[1] + th * 1.1, lb[2] - sv * 0.8, lb[3] - th * 1.3), 0)
    out += G.stroke([(ub[0] + w * 0.2, ub[1] + th * 0.5), (L + w * 0.12, top * 0.2),
                     (L + w * 0.32, lo_top + s.hair * 0.6), (R - w * 0.2, lo_top + s.hair * 0.6)],
                    [(-0.6, -1), (0, -1), (1, 0), (1, 0)], wv=sv * 0.55, wh=s.hair * 1.2, widths=[0.5, 0.9, 1, 1])
    return out


def g_h(s, L, R):
    top = s.xh
    sv = s.sv
    xs = _xs(s, L)
    xr = R - (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, s.asc)
    out += arch(s, xs, xr, top)
    out += flag(s, xs, xs + sv, s.asc)
    out += foot(s, xs, xs + sv, 0) + foot(s, xr - sv, xr, 0)
    return out


def g_i(s, L, R, dotted=True):
    top = s.xh
    sv = s.sv
    if s.mono:
        cx = (L + R) / 2 + sv * 0.1
        out = vstem(cx - sv / 2, cx + sv / 2, 0, top)
        out += G.rect(L + (R - L) * 0.02, 0, R, s.st)
        out += G.rect(L + (R - L) * 0.1, top - s.st, cx + sv / 2, top)
        if dotted:
            out += dot(s, cx - sv * 0.05, top + (s.asc - top) * 0.62)
        return out
    xs = _xs(s, L)
    if not s.sans:
        xs = L + (R - L - sv) * 0.62
    out = vstem(xs, xs + sv, 0, top)
    out += flag(s, xs, xs + sv, top)
    out += foot(s, xs, xs + sv, 0)
    if dotted:
        cy = top + (s.asc - top) * (0.6 if s.sans else 0.62)
        out += dot(s, xs + sv / 2, cy, max(s.sv / 2, s.dot / 2) if not s.sans else sv / 2 * 1.04)
    return out


def g_dotlessi(s, L, R):
    return g_i(s, L, R, False)


def g_j(s, L, R):
    top = s.xh
    sv = s.sv
    th = s.sh if s.sans else s.hair
    xr = R - (s.sl * 0.3 if s.mono else 0)
    xs = xr - sv
    ybot = s.desc - (s.ov * 0.5)
    depth = -ybot * 0.45
    out = vstem(xs, xr, ybot + depth, top)
    o = (L, ybot, xr, ybot + 2 * depth)
    i = (L + sv * 0.2, ybot + th, xs, ybot + 2 * depth - th)
    out += arc_box(s, o, i, 228 if s.sans else 215, 360, 0 if s.sans else 90, None)
    if s.serif:
        out += ball(s, L + sv * 0.55, ybot + depth * 0.7, sv * 0.55)
    out += flag(s, xs, xr, top)
    out += dot(s, (xs + xr) / 2, top + (s.asc - top) * 0.62)
    return out


def g_k(s, L, R):
    top = s.xh
    sv = s.sv
    xs = _xs(s, L)
    out = vstem(xs, xs + sv, 0, s.asc)
    xj = xs + sv
    yj = top * 0.36
    i = s.sl * 0.6 if not s.sans else 0
    xa = R - i
    wa = sv * (0.96 if s.sans else 0.6 if s.serif else 0.92)
    wl = sv * (1.02 if not s.serif else 1.1)
    ha = dw(wa, xa - xj, top - yj)
    out += arm(xj, yj, xa, top, ha)
    lx = xj + (xa - xj) * 0.3
    ly = yj + (top - yj) * 0.3
    xl = R - (i * 0.4)
    hl = dw(wl, xl - lx, ly)
    out += G.poly([(lx - hl * 0.55, ly), (xl - hl, 0), (xl, 0), (lx + hl * 0.55, ly + 30)])
    out += flag(s, xs, xs + sv, s.asc)
    if not s.sans:
        out += foot(s, xs, xs + sv, 0)
        out += dhead(s, xa - ha, xa, top, 0.8, 0.7)
        out += dfoot(s, xl - hl, xl, 0, 0.5, 0.8)
    return out


def g_l(s, L, R):
    sv = s.sv
    if s.mono:
        cx = (L + R) / 2 + sv * 0.1
        out = vstem(cx - sv / 2, cx + sv / 2, 0, s.asc)
        out += G.rect(L + (R - L) * 0.02, 0, R, s.st)
        out += G.rect(L + (R - L) * 0.1, s.asc - s.st, cx + sv / 2, s.asc)
        return out
    xs = _xs(s, L)
    if not s.sans:
        xs = L + (R - L - sv) * 0.62
    out = vstem(xs, xs + sv, 0, s.asc)
    out += flag(s, xs, xs + sv, s.asc)
    out += foot(s, xs, xs + sv, 0)
    return out


def g_m(s, L, R):
    top = s.xh
    sv = s.sv * (0.94 if not s.mono else 0.85)
    xs = _xs(s, L) if not s.mono else L + s.sl * 0.5
    xr = R - (s.sl if s.serif else (s.sl * 0.3 if s.mono else 0))
    xm = (xs + xr) / 2 + sv / 2 - (sv * 0.02)
    out = vstem(xs, xs + sv, 0, top)
    th = s.sh * (0.95 if s.sans else 1.0)
    out += arch(s, xs, xm, top, sv=sv, th=th)
    out += arch(s, xm - sv, xr, top, sv=sv, th=th)
    out += flag(s, xs, xs + sv, top)
    if s.mono:
        out += G.rect(L, 0, xs + sv + s.sl * 0.3, s.st) + G.rect(xr - sv - s.sl * 0.3, 0, R, s.st)
    else:
        out += foot(s, xs, xs + sv, 0) + foot(s, xm - sv, xm, 0, 0.8, 0.8) + foot(s, xr - sv, xr, 0)
    return out


def g_n(s, L, R):
    if s.italic and s.serif:
        return gi_n(s, L, R)
    top = s.xh
    sv = s.sv
    xs = _xs(s, L)
    xr = R - (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    out += arch(s, xs, xr, top)
    out += flag(s, xs, xs + sv, top)
    out += foot(s, xs, xs + sv, 0) + foot(s, xr - sv, xr, 0)
    return out


def g_o(s, L, R):
    return bowl(s, L, R, -s.ov, s.xh + s.ov, s.sv * (1.03 if s.serif else 1.0), s.sh if s.sans else s.hair)


def g_p(s, L, R):
    top = s.xh
    sv = s.sv
    xs = _xs(s, L)
    out = vstem(xs, xs + sv, s.desc, top)
    th = s.sh if s.sans else s.hair
    out += side_bowl(s, xs, xs + sv, R, -s.ov, top + s.ov, sv, th, True)
    out += flag(s, xs, xs + sv, top)
    out += foot(s, xs, xs + sv, s.desc)
    return out


def g_q(s, L, R):
    top = s.xh
    sv = s.sv
    xr = R - (s.sl if not s.sans else 0)
    xs = xr - sv
    out = vstem(xs, xr, s.desc, top + (s.ov if s.sans else 0))
    th = s.sh if s.sans else s.hair
    out += side_bowl(s, xs, xr, L, -s.ov, top + s.ov, sv, th, False)
    out += foot(s, xs, xr, s.desc)
    if s.mono:
        out += G.rect(xs, top - s.st, R, top)
    return out


def g_r(s, L, R):
    top = s.xh
    sv = s.sv
    th = s.sh if s.sans else s.hair
    xs = _xs(s, L) if not s.mono else L + (R - L) * 0.28
    out = vstem(xs, xs + sv, 0, top)
    # the ear: an arc from the stem to the upper right
    r_h = top * (0.42 if s.sans else 0.4)
    o = (xs + sv * 0.5, top - 2 * r_h + s.ov, R + (R - xs) * (0.25 if s.sans else 0.3), top + s.ov)
    i = (xs + sv - 6, top - 2 * r_h + s.ov + th, o[2] - sv * 0.9, top + s.ov - th)
    out += arc_box(s, o, i, 60 if s.sans else 55, 180, 0 if s.sans else 90, None)
    if s.serif:
        pt = G.se_point((o[0] + o[2]) / 2, (o[1] + o[3]) / 2, (o[2] - o[0]) / 2, (o[3] - o[1]) / 2, s.n, math.radians(55))
        out += ball(s, pt[0] - sv * 0.35, pt[1] - sv * 0.42, sv * 0.55)
    out += flag(s, xs, xs + sv, top)
    if s.mono:
        out += G.rect(L, 0, R - (R - L) * 0.1, s.st)
    else:
        out += foot(s, xs, xs + sv, 0, 1.0, 1.4)
    return out


def g_s(s, L, R):
    top = s.xh
    out = s_curve(s, L, R, -s.ov, top + s.ov, s.sv * (1.0 if s.sans else 1.05), s.sh if s.sans else s.hair * 1.1, False)
    return out


def g_t(s, L, R):
    top = s.xh
    sv = s.sv
    th = s.sh if s.sans else s.hair
    xs = L + (R - L) * (0.2 if s.sans else 0.22)
    if s.mono:
        xs = L + (R - L) * 0.28
    tt = top + (s.asc - top) * (0.5 if s.sans else 0.42)
    rx = (R - xs) * 0.62
    ry = top * 0.25
    out = G.poly([(xs, ry - s.ov - 12), (xs + sv, ry - s.ov - 12), (xs + sv, tt), (xs, tt - (0 if s.sans else sv * 0.7))])
    o = (xs, -s.ov, xs + 2 * rx, 2 * ry - s.ov)
    i = (xs + sv, -s.ov + th, xs + 2 * rx - sv * 0.6, 2 * ry - s.ov - th)
    out += arc_box(s, o, i, 180, 302 if s.sans else 310, None, 0 if s.sans else 90)
    out += G.rect(L, top - (s.sh if s.sans else s.hair * 1.2), R - (R - L) * (0.1 if s.sans else 0.16), top)
    return out


def g_u(s, L, R):
    if s.italic and s.serif:
        return gi_u(s, L, R)
    top = s.xh
    sv = s.sv
    i = s.sl if not s.sans else 0
    x0 = L + (s.sl * 0.95 if not s.sans else 0)
    x1 = R - (s.sl * 0.3 if s.serif else (s.sl * 0.5 if s.mono else 0))
    # n turned round
    n = arch(s, x0, x1, top)
    cx, cy = (x0 + x1) / 2, top / 2
    out = G.rotate(n, 180, cx, cy)
    out += vstem(x1 - sv, x1, 0, top)
    out += flag(s, x0, x0 + sv, top) + flag(s, x1 - sv, x1, top)
    if s.serif:
        out += tail_out(s, x1 - sv, x1, 0)
    if s.mono:
        out += G.rect(x1 - sv, 0, R, s.st)
    del i
    return out


def g_v(s, L, R):
    top = s.xh
    i = s.sl * 0.85 if not s.sans else 0
    x0, x1 = L + i, R - i
    wl = s.sv * (1.1 if s.serif else 1.0)
    wr = s.thin if s.serif else s.sv * 0.95
    flat = s.sv * 0.3 if s.sans else 0
    out = vee(s, x0, x1, top, wl, wr, flat, -s.ov if s.serif else 0)
    if not s.sans:
        out += dhead(s, x0, x0 + dw(wl, (x1 - x0) / 2, top), top, 1, 0.7)
        out += dhead(s, x1 - dw(wr, (x1 - x0) / 2, top), x1, top, 0.6, 1)
    return out


def g_w(s, L, R):
    top = s.xh
    i = s.sl * 0.6 if not s.sans else 0
    x0, x1 = L + i, R - i
    q = (x1 - x0) / 4
    if s.mono:
        return mono_w(s, x0, x1, top, s.sv)
    thick = s.sv * (1.05 if s.serif else 0.9 if s.sans else 0.74)
    thin = s.thin if s.serif else s.sv * (0.85 if s.sans else 0.68)
    flat = s.sv * 0.25 if s.sans else 0
    out = vee(s, x0, x0 + 2 * q + q * 0.05, top, thick, thin, flat)
    out += vee(s, x0 + 2 * q - q * 0.05, x1, top, thick, thin, flat)
    if not s.sans:
        out += dhead(s, x0, x0 + dw(thick, q, top), top, 1, 0.6)
        out += dhead(s, x1 - dw(thin, q, top), x1, top, 0.6, 1)
    return out


def g_x(s, L, R):
    top = s.xh
    i = s.sl * 0.7 if not s.sans else 0
    x0, x1 = L + i, R - i
    w1 = s.sv * (1.08 if s.serif else 0.98)
    w2 = s.thin if s.serif else s.sv * 0.94
    h1 = dw(w1, x1 - x0, top)
    h2 = dw(w2, x1 - x0, top)
    out = G.poly([(x0, top), (x0 + h1, top), (x1, 0), (x1 - h1, 0)])
    out += G.poly([(x1 - h2, top), (x1, top), (x0 + h2, 0), (x0, 0)])
    if not s.sans:
        out += dhead(s, x0, x0 + h1, top, 1, 0.6) + dhead(s, x1 - h2, x1, top, 0.6, 1)
        out += dfoot(s, x0, x0 + h2, 0, 1, 0.6) + dfoot(s, x1 - h1, x1, 0, 0.6, 1)
    return out


def g_y(s, L, R):
    top = s.xh
    i = s.sl * 0.85 if not s.sans else 0
    x0, x1 = L + i, R - i
    wl = s.sv * (1.1 if s.serif else 1.0)
    wr = s.thin if s.serif else s.sv * 0.95
    flat = s.sv * 0.3 if s.sans else 0
    # the v, its point at the baseline
    cx = (x0 + x1) / 2
    out = diag_pair(s, x0, x1, cx, top, wl, wr, flat)
    out = G.mirror_y(out, top / 2)
    # the right arm runs on below the baseline
    al = (cx + flat / 2, 0)
    hr = dw(wr, x1 - al[0], top)
    slope = (x1 - al[0]) / top
    yb = s.desc * 0.5
    xb = al[0] - (-yb) * slope
    out += G.poly([(x1 - hr, top), (x1, top), (xb, yb), (xb - hr, yb)])
    # and bends left into the tail
    th = s.sh if s.sans else s.hair * 1.3
    end_x = L + (s.sv * 0.6 if s.serif else (R - L) * 0.05)
    pts = [(xb - hr / 2 + slope * 60, yb + 60), (end_x, s.desc + th / 2)]
    out += G.stroke(pts, [(-slope, -1), (-1, 0)], wv=wr if not s.serif else s.sv * 0.7,
                    wh=th, cap1=90, tension=0.62)
    if s.serif:
        out += ball(s, end_x + s.sv * 0.1, s.desc + s.sv * 0.5, s.sv * 0.5)
    if not s.sans:
        hl = dw(wl, cx - x0, top)
        out += dhead(s, x0, x0 + hl, top, 1, 0.7) + dhead(s, x1 - hr, x1, top, 0.6, 1)
    return out


def g_z(s, L, R):
    top = s.xh
    th = s.sh if s.sans else s.hair * 1.4
    wd = s.sv * (1.02 if s.serif else 0.98)
    h = dw(wd, R - L, top)
    out = G.rect(L + (R - L) * 0.06, top - th, R - (R - L) * 0.02, top)
    out += G.rect(L, 0, R, th)
    out += G.poly([(R - (R - L) * 0.02 - h, top - th + 1), (R - (R - L) * 0.02, top - th + 1), (L + h, th - 1), (L, th - 1)])
    if not s.sans:
        out += beak(s, L + (R - L) * 0.06 + 0.5, top, True, s.sl * 0.9)
        out += beak(s, R - 0.5, 0, False, s.sl * 1.1)
    return out


# --------------------------------------------- the roman's italic lowercase


def entry(s, xl, xr, ytop):
    """the entry stroke of an italic stem: a small hairline wedge in from
    the left at the top"""
    p = G.Path(xr, ytop)
    p.line(xl - s.sl * 0.9, ytop - s.sv * 0.55)
    p.line(xl - s.sl * 0.9, ytop - s.sv * 0.85)
    p.qto(xl, ytop - s.sv * 0.8, xl, ytop - s.sv * 1.6)
    p.line(xr, ytop - s.sv * 1.3)
    return p.close()


def exit_(s, xl, xr, y=0, reach=None):
    """the exit stroke at the foot of an italic stem: the stem bends right
    and ends in an upturned hairline"""
    reach = reach or s.sl * 1.6
    sv = xr - xl
    p = G.Path(xl, y + sv * 1.6)
    p.qto(xl, y - s.ov, xl + sv * 0.9, y - s.ov)
    p.qto(xr + reach * 0.55, y - s.ov, xr + reach, y + s.sv * 1.2)
    p.line(xr + reach - s.hair * 0.9, y + s.sv * 1.3)
    p.qto(xr + reach * 0.4, y + s.hair * 1.3, xr - 2, y + s.hair * 1.6 + sv * 0.6)
    p.line(xr, y + sv * 1.6)
    return p.close()


def gi_a(s, L, R):
    top = s.xh
    sv, th = s.sv, s.hair
    reach = (R - L) * 0.16
    xr = R - reach
    xs = xr - sv
    out = vstem(xs, xr, s.sv * 1.2, top + s.ov * 0.4)
    out += exit_(s, xs, xr, 0, reach)
    o = (L, -s.ov, xr - sv * 0.35, top + s.ov)
    i = (L + sv * 0.9, -s.ov + th, xs + 4, top + s.ov - th)
    out += ring_box(s, o, i, 20)
    return out


def gi_n(s, L, R):
    top = s.xh
    sv = s.sv
    reach = (R - L) * 0.16
    xs = L + s.sl * 0.7
    xr = R - reach
    out = vstem(xs, xs + sv, 0, top)
    out += entry(s, xs, xs + sv, top)
    out += arch(s, xs, xr, top, join=top * 0.55)
    out = [c for c in out]
    # right stem ends in an exit stroke instead of a plain foot
    out += exit_(s, xr - sv, xr, 0, reach)
    return out


def gi_u(s, L, R):
    top = s.xh
    sv = s.sv
    reach = (R - L) * 0.16
    x0 = L + s.sl * 0.7
    x1 = R - reach
    n = arch(s, x0, x1, top, join=top * 0.45)
    out = G.rotate(n, 180, (x0 + x1) / 2, top / 2)
    out += vstem(x1 - sv, x1, sv * 1.4, top)
    out += entry(s, x0, x0 + sv, top) + entry(s, x1 - sv, x1, top)
    out += exit_(s, x1 - sv, x1, 0, reach)
    return out


def gi_f(s, L, R):
    """the italic f: descends below the baseline, hooked at both ends"""
    top = s.xh
    sv = s.sv * 0.95
    th = s.hair
    cx = (L + R) / 2
    out = vstem(cx - sv / 2, cx + sv / 2, s.desc * 0.55, s.asc * 0.84)
    r = (R - cx) * 0.9
    o = (cx - sv / 2, s.asc * 0.84 - r, R, s.asc + s.ov)
    i = (cx + sv / 2, s.asc * 0.84 - r + th, R - sv * 0.5, s.asc + s.ov - th)
    out += arc_box(s, o, i, 30, 180, 90, None)
    out += ball(s, R - sv * 0.75, s.asc - sv * 0.75, sv * 0.55)
    o2 = (L, s.desc - s.ov, cx + sv / 2, s.desc * 0.55 + (s.desc * 0.55 - s.desc) * 0.0 + (-(s.desc) * 0.45))
    i2 = (L + sv * 0.5, s.desc - s.ov + th, cx - sv / 2, o2[3] - th)
    out += arc_box(s, o2, i2, 210, 360, 90, None)
    out += ball(s, L + sv * 0.75, s.desc + sv * 0.6, sv * 0.55)
    out += G.rect(cx - (R - L) * 0.3, top - s.hair * 1.3, cx + (R - L) * 0.32, top)
    return out


# ================================================================== digits


def fig_h(s):
    """the height of the figures"""
    return s.cap * (0.985 if s.sans else 0.97 if s.serif else 0.95)


def g_0(s, L, R):
    h = fig_h(s)
    tv = s.sv * (1.05 if s.sans else 1.12)
    out = ring_box(s, (L, -s.ov, R, h + s.ov), (L + tv, -s.ov + s.sh * (1.0 if s.sans else 0.9),
                                                 R - tv, h + s.ov - s.sh * (1.0 if s.sans else 0.9)),
                   0, n=s.n - 0.1, ni=s.ni - 0.1)
    # (no dot in w16 Mono's zero: at 8-10 pt it filled the counter and
    # the 0 read as an 8)
    return out


def g_1(s, L, R):
    h = fig_h(s)
    sv = s.svc * (1.04 if s.sans else 1.0)
    if s.sans:
        xs = R - sv
        out = vstem(xs, R, 0, h)
        out += G.poly([(xs + 2, h), (xs + 2, h - sv * 0.9), (L, h * 0.62), (L, h * 0.62 + sv * 1.02),
                       (xs - sv * 0.5, h)])
        return out
    cx = (L + R) / 2 + sv * 0.1
    out = vstem(cx - sv / 2, cx + sv / 2, 0, h + s.ov)
    out += G.poly([(cx + sv / 2 - 2, h + s.ov), (cx - sv / 2, h + s.ov - 2), (L + (R - L) * 0.08, h * 0.78),
                   (L + (R - L) * 0.12, h * 0.75)])
    if s.mono:
        out += G.rect(L + (R - L) * 0.05, 0, R - (R - L) * 0.05, s.st)
    else:
        out += foot(s, cx - sv / 2, cx + sv / 2, 0, 1.8, 1.8)
    return out


def g_2(s, L, R):
    h = fig_h(s)
    w = R - L
    sv, sh = (s.svc * 0.98 if s.sans else s.sv * 0.94 if s.serif else s.svc * 1.04), (s.shc if s.sans else s.hair * 1.2)
    pts = [(L + w * 0.03, h * 0.7), (L + w * 0.5, h + s.ov - sh / 2), (R - sv / 2, h * 0.7),
           (L + w * 0.55, h * 0.32), (L + sv * 0.5, sh * 0.45)]
    tans = [(0.35, 1), (1, 0), (0, -1), (-1, -0.9), (-0.7, -1)]
    out = G.stroke(pts, tans, wv=sv, wh=sh, cap0=0 if s.sans else None, cap1=None, tension=0.5)
    out += G.rect(L, 0, R, sh * (1.0 if s.sans else 1.2))
    return out


def g_3(s, L, R):
    h = fig_h(s)
    w = R - L
    sv, sh = (s.svc * 0.98 if s.sans else s.sv * 0.94 if s.serif else s.svc * 1.04), (s.shc if s.sans else s.hair * 1.2)
    ym = h * 0.55
    up = open_bowl(s, L + w * 0.06, R - w * 0.05, ym - sh / 2, h + s.ov, -90, 150, None, 0 if s.sans else 90, sv * 0.95, sh)
    lo = open_bowl(s, L, R, -s.ov, ym + sh / 2, -150, 90, 0 if s.sans else 90, None, sv, sh)
    bar = G.rect(L + w * 0.35, ym - sh / 2, L + w * 0.6, ym + sh / 2)
    out = up + lo + bar
    return out


def g_4(s, L, R):
    h = fig_h(s)
    w = R - L
    sv = s.svc * (1.0 if s.sans else 1.05)
    sh = s.shc if s.sans else s.hair * 1.3
    xs = L + w * 0.62
    yb = h * 0.27
    out = vstem(xs, xs + sv, 0, h)
    out += G.rect(L, yb, R, yb + sh)
    wd = s.thin * 1.2 if s.serif else sv * 0.95
    hd = dw(wd, xs - L, h - yb)
    out += G.poly([(L, yb + 1), (L + hd, yb + 1), (xs + sv, h), (xs + sv - hd * 1.0, h)])
    if not s.sans:
        out += foot(s, xs, xs + sv, 0, 1.2, 1.2)
    return out


def g_5(s, L, R):
    h = fig_h(s)
    w = R - L
    sv, sh = (s.svc * 0.98 if s.sans else s.sv * 0.94 if s.serif else s.svc * 1.04), (s.shc if s.sans else s.hair * 1.2)
    ytop = h
    xl = L + w * 0.1
    out = G.rect(xl, ytop - sh, R - w * 0.06, ytop)
    yj = h * 0.55
    out += G.poly([(xl, ytop - 2), (xl + sv * 0.95, ytop - 2), (xl + sv * 0.85, yj), (xl - sv * 0.05, yj - sv * 0.2)])
    lo = open_bowl(s, L, R, -s.ov, h * 0.64, -150, 140, 0 if s.sans else 90, None, sv, sh)
    out += lo
    return out


def g_6(s, L, R):
    h = fig_h(s)
    w = R - L
    sv, sh = (s.svc * 0.98 if s.sans else s.sv * 0.94 if s.serif else s.svc * 1.04), (s.shc if s.sans else s.hair * 1.2)
    yb = h * 0.62
    out = ring_box(s, (L, -s.ov, R, yb), (L + sv, -s.ov + sh, R - sv, yb - sh), 0)
    # the stem rising from the bowl's left side to the top right
    o = (L, -s.ov, R + w * 0.05, h + s.ov)
    i = (L + sv, -s.ov + sh, R - sv * 0.6, h + s.ov - sh)
    out += arc_box(s, o, i, 50 if s.sans else 45, 220, 0 if s.sans else 90, None)
    if s.serif:
        out += ball(s, R - sv * 0.6, h * 0.8, sv * 0.5)
    return out


def g_7(s, L, R):
    h = fig_h(s)
    w = R - L
    sv = s.svc * (1.0 if s.sans else 1.0)
    sh = s.shc if s.sans else s.hair * 1.3
    out = G.rect(L + (w * 0.02 if s.sans else w * 0.05), h - sh, R, h)
    xb = L + w * (0.3 if s.sans else 0.35)
    out += G.stroke([(R - sv * 0.5, h - sh * 0.5), (L + w * 0.55, h * 0.5), (xb, 0)],
                    [(-0.6, -1), (-0.35, -1), (-0.15, -1)], wv=sv, wh=sv, cap0=0, cap1=0,
                    widths=[0.8 if s.serif else 1.0, 1.0, 1.0])
    if s.serif:
        out += G.rect(L + w * 0.05, h * 0.82, L + w * 0.05 + s.st * 1.2, h)
    return out


def g_8(s, L, R):
    h = fig_h(s)
    w = R - L
    sv, sh = (s.svc * 0.96 if s.sans else s.sv * 0.92 if s.serif else s.svc), (s.shc if s.sans else s.hair * 1.2)
    ym = h * 0.55
    up = ring_box(s, (L + w * 0.07, ym - sh * 0.6, R - w * 0.07, h + s.ov),
                  (L + w * 0.07 + sv * 0.9, ym + sh * 0.4, R - w * 0.07 - sv * 0.9, h + s.ov - sh), 0)
    lo = ring_box(s, (L, -s.ov, R, ym + sh * 0.6), (L + sv, -s.ov + sh, R - sv, ym - sh * 0.4), 0)
    return up + lo


def g_9(s, L, R):
    h = fig_h(s)
    return G.rotate(g_6(s, L, R), 180, (L + R) / 2, h / 2)


# ============================================================ punctuation


def g_space(s, L, R):
    return []


def g_period(s, L, R, y=0):
    r = (R - L) / 2
    return dot(s, (L + R) / 2, y + r * 1.02, r)


def comma_shape(s, cx, ytop, r):
    """a comma: a dot with a tail curling down-left"""
    if s.sans:
        return G.poly([(cx - r, ytop), (cx + r, ytop), (cx + r, ytop - 2 * r),
                       (cx + r * 0.1, ytop - 2 * r - r * 1.5), (cx - r * 0.55, ytop - 2 * r - r * 1.5),
                       (cx - r * 0.1, ytop - 2 * r)])
    out = G.ellipse(cx, ytop - r, r, r, 2.0)
    out += G.stroke([(cx + r * 0.75, ytop - r), (cx + r * 0.45, ytop - 2.4 * r), (cx - r * 0.7, ytop - 3.3 * r)],
                    [(0, -1), (-0.6, -1), (-1, -0.6)], wv=r * 1.0, wh=r * 0.5, widths=[1.0, 0.7, 0.25])
    return out


def g_comma(s, L, R):
    r = min((R - L) / 2, s.dot / 2 * 1.05)
    return comma_shape(s, (L + R) / 2 + (0 if s.sans else (R - L) * 0.1), 2 * r, r)


def g_colon(s, L, R):
    r = (R - L) / 2
    return dot(s, (L + R) / 2, r * 1.02, r) + dot(s, (L + R) / 2, s.xh - r * 1.02, r)


def g_semicolon(s, L, R):
    r = min((R - L) / 2, s.dot / 2 * 1.05)
    cx = (L + R) / 2
    return dot(s, cx + (R - L) * 0.1 if not s.sans else cx, s.xh - r, r) + comma_shape(s, cx + (0 if s.sans else (R - L) * 0.1), 2 * r, r)


def g_exclam(s, L, R):
    r = (R - L) / 2
    cx = (L + R) / 2
    w = (R - L) * (0.92 if s.sans else 0.72)
    top = s.cap
    out = dot(s, cx, r * 1.02, r)
    if s.sans:
        out += G.poly([(cx - w / 2, top), (cx + w / 2, top), (cx + w * 0.38, top * 0.27), (cx - w * 0.38, top * 0.27)])
    else:
        out += G.stroke([(cx, top - w * 0.4), (cx, top * 0.3)], [(0, -1), (0, -1)], wv=w, widths=[1, 0.4])
        out += G.ellipse(cx, top - w * 0.42, w / 2, w * 0.45)
    return out


def g_question(s, L, R):
    top = s.cap
    w = R - L
    sv = s.svc * (0.95 if s.sans else 0.95)
    sh = s.shc if s.sans else s.hair * 1.2
    r = s.dot / 2 * (1.0 if s.sans else 1.05)
    cx = L + w * 0.48
    out = dot(s, cx, r, r)
    pts = [(L + w * 0.03, top * 0.73), (L + w * 0.5, top + s.ov - sh / 2), (R - sv / 2, top * 0.72),
           (cx + w * 0.1, top * 0.47), (cx, top * 0.27)]
    tans = [None, (1, 0), (0, -1), (-1, -0.6), (0, -1)]
    out += G.stroke(pts, tans, wv=sv, wh=sh, cap0=0 if s.sans else 90, cap1=0)
    if s.serif:
        out += ball(s, L + sv * 0.5, top * 0.72, sv * 0.5)
    return out


def g_quotedbl(s, L, R):
    w = R - L
    q = w * (0.36 if s.sans else 0.38)
    return g_quotesingle(s, L, L + q) + g_quotesingle(s, R - q, R)


def g_quotesingle(s, L, R):
    top = s.cap
    h = top * 0.34
    w = R - L
    if s.serif:
        return G.poly([(L, top), (R, top), (L + w * 0.68, top - h), (L + w * 0.32, top - h)])
    return G.poly([(L, top), (R, top), (R - w * 0.08, top - h), (L + w * 0.08, top - h)])


def g_grave_ascii(s, L, R):
    return accent_shape(s, 'grave', (L + R) / 2, s.xh + s.ov * 2, False, R - L)


def g_hyphen(s, L, R):
    t = s.sh * (1.05 if s.sans else 1.5 if s.serif else 1.0)
    yc = s.xh * (0.42 if s.sans else 0.4)
    return G.rect(L, yc - t / 2, R, yc + t / 2)


def g_slash(s, L, R):
    top = s.cap
    w = s.sv * (0.8 if s.sans else 0.62 if s.serif else 0.8)
    h = dw(w, R - L, top + s.ov * 2)
    return G.poly([(R - h, top + s.ov), (R, top + s.ov), (L + h, -s.ov), (L, -s.ov)])


def g_backslash(s, L, R):
    return G.mirror_x(g_slash(s, L, R), (L + R) / 2)


def g_bar(s, L, R):
    return G.rect(L, s.desc * 0.95, R, s.asc + s.ov)


def g_underscore(s, L, R):
    t = s.sh * (0.9 if s.sans else 0.9)
    y = s.desc * (0.62 if not s.mono else 0.5)
    return G.rect(L, y - t / 2, R, y + t / 2)


def g_plus(s, L, R):
    t = s.sh * (1.04 if s.sans else 1.0) if not s.serif else s.sv * 0.4
    cy = math_axis(s)
    hw = (R - L) / 2
    cx = (L + R) / 2
    return hbar(L, R, cy, t) + G.rect(cx - t / 2, cy - hw, cx + t / 2, cy + hw)


def math_axis(s):
    return s.xh * (0.53 if s.sans else 0.55 if s.serif else 0.52)


def g_minus_like(s, L, R, y, t):
    return hbar(L, R, y, t)


def g_equal(s, L, R):
    t = s.sh if not s.serif else s.sv * 0.4
    cy = math_axis(s)
    g = (R - L) * 0.21
    return hbar(L, R, cy + g, t) + hbar(L, R, cy - g, t)


def g_less(s, L, R):
    t = s.sh * 1.0 if not s.serif else s.sv * 0.4
    cy = math_axis(s)
    h = (R - L) * 0.5
    # the strokes' ends are cut vertically at R
    w = dw(t, h, R - L)
    out = G.poly([(L, cy + t * 0.55), (L, cy - t * 0.55), (R, cy + h - w * 0.5), (R, cy + h + w * 0.5)])
    out += G.poly([(L, cy - t * 0.55), (L, cy + t * 0.55), (R, cy - h + w * 0.5), (R, cy - h - w * 0.5)])
    return out


def g_greater(s, L, R):
    return G.mirror_x(g_less(s, L, R), (L + R) / 2)


def g_parenleft(s, L, R, mirror=False):
    top = s.asc + s.ov * 2
    bot = s.desc * 0.95
    w = R - L
    sv = s.sv * (0.95 if s.sans else 1.0)
    th = s.sh * 0.6 if s.sans else s.hair * 0.6
    o = (L, bot, L + 2 * w, top)
    i = (L + sv, bot + th, L + 2 * w - sv * 0.2, top - th)
    sh = arc_box(s, o, i, 120, 240, 0, 0, n=2.0, ni=2.0)
    # keep only the part within L..R
    if mirror:
        sh = G.mirror_x(sh, (L + R) / 2)
    return sh


def g_parenright(s, L, R):
    return g_parenleft(s, L, R, True)


def g_bracketleft(s, L, R, mirror=False):
    top = s.asc + s.ov * 2
    bot = s.desc * 0.95
    sv = s.sv * (0.9 if s.sans else 0.95)
    th = s.sh * (0.95 if s.sans else 0.9 if not s.serif else 0.9)
    if s.serif:
        th = s.hair * 1.3
    out = G.rect(L, bot, L + sv, top) + G.rect(L, top - th, R, top) + G.rect(L, bot, R, bot + th)
    if mirror:
        out = G.mirror_x(out, (L + R) / 2)
    return out


def g_bracketright(s, L, R):
    return g_bracketleft(s, L, R, True)


def g_braceleft(s, L, R, mirror=False):
    top = s.asc + s.ov * 2
    bot = s.desc * 0.95
    cy = (top + bot) / 2
    w = R - L
    sv = s.sv * (0.85 if s.sans else 0.9)
    th = s.sh * 0.8 if not s.serif else s.hair
    pts = [(R, top - th / 2), (L + w * 0.55, top - w * 0.3), (L + w * 0.5, cy + w * 0.6), (L, cy)]
    out = G.stroke(pts, [(-1, 0), (0, -1), (0, -1), (-1, -0.3)], wv=sv, wh=th, cap0=90, cap1=90, tension=0.45)
    out += G.mirror_y(out, cy)
    if mirror:
        out = G.mirror_x(out, (L + R) / 2)
    return out


def g_braceright(s, L, R):
    return g_braceleft(s, L, R, True)


def g_asciicircum(s, L, R):
    top = s.cap
    t = s.sh * 1.0 if not s.serif else s.sv * 0.4
    h = (R - L) * 0.72
    cx = (L + R) / 2
    hh = dw(t, (R - L) / 2, h)
    return G.poly([(L, top - h), (L + hh, top - h), (cx, top - hh * 0.6), (cx - hh * 0.5, top)]) + \
        G.poly([(R - hh, top - h), (R, top - h), (cx + hh * 0.5, top), (cx, top - hh * 0.6)])


def g_asciitilde(s, L, R):
    cy = math_axis(s)
    w = R - L
    t = s.sh * 1.05 if not s.serif else s.sv * 0.45
    amp = w * 0.13
    pts = [(L + t * 0.3, cy - amp * 0.7), (L + w * 0.27, cy + amp), (L + w * 0.73, cy - amp), (R - t * 0.3, cy + amp * 0.7)]
    return G.stroke(pts, [(0.6, 1), (1, 0), (1, 0), (0.6, 1)], wv=t, wh=t)


def g_asterisk(s, L, R):
    top = s.cap
    w = R - L
    cx = (L + R) / 2
    r = w / 2
    cy = top - r * 0.95
    t = s.sh * (1.0 if s.sans else 0.9) if not s.serif else s.sv * 0.5
    out = []
    for k in range(5):
        a = math.radians(90 + 72 * k)
        out += G.nstroke(cx, cy, cx + math.cos(a) * r, cy + math.sin(a) * r * 0.98, t * (1.0 if k else 1.1))
    return out


def g_numbersign(s, L, R):
    top = s.cap
    w = R - L
    t = s.sh * 1.0 if not s.serif else s.sv * 0.45
    out = []
    sl = 0.18 * w
    for x in (L + w * 0.3, L + w * 0.66):
        out += G.stem(x - sl * 0.5, 0, x + sl * 0.5, top, t * 1.05)
    for y in (top * 0.33, top * 0.67):
        out += hbar(L, R, y, t)
    return out


def g_dollar(s, L, R):
    top = s.cap
    out = s_curve(s, L, R, top * 0.0 - s.ov, top + s.ov, s.svc * (0.98 if s.sans else 1.0),
                  s.shc if s.sans else s.hair * 1.1)
    cx = (L + R) / 2
    t = s.sh * 0.75 if not s.serif else s.hair * 1.2
    out += G.rect(cx - t / 2, -top * 0.12, cx + t / 2, top * 1.07)
    return out


def g_percent(s, L, R):
    top = s.cap
    w = R - L
    sv = s.sv * 0.8
    th = s.sh * 0.8 if not s.serif else s.hair
    rw = w * 0.36
    out = ring_box(s, (L, top * 0.5, L + rw, top + s.ov), (L + sv, top * 0.5 + th, L + rw - sv, top + s.ov - th), 0)
    out += ring_box(s, (R - rw, -s.ov, R, top * 0.5), (R - rw + sv, -s.ov + th, R - sv, top * 0.5 - th), 0)
    t = s.sh * 0.95 if not s.serif else s.hair * 1.4
    out += G.poly([(L + w * 0.75, top + s.ov), (L + w * 0.75 + dw(t, w * 0.5, top), top + s.ov),
                   (L + w * 0.25 + dw(t, w * 0.5, top), -s.ov), (L + w * 0.25, -s.ov)])
    return out


def g_ampersand(s, L, R):
    top = s.cap
    w = R - L
    sv = s.svc * (0.98 if s.sans else 1.0)
    sh = s.shc if s.sans else s.hair * 1.2
    # upper loop
    ul = (L + w * 0.13, top * 0.52, L + w * 0.62, top + s.ov)
    out = ring_box(s, ul, (ul[0] + sv * 0.8, ul[1] + sh, ul[2] - sv * 0.8, ul[3] - sh), 0)
    # lower bowl from the upper loop's left down round the bottom to the
    # right, and the diagonal leg
    pts = [(L + w * 0.42, top * 0.55), (L + sv * 0.5, top * 0.26), (L + w * 0.38, -s.ov + sh / 2),
           (L + w * 0.7, top * 0.15), (R - sv * 0.4, top * 0.45)]
    out += G.stroke(pts, [(-1, -0.8), (0, -1), (1, 0), (1, 1), (0.3, 1)], wv=sv, wh=sh, cap0=None, cap1=0)
    out += G.poly([(L + w * 0.3, top * 0.6), (L + w * 0.3 + dw(sv, w * 0.6, top * 0.6), top * 0.6),
                   (R, 0), (R - dw(sv, w * 0.6, top * 0.6), 0)])
    return out


def g_at(s, L, R):
    top = s.cap
    w = R - L
    sv = s.sv * (0.62 if s.sans else 0.6)
    th = s.sh * 0.6 if not s.serif else s.hair
    yb = s.desc * 0.45
    o = (L, yb, R, top + s.ov)
    i = (L + sv * 1.1, yb + th * 1.3, R - sv * 1.1, top + s.ov - th * 1.3)
    out = arc_box(s, o, i, 335, 335 + 345 - 30, None, 0, n=2.0, ni=2.0)
    # the inner a
    cx = L + w * 0.5
    ia = (L + w * 0.27, top * 0.2, L + w * 0.66, top * 0.72)
    out += ring_box(s, ia, (ia[0] + sv * 1.2, ia[1] + th * 1.4, ia[2] - sv * 1.1, ia[3] - th * 1.4), 0, n=2.1, ni=2.2)
    out += G.stem(ia[2] - sv * 0.6, top * 0.18, ia[2] - sv * 0.3, top * 0.73, sv * 1.15)
    # the hook joining the a's stem to the outer ring
    hk = (ia[2] - sv * 1.5, top * 0.02, R, top * 0.62)
    out += arc_box(s, hk, (hk[0] + sv * 1.2, hk[1] + th * 1.3, hk[2] - sv * 1.1, hk[3] - th * 1.0), 200, 360, None, None, n=2.0, ni=2.0)
    del cx
    return out


# ================================================================ accents


def accent_shape(s, name, cx, base, caps, width=None):
    """an accent centred on cx with its bottom at base"""
    sv = s.sv
    w = width or (s.xh * (0.42 if not caps else 0.44))
    h = s.xh * (0.25 if caps else 0.3)
    t = sv * (0.92 if s.sans else 0.9)
    if caps:
        t *= 0.95
    if name in ('acute', 'grave'):
        ww = w * 0.62
        if s.serif:
            p = G.poly([(cx - ww / 2, base), (cx - ww / 2 + t * 0.35, base), (cx + ww / 2, base + h),
                        (cx + ww / 2 - t * 1.0, base + h)])
        else:
            hh = dw(t * 0.95, ww, h)
            p = G.poly([(cx - ww / 2, base), (cx - ww / 2 + hh, base), (cx + ww / 2, base + h),
                        (cx + ww / 2 - hh, base + h)])
        return p if name == 'acute' else G.mirror_x(p, cx)
    if name in ('circumflex', 'caron'):
        hh = dw(t * (0.95 if not s.serif else 0.6), w / 2, h)
        sh = G.poly([(cx - w / 2, base), (cx - w / 2 + hh, base), (cx + hh * 0.5, base + h), (cx - hh * 0.5, base + h)])
        sh += G.poly([(cx + w / 2 - hh, base), (cx + w / 2, base), (cx + hh * 0.5, base + h), (cx - hh * 0.5, base + h)])
        if name == 'caron':
            sh = G.mirror_y(sh, base + h / 2)
        return sh
    if name == 'tilde':
        tt = t * (0.85 if not s.serif else 0.6)
        amp = h * 0.33
        cy = base + h / 2
        pts = [(cx - w / 2 + tt * 0.3, cy - amp * 0.9), (cx - w * 0.22, cy + amp), (cx + w * 0.22, cy - amp),
               (cx + w / 2 - tt * 0.3, cy + amp * 0.9)]
        return G.stroke(pts, [(0.5, 1), (1, 0), (1, 0), (0.5, 1)], wv=tt, wh=tt)
    if name == 'dieresis':
        r = s.dot / 2 * (0.95 if not caps else 0.92)
        g = max(w * 0.6, r * 2 + sv * 0.9)
        return dot(s, cx - g / 2, base + r, r) + dot(s, cx + g / 2, base + r, r)
    if name == 'ring':
        r = w * 0.36
        tt = t * 0.55
        return ring_box(s, (cx - r, base, cx + r, base + 2 * r), (cx - r + tt, base + tt, cx + r - tt, base + 2 * r - tt), 0,
                        n=2.0, ni=2.0)
    if name == 'macron':
        tt = s.sh * (1.0 if not s.serif else 1.4)
        return G.rect(cx - w / 2, base, cx + w / 2, base + tt)
    if name == 'cedilla':
        tt = s.sh * 0.75 if not s.serif else s.hair * 1.3
        top = base
        out = G.rect(cx - tt / 2, top - h * 0.75, cx + tt / 2, top + 30)
        pts = [(cx, top - h * 0.7), (cx + w * 0.3, top - h * 1.1), (cx, top - h * 1.6), (cx - w * 0.32, top - h * 1.5)]
        out += G.stroke(pts, [(1, 0), (0, -1), (-1, 0), (-1, 0.2)], wv=t * 0.75, wh=tt)
        return out
    raise KeyError(name)


# ============================================================== symbols


def g_cent(s, L, R):
    out = g_c(s, L, R)
    cx = (L + R) / 2
    t = s.sh * 0.7 if not s.serif else s.hair * 1.2
    out += G.rect(cx - t / 2, -s.xh * 0.18, cx + t / 2, s.xh * 1.16)
    return out


def g_sterling(s, L, R):
    top = s.cap
    w = R - L
    sv = s.svc * 0.95
    sh = s.shc if not s.serif else s.hair * 1.4
    xs = L + w * 0.18
    # the stem curving up into the top hook
    o = (xs, top * 0.35, R, top + s.ov)
    i = (xs + sv, top * 0.35 + sh, R - sv * 0.6, top + s.ov - sh)
    out = arc_box(s, o, i, 30 if s.sans else 35, 180, 0 if not s.serif else 90, None)
    out += vstem(xs, xs + sv, top * 0.12, (o[1] + o[3]) / 2 + 2)
    out += G.rect(L, 0, R, sh)
    out += G.stroke([(xs + sv * 0.4, top * 0.2), (xs - w * 0.02, sh * 0.5), (L, sh * 0.5)], [(0, -1), (-1, -0.5), (-1, 0)],
                    wv=sv, wh=sh)
    out += G.rect(L + w * 0.04, top * 0.42, L + w * 0.68, top * 0.42 + sh)
    return out


def g_euro(s, L, R):
    top = s.cap
    w = R - L
    sv = s.svc * 0.95
    sh = s.shc if not s.serif else s.hair * 1.3
    xo = L + w * 0.12
    out = open_bowl(s, xo, R, -s.ov, top + s.ov, 42, 318, 0 if s.sans else 90, 0 if s.sans else 90, sv, sh)
    for y in (top * 0.58, top * 0.4):
        out += G.rect(L, y - sh / 2, L + w * 0.62, y + sh / 2)
    return out


def g_yen(s, L, R):
    top = s.cap
    out = g_Y(s, L, R)
    w = R - L
    sh = s.shc if not s.serif else s.hair * 1.4
    for y in (top * 0.36, top * 0.18):
        out += G.rect(L + w * 0.16, y - sh / 2, R - w * 0.16, y + sh / 2)
    return out


def g_section(s, L, R):
    top = s.cap
    w = R - L
    sv = s.sv * 0.95
    sh = s.sh if not s.serif else s.hair * 1.2
    up = s_curve(s, L, R, top * 0.18, top + s.ov, sv, sh)
    lo = s_curve(s, L, R, s.desc * 0.55, top * 0.62, sv, sh)
    # keep the upper s's top half and the lower s's bottom, joined by loops
    out = up + lo
    del w
    return out


def g_copyright(s, L, R, letter='C'):
    top = s.cap
    w = R - L
    t = s.sh * 0.55 if not s.serif else s.hair
    out = ring_box(s, (L, -s.ov, R, top + s.ov), (L + t, -s.ov + t, R - t, top + s.ov - t), 0, n=2.0, ni=2.0)
    cx, cy = (L + R) / 2, top / 2
    iw = w * 0.36
    ih = top * 0.55
    sv = s.sv * 0.6
    if letter == 'C':
        out += open_bowl(s, cx - iw / 2, cx + iw / 2, cy - ih / 2, cy + ih / 2, 45, 315, 0, 0, sv, sv * 0.7)
    else:
        x0 = cx - iw * 0.45
        out += vstem(x0, x0 + sv, cy - ih / 2, cy + ih / 2)
        out += dbowl(s, x0 + sv, cx + iw * 0.5, cy, cy + ih / 2, sv, sv * 0.7)
        out += G.poly([(cx - iw * 0.05, cy + 5), (cx + iw * 0.12, cy + 5), (cx + iw * 0.55, cy - ih / 2), (cx + iw * 0.32, cy - ih / 2)])
    return out


def g_registered(s, L, R):
    return g_copyright(s, L, R, 'R')


def superior(s, fn, L, R, ybase=None):
    """a smaller copy of a figure/letter function raised (ordinals,
    superscripts): drawn at full size into a wider box, then scaled"""
    f = 0.62
    w = (R - L) / f
    sh = fn(s, 0, w)
    ybase = ybase if ybase is not None else s.cap * 0.42
    return G.xform(sh, lambda x, y: (L + x * f, ybase + y * f))


def g_ordfeminine(s, L, R):
    out = superior(s, g_a, L, R, s.cap * 0.48)
    out += G.rect(L, s.cap * 0.3, R, s.cap * 0.3 + s.sh * 0.7)
    return out


def g_ordmasculine(s, L, R):
    out = superior(s, g_o, L, R, s.cap * 0.48)
    out += G.rect(L, s.cap * 0.3, R, s.cap * 0.3 + s.sh * 0.7)
    return out


def g_guillemot(s, L, R, right=False, single=False):
    cy = s.xh * 0.5
    w = R - L
    t = s.sv * (0.82 if s.sans else 0.7)
    h = s.xh * 0.33
    out = []
    parts = [(L, R)] if single else [(L, L + w * 0.48), (L + w * 0.52, R)]
    for a, b in parts:
        hh = dw(t, b - a, h)
        out += G.poly([(b - hh, cy + h), (b, cy + h), (a + hh, cy), (a, cy)])
        out += G.poly([(a, cy), (a + hh, cy), (b, cy - h), (b - hh, cy - h)])
    if right:
        out = G.mirror_x(out, (L + R) / 2)
    return out


def g_guillemotleft(s, L, R):
    return g_guillemot(s, L, R)


def g_guillemotright(s, L, R):
    return g_guillemot(s, L, R, True)


def g_logicalnot(s, L, R):
    t = s.sh if not s.serif else s.sv * 0.4
    cy = math_axis(s) + (R - L) * 0.08
    return G.rect(L, cy - t / 2, R, cy + t / 2) + G.rect(R - t, cy - (R - L) * 0.32, R, cy + t / 2)


def g_degree(s, L, R):
    top = s.cap + s.ov
    w = R - L
    t = s.sh * 0.75 if not s.serif else s.hair * 1.3
    return ring_box(s, (L, top - w, R, top), (L + t, top - w + t, R - t, top - t), 0, n=2.0, ni=2.0)


def g_plusminus(s, L, R):
    t = s.sh if not s.serif else s.sv * 0.4
    hw = (R - L) / 2
    cy = math_axis(s) + hw * 0.18
    cx = (L + R) / 2
    out = hbar(L, R, cy, t) + G.rect(cx - t / 2, cy - hw * 0.82, cx + t / 2, cy + hw * 0.82)
    out += G.rect(L, 0, R, t)
    return out


def g_sup(s, fn, L, R):
    return superior(s, fn, L, R, s.cap * 0.42)


def g_mu(s, L, R):
    top = s.xh
    sv = s.sv
    u = g_u_plain(s, L, R)
    xs = L + (s.sl * 0.95 if not s.sans else 0)
    return u + vstem(xs, xs + sv, s.desc, top * 0.4)


def g_u_plain(s, L, R):
    s2 = s
    if s.italic and s.serif:
        return gi_u(s2, L, R)
    top = s.xh
    sv = s.sv
    x0 = L + (s.sl * 0.95 if not s.sans else 0)
    x1 = R - (s.sl * 0.3 if s.serif else (s.sl * 0.5 if s.mono else 0))
    n = arch(s, x0, x1, top)
    out = G.rotate(n, 180, (x0 + x1) / 2, top / 2)
    out += vstem(x1 - sv, x1, 0, top)
    return out


def g_paragraph(s, L, R):
    top = s.cap
    w = R - L
    sv = s.sv * 0.85
    out = vstem(R - sv, R, s.desc * 0.95, top)
    out += vstem(L + w * 0.5, L + w * 0.5 + sv, s.desc * 0.95, top)
    out += G.rect(L + w * 0.3, top - s.sh, R, top)
    out += ring_box(s, (L, top * 0.4, L + w * 0.62, top), (L - w, 0, L - w * 0.5, 1), 0) if False else []
    out += G.ellipse(L + w * 0.32, top * 0.7, w * 0.32, top * 0.3, 2.0)
    return out


def g_periodcentered(s, L, R):
    r = (R - L) / 2
    return dot(s, (L + R) / 2, math_axis(s) * 0.95, r)


def g_multiply(s, L, R):
    t = s.sh if not s.serif else s.sv * 0.4
    cy = math_axis(s)
    hw = (R - L) / 2
    cx = (L + R) / 2
    return G.nstroke(cx - hw, cy - hw, cx + hw, cy + hw, t) + G.nstroke(cx - hw, cy + hw, cx + hw, cy - hw, t)


def g_divide(s, L, R):
    t = s.sh if not s.serif else s.sv * 0.4
    cy = math_axis(s)
    r = s.dot / 2 * 0.9
    cx = (L + R) / 2
    return hbar(L, R, cy, t) + dot(s, cx, cy + (R - L) * 0.3, r) + dot(s, cx, cy - (R - L) * 0.3, r)


def g_exclamdown(s, L, R):
    # turned round about half the x-height: the dot at the x-height
    return G.rotate(g_exclam(s, L, R), 180, (L + R) / 2, s.xh / 2)


def g_questiondown(s, L, R):
    return G.rotate(g_question(s, L, R), 180, (L + R) / 2, s.xh / 2)


def g_AE(s, L, R):
    top = s.cap
    w = R - L
    sv = s.svc
    xs = L + w * 0.5
    # the A's left leg and bar, the E's arms
    i = s.sl if not s.sans else 0
    out = G.poly([(L + i * 0.5, 0), (L + i * 0.5 + dw(sv * (0.98 if not s.serif else 0.55), w * 0.4, top), 0),
                  (xs + sv * 0.5, top), (xs - sv * 0.4, top)])
    out += vstem(xs, xs + sv, 0, top)
    out += G.rect(L + w * 0.2, top * 0.3 - s.shc / 2, xs + 5, top * 0.3 + s.shc / 2)
    out += _arms(s, L, R, xs, sv, [(top, 0.95, 'top'), (top * 0.53, 0.86, 'mid'), (0, 1.0, 'bot')])
    out += G.rect(xs - sv * 0.4, top - s.shc, xs + 5, top)
    if not s.sans:
        out += foot(s, xs, xs + sv, 0, 0.6, 0) + foot(s, L + i * 0.5, L + i * 0.5 + sv * 0.6, 0)
    return out


def g_ae(s, L, R):
    w = R - L
    m = L + w * 0.5
    a = g_a_plain_for_ae(s, L, m + s.sv * 0.5)
    e = g_e(s, m - s.sv * 0.5, R)
    return a + e


def g_a_plain_for_ae(s, L, R):
    top = s.xh
    sv, th = s.sv, s.sh if s.sans else s.hair
    xs0 = R - sv
    ytop = top * 0.6
    out = vstem(xs0, R, top * 0.25, ytop)
    o = (L + (R - L) * 0.05, ytop - (top + s.ov - ytop), R, top + s.ov)
    i = (L + (R - L) * 0.05 + sv * 0.9, ytop - (top + s.ov - ytop) + th, xs0, top + s.ov - th)
    out += arc_box(s, o, i, 0, 150, None, 0 if s.sans else 90)
    yb = top * 0.56
    bo = (L, -s.ov, R, yb)
    bi = (L + sv * 0.9, -s.ov + th, xs0 + 6, yb - s.sh * 0.9)
    out += ring_box(s, bo, bi, 0)
    return out


def g_OE(s, L, R):
    top = s.cap
    w = R - L
    sv = s.svc
    xs = L + w * 0.48
    out = bowl(s, L, xs + sv * 0.6, -s.ov, top + s.ov, sv, s.shc if s.sans else s.hair)
    out += vstem(xs, xs + sv, 0, top)
    out += _arms(s, L, R, xs, sv, [(top, 0.95, 'top'), (top * 0.53, 0.86, 'mid'), (0, 1.0, 'bot')])
    out += G.rect(L + w * 0.3, top - s.shc, xs + 5, top) + G.rect(L + w * 0.3, 0, xs + 5, s.shc)
    return out


def g_oe(s, L, R):
    w = R - L
    m = L + w * 0.5
    return g_o(s, L, m + s.sv * 0.5) + g_e(s, m - s.sv * 0.5, R)


def g_Oslash(s, L, R, caps=True):
    out = g_O(s, L, R) if caps else g_o(s, L, R)
    top = s.cap if caps else s.xh
    t = s.sh * 0.9 if not s.serif else s.hair * 1.4
    w = R - L
    out += G.nstroke(L + w * 0.02, -s.ov * 3 - top * 0.02, R - w * 0.02, top + s.ov * 3 + top * 0.02, t)
    return out


def g_oslash(s, L, R):
    return g_Oslash(s, L, R, False)


def g_Eth(s, L, R):
    out = g_D(s, L, R)
    xs = L + (s.sl if not s.sans else 0)
    w = R - L
    out += hbar(L, xs + s.svc + w * 0.18, s.cap * 0.5, s.shc)
    return out


def g_eth(s, L, R):
    top = s.xh
    w = R - L
    sv = s.sv
    th = s.sh if s.sans else s.hair
    out = bowl(s, L, R, -s.ov, top * 0.95 + s.ov, sv, th)
    # the rising stroke from the bowl's right side to the upper left
    pts = [(R - sv / 2, top * 0.5), (R - sv * 0.6, top * 1.0), (L + w * 0.32, s.asc)]
    out += G.stroke(pts, [(0, 1), (-0.5, 1), (-1, 0.6)], wv=sv, wh=th, cap1=0)
    out += G.nstroke(L + w * 0.3, s.asc - (s.asc - top) * 0.6, R - w * 0.1, s.asc - (s.asc - top) * 0.05,
                     s.sh * 0.85 if not s.serif else s.hair * 1.3)
    return out


def g_Thorn(s, L, R):
    top = s.cap
    sv = s.svc
    xs = L + (s.sl if not s.sans else 0)
    out = vstem(xs, xs + sv, 0, top)
    out += dbowl(s, xs + sv, R, top * 0.2, top * 0.8, sv, s.shc)
    if not s.sans:
        out += head(s, xs, xs + sv, top) + foot(s, xs, xs + sv, 0)
    return out


def g_thorn(s, L, R):
    top = s.xh
    sv = s.sv
    xs = _xs(s, L)
    out = vstem(xs, xs + sv, s.desc, s.asc)
    th = s.sh if s.sans else s.hair
    out += side_bowl(s, xs, xs + sv, R, -s.ov, top + s.ov, sv, th, True)
    out += flag(s, xs, xs + sv, s.asc)
    out += foot(s, xs, xs + sv, s.desc)
    return out


def g_germandbls(s, L, R):
    top = s.xh
    w = R - L
    sv = s.sv
    th = s.sh if s.sans else s.hair * 1.2
    xs = L + (s.sl * 0.5 if not s.sans else 0)
    ym = top * 0.64
    xr1 = L + w * 0.86
    yt = (ym + s.asc) / 2
    out = vstem(xs, xs + sv, 0, yt + 4)
    # the upper bowl over the top and down to the waist
    o = (xs, ym - th / 2, xr1, s.asc + s.ov)
    i = (xs + sv, ym + th / 2, xr1 - sv * 0.92, s.asc + s.ov - th)
    out += arc_box(s, o, i, -90, 180, None, None)
    # the lower bowl, from the waist round to the bottom left
    o2 = (L + w * 0.3, -s.ov, R, ym + th / 2)
    i2 = (L + w * 0.3 + sv * 0.6, -s.ov + th, R - sv, ym - th / 2)
    out += arc_box(s, o2, i2, -100, 90, 90, None)
    out += G.rect((o[0] + o[2]) / 2 - 4, ym - th / 2, (o2[0] + o2[2]) / 2 + 4, ym + th / 2)
    if not s.sans:
        out += foot(s, xs, xs + sv, 0, 1, 0.4)
    return out


# ================================================================= tables


CAPS = {c: globals()['g_' + c] for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'}
LOWER = {c: globals()['g_' + c] for c in 'abcdefghijklmnopqrstuvwxyz'}
DIGITS = {c: globals()['g_' + c] for c in '0123456789'}

BASE = {}
BASE.update(CAPS)
BASE.update(LOWER)
BASE.update(DIGITS)
BASE.update({
    ' ': g_space, ' ': g_space,
    '!': g_exclam, '"': g_quotedbl, '#': g_numbersign, '$': g_dollar, '%': g_percent,
    '&': g_ampersand, "'": g_quotesingle, '(': g_parenleft, ')': g_parenright,
    '*': g_asterisk, '+': g_plus, ',': g_comma, '-': g_hyphen, '­': g_hyphen,
    '.': g_period, '/': g_slash, ':': g_colon, ';': g_semicolon, '<': g_less,
    '=': g_equal, '>': g_greater, '?': g_question, '@': g_at, '[': g_bracketleft,
    '\\': g_backslash, ']': g_bracketright, '^': g_asciicircum, '_': g_underscore,
    '`': g_grave_ascii, '{': g_braceleft, '|': g_bar, '}': g_braceright, '~': g_asciitilde,
    '¡': g_exclamdown, '¢': g_cent, '£': g_sterling, '€': g_euro,
    '¥': g_yen, '§': g_section, '©': g_copyright, 'ª': g_ordfeminine,
    '«': g_guillemotleft, '¬': g_logicalnot, '®': g_registered,
    '°': g_degree, '±': g_plusminus,
    '²': lambda s, L, R: g_sup(s, g_2, L, R),
    '³': lambda s, L, R: g_sup(s, g_3, L, R),
    '¹': lambda s, L, R: g_sup(s, g_1, L, R),
    'µ': g_mu, '¶': g_paragraph, '·': g_periodcentered,
    'º': g_ordmasculine, '»': g_guillemotright, '¿': g_questiondown,
    'Æ': g_AE, 'æ': g_ae, 'Œ': g_OE, 'œ': g_oe,
    'Ð': g_Eth, 'ð': g_eth, 'Þ': g_Thorn, 'þ': g_thorn,
    '×': g_multiply, '÷': g_divide, 'Ø': g_Oslash, 'ø': g_oslash,
    'ß': g_germandbls, 'ı': g_dotlessi,
})

# standalone accents (8859-15 has only the macron of the spacing ones)
SPACING_ACCENTS = {'¯': 'macron'}

# accented letters: base, accent
COMPOSED = {}
for base, acc, chars in (
        ('A', 'grave', 'À'), ('A', 'acute', 'Á'), ('A', 'circumflex', 'Â'),
        ('A', 'tilde', 'Ã'), ('A', 'dieresis', 'Ä'), ('A', 'ring', 'Å'),
        ('C', 'cedilla', 'Ç'), ('E', 'grave', 'È'), ('E', 'acute', 'É'),
        ('E', 'circumflex', 'Ê'), ('E', 'dieresis', 'Ë'), ('I', 'grave', 'Ì'),
        ('I', 'acute', 'Í'), ('I', 'circumflex', 'Î'), ('I', 'dieresis', 'Ï'),
        ('N', 'tilde', 'Ñ'), ('O', 'grave', 'Ò'), ('O', 'acute', 'Ó'),
        ('O', 'circumflex', 'Ô'), ('O', 'tilde', 'Õ'), ('O', 'dieresis', 'Ö'),
        ('U', 'grave', 'Ù'), ('U', 'acute', 'Ú'), ('U', 'circumflex', 'Û'),
        ('U', 'dieresis', 'Ü'), ('Y', 'acute', 'Ý'), ('Y', 'dieresis', 'Ÿ'),
        ('S', 'caron', 'Š'), ('Z', 'caron', 'Ž'),
        ('a', 'grave', 'à'), ('a', 'acute', 'á'), ('a', 'circumflex', 'â'),
        ('a', 'tilde', 'ã'), ('a', 'dieresis', 'ä'), ('a', 'ring', 'å'),
        ('c', 'cedilla', 'ç'), ('e', 'grave', 'è'), ('e', 'acute', 'é'),
        ('e', 'circumflex', 'ê'), ('e', 'dieresis', 'ë'), ('ı', 'grave', 'ì'),
        ('ı', 'acute', 'í'), ('ı', 'circumflex', 'î'), ('ı', 'dieresis', 'ï'),
        ('n', 'tilde', 'ñ'), ('o', 'grave', 'ò'), ('o', 'acute', 'ó'),
        ('o', 'circumflex', 'ô'), ('o', 'tilde', 'õ'), ('o', 'dieresis', 'ö'),
        ('u', 'grave', 'ù'), ('u', 'acute', 'ú'), ('u', 'circumflex', 'û'),
        ('u', 'dieresis', 'ü'), ('y', 'acute', 'ý'), ('y', 'dieresis', 'ÿ'),
        ('s', 'caron', 'š'), ('z', 'caron', 'ž')):
    COMPOSED[chars] = (base, acc)


def draw(s, ch, L, R):
    """the shape of character ch with its ink in L..R (upright)"""
    _DIAG[0] = s.diag
    if ch in BASE:
        return BASE[ch](s, L, R)
    if ch in SPACING_ACCENTS:
        caps = False
        return accent_shape(s, SPACING_ACCENTS[ch], (L + R) / 2, s.cap + s.ov * 2, caps, R - L)
    if ch in COMPOSED:
        base, acc = COMPOSED[ch]
        sh = BASE[base](s, L, R)
        caps = base.isupper()
        bx = G.bbox(sh)
        if acc == 'cedilla':
            cx = (bx[0] + bx[2]) / 2 + (s.sv * 0.1)
            return sh + accent_shape(s, acc, cx, 0, caps)
        top = s.cap if caps else s.xh
        cx = (bx[0] + bx[2]) / 2
        if base == 'ı':
            cx = _stem_centre(sh, top)
        if base in 'a' and not (s.italic and s.serif):
            cx = (bx[0] + bx[2]) / 2 + (bx[2] - bx[0]) * 0.02
        gap = s.xh * (0.09 if caps else 0.12) + s.ov
        width = (bx[2] - bx[0]) * 0.8 if acc in ('dieresis',) else None
        if acc == 'dieresis':
            width = min(width, s.xh * 0.55)
        return sh + accent_shape(s, acc, cx, top + gap, caps, None if acc != 'dieresis' else None)
    raise KeyError('no design for U+%04X' % ord(ch))


def _stem_centre(sh, top):
    """the x centre of the ink near the top of a shape"""
    xs = [x for c in sh for x, y in c.p if top * 0.5 < y <= top + 1]
    return (min(xs) + max(xs)) / 2 if xs else 0
