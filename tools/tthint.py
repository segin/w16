"""tthint - TrueType hinting for w16's fonts (tools/ttgen.py,
tools/ttautohint.py; ARCHITECTURE.md 6.9.3).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).
Written from the published TrueType instruction set (Apple's TrueType
Reference Manual, Microsoft's TrueType instruction specification); no
other hinter's code or bytecode is used.

The hints are made from the outlines, which tools/ttgeom.py fits with
on-curve points at every corner and every horizontal or vertical
extremum, so edges can be read off the points:

  - an edge is a set of on-curve points on one level (y for horizontal
    edges, x for vertical ones) where the outline runs along that level;
    which side the ink is on follows from the direction of travel
    (TrueType's ink contours run clockwise);
  - a stem is an edge and an edge facing it across the ink (a bar, a
    hairline, a serif's thickness, a vertical stem, a bowl's side),
    matched narrowest first so that no edge is in two stems;
  - y: an edge with ink above at the baseline or the descender, or with
    ink below at the x-height, cap height or ascender, is pinned to that
    height (MIAP to its control value; round overshoots to the overshoot
    value); a stem with one edge placed gets the other at its control
    value (MIRP, minimum distance one pixel); a stem with neither (a
    crossbar, the middle of B, e, 3, 8) is interpolated between the
    pinned edges below and above it (IP), rounded (MDAP) and given its
    width the same way; other edges are interpolated and rounded;
  - x: the stems from left to right, the first one's left edge rounded
    (MDAP: relative to the glyph's origin), each next one's at its
    rounded distance from the stem before it (MDRP, a pixel at least
    when they do not overlap), each stem's other edge at its control
    value (MIRP); edges beside a placed one that overlaps them (serif
    tips, arm ends) at their rounded distance from it, a pixel at least
    unless they nearly touch;
  - everything else moves with IUP (interpolation) in y, then in x.

Control values: the zones (flat and overshoot heights) and the standard
stem widths. The control value program (prep) turns dropout control on
and, for each size up to SCREEN_MAX pixels per em, writes every control
value in whole pixels from a table: the zones' heights and the stems'
widths a face should have on a screen (Hinter's `screen`; for w16's
fonts the heights and stem widths the matching Microsoft face shows,
fonts/tt/screen.txt), all stems of the face from its main stem's width so
that stems of one kind come out alike. Above that size the scaled values
are used as they are, the overshoots kept flat until they reach a pixel.
Only TrueType 1.0 instructions are used (Windows 3.1's rasterizer);
advance widths are never touched (no phantom point moves).
"""
# opcodes
SVTCA_Y, SVTCA_X = 0x00, 0x01
SRP0, SRP1, SRP2 = 0x10, 0x11, 0x12
IUP_Y, IUP_X = 0x30, 0x31
IP = 0x39
ALIGNRP = 0x3C
MIAP_R = 0x3F
MDAP_R = 0x2F
MIRP = 0xE0          # + flags: 0x10 set rp0, 0x08 min distance, 0x04 round, 1 black
MIRP_STEM = MIRP | 0x10 | 0x08 | 0x04 | 0x01
MDRP_STEM = 0xC0 | 0x10 | 0x08 | 0x04 | 0x01
MDRP_LINK = MDRP_STEM                 # set rp0, minimum distance, round
MDRP_NEAR = 0xC0 | 0x10 | 0x04 | 0x01  # set rp0, round
SCANCTRL, SCANTYPE = 0x85, 0x8D
MPPEM, LT, GT, EQ, IF, EIF = 0x4B, 0x50, 0x52, 0x54, 0x58, 0x59
RCVT, WCVTP = 0x45, 0x44
ADD = 0x60
FDEF, ENDF, CALL = 0x2C, 0x2D, 0x2B
FN_LINK, FN_LINK_FREE, FN_FIT, FN_ADV = 0, 1, 2, 3      # the font program's functions
N_FUNCS = 4
MAX_STACK = 256                   # maxp maxStackElements: a LOOPCALL takes 3 a size
LOOPCALL, SHPIX, WS, RS, MUL = 0x2A, 0x38, 0x42, 0x43, 0x63
GC, ROUND, SUB, CINDEX, SWAP, POP, ELSE, SCFS = 0x46, 0x68, 0x61, 0x25, 0x23, 0x21, 0x1B, 0x48
MD1, ABS, DUP = 0x4A, 0x64, 0x20
RTG, RUTG, RDTG = 0x18, 0x7C, 0x7D
SCREEN_MAX = 48                   # prep's table covers 6..SCREEN_MAX ppem


def fpgm():
    """the font program: two functions linking a stem's second edge to its
    first (the reference point): 0 (b, cvt, a): SRP0 a, MIRP b to the
    control value; 1 (b, a): SRP0 a, MDRP b at its own rounded width; both
    with a minimum distance of one pixel"""
    # 2 (p, adv, gap): point p (a glyph's rightmost edge, rounded) no
    # further right than the advance width point adv, rounded, less the
    # glyph's right side bearing (the original distance, rounded) or gap
    # if that is more: x = min(x, round(adv) - max(round(rsb), gap))
    fit = bytes([FDEF]) + push([3]) + bytes([CINDEX]) + push([3]) + bytes([CINDEX, MD1, ABS, ROUND])
    fit += push([2]) + bytes([CINDEX]) + push([2]) + bytes([CINDEX, GT, IF, POP, DUP, EIF, SWAP, POP])
    fit += bytes([SWAP, GC, ROUND, SWAP, SUB]) + push([2]) + bytes([CINDEX, GC, ROUND])
    fit += push([2]) + bytes([CINDEX]) + push([2]) + bytes([CINDEX, LT, IF, POP, ELSE, SWAP, POP, EIF, SCFS, ENDF])
    return (push([FN_LINK]) + bytes([FDEF, SRP0, MIRP_STEM, ENDF])
            + push([FN_LINK_FREE]) + bytes([FDEF, SRP0, MDRP_STEM, ENDF])
            + push([FN_FIT]) + fit
            # 3 (e, ppem): at that size the point in storage 0 moved by
            # e - 2 pixels
            + push([FN_ADV]) + bytes([FDEF, MPPEM, EQ, IF]) + push([2]) + bytes([SUB]) + push([4096])
            + bytes([MUL]) + push([0]) + bytes([RS, SWAP, SHPIX, ELSE, POP, EIF, ENDF]))


def push(vals):
    """the shortest pushes for vals (bytes when all fit, else words), in
    order: the last value ends on top"""
    vals = list(vals)
    out = b''
    while vals:
        chunk, vals = vals[:255], vals[255:]
        if all(0 <= v < 256 for v in chunk):
            if len(chunk) <= 8:
                out += bytes([0xB0 + len(chunk) - 1] + chunk)
            else:
                out += bytes([0x40, len(chunk)] + chunk)
        else:
            out += bytes([0xB8 + len(chunk) - 1]) if len(chunk) <= 8 else bytes([0x41, len(chunk)])
            for v in chunk:
                out += (v & 0xFFFF).to_bytes(2, 'big')
    return out


# ------------------------------------------------------------- analysis


def edges(contours, axis):
    """clusters of edge points: [kind, level, [point indices], (lo, hi)]
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
            # along the level: a neighbour on it, or (a curve's extremum,
            # its control points fitted a unit or two off) one at a slope
            # under 1:8 with the other not across the level
            if abs(nxt[axis] - p[axis]) <= 1 and nxt[other] != p[other]:
                d = nxt[other] - p[other]
            elif abs(prv[axis] - p[axis]) <= 1 and prv[other] != p[other]:
                d = p[other] - prv[other]
            else:
                if 8 * abs(nxt[axis] - p[axis]) <= abs(nxt[other] - p[other]):
                    d = nxt[other] - p[other]
                elif 8 * abs(prv[axis] - p[axis]) <= abs(prv[other] - p[other]):
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
    and a +1 edge above it whose extents overlap, narrowest first, each
    edge in one pair at most"""
    cand = []
    lows = [c for c in clusters if c[0] == -1]
    highs = [c for c in clusters if c[0] == 1]
    for lo in lows:
        for hi in highs:
            w = hi[1] - lo[1]
            if 0 < w <= maxw and hi[3][0] < lo[3][1] and lo[3][0] < hi[3][1]:
                cand.append((w, id(lo), id(hi), lo, hi))
    cand.sort(key=lambda c: c[:3])
    used = set()
    out = []
    for w, il, ih, lo, hi in cand:
        if il in used or ih in used:
            continue
        used.add(il)
        used.add(ih)
        out.append((lo, hi))
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
    for bottom zones (baseline, descender), +1 for top zones.

    stems: None (found from the glyphs: the most frequent widths) or
    {'x': [widths], 'y': [widths], 'main': the main vertical stem}.
    screen: None or {ppem: ([zone flat heights in pixels], main stem
    pixels)} for the sizes prep sets from a table."""

    def __init__(self, upem, zones, maxw_y, maxw_x, do_x=True, stems=None, screen=None, gap=64, italic=False):
        self.upem = upem
        self.italic = italic
        self.gap = gap         # least pixels (26.6) right of a glyph's ink
        self.zones = zones
        self.maxw_y = maxw_y
        self.maxw_x = maxw_x
        self.do_x = do_x
        self.fixed_stems = stems
        self.screen = screen
        self.cvt = []
        self.zone_cvt = []
        for flat, over, kind in zones:
            self.zone_cvt.append((len(self.cvt), len(self.cvt) + 1))
            self.cvt += [int(round(flat)), int(round(over))]
        self.ystems = []
        self.xstems = []
        self.diag_cvt = {}

    def learn(self, glyph_contours):
        """pass 1: the stem widths (given, or the most frequent ones)"""
        if self.fixed_stems:
            self.ystems = sorted(set(int(round(w)) for w in self.fixed_stems['y']))
            self.xstems = sorted(set(int(round(w)) for w in self.fixed_stems['x']))
            self.main = self.fixed_stems['main']
        else:
            wy, wx = [], []
            for cs in glyph_contours:
                for lo, hi in stems(edges(cs, 1), self.maxw_y):
                    wy.append(hi[1] - lo[1])
                if self.do_x:
                    for lo, hi in stems(edges(cs, 0), self.maxw_x):
                        wx.append(hi[1] - lo[1])
            self.ystems = common_widths(wy) if wy else []
            self.xstems = common_widths(wx) if wx else []
            self.main = self.xstems[len(self.xstems) // 2] if self.xstems else self.upem / 12
        self.ystem_cvt = {w: len(self.cvt) + i for i, w in enumerate(self.ystems)}
        self.cvt += self.ystems
        self.xstem_cvt = {w: len(self.cvt) + i for i, w in enumerate(self.xstems)}
        self.cvt += self.xstems

    # ------------------------------------------------------------ prep

    def stem_pixels(self, w, ppem, main_px):
        """a stem's width on the screen: in proportion to the main stem's,
        at least one pixel"""
        return max(1, int(w / self.main * main_px + 0.5))

    def screen_values(self, ppem):
        """{cvt index: pixels} at a size of the table"""
        zpx, main_px = self.screen[ppem]
        out = {}
        for (flat, over, kind), (cf, co), px in zip(self.zones, self.zone_cvt, zpx):
            out[cf] = px
            # overshoot: a pixel beyond the flat height once the scaled
            # overshoot reaches 0.75 pixel (none at text sizes)
            ov = abs(over - flat) * ppem / self.upem
            out[co] = px + (kind if ov >= 0.75 else 0)
        for w, c in list(self.ystem_cvt.items()) + list(self.xstem_cvt.items()):
            out[c] = self.stem_pixels(w, ppem, main_px)
        for (w, cos), c in self.diag_cvt.items():
            out[c] = self.stem_pixels(w, ppem, main_px) * cos
        return out

    def prep(self):
        """dropout control on at every size; above the table's sizes the
        overshoots are flat below the size where they reach one pixel;
        at the table's sizes every control value from the table"""
        p = push([0x1FF]) + bytes([SCANCTRL]) + push([1]) + bytes([SCANTYPE])
        for (flat, over, kind), (cf, co) in zip(self.zones, self.zone_cvt):
            ov = abs(over - flat)
            if ov == 0:
                continue
            limit = min(255, -(-self.upem // ov))     # ppem where ov is 1 px
            p += bytes([MPPEM]) + push([limit]) + bytes([LT, IF])
            p += push([co, cf]) + bytes([RCVT, WCVTP, EIF])
        if self.screen:
            for ppem in sorted(self.screen):
                vals = self.screen_values(ppem)
                seq = []
                for c in sorted(vals):
                    seq += [c, int(round(vals[c] * 64))]
                p += bytes([MPPEM]) + push([ppem]) + bytes([EQ, IF]) + push(seq)
                p += bytes([WCVTP]) * len(vals) + bytes([EIF])
        return p

    # ------------------------------------------------------------ glyphs

    def _stem_cvt(self, table, w):
        best = None
        for v, idx in table.items():
            if abs(v - w) <= max(8, v * 0.25) and (best is None or abs(v - w) < abs(best[0] - w)):
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

    def glyph(self, contours, advance=None, device=None):
        """the glyph's instructions (advance: its advance width, to keep a
        pixel between its ink and the next glyph's; device: {ppem: the
        advance in pixels} the font's hdmx gives, which the advance width
        point is moved to at 9-40 pixels per em, so that the ink keeps to
        the width text is set with)"""
        if not contours:
            return b''
        code = bytes([SVTCA_Y]) + self._yaxis(contours) + bytes([IUP_Y])
        if self.do_x:
            x = self._xaxis(contours, advance)
            if x:
                code += bytes([SVTCA_X]) + self._phantom(contours, advance, device) + x + bytes([IUP_X])
        return code

    def _phantom(self, contours, advance, device):
        """the advance width point moved by whole pixels at the sizes
        where hdmx's width differs from the rounded scaled advance:
        the point's number in storage 0, LOOPCALL of the font program's
        function 3 (a size test and SHPIX) with (pixels + 2, size) pairs.
        Windows 3.1 did not move the point with DELTAP1 (shift 3 or 0)"""
        if advance is None or not device:
            return b''
        ph = sum(len(c) for c in contours) + 1
        seq = []
        for ppem in sorted(device):
            diff = device[ppem] - int(advance * ppem / self.upem + 0.5)
            if diff and abs(diff) <= 2 and ppem <= 255:
                seq += [diff + 2, ppem]
        if not seq:
            return b''
        code = push([0, ph]) + bytes([WS])
        for k in range(0, len(seq), 200):
            part = seq[k:k + 200]
            code += push(part + [len(part) // 2, FN_ADV]) + bytes([LOOPCALL])
        return code

    def _emit_rest(self, cl, touched):
        code = b''
        for q in cl[2][1:]:
            if q not in touched:
                code += push([q]) + bytes([ALIGNRP])
                touched.add(q)
        return code

    def _link(self, a, b, table, touched):
        """b at its stem width from a (a placed)"""
        w = abs(b[1] - a[1])
        c = self._stem_cvt(table, w)
        if c is not None:
            code = push([b[2][0], c, a[2][0], FN_LINK]) + bytes([CALL])
        else:
            code = push([b[2][0], a[2][0], FN_LINK_FREE]) + bytes([CALL])
        touched.add(b[2][0])
        return code + self._emit_rest(b, touched)

    def _yaxis(self, contours):
        cls = edges(contours, 1)
        pairs = stems(cls, self.maxw_y)
        partner = {}
        for lo, hi in pairs:
            partner[id(lo)] = hi
            partner[id(hi)] = lo
        touched = set()
        placed = []           # clusters placed, as (level, cluster)
        code = b''
        # blue zones
        for cl in cls:
            z = self._zone(cl[0], cl[1])
            if z is None:
                continue
            for q in cl[2]:
                if q not in touched:
                    code += push([q, z]) + bytes([MIAP_R])
                    touched.add(q)
            placed.append(cl)
        # stems with one edge pinned
        pid = set(id(c) for c in placed)
        for cl in list(placed):
            o = partner.get(id(cl))
            if o is not None and id(o) not in pid:
                code += self._link(cl, o, self.ystem_cvt, touched)
                placed.append(o)
                pid.add(id(o))
        # the rest from the bottom up: interpolated between the nearest
        # placed edges below and above (by original position), rounded;
        # a stem's other edge at its width
        rest = sorted([c for c in cls if id(c) not in pid], key=lambda c: c[1])
        small = (min(self.ystems) if self.ystems else self.upem / 30) * 0.5
        for cl in rest:
            if id(cl) in pid:
                continue
            # beside a placed edge that overlaps it (a serif's tip beside
            # the serif, a terminal beside its bar): at its rounded
            # distance from it, a pixel at least unless they nearly touch
            near = [(abs(c[1] - cl[1]), c) for c in placed
                    if c[3][0] < cl[3][1] and cl[3][0] < c[3][1] and abs(c[1] - cl[1]) <= self.maxw_y * 0.7]
            if near:
                d, ref = min(near, key=lambda t: t[0])
                code += push([ref[2][0]]) + bytes([SRP0])
                code += push([cl[2][0]]) + bytes([MDRP_LINK if d >= small else MDRP_NEAR])
                touched.add(cl[2][0])
                code += self._emit_rest(cl, touched)
                placed.append(cl)
                pid.add(id(cl))
                continue
            below = [c for c in placed if c[1] < cl[1]]
            above = [c for c in placed if c[1] > cl[1]]
            if below and above:
                b = max(below, key=lambda c: c[1])
                a = min(above, key=lambda c: c[1])
                code += push([cl[2][0], b[2][0], a[2][0]]) + bytes([SRP2, SRP1, IP])
            code += push([cl[2][0]]) + bytes([MDAP_R])
            touched.add(cl[2][0])
            code += self._emit_rest(cl, touched)
            placed.append(cl)
            pid.add(id(cl))
            o = partner.get(id(cl))
            if o is not None and id(o) not in pid:
                code += self._link(cl, o, self.ystem_cvt, touched)
                placed.append(o)
                pid.add(id(o))
        return code

    def _diag_cvt(self, w, cos):
        """the control value for a diagonal stroke of perpendicular width
        w at cos (of its angle from the vertical): its width across x is
        to be the stem's it stands for (the main stem, or a hairline),
        so the perpendicular one is that times cos"""
        thin = bool(self.ystems) and w < self.main * 0.7
        base = min(self.ystems, key=lambda v: abs(v - w)) if thin else self.main
        key = (base, round(cos * 50) / 50.0)
        if key not in self.diag_cvt:
            self.diag_cvt[key] = len(self.cvt)
            self.cvt.append(int(round(key[0] * key[1])))
        return self.diag_cvt[key]

    def _diagonals(self, contours, touched):
        """diagonal strokes (v, w, x, y, A, K, V ...): the two straight
        edges of a stroke, parallel, ink between them; the second edge's
        ends moved across x (SFVTCA x) to a control value's distance from
        the first edge's first point, measured square to it (SPVTL): a
        width across x of the stem the stroke stands for, one pixel at
        text sizes, so that each row has one pixel of it (as thick as a
        stem, square to its slope, a stroke came out a pixel here and two
        there)"""
        segs = []
        base = 0
        for c in contours:
            n = len(c)
            on = [i for i in range(n) if c[i][2]]
            for k, i in enumerate(on):
                j = on[(k + 1) % len(on)]
                if (j - i) % n != 1:
                    continue           # a curve between them
                (x0, y0), (x1, y1) = c[i][:2], c[j][:2]
                dx, dy = x1 - x0, y1 - y0
                if not dy or not (abs(dy) * 0.17 < abs(dx) < abs(dy) * 1.8):
                    continue
                ln = (dx * dx + dy * dy) ** 0.5
                if ln < self.upem / 10:
                    continue
                segs.append((base + i, base + j, x0, y0, x1, y1, dx / ln, dy / ln, ln))
            base += n
        code = b''
        used = set()
        for a in segs:
            if id(a) in used:
                continue
            best = None
            for b in segs:
                if b is a or id(b) in used:
                    continue
                # parallel, opposite directions (the two sides of a stroke)
                if a[6] * b[6] + a[7] * b[7] > -0.998:
                    continue
                # perpendicular distance of b's start from a's line, on
                # a's ink side (the right of its direction: outer
                # contours run clockwise), not across a counter
                d = (b[2] - a[2]) * a[7] - (b[3] - a[3]) * a[6]
                if not 0 < d <= self.maxw_x:
                    continue
                # overlapping along the line
                t0 = (b[2] - a[2]) * a[6] + (b[3] - a[3]) * a[7]
                t1 = (b[4] - a[2]) * a[6] + (b[5] - a[3]) * a[7]
                lo, hi = max(0, min(t0, t1)), min(a[8], max(t0, t1))
                if hi - lo < 0.5 * min(a[8], b[8]):
                    continue
                if best is None or abs(d) < best[0]:
                    best = (abs(d), b)
            if best is None:
                continue
            w, b = best
            pa, pb = a[0], (b[0], b[1])
            if pa in touched or pb[0] in touched or pb[1] in touched:
                continue
            used |= {id(a), id(b)}
            cos = abs(a[7])
            c = self._diag_cvt(w, cos)
            code += push([pa]) + bytes([0x2E])                         # MDAP[0]: touch
            code += push([a[0], a[1]]) + bytes([0x07, 0x05])           # SPVTL[1], SFVTCA[x]
            code += push([pa]) + bytes([SRP0]) + push([pb[1], c, pb[0], c]) + bytes([0xE0, 0xE0])
            code += bytes([SVTCA_X])
            touched |= {pa, pb[0], pb[1]}
        return code

    def _xaxis(self, contours, advance=None):
        cls = edges(contours, 0)
        if not cls or self.italic:
            # (an italic face: its stems are slanted, hinted as diagonal
            # strokes; rounding its few vertical edges, serif tips and
            # curve extrema, spaced the letters unevenly)
            return self._diagonals(contours, set())
        pairs = sorted(stems(cls, self.maxw_x), key=lambda pr: pr[0][1])
        partner = {}
        for lo, hi in pairs:
            partner[id(lo)] = hi
            partner[id(hi)] = lo
        touched = set()
        placed = []
        pid = set()
        code = b''
        # the outermost edges rounded where they are, so that the ink
        # keeps its pixel box (stems rounded up and serifs made a pixel
        # wide one after another ran letters into each other); the stems
        # they belong to get their width inwards
        el = min(cls, key=lambda c: c[1])
        er = max(cls, key=lambda c: c[1])
        ends = [el] if er is el or partner.get(id(el)) is er else [el, er]
        npts = sum(len(c) for c in contours)
        for e in ends:
            if self.gap == 0 and len(ends) == 2:
                # a fixed-pitch face: the ink out to whole pixels (round
                # down on the left, up on the right, then no further than
                # the advance), as Courier New fills its cell at 8-10 pt
                code += bytes([RDTG if e is el else RUTG]) + push([e[2][0]]) + bytes([MDAP_R, RTG])
            else:
                code += push([e[2][0]]) + bytes([MDAP_R])
            if e is er and e is not el and e[0] == 1 and advance is not None and advance - e[1] > 0:
                code += push([e[2][0], npts + 1, self.gap, FN_FIT]) + bytes([CALL])
            touched.add(e[2][0])
            code += self._emit_rest(e, touched)
            placed.append(e)
            pid.add(id(e))
        for e in ends:
            o = partner.get(id(e))
            if o is not None and id(o) not in pid:
                code += self._link(e, o, self.xstem_cvt, touched)
                placed.append(o)
                pid.add(id(o))
        # the stems between, interpolated between the ends and rounded
        for lo, hi in pairs:
            if id(lo) in pid or id(hi) in pid:
                continue
            # over a placed stem of about its width (an i's dot over its
            # stem): with it, so they stay one above the other
            w = hi[1] - lo[1]
            twin = [c for c in placed if c[0] == -1 and abs(c[1] - lo[1]) < w * 0.5
                    and partner.get(id(c)) is not None and abs((partner[id(c)][1] - c[1]) - w) < w * 0.3]
            if twin:
                code += push([twin[0][2][0]]) + bytes([SRP0]) + push([lo[2][0]]) + bytes([MDRP_NEAR])
            else:
                if len(ends) == 2:
                    code += push([lo[2][0], el[2][0], er[2][0]]) + bytes([SRP2, SRP1, IP])
                code += push([lo[2][0]]) + bytes([MDAP_R])
            touched.add(lo[2][0])
            code += self._emit_rest(lo, touched)
            code += self._link(lo, hi, self.xstem_cvt, touched)
            placed += [lo, hi]
            pid |= {id(lo), id(hi)}
        code += self._diagonals(contours, touched)
        # serif tips, arm ends: beside a placed edge that overlaps them,
        # nearest first, at their rounded distance (no minimum: a serif
        # under half a pixel goes, as in the hinted faces of the time)
        small = self.upem
        left = [cl for cl in cls if id(cl) not in pid]
        while left and placed:
            best = None
            for cl in left:
                for ref in placed:
                    if not (ref[3][0] < cl[3][1] and cl[3][0] < ref[3][1]):
                        continue
                    d = abs(cl[1] - ref[1])
                    if best is None or d < best[0]:
                        best = (d, cl, ref)
            if best is None:
                break
            d, cl, ref = best
            left.remove(cl)
            if d > self.upem / 4:
                continue
            code += push([ref[2][0]]) + bytes([SRP0])
            code += push([cl[2][0]]) + bytes([MDRP_LINK if d >= small else MDRP_NEAR])
            touched.add(cl[2][0])
            code += self._emit_rest(cl, touched)
            placed.append(cl)
            pid.add(id(cl))
        return code
