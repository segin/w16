#!/usr/bin/env python3
"""fontgen - draws w16's second raster font set (ARCHITECTURE.md 6.9.2).

Writes the glyph sources fonts/<name>.txt (tools/fontc.py's format) for
Courier, MS Serif, Small Fonts, Symbol, the DOS-box Terminal sizes
(DOSAPP.FON) and the grabber's fonts (EGA80WOA/EGA40WOA/CGA80WOA/
CGA40WOA.FON) from:

  - fonts/skeleton.txt: w16's own glyph designs, drawn as strokes (lines,
    elliptical arcs, dots, serifs) on a unit box, one design per Unicode
    character; this file is the design, written for w16 from scratch;
  - fonts/tiny.txt: hand-drawn pixel glyphs for the sizes too small for
    strokes (cells under 7 pixels high);
  - tests/fonts/fn3ref.txt: what Windows 3.1 reports through the API for
    each font (probe FN3: TEXTMETRIC, GetCharWidth) - the metrics and
    every advance width, which the sources must match exactly.

    fontgen.py [--ref FN3LOG] [--out DIR] [NAME ...]   (default: all)
    fontgen.py --png OUT.png NAME [--scale N]          a specimen (debug)

Each glyph is rasterised into the cell GetCharWidth gives it: the stroke
skeleton's unit box is mapped onto the ink area (the advance width less
side bearings), its vertical anchors (baseline, x-height, cap height,
ascender, descender) onto rows chosen per size (the SIZES table), so that
every size is hinted to whole pixels. Box-drawing and block characters
of code page 437 are made by rule so that they join across cells.

The sources this writes are committed (fonts/*.txt) and built into .FON
files by fontc (make fonts); fontgen is only rerun when a design
changes. No Microsoft font data is used: only the metrics in fn3ref.txt.
"""
import argparse
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------- glyph sets


def cp_table(name):
    """code -> Unicode character for codes 0-255 (None: no character)"""
    t = {}
    if name == 'ansi':
        for c in range(32, 256):
            try:
                t[c] = bytes([c]).decode('cp1252')
            except UnicodeDecodeError:
                t[c] = None
        t[0x7F] = None
        t[0x80] = None              # (later the euro; not in 3.1)
        t[0x8E] = None
        t[0x9E] = None
    elif name == 'oem':
        for c in range(256):
            t[c] = bytes([c]).decode('cp437')
        # the control range shows code page 437's pictures
        pics = ('\0☺☻♥♦♣♠•◘○◙'
                '♂♀♪♫☼►◄↕‼¶§'
                '▬↨↑↓→←∟↔▲▼')
        for c in range(32):
            t[c] = pics[c]
        t[0] = ' '
        t[0x7F] = '⌂'
        t[0xFF] = ' '
    elif name == 'symbol':
        lo = (' !∀#∃%&∋()∗+,−./0123456789:;<=>?'
              '≅ΑΒΧΔΕΦΓΗΙϑ'
              'ΚΛΜΝΟΠΘΡΣΤΥ'
              'ςΩΞΨΖ[∴]⊥_'
              'αβχδεφγηιϕ'
              'κλμνοπθρστυ'
              'ϖωξψζ{|}∼')
        for i, ch in enumerate(lo):
            t[0x20 + i] = ch
        hi = ('€ϒ′≤⁄∞ƒ♣♦♥♠'
              '↔←↑→↓°±″≥×∝'
              '∂•÷≠≡≈…↵'
              'ℵℑℜ℘⊗⊕∅∩∪⊃⊇'
              '⊄⊂⊆∈∉∠∇∏'
              '√⋅¬∧∨⇔⇐⇑⇒⇓'
              '◊〈®©™∑'
              '〉∫⌠⌡'
              '')
        for i, ch in enumerate(hi):
            t[0xA0 + i] = ch
        t[0xA0] = None
        t[0xF0] = None
        for c in range(0x7F, 0xA0):
            t[c] = None
    return t


# ---------------------------------------------------------------- designs


class Design:
    """fonts/skeleton.txt: 'U+XXXX frame commands' per line"""

    def __init__(self, path):
        self.g = {}
        for ln, line in enumerate(open(path, encoding='utf-8'), 1):
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            m = re.match(r'U\+([0-9A-Fa-f]{4,5})(?:/(\w+))?\s+([cxfn])\s*(.*)$', line)
            if not m:
                raise SystemExit('%s:%d: bad line' % (path, ln))
            key = chr(int(m.group(1), 16))
            if m.group(2):
                key += '/' + m.group(2)       # a style variant (e.g. /serif)
            if key in self.g:
                raise SystemExit('%s:%d: U+%s twice' % (path, ln, m.group(1)))
            cmds = []
            for part in m.group(4).split(';'):
                part = part.strip()
                if part:
                    w = part.split()
                    cmds.append((w[0], w[1:]))
            self.g[key] = (m.group(3), cmds)

    def get(self, ch, style):
        for k in (ch + '/' + style, ch):
            if k in self.g:
                return self.g[k]
        return None


class Tiny:
    """fonts/tiny.txt: 'char U+XXXX' followed by rows (any count, '#'/'.');
    the glyph's bottom row is the baseline row unless the header line says
    'desc N' (rows below the baseline)"""

    def __init__(self, path):
        self.g = {}
        lines = open(path, encoding='utf-8').read().split('\n')
        i = 0
        while i < len(lines):
            line = lines[i].split(';', 1)[0].strip()
            i += 1
            m = re.match(r'(?:char|U\+)\s*U?\+?([0-9A-Fa-f]{4})\s*(?:desc\s+(\d+))?', line)
            if not line or not m:
                continue
            rows = []
            while i < len(lines) and re.fullmatch(r'[#.]+', lines[i].strip()):
                rows.append(lines[i].strip())
                i += 1
            self.g[chr(int(m.group(1), 16))] = (rows, int(m.group(2) or 0))


# ---------------------------------------------------------------- raster


class Bitmap:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0] * w for _ in range(h)]
        self.ink = 1
        self.clip = (0, 0, w - 1, h - 1)

    def set(self, x, y):
        x0, y0, x1, y1 = self.clip
        if x0 <= x <= x1 and y0 <= y <= y1:
            self.px[y][x] = self.ink

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y][x]
        return 0

    def rows(self):
        return [''.join('#' if v else '.' for v in r) for r in self.px]


def rsym(v, c):
    """round v to a pixel, halves toward the centre c (symmetric shapes)"""
    if v >= c:
        return int(math.floor(v + 0.5 - 1e-9))
    return int(math.ceil(v - 0.5 + 1e-9))


class Geom:
    """maps a design's unit box onto a glyph cell.

    m: the size's metrics: asc (baseline row = asc - 1), cap, xh, ah
    (ascender height), desc (descender depth used), pen (stem width),
    hpen (horizontal stroke height), serif (length), dot.
    box: ink columns (L, R)."""

    def __init__(self, m, L, R, frame):
        self.m = m
        self.L, self.R = L, R - (m['pen'] - 1)
        if self.R < self.L:
            self.R = self.L
        self.frame = frame
        base = m['asc'] - 1
        self.base = base - (m['hpen'] - 1)
        self.rows = {}
        self.rows['B'] = self.base
        self.rows['C'] = base - m['cap'] + 1
        self.rows['X'] = base - m['xh'] + 1
        self.rows['A'] = base - m['ah'] + 1
        self.rows['D'] = self.base + m['desc']
        if frame == 'f':                # the whole cell
            self.rows['B'] = m['h'] - 1 - (m['hpen'] - 1)
            self.rows['C'] = 0
        self.cx = (self.L + self.R) / 2.0

    def col(self, x):
        x = float(x)
        return rsym(self.L + x * (self.R - self.L), self.cx)

    def colf(self, x):
        return self.L + float(x) * (self.R - self.L)

    def rowf(self, y):
        """a y token: B C X A D, a number (fraction of the glyph's frame),
        or a frame-prefixed number: c.4 x.5 a.9 d.5 (descender fraction)"""
        if y in self.rows:
            return float(self.rows[y])
        for k, sgn in (('^', -1), ('_', 1)):       # X^2: 2 pixels above X
            if k in y:
                a, n = y.split(k)
                return self.rowf(a) + sgn * int(n)
        f = self.frame
        if y[0] in 'cxad' and len(y) > 1:
            f, y = y[0], y[1:]
        v = float(y)
        top = {'c': 'C', 'x': 'X', 'a': 'A', 'f': 'C', 'n': 'C'}.get(f, 'C')
        if f == 'd' or v < 0:
            return self.rows['B'] + abs(v) * (self.rows['D'] - self.rows['B'])
        return self.rows['B'] - v * (self.rows['B'] - self.rows[top])

    def row(self, y):
        return int(math.floor(self.rowf(y) + 0.5 - 1e-9))


def stamp(bm, x, y, m):
    for dy in range(m['hpen']):
        for dx in range(m['pen']):
            bm.set(x + dx, y + dy)


def line(bm, x0, y0, x1, y1, m):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        stamp(bm, x0, y0, m)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def thin(pts):
    """drop the corner pixel of every L-shaped step of an ordered path,
    except on the path's outer rows and columns (its extremes stay)"""
    out = []
    for p in pts:
        if not out or out[-1] != p:
            out.append(p)
    if not out:
        return out
    x0 = min(p[0] for p in out)
    x1 = max(p[0] for p in out)
    y0 = min(p[1] for p in out)
    y1 = max(p[1] for p in out)
    res = [out[0]]
    for i in range(1, len(out)):
        p = out[i]
        if i + 1 < len(out) and p[0] not in (x0, x1) and p[1] not in (y0, y1):
            a, b = res[-1], out[i + 1]
            if abs(a[0] - b[0]) <= 1 and abs(a[1] - b[1]) <= 1 and a != b:
                continue
        res.append(p)
    return res


def arc(bm, x0, y0, x1, y1, a0, a1, m, g):
    """an elliptical arc in the pixel box (x0..x1, y0..y1), angles in
    degrees counter-clockwise from 3 o'clock"""
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    rx, ry = (x1 - x0) / 2.0, (y1 - y0) / 2.0
    n = max(16, int((abs(a1 - a0) / 360.0) * 8 * (rx + ry + 2)))
    pts = []
    for i in range(n + 1):
        t = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((rsym(cx + rx * math.cos(t), cx), rsym(cy - ry * math.sin(t), cy)))
    for x, y in thin(pts):
        stamp(bm, x, y, m)


def fill_ellipse(bm, x0, y0, x1, y1):
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    rx, ry = (x1 - x0) / 2.0 + 0.5, (y1 - y0) / 2.0 + 0.5
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0 + 1e-9:
                bm.set(x, y)


def fill_poly(bm, pts):
    """the pixels whose centres lie inside the polygon (even-odd)"""
    ys = [p[1] for p in pts]
    for y in range(min(ys), max(ys) + 1):
        xs = []
        for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
            if (y0 <= y < y1) or (y1 <= y < y0):
                xs.append(x0 + (y - y0) * (x1 - x0) / float(y1 - y0))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            for x in range(int(math.ceil(xs[i] - 1e-9)), int(math.floor(xs[i + 1] + 1e-9)) + 1):
                bm.set(x, y)


class Renderer:
    def __init__(self, design, style, tiny=None):
        self.d = design
        self.style = style          # 'sans', 'serif', 'slab'
        self.tiny = tiny            # pixel glyphs for small sub-glyphs

    def draw(self, bm, ch, m, L, R, depth=0):
        spec = self.d.get(ch, self.style)
        if spec is None:
            return False
        frame, cmds = spec
        g = Geom(m, L, R, frame)
        if frame == 'f':
            g.L, g.R = L, R
            g.cx = (L + R) / 2.0
        ink = bm.ink
        for op, a in cmds:
            if op not in 'ST':
                self.cmd(bm, g, m, op, a, depth)
        # serifs last, and only where they keep clear of other strokes;
        # none on small letters below an x-height of 7 in the serif style
        if not (self.style == 'serif' and frame == 'x' and m['xh'] < 7):
            for op, a in cmds:
                if op in 'ST':
                    self.cmd(bm, g, m, op, a, depth)
        bm.ink = ink
        return True

    def cmd(self, bm, g, m, op, a, depth):
        if op == 'L':                       # polyline
            pts = [(g.col(a[i]), g.row(a[i + 1])) for i in range(0, len(a), 2)]
            for p, q in zip(pts, pts[1:]):
                line(bm, p[0], p[1], q[0], q[1], m)
            if len(pts) == 1:
                stamp(bm, pts[0][0], pts[0][1], m)
        elif op == 'E':                     # arc in a box
            x0, y0, x1, y1 = g.col(a[0]), g.row(a[1]), g.col(a[2]), g.row(a[3])
            a0, a1 = (float(a[4]), float(a[5])) if len(a) > 4 else (0.0, 360.0)
            arc(bm, min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), a0, a1, m, g)
        elif op == 'D':                     # a dot, its bottom row at y
            x, y = g.col(a[0]), g.row(a[1]) + m['hpen'] - 1
            d = m['dot']
            for dy in range(d):
                for dx in range(max(d, m['pen'])):
                    bm.set(x + dx, y - dy)
        elif op == 'F':                     # filled rectangle
            x0, y0, x1, y1 = g.col(a[0]), g.row(a[1]), g.col(a[2]), g.row(a[3])
            for y in range(min(y0, y1), max(y0, y1) + 1 + (m['hpen'] - 1)):
                for x in range(min(x0, x1), max(x0, x1) + 1 + (m['pen'] - 1)):
                    bm.set(x, y)
        elif op == 'G':                     # filled ellipse
            x0, y0, x1, y1 = g.col(a[0]), g.row(a[1]), g.col(a[2]), g.row(a[3])
            fill_ellipse(bm, min(x0, x1), min(y0, y1), max(x0, x1) + m['pen'] - 1,
                         max(y0, y1) + m['hpen'] - 1)
        elif op == 'P':                     # filled polygon
            pts = [(g.col(a[i]), g.row(a[i + 1])) for i in range(0, len(a), 2)]
            fill_poly(bm, pts)
            for p, q in zip(pts, pts[1:] + pts[:1]):
                line(bm, p[0], p[1], q[0], q[1], dict(m, pen=1, hpen=1))
        elif op == 'K':                     # erase from here on
            bm.ink = 0
        elif op == 'S':                     # serif: S x y [l|r|lr]
            if self.style == 'sans':
                return
            sides = a[2] if len(a) > 2 else 'lr'
            x, y = g.col(a[0]), g.row(a[1])
            s = m['serif']
            if self.style == 'slab':
                s = m.get('slab', s)
            for side, step, start in (('l', -1, x - 1), ('r', 1, x + m['pen'])):
                if side not in sides:
                    continue
                pts = [start + step * k for k in range(s)]
                if any(bm.get(px, y) or bm.get(px + step, y) for px in pts):
                    continue
                for px in pts:
                    for dy in range(m['hpen']):
                        bm.set(px, y + dy)
        elif op == 'T':                     # vertical serif: T x y [u|d|ud]
            if self.style == 'sans':
                return
            sides = a[2] if len(a) > 2 else 'd'
            x, y = g.col(a[0]), g.row(a[1])
            s = max(1, m['serif'])
            for side, step, start in (('u', -1, y - 1), ('d', 1, y + m['hpen'])):
                if side not in sides:
                    continue
                pts = [start + step * k for k in range(s)]
                if any(bm.get(x, py) or bm.get(x, py + step) for py in pts):
                    continue
                for py in pts:
                    for dx in range(m['pen']):
                        bm.set(x + dx, py)
        elif op == 'g':                     # another glyph in a sub-box:
            # g U+XXXX x0 y0 x1 y1  (x in the ink box, y tokens)
            ch = chr(int(a[0][2:], 16))
            x0, x1 = g.col(a[1]), g.col(a[3])
            top, bot = g.row(a[4]), g.row(a[2])
            hh = bot - top + 1
            if self.tiny and hh <= 6 and ch in self.tiny.g and not self.tiny.g[ch][1]:
                rows = resample(self.tiny.g[ch][0], min(x1 - x0 + 1, len(self.tiny.g[ch][0][0])), hh)
                ox = x0 + (x1 - x0 + 1 - len(rows[0])) // 2
                for j, r in enumerate(rows):
                    for i, c in enumerate(r):
                        if c == '#':
                            bm.set(ox + i, top + j)
                return
            sub = dict(m)
            sub['asc'] = bot + 1
            sub['cap'] = hh
            sub['ah'] = hh
            sub['xh'] = max(1, int(round(hh * m['xh'] / float(m['cap']))))
            sub['desc'] = max(0, int(round(hh * m['desc'] / float(m['cap']))))
            sub['pen'] = 1 if len(a) < 6 else int(a[5])
            sub['hpen'] = 1
            sub['dot'] = 1
            sub['serif'] = 0 if hh < 12 else 1
            self.draw(bm, ch, sub, x0, x1 + sub['pen'] - 1, depth=depth + 1)
        elif op == 'R':                     # reuse another glyph whole: R U+XXXX
            self.draw(bm, chr(int(a[0][2:], 16)), m, g.L, g.R + m['pen'] - 1, depth=depth + 1)
        else:
            raise SystemExit('unknown command %r' % op)


# ---------------------------------------------------------------- sizes
# Per font: the source name, its .FON file and resource ID (FN3's MODULE
# section: FindResource), FN3's label, the style, the code page and the
# vertical design per size (rows): cap height, x-height, ascender height,
# descender depth drawn, stem width (pen), horizontal stroke (hpen),
# serif length, dot size, accent height. Everything else comes from the
# log. 'res' is SizeofResource on 3.1 (a check: the source's .FNT must
# round to it at the file's alignment).
def S(cap, xh, ah, desc, pen=1, hpen=1, serif=1, dot=1, acc=2, **kw):
    d = dict(cap=cap, xh=xh, ah=ah, desc=desc, pen=pen, hpen=hpen, serif=serif,
             dot=dot, acc=acc)
    d.update(kw)
    return d


FONTS = [
    # Courier: w16's slab-serif monospace
    dict(name='coure10', fon='COURE', rid=90, label='Courier 13x8', style='slab',
         cp='ansi', res=3952, **S(8, 6, 9, 2)),
    dict(name='coure12', fon='COURE', rid=91, label='Courier 16x9', style='slab',
         cp='ansi', res=8224, **S(10, 7, 11, 3)),
    dict(name='coure15', fon='COURE', rid=92, label='Courier 20x12', style='slab',
         cp='ansi', res=10016, **S(12, 9, 13, 4, acc=3, slab=2)),
    # MS Serif: w16's proportional serif
    dict(name='serife8', fon='SERIFE', rid=78, label='MS Serif 13x5', style='serif',
         cp='ansi', res=4032, **S(9, 6, 9, 2)),
    dict(name='serife10', fon='SERIFE', rid=79, label='MS Serif 16x6', style='serif',
         cp='ansi', res=5440, **S(10, 7, 11, 3)),
    dict(name='serife12', fon='SERIFE', rid=80, label='MS Serif 19x8', style='serif',
         cp='ansi', res=6656, **S(12, 8, 13, 4, acc=3)),
    dict(name='serife14', fon='SERIFE', rid=81, label='MS Serif 21x9', style='serif',
         cp='ansi', res=8704, **S(13, 9, 14, 4, acc=3)),
    dict(name='serife18', fon='SERIFE', rid=82, label='MS Serif 27x12', style='serif',
         cp='ansi', res=12160, **S(17, 12, 18, 5, pen=2, serif=2, dot=2, acc=3)),
    dict(name='serife24', fon='SERIFE', rid=83, label='MS Serif 35x16', style='serif',
         cp='ansi', res=19312, **S(23, 15, 24, 7, pen=3, serif=3, dot=3, acc=4)),
    # Small Fonts (and the two small MS Serif sizes SMALLE.FON carries)
    dict(name='smalle2', fon='SMALLE', rid=1, label='Small Fonts 3x1', style='sans',
         cp='ansi', res=1536, tiny=True, **S(2, 1, 2, 1, acc=0)),
    dict(name='smalle3', fon='SMALLE', rid=2, label='Small Fonts 5x3', style='sans',
         cp='ansi', res=2560, tiny=True, **S(4, 3, 4, 1, acc=1)),
    dict(name='smalle4', fon='SMALLE', rid=3, label='Small Fonts 6x3', style='sans',
         cp='ansi', res=2560, tiny=True, **S(4, 3, 5, 1, acc=1)),
    dict(name='smalle5', fon='SMALLE', rid=4, label='Small Fonts 8x4', style='sans',
         cp='ansi', res=3072, **S(5, 4, 6, 1, acc=1)),
    dict(name='smalle6', fon='SMALLE', rid=5, label='Small Fonts 10x4', style='sans',
         cp='ansi', res=3584, **S(6, 4, 6, 2)),
    dict(name='smalle7', fon='SMALLE', rid=6, label='Small Fonts 11x5', style='sans',
         cp='ansi', res=3584, **S(7, 5, 7, 2)),
    dict(name='serife6', fon='SMALLE', rid=7, label='MS Serif 10x4', style='serif',
         cp='ansi', res=3584, **S(6, 4, 6, 2, acc=1)),
    dict(name='serife7', fon='SMALLE', rid=8, label='MS Serif 11x5', style='serif',
         cp='ansi', res=3584, **S(7, 5, 7, 2)),
    # Symbol: the symbol character set, in the serif style
    dict(name='symbole8', fon='SYMBOLE', rid=1, label='Symbol 13x5', style='serif',
         cp='symbol', res=3712, **S(9, 6, 9, 2)),
    dict(name='symbole10', fon='SYMBOLE', rid=2, label='Symbol 16x6', style='serif',
         cp='symbol', res=5168, **S(10, 7, 11, 3)),
    dict(name='symbole12', fon='SYMBOLE', rid=3, label='Symbol 19x8', style='serif',
         cp='symbol', res=6752, **S(12, 8, 13, 4, acc=3)),
    dict(name='symbole14', fon='SYMBOLE', rid=4, label='Symbol 21x9', style='serif',
         cp='symbol', res=8144, **S(13, 9, 14, 4, acc=3)),
    dict(name='symbole18', fon='SYMBOLE', rid=5, label='Symbol 27x12', style='serif',
         cp='symbol', res=12256, **S(17, 12, 18, 5, pen=2, serif=2, dot=2, acc=3)),
    dict(name='symbole24', fon='SYMBOLE', rid=6, label='Symbol 35x15', style='serif',
         cp='symbol', res=18672, **S(23, 15, 24, 7, pen=3, serif=3, dot=3, acc=4)),
    # DOSAPP.FON: the DOS-box Terminal sizes (code page 437, 0-255)
    dict(name='dosapp4x6', fon='DOSAPP', rid=1, label='Terminal 6x4', style='sans',
         cp='oem', res=3072, tiny=True, **S(5, 4, 5, 1, acc=1)),
    dict(name='dosapp5x12', fon='DOSAPP', rid=2, label='Terminal 12x5', style='sans',
         cp='oem', res=4240, **S(7, 5, 8, 2)),
    dict(name='dosapp6x8', fon='DOSAPP', rid=3, label='Terminal 8x6', style='sans',
         cp='oem', res=3216, **S(6, 4, 6, 1, pen=2, acc=1)),
    dict(name='dosapp7x12', fon='DOSAPP', rid=4, label='Terminal 12x7', style='sans',
         cp='oem', res=4240, **S(7, 5, 8, 2)),
    dict(name='dosapp10x18', fon='DOSAPP', rid=5, label='Terminal 18x10', style='sans',
         cp='oem', res=10400, **S(11, 8, 12, 2, pen=2, dot=2, acc=3)),
    dict(name='dosapp12x16', fon='DOSAPP', rid=6, label='Terminal 16x12', style='sans',
         cp='oem', res=9728, **S(10, 7, 11, 2, pen=2, hpen=2, dot=2)),
    # the grabber's fonts (code page 437, 1-254): CGA80WOA drawn here,
    # CGA40WOA that doubled in width; EGA80WOA is the Terminal design of
    # VGAOEM.FON (fonts/vgaoem.txt, the same 8x12 cell), EGA40WOA that
    # doubled. Their own metrics are FN3's FILE/NEW lines: what GDI
    # realises for their cell can be another file's font.
    dict(name='cga80woa', fon='CGA80WOA', rid=101, label='Terminal 8x8', style='sans',
         cp='oem', res=3200, **S(6, 4, 6, 1, acc=1)),
    dict(name='cga40woa', fon='CGA40WOA', rid=100, label='Terminal 8x16', style='sans',
         cp='oem', res=5232, double='cga80woa', **S(6, 4, 6, 1)),
    dict(name='ega80woa', fon='EGA80WOA', rid=131, label='Terminal 12x8', style='sans',
         cp='oem', res=4208, copy='vgaoem', **S(8, 6, 9, 2)),
    dict(name='ega40woa', fon='EGA40WOA', rid=130, label='Terminal 12x16', style='sans',
         cp='oem', res=7264, double='ega80woa', **S(8, 6, 9, 2)),
]

# ---------------------------------------------------------------- glyphs

ACCENTS = {'̀': 'grave', '́': 'acute', '̂': 'circ', '̃': 'tilde',
           '̈': 'diaer', '̊': 'ring', '̧': 'cedil', '̌': 'caron'}
TINY_ACC = {'grave': ['#..'], 'acute': ['..#'], 'circ': ['.#.'], 'tilde': ['##.'],
            'diaer': ['#.#'], 'ring': ['.#.'], 'caron': ['.#.']}


def span(j, n, s):
    """the source range target index j of n covers in a source of s"""
    if s > n:
        return int(j * s / n), int(math.ceil((j + 1) * s / float(n)))
    k = (j * s) // n
    return k, k + 1


def resample(rows, w, h):
    """scale a pixel glyph to w x h: shrinking ORs the merged pixels"""
    sh = len(rows)
    sw = len(rows[0]) if rows else 0
    out = []
    for j in range(h):
        a0, a1 = span(j, h, sh)
        r = ''
        for i in range(w):
            b0, b1 = span(i, w, sw) if sw else (0, 0)
            ink = any(rows[y][x] == '#' for y in range(a0, min(a1, sh))
                      for x in range(b0, min(b1, sw)))
            r += '#' if ink else '.'
        out.append(r)
    return out


class Maker:
    def __init__(self, design, tiny, spec, tm, widths):
        self.spec = spec
        self.tm = tm
        self.w = widths
        self.r = Renderer(design, spec['style'], tiny)
        self.tiny = tiny
        self.m = dict((k, spec[k]) for k in ('cap', 'xh', 'ah', 'desc', 'pen', 'hpen',
                                              'serif', 'dot', 'acc'))
        if 'slab' in spec:
            self.m['slab'] = spec['slab']
        self.m['asc'] = tm['a']
        self.m['h'] = tm['h']
        self.fixed = spec['cp'] == 'oem' or spec['style'] == 'slab'
        self.missing = []

    def box(self, w):
        """the ink columns of a cell w wide"""
        if w <= 2:
            return 0, 0
        if self.fixed:
            if w >= 8:
                return 1, w - 2
            return 0, w - 2
        lsb, rsb = 0, 1
        if w >= 9 and self.m['cap'] >= 9:
            lsb = 1
        if w >= 14:
            lsb, rsb = 1, 2
        return lsb, max(lsb, w - 1 - rsb)

    def glyph(self, code, ch):
        w = self.w[code]
        h = self.tm['h']
        if w == 0:
            return [''] * h
        bm = self.bitmap(w)
        if self.spec['cp'] == 'oem' and self.boxchar(bm, ch):
            return bm.rows()
        L, R = self.box(w)
        if self.spec.get('tiny') and self.tinyglyph(bm, ch, L, R):
            return bm.rows()
        if self.r.draw(bm, ch, self.m, L, R):
            return bm.rows()
        bm = self.bitmap(w)
        if self.compose(bm, ch, L, R):
            return bm.rows()
        self.missing.append(ch)
        return self.default(w)

    def bitmap(self, w):
        """a cell; a proportional font's glyphs keep a blank last column"""
        bm = Bitmap(w, self.tm['h'])
        if not self.fixed and w >= 2:
            bm.clip = (0, 0, w - 2, self.tm['h'] - 1)
        return bm

    def default(self, w):
        """the default character: a hollow box of cap height"""
        h = self.tm['h']
        bm = Bitmap(w, h)
        if w == 0:
            return [''] * h
        base = self.m['asc'] - 1
        top = base - max(1, self.m['cap'] - 1) + 1
        L, R = self.box(w)
        if R - L >= 1 and base - top >= 1:
            for x in range(L, R + 1):
                bm.set(x, top)
                bm.set(x, base)
            for y in range(top, base + 1):
                bm.set(L, y)
                bm.set(R, y)
        return bm.rows()

    def tinyglyph(self, bm, ch, L, R):
        """the hand-drawn pixel glyphs (fonts/tiny.txt) fitted to the cell"""
        import unicodedata
        base_ch, marks = ch, []
        if ch not in self.tiny.g:
            d = unicodedata.normalize('NFD', ch)
            if len(d) < 2 or d[0] not in self.tiny.g:
                return False
            base_ch, marks = d[0], [ACCENTS.get(c) for c in d[1:]]
            if None in marks:
                return False
        rows, desc = self.tiny.g[base_ch]
        asc = self.m['asc']
        h = self.tm['h']
        up = len(rows) - desc               # the design's rows down to the baseline
        th = max(1, min(asc, int(round(up * self.m['cap'] / 5.0))))
        td = min(h - asc, desc)
        w = R - L + 1
        sw = len(rows[0])
        tw = min(w, sw)
        up_rows = resample(rows[:up], tw, th)
        dn_rows = resample(rows[up:], tw, td) if td and desc else []
        x0 = L + (w - tw) // 2
        for j, r in enumerate(up_rows):
            for i, c in enumerate(r):
                if c == '#':
                    bm.set(x0 + i, asc - th + j)
        for j, r in enumerate(dn_rows):
            for i, c in enumerate(r):
                if c == '#':
                    bm.set(x0 + i, asc + j)
        top = asc - th
        while top < asc and '#' not in bm.rows()[top]:
            top += 1
        for mk in marks:
            if mk == 'cedil':
                bm.set(x0 + tw // 2, min(h - 1, asc))
                continue
            y = top - 1
            if y < 0:
                continue
            pat = resample(TINY_ACC[mk], tw, 1)[0]
            for i, c in enumerate(pat):
                if c == '#':
                    bm.set(x0 + i, y)
        return True

    def compose(self, bm, ch, L, R):
        """a letter with accents: the base glyph, then the marks above
        (below for the cedilla); capitals are drawn shorter when the
        accent does not fit above them"""
        import unicodedata
        d = unicodedata.normalize('NFD', ch)
        if len(d) < 2 or any(c not in ACCENTS for c in d[1:]):
            return False
        base, marks = d[0], d[1:]
        above = [c for c in marks if c != '̧']
        if base == 'i' and above:
            base = 'ı'
        if self.r.d.get(base, self.r.style) is None:
            return False
        m = dict(self.m)
        acc = m['acc']
        if above:
            upper = base.isupper()
            tall = upper or base in 'bdhkl'
            height = m['cap'] if upper else (m['ah'] if tall else m['xh'])
            btop = (m['asc'] - 1) - height + 1
            if upper and btop < acc + 1:
                short = acc + 1 - btop
                if m['cap'] - short < int(m['cap'] * 0.7):
                    short = max(0, acc - btop)
                m['cap'] -= short
                m['ah'] = min(m['ah'], m['cap'])
                m['xh'] = min(m['xh'], m['cap'])
                btop += short
            gap = 1 if btop >= acc + 1 else 0
            acc_bot = btop - 1 - gap
            acc_top = max(0, acc_bot - acc + 1)
        if not self.r.draw(bm, base, m, L, R):
            return False
        for c in marks:
            am = dict(self.m)
            am['hpen'] = 1
            am['pen'] = 1 if self.m['pen'] < 3 else 2
            if ACCENTS[c] == 'cedil':
                am['asc'] = self.m['asc'] + max(1, self.m['desc'])
                am['cap'] = am['ah'] = am['xh'] = max(1, self.m['desc'])
                cx = (L + R) // 2 - 1
                self.r.draw(bm, c, am, cx, cx + 2)
                continue
            am['asc'] = acc_bot + 1
            am['cap'] = am['ah'] = am['xh'] = max(1, acc_bot - acc_top + 1)
            am['desc'] = 0
            aw = min(R - L + 1, max(2, 2 * acc - 1 + (1 if self.m['cap'] >= 12 else 0)))
            if ACCENTS[c] in ('diaer', 'tilde', 'circ', 'caron'):
                aw = min(R - L + 1, max(aw, 3, 2 * acc))
            cx = (L + R + 1 - aw) // 2
            if not self.r.draw(bm, c, am, cx, cx + aw - 1):
                return False
        return True

    def boxchar(self, bm, ch):
        """code page 437's box drawing, blocks and shades, by rule, so
        that they join across cells"""
        o = ord(ch)
        w, h = bm.w, bm.h
        if o in (0x2591, 0x2592, 0x2593):       # shades
            for y in range(h):
                for x in range(w):
                    light = (x % 4 == 0 and y % 2 == 0) or (x % 4 == 2 and y % 2 == 1)
                    on = {0x2591: light, 0x2592: (x + y) % 2 == 0, 0x2593: not light}[o]
                    if on:
                        bm.set(x, y)
            return True
        blocks = {0x2588: (0, 0, w, h), 0x2580: (0, 0, w, h // 2), 0x2584: (0, h // 2, w, h),
                  0x258C: (0, 0, w // 2, h), 0x2590: (w // 2, 0, w, h)}
        if o in blocks:
            x0, y0, x1, y1 = blocks[o]
            for y in range(y0, y1):
                for x in range(x0, x1):
                    bm.set(x, y)
            return True
        arms = box_arms(o)
        if arms is None:
            return False
        draw_box(bm, arms)
        return True


def box_arms(o):
    """(up, down, left, right) line types (0 none, 1 single, 2 double)
    of a box-drawing character, from its Unicode name"""
    import unicodedata
    if not 0x2500 <= o <= 0x257F:
        return None
    n = unicodedata.name(chr(o)).replace('BOX DRAWINGS ', '')
    words = n.split()
    if words[0] not in ('LIGHT', 'DOUBLE') and 'SINGLE' not in words:
        return None
    d = {'UP': 0, 'DOWN': 0, 'LEFT': 0, 'RIGHT': 0}
    simple = {'HORIZONTAL': ('LEFT', 'RIGHT'), 'VERTICAL': ('UP', 'DOWN')}
    default = 2 if words[0] == 'DOUBLE' else 1
    for part in n.split(' AND '):
        ws = part.split()
        t = 2 if 'DOUBLE' in ws else 1 if ('SINGLE' in ws or 'LIGHT' in ws) else default
        for wd in ws:
            for k in simple.get(wd, (wd,)):
                if k in d:
                    d[k] = t
    return d['UP'], d['DOWN'], d['LEFT'], d['RIGHT']


def draw_box(bm, arms):
    """each arm's lines run from the cell's edge to where they meet the
    perpendicular arms: a line ends at the nearer line of a double
    perpendicular arm on its side, at the centre for a single one, runs
    through the centre when the opposite arm continues it, and otherwise
    (a corner) reaches the far line of the perpendicular arm"""
    up, dn, lf, rt = arms
    w, h = bm.w, bm.h
    pw = 2 if w >= 12 else 1
    ph = 2 if h >= 16 and w >= 12 else 1
    sc = (w - pw) // 2
    sr = (h - ph) // 2

    def cols(t):
        return [sc] if t == 1 else [sc - pw, sc + pw] if t == 2 else []

    def rows(t):
        return [sr] if t == 1 else [sr - ph, sr + ph] if t == 2 else []

    def end_of(t, side_perp, both_perp, other_perp, opp, pos, centre, low):
        near = min if low else max
        far = max if low else min
        if t == 1:
            if opp or not both_perp:
                return centre
            if len(both_perp) == 2:
                pt = max(both_perp)
                return near(pos(pt)) if pt == 2 else centre
            pt = both_perp[0]
            return far(pos(pt)) if pt == 2 else centre
        if side_perp:
            return near(pos(side_perp)) if side_perp == 2 else centre
        if opp:
            return centre
        if other_perp == 2:
            return far(pos(other_perp))
        return centre

    for t, frm, opp, low in ((up, 0, dn, True), (dn, h - 1, up, False)):
        both = [p for p in (lf, rt) if p]
        for i, x in enumerate(cols(t)):
            side, other = (lf, rt) if i == 0 else (rt, lf)
            e = end_of(t, side, both, other, opp, rows, sr, low)
            y0, y1 = (frm, e + ph - 1) if low else (e, frm)
            for y in range(y0, y1 + 1):
                for k in range(pw):
                    bm.set(x + k, y)
    for t, frm, opp, low in ((lf, 0, rt, True), (rt, w - 1, lf, False)):
        both = [p for p in (up, dn) if p]
        for i, y in enumerate(rows(t)):
            side, other = (up, dn) if i == 0 else (dn, up)
            e = end_of(t, side, both, other, opp, cols, sc, low)
            x0, x1 = (frm, e + pw - 1) if low else (e, frm)
            for x in range(x0, x1 + 1):
                for k in range(ph):
                    bm.set(x, y + k)


# ---------------------------------------------------------------- output

def points(tm, vres):
    return int(math.floor((tm['h'] - tm['il']) * 72.0 / vres + 0.5))


def sentinel(tm, widths):
    """the extra character-table entry after the last character: in a
    proportional font that ends before 255, GetCharWidth(last + 1) reports
    it (Symbol: 8); otherwise unobservable, and as wide as the break
    character up to 8 (one byte, which keeps 3.1's resource sizes)"""
    if tm['lc'] < 255 and not (tm['pf'] & 1) == 0 and tm['aw'] != tm['mw']:
        return widths[tm['lc'] + 1]
    return min(8, max(1, widths[tm['bc']]))


def source(spec, tm, widths, glyphs, note):
    """the fontc source text"""
    # 3.1 reports dfVertRes as tmDigitizedAspectX and dfHorizRes as ...Y
    # (FN3: a font written with 96/72 shows ax=96 ay=72)
    vres, hres = tm['ax'], tm['ay']
    first, last = tm['fc'], tm['lc']
    face = spec['label'].rsplit(' ', 1)[0]
    out = ['; %s.txt - "%s" %dx%d, %s: w16\'s own design.' % (
        spec['name'], face, tm['h'], tm['aw'], note),
        '; Original design for w16 (public domain, CC0 1.0), drawn by tools/fontgen.py',
        '; from fonts/skeleton.txt and fonts/tiny.txt; built into %s.FON by' % spec['fon'],
        '; tools/fontc.py (ARCHITECTURE.md 6.9). The metrics and every advance width',
        '; are Windows 3.1\'s, observed through the API (probe FN3, tests/fonts/',
        '; fn3ref.txt); the glyph shapes are ours. Regenerate rather than edit.',
        '',
        'face %s' % face,
        'version 2',
        'copyright "w16 %s font. Public domain (CC0 1.0)"' % face,
        'points %d' % points(tm, tm['ay']),
        'vertres %d' % vres,
        'horizres %d' % hres,
        'height %d' % tm['h'],
        'ascent %d' % tm['a'],
        'internal-leading %d' % tm['il'],
        'external-leading %d' % tm['el'],
        'weight %d' % tm['wt'],
        'charset %d' % tm['cs'],
        'pitch-family 0x%02X' % tm['pf'],
        'avgwidth %d' % tm['aw'],
        'maxwidth %d' % tm['mw'],
        'first %d' % first,
        'last %d' % last,
        'default %d' % tm['dc'],
        'break %d' % tm['bc'],
        'sentinel-width %d' % sentinel(tm, widths),
        '']
    for c in range(first, last + 1, 16):
        out.append('widths %d: %s' % (c, ' '.join(str(widths[k])
                                                  for k in range(c, min(last + 1, c + 16)))))
    out.append('')
    for c in range(first, last + 1):
        ch, rows = glyphs[c]
        nm = ''
        if ch is not None:
            if ch.isprintable() and 32 < ord(ch) < 0xE000 and ch not in ';=':
                nm = ' ' + ch
            nm += ' U+%04X' % ord(ch)
        out.append('char 0x%02X%s' % (c, nm))
        out += [r if r else '-' for r in rows]
        out.append('')
    return '\n'.join(out)


MADE = {}


def make(spec, log, design, tiny, srcdir):
    if 'copy' in spec or 'double' in spec:
        return derived(spec, log, srcdir)
    tm = dict(log['fonts'][spec['label']]['tm'])
    widths = log['fonts'][spec['label']]['w']
    tm = own_tm(log, spec, tm)
    table = cp_table(spec['cp'])
    mk = Maker(design, tiny, spec, tm, widths)
    glyphs = {}
    for c in range(tm['fc'], tm['lc'] + 1):
        ch = table.get(c)
        if spec['cp'] == 'ansi' and 0x80 <= c <= 0x9F and ch is not None:
            # 3.1's ANSI raster fonts have only the single quotation marks
            # here, Small Fonts the Windows set: a character is drawn where
            # its width differs from the default character's
            if mk.fixed and c not in (0x91, 0x92):
                ch = None
            elif not mk.fixed and widths[c] == widths[tm['dc']] and c not in (0x91, 0x92):
                ch = None
        if ch is None:
            if widths[c] != widths[tm['dc']]:
                print('fontgen: %s 0x%02X has no character but is %d wide'
                      % (spec['name'], c, widths[c]))
            glyphs[c] = (None, mk.default(widths[c]))
        else:
            glyphs[c] = (ch, mk.glyph(c, ch))
    if mk.missing:
        print('fontgen: %s: no design for %s' % (spec['name'], ' '.join(
            '%s U+%04X' % (c, ord(c)) for c in mk.missing)))
    return tm, widths, glyphs


def own_tm(log, spec, tm):
    """a DOS-box font's own TEXTMETRIC, from the FILE section of its file
    (EnumFonts: the font GDI realises for the cell may be another file's,
    VGAOEM's for 12x8)"""
    h, aw = (int(v) for v in spec['label'].rsplit(' ', 1)[1].split('x'))
    for t in log['new'].get(spec['fon'] + '.FON', []):
        if t['h'] == h and t['aw'] == aw:
            return dict(t)
    if tm is None:
        raise SystemExit('fontgen: no FILE/NEW line for %s' % spec['name'])
    return tm


def derived(spec, log, srcdir):
    """a font made from another source: copied (EGA80WOA from VGAOEM's
    Terminal) or doubled in width (the 40-column fonts)"""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import fontc
    src = spec.get('copy') or spec.get('double')
    if src in MADE:
        have = {c: rows for c, (_, rows) in MADE[src].items()}
    else:
        f = fontc.Font(os.path.join(srcdir, src + '.txt'))
        have = {c: g.rows for c, g in f.glyphs.items()}
    tm = own_tm(log, spec, None)
    widths = [tm['aw']] * 256
    table = cp_table('oem')
    glyphs = {}
    for c in range(tm['fc'], tm['lc'] + 1):
        rows = have[c]
        if 'double' in spec:
            rows = [''.join(p * 2 for p in r) for r in rows]
        glyphs[c] = (table[c], rows)
    return tm, widths, glyphs


def read_log(path):
    """FN3's log: FONT sections ({label: {'tm', 'w'}}) and the FILE
    sections' NEW lines ({file: TEXTMETRIC of the first font it adds that
    no other file had})"""
    fonts, new = {}, {}
    cur = None
    fil = None
    lines = [ln.rstrip('\r\n') for ln in open(path, encoding='latin-1')]
    for i, line in enumerate(lines):
        if line.startswith('FONT '):
            cur = {'tm': None, 'w': []}
            fonts.setdefault(line[5:], cur)
            continue
        m = re.match(r'FILE (\S+)', line)
        if m:
            fil = m.group(1)
            cur = None
            continue
        if line.startswith('NEW ') and fil:
            new.setdefault(fil, []).append(
                {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', lines[i + 1])})
            continue
        if cur is None:
            continue
        if cur['tm'] is None and line.startswith(' face='):
            cur['tm'] = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
            continue
        m = re.match(r' w(\d+)=([\d,-]+)$', line)
        if m:
            cur['w'] += [int(x) for x in m.group(2).split(',')]
            continue
        if not line.startswith(' '):
            cur = None
    return {'fonts': fonts, 'new': new}


def specimen(path, glyphs, tm, scale):
    """a PNG of every glyph (for looking at the designs)"""
    import struct
    import zlib
    codes = sorted(glyphs)
    cw = max(max((len(r[1][0]) for r in glyphs.values() if r[1] and r[1][0]), default=1), 1) + 1
    chh = tm['h'] + 1
    cols = 16
    nrows = (len(codes) + cols - 1) // cols
    W, H = (cols * cw + 1) * scale, (nrows * chh + 1) * scale
    img = [bytearray([255]) * W for _ in range(H)]
    for i, c in enumerate(codes):
        ox, oy = (i % cols) * cw + 1, (i // cols) * chh + 1
        rows = glyphs[c][1]
        w = len(rows[0]) if rows and rows[0] else 0
        for y in range(tm['h']):
            for x in range(w):
                v = 0 if rows[y][x] == '#' else 225
                for sy in range(scale):
                    for sx in range(scale):
                        img[(oy + y) * scale + sy][(ox + x) * scale + sx] = v
    raw = b''.join(b'\0' + bytes(r) for r in img)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    open(path, 'wb').write(png)


def main():
    ap = argparse.ArgumentParser(description='w16 font designs -> fontc sources')
    ap.add_argument('names', nargs='*')
    ap.add_argument('--ref', default=os.path.join(ROOT, 'tests/fonts/fn3ref.txt'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'fonts'))
    ap.add_argument('--png', help='write specimens DIR/<name>.png instead')
    ap.add_argument('--scale', type=int, default=4)
    args = ap.parse_args()
    log = read_log(args.ref)
    design = Design(os.path.join(ROOT, 'fonts/skeleton.txt'))
    tiny = Tiny(os.path.join(ROOT, 'fonts/tiny.txt'))
    names = args.names or [f['name'] for f in FONTS]
    for spec in FONTS:
        if spec['name'] not in names:
            continue
        tm, widths, glyphs = make(spec, log, design, tiny, args.out)
        MADE[spec['name']] = glyphs
        if args.png:
            specimen(os.path.join(args.png, spec['name'] + '.png'), glyphs, tm, args.scale)
            continue
        note = {'slab': 'slab serif, fixed pitch', 'serif': 'serif',
                'sans': 'sans serif'}[spec['style']]
        if spec['cp'] == 'oem':
            note = 'code page 437, fixed pitch'
        if spec['cp'] == 'symbol':
            note = 'the symbol character set'
        text = source(spec, tm, widths, glyphs, note)
        open(os.path.join(args.out, spec['name'] + '.txt'), 'w').write(text + '\n')


if __name__ == '__main__':
    main()
