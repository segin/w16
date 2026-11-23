"""tthint - TrueType hinting for w16's fonts (tools/ttgen.py; ARCHITECTURE.md
6.9.3).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).
Written from the published TrueType instruction set (Apple's TrueType
Reference Manual, Microsoft's TrueType instruction specification); no
other hinter's code or bytecode is used.

The hints are made from w16's own outlines, which tools/ttgeom.py fits
with on-curve points at every corner and every horizontal or vertical
extremum, so edges can be read off the points:

  - an edge is a set of on-curve points on one level (y for horizontal
    edges, x for vertical ones) where the outline runs along that level;
    which side the ink is on follows from the direction of travel
    (TrueType's ink contours run clockwise);
  - blue zones: an edge with ink above at the baseline or the descender,
    or with ink below at the x-height, cap height or ascender, is pinned
    to that height (MIAP to its control value), round overshoots to an
    overshoot control value that the prep program makes equal to the
    flat height below the size where the overshoot reaches a pixel;
  - stems: an edge and the nearest edge facing it across the ink (a bar,
    a hairline, a serif's thickness, a vertical stem, a bowl's side) are
    linked with MIRP (minimum distance one pixel, rounded, the face's
    common stem width as control value when the stem is near it), the
    first one pinned (blue zone) or rounded (MDAP); other points of an
    edge follow with ALIGNRP;
  - everything else moves with IUP (interpolation) in y, then in x.

The font program holds the stem-linking functions (fpgm()). The prep
program turns on dropout control (SCANCTRL 511, SCANTYPE 1) and
raises the top zones' control values by 3/8 pixel (RCVT, ADD, WCVTP) and
does the overshoot suppression (MPPEM, LT, IF, RCVT, WCVTP). Only TrueType 1.0 instructions are used (Windows 3.1's
rasterizer); advance widths are never touched (no phantom point moves).
"""
# opcodes
SVTCA_Y, SVTCA_X = 0x00, 0x01
SRP0 = 0x10
IUP_Y, IUP_X = 0x30, 0x31
ALIGNRP = 0x3C
MIAP_R = 0x3F
MDAP_R = 0x2F
MIRP = 0xE0          # + flags: 0x10 set rp0, 0x08 min distance, 0x04 round, 1 black
MIRP_STEM = MIRP | 0x10 | 0x08 | 0x04 | 0x01
MDRP_STEM = 0xC0 | 0x10 | 0x08 | 0x04 | 0x01
SCANCTRL, SCANTYPE = 0x85, 0x8D
MPPEM, LT, IF, EIF = 0x4B, 0x50, 0x58, 0x59
RCVT, WCVTP = 0x45, 0x44
ADD = 0x60
FDEF, ENDF, CALL = 0x2C, 0x2D, 0x2B
FN_LINK, FN_LINK_FREE = 0, 1      # the font program's functions


def fpgm():
    """the font program: two functions linking a stem's second edge to its
    first (the reference point): 0 (b, cvt, a): SRP0 a, MIRP b to the
    control value; 1 (b, a): SRP0 a, MDRP b at its own rounded width; both
    with a minimum distance of one pixel"""
    return (push([FN_LINK]) + bytes([FDEF, SRP0, MIRP_STEM, ENDF])
            + push([FN_LINK_FREE]) + bytes([FDEF, SRP0, MDRP_STEM, ENDF]))


def push(vals):
    vals = list(vals)
    if not vals:
        return b''
    if all(0 <= v < 256 for v in vals):
        if len(vals) <= 8:
            return bytes([0xB0 + len(vals) - 1] + vals)
        return bytes([0x40, len(vals)] + vals)
    out = bytes([0xB8 + len(vals) - 1]) if len(vals) <= 8 else bytes([0x41, len(vals)])
    for v in vals:
        out += (v & 0xFFFF).to_bytes(2, 'big')
    return out


# ------------------------------------------------------------- analysis


def edges(contours, axis):
    """clusters of edge points: (kind, level, [point indices], (lo, hi))
    kind +1: ink on the low side (top / right edge), -1: on the high side
    (bottom / left edge); axis 1: horizontal edges (levels in y)"""
    other = 1 - axis
    found = []
    base = 0
    for c in contours:
        n = len(c)
        for i, p in enumerate(c):
            if not p[2]:
                continue
            prv, nxt = c[i - 1], c[(i + 1) % n]
            d = None
            if abs(nxt[axis] - p[axis]) <= 1 and nxt[other] != p[other]:
                d = nxt[other] - p[other]
            elif abs(prv[axis] - p[axis]) <= 1 and prv[other] != p[other]:
                d = p[other] - prv[other]
            if d is None:
                continue
            # clockwise ink: y edges travelled +x have ink below (kind +1);
            # x edges travelled +y have ink to the right (kind -1)
            kind = (1 if d > 0 else -1) if axis == 1 else (-1 if d > 0 else 1)
            ext = (min(p[other], prv[other], nxt[other]), max(p[other], prv[other], nxt[other]))
            found.append((kind, p[axis], base + i, ext))
        base += n
    found.sort()
    clusters = []
    for kind, lvl, idx, ext in found:
        if clusters and clusters[-1][0] == kind and abs(clusters[-1][1] - lvl) <= 2:
            cl = clusters[-1]
            cl[2].append(idx)
            cl[3] = (min(cl[3][0], ext[0]), max(cl[3][1], ext[1]))
        else:
            clusters.append([kind, lvl, [idx], ext])
    return clusters


def stems(clusters, maxw):
    """pairs (low edge, high edge) facing each other across ink: a -1 edge
    with the nearest +1 edge above it whose extent overlaps"""
    out = []
    lows = [c for c in clusters if c[0] == -1]
    highs = [c for c in clusters if c[0] == 1]
    for lo in lows:
        best = None
        for hi in highs:
            w = hi[1] - lo[1]
            if 0 < w <= maxw and hi[3][0] < lo[3][1] and lo[3][0] < hi[3][1]:
                if best is None or w < best[1] - lo[1]:
                    best = hi
        if best is not None:
            out.append((lo, best))
    return out


def common_widths(all_widths, n=6):
    """the most frequent stem widths (clusters within 6%)"""
    ws = sorted(all_widths)
    groups = []
    for w in ws:
        if groups and w <= groups[-1][0] * 1.06 + 2:
            groups[-1][1].append(w)
        else:
            groups.append([w, [w]])
    groups.sort(key=lambda g: -len(g[1]))
    out = []
    for g in groups[:n]:
        v = sorted(g[1])[len(g[1]) // 2]
        out.append(v)
    return sorted(out)


class Hinter:
    """zones: list of (flat height, overshoot height, kind) with kind -1
    for bottom zones (baseline, descender), +1 for top zones"""

    def __init__(self, upem, zones, maxw_y, maxw_x, do_x=True):
        self.upem = upem
        self.zones = zones
        self.maxw_y = maxw_y
        self.maxw_x = maxw_x
        self.do_x = do_x
        self.cvt = []
        self.zone_cvt = []
        for flat, over, kind in zones:
            self.zone_cvt.append((len(self.cvt), len(self.cvt) + 1))
            self.cvt += [int(flat), int(over)]
        self.ystems = []
        self.xstems = []

    def learn(self, glyph_contours):
        """pass 1: collect stem widths of every glyph"""
        wy, wx = [], []
        for cs in glyph_contours:
            for lo, hi in stems(edges(cs, 1), self.maxw_y):
                wy.append(hi[1] - lo[1])
            if self.do_x:
                for lo, hi in stems(edges(cs, 0), self.maxw_x):
                    wx.append(hi[1] - lo[1])
        self.ystems = common_widths(wy) if wy else []
        self.xstems = common_widths(wx) if wx else []
        self.ystem_cvt = {w: len(self.cvt) + i for i, w in enumerate(self.ystems)}
        self.cvt += self.ystems
        self.xstem_cvt = {w: len(self.cvt) + i for i, w in enumerate(self.xstems)}
        self.cvt += self.xstems

    def prep(self):
        """dropout control on at every size; overshoots off below the size
        where they reach one pixel"""
        p = push([1, 0xFF]) if False else b''
        p = bytes([0xB8, 0x01, 0xFF]) + bytes([SCANCTRL]) + push([1]) + bytes([SCANTYPE])
        # top zones (x-height, cap height, ascender) round up from an
        # eighth of a pixel: 3/8 px is added to their control values, so a
        # 6.27-pixel cap height becomes 7, as hinted fonts of the time do
        for (flat, over, kind), (cf, co) in zip(self.zones, self.zone_cvt):
            if kind == 1:
                for c in (cf, co):
                    p += push([c, c]) + bytes([RCVT]) + push([24]) + bytes([ADD, WCVTP])
        for (flat, over, kind), (cf, co) in zip(self.zones, self.zone_cvt):
            ov = abs(over - flat)
            if ov == 0:
                continue
            limit = min(255, -(-self.upem // ov))     # ppem where ov is 1 px
            p += bytes([MPPEM]) + push([limit]) + bytes([LT, IF])
            p += push([co, cf]) + bytes([RCVT, WCVTP, EIF])
        return p

    def _stem_cvt(self, table, w):
        best = None
        for v, idx in table.items():
            if abs(v - w) <= max(8, v * 0.2) and (best is None or abs(v - w) < abs(best[0] - w)):
                best = (v, idx)
        return best[1] if best else None

    def _zone(self, kind, lvl):
        for (flat, over, zk), (cf, co) in zip(self.zones, self.zone_cvt):
            if zk != kind:
                continue
            if abs(lvl - flat) <= 4:
                return cf
            lo, hi = sorted((flat, flat + (over - flat) * 2.2))
            if lo - 2 <= lvl <= hi and lvl != flat:
                return co
        return None

    def glyph(self, contours):
        """the glyph's instructions"""
        if not contours:
            return b''
        code = bytes([SVTCA_Y])
        code += self._axis(contours, 1)
        code += bytes([IUP_Y])
        if self.do_x:
            x = self._axis(contours, 0)
            if x:
                code += bytes([SVTCA_X]) + x + bytes([IUP_X])
        return code

    def _axis(self, contours, axis):
        cls = edges(contours, axis)
        maxw = self.maxw_y if axis == 1 else self.maxw_x
        pairs = stems(cls, maxw)
        table = self.ystem_cvt if axis == 1 else self.xstem_cvt
        touched = set()
        done = set()
        code = b''

        def align_rest(cl):
            nonlocal code
            for q in cl[2][1:]:
                if q not in touched:
                    code += push([q]) + bytes([ALIGNRP])
                    touched.add(q)

        if axis == 1:
            # blue zones
            for cl in cls:
                z = self._zone(cl[0], cl[1])
                if z is None:
                    continue
                for q in cl[2]:
                    if q not in touched:
                        code += push([q, z]) + bytes([MIAP_R])
                        touched.add(q)
                done.add(id(cl))
        # stems: from an edge already placed, else round the low edge
        order = sorted(pairs, key=lambda pr: (id(pr[0]) not in done and id(pr[1]) not in done))
        for lo, hi in order:
            if id(lo) in done and id(hi) in done:
                continue
            if id(hi) in done and id(lo) not in done:
                a, b = hi, lo
            else:
                a, b = lo, hi
            if b[2][0] in touched:
                continue
            if id(a) not in done:
                if a[2][0] in touched:
                    continue
                code += push([a[2][0]]) + bytes([MDAP_R])
                touched.add(a[2][0])
                align_rest(a)
                done.add(id(a))
            w = abs(b[1] - a[1])
            c = self._stem_cvt(table, w)
            if c is not None:
                code += push([b[2][0], c, a[2][0], FN_LINK]) + bytes([CALL])
            else:
                code += push([b[2][0], a[2][0], FN_LINK_FREE]) + bytes([CALL])
            touched.add(b[2][0])
            align_rest(b)
            done.add(id(b))
        return code
