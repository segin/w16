#!/usr/bin/env python3
"""ttgen - builds w16's TrueType fonts (ARCHITECTURE.md 6.9.3).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).

Twelve faces, metric-compatible with Windows 3.1's TrueType core set:

    w16 Sans  (W16SANS, W16SANBD, W16SANIT, W16SANBI)    widths of Arial
    w16 Serif (W16SERIF, W16SERBD, W16SERIT, W16SERBI)   widths of Times New Roman
    w16 Mono  (W16MONO, W16MONBD, W16MONIT, W16MONBI)    widths of Courier New

The outlines are w16's own designs (fonts/tt/design.py, built with
tools/ttgeom.py); only metrics come from Windows 3.1, observed through the
API by probe FN4 (tests/apps/fn4.c) into tests/fonts/fn4ref.txt:

  - every advance width (GetCharWidth at the face's em, 2048 pixels:
    font units), and the hinted device widths at 6-255 pixels per em
    (into an hdmx table, which 3.1's GDI uses for GetCharWidth and layout);
  - the horizontal extent of each character's ink (GetCharABCWidths A and
    B at 256 pixels per em, times 8), into which the design is fitted;
  - x-height, cap height, ascender, descender and overshoot (GGO_METRICS
    black boxes of x, H, d, p, o at 256 pixels per em);
  - the vertical metrics, underline and strike-out, sub/superscript,
    italic angle and caret slope (OUTLINETEXTMETRIC, TEXTMETRIC);
  - kerning pairs (GetKerningPairs).

Repertoire: ISO 8859-15 (Latin-9) with .notdef, .null and CR; cmap (3,1)
format 4. 3.1 shows ANSI 0xB7 through U+2219, so that maps to the middle
dot as well (FN4's MAP section, tools/ttmap.py).

    ttgen.py [--ref FN4LOG] [--out DIR] [FACE ...]     build (default all)
    ttgen.py --check DIR                               validate built fonts
    ttgen.py --specimen DIR OUTDIR                     PNG specimen sheets
    ttgen.py --glyphs FACE OUT.png [CHARS]             one face's glyphs, large

Validation (--check, also run after building): tools/w16ttf.py's
structural checks (tables, checksums, maxp, loca, bounds, cmap, names), the
advance widths and hdmx against the reference, every character of the
repertoire mapped, no composite glyphs or instructions; and, when the
Python module fontTools is importable, that it reads every table back.
"""
import argparse
import math
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'fonts', 'tt'))
import w16ttf  # noqa: E402
import ttgeom as G  # noqa: E402
import design  # noqa: E402

REF = os.path.join(ROOT, 'tests', 'fonts', 'fn4ref.txt')
VERSION = '1.00'
# 2026-11-22 00:00 UTC in seconds since 1904-01-01 (head.created/modified)
CREATED = 2082844800 + 1795305600

CC0_URL = 'https://creativecommons.org/publicdomain/zero/1.0/'
LICENSE = ('To the extent possible under law, the w16 contributors have waived all '
           'copyright and related or neighboring rights to this font (CC0 1.0 '
           'Universal public domain dedication). It may be used, modified and '
           'distributed for any purpose without permission.')

# base name, family, style, kind, bold, italic, reference face
FACES = [
    ('W16SANS', 'w16 Sans', 'Regular', 'sans', 0, 0, ('Arial', 400, 0)),
    ('W16SANBD', 'w16 Sans', 'Bold', 'sans', 1, 0, ('Arial', 700, 0)),
    ('W16SANIT', 'w16 Sans', 'Italic', 'sans', 0, 1, ('Arial', 400, 1)),
    ('W16SANBI', 'w16 Sans', 'Bold Italic', 'sans', 1, 1, ('Arial', 700, 1)),
    ('W16SERIF', 'w16 Serif', 'Regular', 'serif', 0, 0, ('Times New Roman', 400, 0)),
    ('W16SERBD', 'w16 Serif', 'Bold', 'serif', 1, 0, ('Times New Roman', 700, 0)),
    ('W16SERIT', 'w16 Serif', 'Italic', 'serif', 0, 1, ('Times New Roman', 400, 1)),
    ('W16SERBI', 'w16 Serif', 'Bold Italic', 'serif', 1, 1, ('Times New Roman', 700, 1)),
    ('W16MONO', 'w16 Mono', 'Regular', 'mono', 0, 0, ('Courier New', 400, 0)),
    ('W16MONBD', 'w16 Mono', 'Bold', 'mono', 1, 0, ('Courier New', 700, 0)),
    ('W16MONIT', 'w16 Mono', 'Italic', 'mono', 0, 1, ('Courier New', 400, 1)),
    ('W16MONBI', 'w16 Mono', 'Bold Italic', 'mono', 1, 1, ('Courier New', 700, 1)),
]

# PANOSE of w16's designs (their own classification)
PANOSE = {
    ('sans', 0, 0): (2, 11, 6, 4, 2, 2, 2, 2, 2, 4),
    ('sans', 1, 0): (2, 11, 8, 4, 2, 2, 2, 2, 2, 4),
    ('sans', 0, 1): (2, 11, 6, 4, 2, 2, 2, 9, 2, 4),
    ('sans', 1, 1): (2, 11, 8, 4, 2, 2, 2, 9, 2, 4),
    ('serif', 0, 0): (2, 2, 5, 3, 6, 4, 5, 2, 3, 4),
    ('serif', 1, 0): (2, 2, 8, 3, 6, 5, 5, 2, 3, 4),
    ('serif', 0, 1): (2, 2, 5, 3, 6, 4, 5, 9, 3, 4),
    ('serif', 1, 1): (2, 2, 8, 3, 6, 5, 5, 9, 3, 4),
    ('mono', 0, 0): (2, 7, 4, 9, 2, 2, 5, 2, 4, 4),
    ('mono', 1, 0): (2, 7, 7, 9, 2, 2, 5, 2, 4, 4),
    ('mono', 0, 1): (2, 7, 4, 9, 2, 2, 5, 9, 4, 4),
    ('mono', 1, 1): (2, 7, 7, 9, 2, 2, 5, 9, 4, 4),
}
FAMILY_CLASS = {'sans': 0x0805, 'serif': 0x0104, 'mono': 0x0501}

# ------------------------------------------------------------ repertoire


def ansi_to_unicode():
    """the Unicode character 3.1's TrueType engine looks up for each ANSI
    code (FN4's MAP section): Windows code page 1252 as it stood in 3.1
    (no euro at 0x80, nothing at 0x8E/0x9E), 0xB7 as U+2219"""
    t = {}
    for c in range(0x20, 0x100):
        if c in (0x7F, 0x80, 0x81, 0x8D, 0x8E, 0x8F, 0x90, 0x9D, 0x9E):
            continue
        t[c] = ord(bytes([c]).decode('cp1252'))
    t[0xB7] = 0x2219
    return t


def repertoire():
    """the characters: ISO 8859-15's printable ones, in code order"""
    chars = [chr(c) for c in range(0x20, 0x7F)]
    chars += list(bytes(range(0xA0, 0x100)).decode('iso8859_15'))
    return chars


def glyph_name(ch):
    names = w16ttf.mac_glyph_names()
    special = {' ': 'space', ' ': 'nbspace', '­': 'softhyphen', '€': 'Euro',
               'µ': 'mu', '·': 'periodcentered', '¯': 'macron'}
    if ch in special:
        return special[ch]
    import unicodedata
    n = unicodedata.name(ch, '')
    std = {
        'EXCLAMATION MARK': 'exclam', 'QUOTATION MARK': 'quotedbl', 'NUMBER SIGN': 'numbersign',
        'DOLLAR SIGN': 'dollar', 'PERCENT SIGN': 'percent', 'AMPERSAND': 'ampersand',
        'APOSTROPHE': 'quotesingle', 'LEFT PARENTHESIS': 'parenleft', 'RIGHT PARENTHESIS': 'parenright',
        'ASTERISK': 'asterisk', 'PLUS SIGN': 'plus', 'COMMA': 'comma', 'HYPHEN-MINUS': 'hyphen',
        'FULL STOP': 'period', 'SOLIDUS': 'slash', 'COLON': 'colon', 'SEMICOLON': 'semicolon',
        'LESS-THAN SIGN': 'less', 'EQUALS SIGN': 'equal', 'GREATER-THAN SIGN': 'greater',
        'QUESTION MARK': 'question', 'COMMERCIAL AT': 'at', 'LEFT SQUARE BRACKET': 'bracketleft',
        'REVERSE SOLIDUS': 'backslash', 'RIGHT SQUARE BRACKET': 'bracketright',
        'CIRCUMFLEX ACCENT': 'asciicircum', 'LOW LINE': 'underscore', 'GRAVE ACCENT': 'grave',
        'LEFT CURLY BRACKET': 'braceleft', 'VERTICAL LINE': 'bar', 'RIGHT CURLY BRACKET': 'braceright',
        'TILDE': 'asciitilde', 'INVERTED EXCLAMATION MARK': 'exclamdown', 'CENT SIGN': 'cent',
        'POUND SIGN': 'sterling', 'YEN SIGN': 'yen', 'SECTION SIGN': 'section',
        'COPYRIGHT SIGN': 'copyright', 'FEMININE ORDINAL INDICATOR': 'ordfeminine',
        'LEFT-POINTING DOUBLE ANGLE QUOTATION MARK': 'guillemotleft', 'NOT SIGN': 'logicalnot',
        'REGISTERED SIGN': 'registered', 'DEGREE SIGN': 'degree', 'PLUS-MINUS SIGN': 'plusminus',
        'SUPERSCRIPT TWO': 'twosuperior', 'SUPERSCRIPT THREE': 'threesuperior',
        'PILCROW SIGN': 'paragraph', 'SUPERSCRIPT ONE': 'onesuperior',
        'MASCULINE ORDINAL INDICATOR': 'ordmasculine',
        'RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK': 'guillemotright',
        'INVERTED QUESTION MARK': 'questiondown', 'MULTIPLICATION SIGN': 'multiply',
        'DIVISION SIGN': 'divide', 'LATIN CAPITAL LETTER AE': 'AE', 'LATIN SMALL LETTER AE': 'ae',
        'LATIN CAPITAL LIGATURE OE': 'OE', 'LATIN SMALL LIGATURE OE': 'oe',
        'LATIN CAPITAL LETTER ETH': 'Eth', 'LATIN SMALL LETTER ETH': 'eth',
        'LATIN CAPITAL LETTER THORN': 'Thorn', 'LATIN SMALL LETTER THORN': 'thorn',
        'LATIN SMALL LETTER SHARP S': 'germandbls', 'LATIN CAPITAL LETTER O WITH STROKE': 'Oslash',
        'LATIN SMALL LETTER O WITH STROKE': 'oslash',
    }
    if n in std:
        return std[n]
    m = re.match(r'(DIGIT) (\w+)$', n)
    if m:
        return m.group(2).lower()
    m = re.match(r'LATIN (CAPITAL|SMALL) LETTER (\w)(?: WITH (.*))?$', n)
    if m:
        base = m.group(2) if m.group(1) == 'CAPITAL' else m.group(2).lower()
        if not m.group(3):
            return base
        acc = {'GRAVE': 'grave', 'ACUTE': 'acute', 'CIRCUMFLEX': 'circumflex', 'TILDE': 'tilde',
               'DIAERESIS': 'dieresis', 'RING ABOVE': 'ring', 'CEDILLA': 'cedilla', 'CARON': 'caron'}
        return base + acc[m.group(3)]
    if n in names:
        return n
    return 'uni%04X' % ord(ch)


# --------------------------------------------------------- the reference


def parse_ref(path):
    """FN4's log: per face (name, weight, italic) its data"""
    faces = {}
    cur = None
    section = None
    with open(path, encoding='latin-1') as f:
        for line in f:
            line = line.rstrip('\r\n')
            if line.startswith('FACE '):
                m = re.match(r'FACE name="([^"]*)" wt=(\d+) it=(\d+)', line)
                cur = {'w': [0] * 256, 'abc': {}, 'gm': {}, 'kern': [], 'sizes': {}, 'tmsize': {}}
                faces[(m.group(1), int(m.group(2)), int(m.group(3)))] = cur
                section = None
                continue
            if line.startswith(('MAP', 'FOT', 'FAMILIES', 'FAMILY', 'FN4 done')):
                cur = None
                continue
            if cur is None:
                continue
            s = line.strip()
            kv = dict(re.findall(r'(\w[\w.]*)=("[^"]*"|[-\d,]+)', s))
            if s.startswith('got="'):
                cur['em'] = int(kv['em'])
                cur['tm'] = {k: int(v) for k, v in kv.items() if k not in ('got',) and ',' not in v and not v.startswith('"')}
            elif s.startswith(('OTM ', 'OTM2', 'OTM3')):
                cur.setdefault('otm', {}).update({k: int(v) for k, v in kv.items() if not v.startswith('"')})
            elif s.startswith('OTM4'):
                cur['panose'] = [int(v) for v in kv['panose'].split(',')]
            elif s.startswith('OTM5'):
                cur['names'] = {k: v.strip('"') for k, v in kv.items()}
            elif s.startswith('SIZE '):
                section = int(kv['ppem'])
                cur['sizes'][section] = [0] * 256
                cur['tmsize'][section] = {k: int(v) for k, v in kv.items()}
            elif re.match(r'w\d+=', s):
                k, v = s.split('=')
                base = int(k[1:])
                for i, x in enumerate(v.split(',')):
                    cur['w'][base + i] = int(x)
            elif re.match(r's\d+=', s):
                k, v = s.split('=')
                base = int(k[1:])
                for i, x in enumerate(v.split(',')):
                    cur['sizes'][section][base + i] = int(x)
            elif s.startswith('abc'):
                m = re.match(r'abc(\d+) (\d+) (-?\d+) (\d+) (-?\d+)', s)
                if m:
                    cur['abc'].setdefault(int(m.group(1)), {})[int(m.group(2))] = (
                        int(m.group(3)), int(m.group(4)), int(m.group(5)))
            elif s.startswith('gm'):
                m = re.match(r'gm(\d+) (\d+) ', s)
                if m:
                    cur['gm'].setdefault(int(m.group(1)), {})[int(m.group(2))] = {
                        k: int(v) for k, v in kv.items()}
            elif s.startswith('kp '):
                a, b, v = map(int, s.split()[1:])
                cur['kern'].append((a, b, v))
    return faces


def vmetrics(r):
    """x-height, cap height, ascender, descender, overshoot (font units)
    from the GGO_METRICS black boxes at 256 pixels per em"""
    gm = r['gm'][256]
    k = r['em'] / 256.0
    x = gm[ord('x')]['oy'] * k
    H = gm[ord('H')]['oy'] * k
    d = gm[ord('d')]['oy'] * k
    p = (gm[ord('p')]['oy'] - gm[ord('p')]['by']) * k
    o = gm[ord('o')]['oy'] * k - x
    return {'xh': round(x), 'cap': round(H), 'asc': round(d), 'desc': round(p), 'ov': max(12, min(30, round(o)))}


# -------------------------------------------------------------- building


class Face:
    def __init__(self, spec, ref):
        self.base, self.family, self.style, self.kind, self.bold, self.italic, self.refkey = spec
        self.ref = ref[self.refkey]
        self.em = self.ref['em']
        vm = vmetrics(self.ref)
        vm['angle'] = -self.ref['otm']['angle'] / 10.0
        self.s = design.Style(self.kind, bool(self.bold), bool(self.italic), vm)
        self.ansi = ansi_to_unicode()
        self.uni2ansi = {u: c for c, u in self.ansi.items()}

    def advance(self, ch):
        u = ord(ch)
        stand_in = {'€': '0', 'Ž': 'Z', 'ž': 'z'}
        if u in self.uni2ansi:
            return self.ref['w'][self.uni2ansi[u]]
        if ch == '·':
            return self.ref['w'][0xB7]
        return self.advance(stand_in[ch])

    def inkbox(self, ch):
        """(L, R) of the reference's ink, font units, from ABC at 256 ppem"""
        u = ord(ch)
        stand_in = {'€': '0', 'Ž': 'Z', 'ž': 'z', '·': None}
        if ch in design.COMPOSED:
            # an accented letter takes its base's ink (the accent's is wider)
            return self.inkbox(design.COMPOSED[ch][0])
        if ch == 'ı':
            return self.inkbox('i')
        code = self.uni2ansi.get(u)
        if ch == '·':
            code = 0xB7
        if code is None:
            code = self.uni2ansi[ord(stand_in[ch])]
        a, b, c = self.ref['abc'][256][code]
        k = self.em / 256.0
        L, R = a * k, (a + b) * k
        if ch == '€':
            # a little wider than a figure
            w = R - L
            L, R = L - w * 0.04, R + w * 0.04
        return L, R

    def device_widths(self, ch):
        """{ppem: width} as 3.1 reports for the reference character"""
        u = ord(ch)
        stand_in = {'€': '0', 'Ž': 'Z', 'ž': 'z'}
        if ch == '·':
            code = 0xB7
        elif u in self.uni2ansi:
            code = self.uni2ansi[u]
        else:
            code = self.uni2ansi[ord(stand_in[ch])]
        return {p: ws[code] for p, ws in self.ref['sizes'].items()}

    def outline(self, ch):
        """the glyph's TrueType contours"""
        L, R = self.inkbox(ch)
        s = self.s
        if R - L < 1 or ch in (' ', ' '):
            return []
        if not s.slant:
            sh = design.draw(s, ch, L, R)
            sh = G.union(sh)
            return G.fit(sh)
        # italic: find the upright ink box whose slanted ink is L..R
        w = R - L
        L0, R0 = L, R
        for _ in range(4):
            pieces = design.draw(s, ch, L0, R0)
            sl = G.shear(pieces, s.slant)
            x0, _, x1, _ = G.bbox(sl)
            err = (x1 - x0) - w
            if abs(err) < 2:
                break
            R0 = R0 - err
            if R0 - L0 < w * 0.3:
                R0 = L0 + w * 0.3
                break
        sh = G.shear(G.union(design.draw(s, ch, L0, R0)), s.slant)
        x0 = G.bbox(sh)[0]
        sh = G.move(sh, L - x0, 0)
        return G.fit(sh)

    def build(self):
        f = w16ttf.Font()
        f.upem = self.em
        r = self.ref
        otm = r['otm']
        tm = r['tm']
        notdef_w = r['w'][0]
        # .notdef: an open box
        s = self.s
        bw = notdef_w * 0.62
        x0 = (notdef_w - bw) / 2
        t = max(60, s.sh * 0.6)
        nd = G.rect(x0, 0, x0 + bw, s.cap) + [G.rect(x0 + t, t, x0 + bw - t, s.cap - t)[0].cw()]
        if s.slant:
            nd = G.shear(nd, s.slant)
        f.glyphs.append(w16ttf.Glyph('.notdef', notdef_w, G.fit(nd)))
        f.glyphs.append(w16ttf.Glyph('.null', 0))
        f.glyphs.append(w16ttf.Glyph('nonmarkingreturn', self.advance(' ')))
        f.cmap[0x0D] = 2
        self.chars = repertoire()
        dev = {0: {p: ws[0x1F] for p, ws in r['sizes'].items()},
               1: {p: 0 for p in r['sizes']},
               2: self.device_widths(' ')}
        for ch in self.chars:
            gid = len(f.glyphs)
            g = w16ttf.Glyph(glyph_name(ch), self.advance(ch), self.outline(ch))
            f.glyphs.append(g)
            f.cmap[ord(ch)] = gid
            dev[gid] = self.device_widths(ch)
        f.cmap[0x2219] = f.cmap[0xB7]
        self.gid = {ch: f.cmap[ord(ch)] for ch in self.chars}
        # metrics
        # 3.1 reports these negative values one nearer zero than the file
        # has them (w16's fonts written with 3.1's figures showed -429 for
        # -430): the file holds one less
        f.hhea = (otm['macasc'], otm['macdesc'] - 1, otm['macgap'])
        f.typo = (otm['asc'], otm['desc'] - 1, otm['gap'])
        f.win = (tm['a'], tm['d'])
        f.avg_width = tm['aw']
        f.weight = tm['wt']
        f.bold = bool(self.bold)
        f.italic = bool(self.italic)
        f.fixed_pitch = self.kind == 'mono'
        f.italic_angle = otm['angle'] / 10.0
        f.caret = (otm['rise'], otm['run'])
        f.subscript = (otm['subx'], otm['suby'], otm['subox'], otm['suboy'])
        f.superscript = (otm['supx'], otm['supy'], otm['supox'], otm['supoy'])
        f.strikeout = (otm['strsize'], otm['strpos'])
        f.underline = (otm['ulpos'] - 1, otm['ulsize'])
        # tmMaxCharWidth (and the .FOT's dfMaxWidth) come from head's box:
        # it is at least the reference's (otmrcFontBox)
        f.bbox_min = (otm['box.l'], otm['box.b'], otm['box.r'], otm['box.t'])
        # per-size ascent and descent as 3.1 reports them (VDMX)
        f.vdmx = {p: (t['a'], -t['d']) for p, t in r['tmsize'].items()}
        f.lowest_ppem = otm['minppem']
        f.panose = bytes(PANOSE[(self.kind, self.bold, self.italic)])
        f.family_class = FAMILY_CLASS[self.kind]
        f.fs_type = 0
        # Basic Latin, Latin-1 Supplement, Latin Extended-A; General
        # Punctuation is not claimed; Currency Symbols (bit 33), Math
        # Operators (bit 60, for U+2219)
        f.unicode_ranges = (0x00000007, (1 << 1) | (1 << 28), 0, 0)
        f.codepages = (0x00000001, 0)
        f.first_char = 0x20
        f.last_char = 0x20AC
        f.created = CREATED
        f.revision = float(VERSION)
        full = self.family if self.style == 'Regular' else '%s %s' % (self.family, self.style)
        ps = self.family.replace(' ', '') + ('' if self.style == 'Regular' else '-' + self.style.replace(' ', ''))
        f.names = {
            0: 'No rights reserved. %s is dedicated to the public domain under CC0 1.0 by the w16 contributors.' % full,
            1: self.family, 2: self.style,
            3: 'w16: %s: %s' % (full, VERSION),
            4: full, 5: 'Version %s' % VERSION, 6: ps,
            8: 'The w16 project', 9: 'The w16 contributors',
            10: ('%s: an original design by the w16 project, metric-compatible with '
                 'Windows 3.1\'s TrueType core fonts.' % full),
            13: LICENSE, 14: CC0_URL,
        }
        # kerning: GetKerningPairs gives ANSI codes
        for a, b, v in r['kern']:
            ua, ub = self.ansi.get(a), self.ansi.get(b)
            if ua is None or ub is None or ua not in f.cmap or ub not in f.cmap:
                continue
            f.kern[(f.cmap[ua], f.cmap[ub])] = v
        # hdmx: the device widths 3.1 reports, 6-255 pixels per em
        # (a record holds widths up to 255: sizes with a wider glyph are
        # left out, 3.1 then scales the advance widths)
        self.hdmx_skipped = []
        for p in sorted(r['sizes']):
            ws = [dev.get(g, {}).get(p, round(f.glyphs[g].advance * p / self.em))
                  for g in range(len(f.glyphs))]
            if max(ws) > 255:
                self.hdmx_skipped.append(p)
                continue
            f.hdmx[p] = ws
        self.font = f
        return f.build()


# ------------------------------------------------------------ validation


def check_font(path, face):
    """problems with a built font against the reference"""
    data = open(path, 'rb').read()
    errs = list(w16ttf.check(data))
    if errs:
        return errs
    f = w16ttf.parse(data)
    cmap = f['cmap'][(3, 1, 4)]
    ansi = ansi_to_unicode()
    for ch in repertoire():
        if ord(ch) not in cmap:
            errs.append('U+%04X not mapped' % ord(ch))
    for code, u in ansi.items():
        if u in cmap:
            gid = cmap[u]
            if f['advance'][gid] != face.ref['w'][code]:
                errs.append('ANSI %02X width %d, reference %d' % (code, f['advance'][gid], face.ref['w'][code]))
            for p, ws in face.ref['sizes'].items():
                if p not in f.get('hdmx', {}):
                    continue
                if f['hdmx'][p][gid] != ws[code]:
                    errs.append('ANSI %02X at %d ppem: hdmx %s, reference %d' % (
                        code, p, f.get('hdmx', {}).get(p, [None] * (gid + 1))[gid], ws[code]))
                    break
    if f['advance'][0] != face.ref['w'][0x1F]:
        errs.append('.notdef width %d, reference default %d' % (f['advance'][0], face.ref['w'][0x1F]))
    o = f['os2']
    tm = face.ref['tm']
    if (o['wasc'], o['wdesc'], o['avg'], o['weight']) != (tm['a'], tm['d'], tm['aw'], tm['wt']):
        errs.append('OS/2 win metrics/avg/weight differ')
    try:
        from fontTools.ttLib import TTFont
        t = TTFont(path)
        for tag in t.keys():
            t[tag]
        t['glyf'].glyphs  # noqa: B018
        for gname in t.getGlyphOrder():
            t['glyf'][gname].getCoordinates(t['glyf']) if t['glyf'][gname].numberOfContours else None
    except ImportError:
        pass
    except Exception as e:  # noqa: BLE001
        errs.append('fontTools: %s' % e)
    return errs


# --------------------------------------------------------------- previews


SAMPLE = ('The quick brown fox jumps over the lazy dog. 0123456789',
          'ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz',
          '!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~ ¡¢£€¥Š§š©ª«¬®¯°±²³Žµ¶·ž¹º»ŒœŸ¿',
          'ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß',
          'àáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ')

TEXT = ('Windows 3.1 sets this paragraph in w16\'s own TrueType faces. Their '
        'outlines are original; only the widths match the fonts they stand in '
        'for, so a document keeps its line breaks and page layout. '
        'Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich. '
        'Le cœur déçu mais l\'âme plutôt naïve, Louÿs rêva de crapaüter en canoë.')


def specimen(paths, out):
    from PIL import Image, ImageDraw, ImageFont
    W = 1700
    rows = []
    for p in paths:
        rows.append(p)
    H = 0
    imgs = []
    for p in paths:
        name = os.path.basename(p)
        f = w16ttf.parse(open(p, 'rb').read())
        title = f['name'].get((3, 1, 0x409, 4), name)
        img = Image.new('L', (W, 1240), 255)
        d = ImageDraw.Draw(img)
        label = ImageFont.truetype(p, 18)
        d.text((20, 10), '%s  (%s)' % (title, name), font=label, fill=0)
        y = 44
        big = ImageFont.truetype(p, 96)
        d.text((20, y), 'Hamburgefonstiv 0123', font=big, fill=0)
        y += 130
        for size in (11, 13, 16, 20, 28):
            fo = ImageFont.truetype(p, size)
            for line in SAMPLE:
                d.text((20, y), line, font=fo, fill=0)
                y += int(size * 1.25)
            y += 6
        fo = ImageFont.truetype(p, 15)
        words = TEXT.split()
        line = ''
        for wd in words:
            t = (line + ' ' + wd).strip()
            if d.textlength(t, font=fo) > W - 40:
                d.text((20, y), line, font=fo, fill=0)
                y += 20
                line = wd
            else:
                line = t
        d.text((20, y), line, font=fo, fill=0)
        y += 30
        imgs.append(img.crop((0, 0, W, y)))
        H += y
    sheet = Image.new('L', (W, H), 255)
    y = 0
    for im in imgs:
        sheet.paste(im, (0, y))
        y += im.size[1]
    sheet.save(out)


def glyph_sheet(path, out, chars=None, size=150, cols=10):
    from PIL import Image, ImageDraw, ImageFont
    fo = ImageFont.truetype(path, size)
    lab = ImageFont.load_default()
    chars = chars or ''.join(repertoire())
    cw, chh = int(size * 1.15), int(size * 1.45)
    rows = (len(chars) + cols - 1) // cols
    img = Image.new('L', (cw * cols + 10, chh * rows + 10), 255)
    d = ImageDraw.Draw(img)
    for i, ch in enumerate(chars):
        x = 5 + (i % cols) * cw
        y = 5 + (i // cols) * chh
        base = y + int(size * 1.05)
        d.line((x, base, x + cw - 4, base), fill=200)
        d.text((x + int(size * 0.1), y + int(size * 0.1)), ch, font=fo, fill=0)
        d.text((x + 2, y + 2), 'U+%04X' % ord(ch), font=lab, fill=120)
    img.save(out)


# ------------------------------------------------------------------ main


def main():
    ap = argparse.ArgumentParser(description='build w16\'s TrueType fonts')
    ap.add_argument('--ref', default=REF)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'ttfonts'))
    ap.add_argument('--check', metavar='DIR')
    ap.add_argument('--specimen', nargs=2, metavar=('DIR', 'OUTDIR'))
    ap.add_argument('--glyphs', nargs='+', metavar='ARG')
    ap.add_argument('faces', nargs='*')
    a = ap.parse_args()
    if a.glyphs:
        face, out = a.glyphs[0], a.glyphs[1]
        glyph_sheet(os.path.join(a.out, face + '.TTF'), out, a.glyphs[2] if len(a.glyphs) > 2 else None)
        return 0
    if a.specimen:
        src, outdir = a.specimen
        os.makedirs(outdir, exist_ok=True)
        groups = {}
        for spec in FACES:
            groups.setdefault(spec[1], []).append(os.path.join(src, spec[0] + '.TTF'))
        for fam, paths in groups.items():
            specimen(paths, os.path.join(outdir, fam.replace(' ', '-') + '.png'))
        return 0
    ref = parse_ref(a.ref)
    todo = [f for f in FACES if not a.faces or f[0] in a.faces]
    if a.check:
        bad = 0
        for spec in todo:
            p = os.path.join(a.check, spec[0] + '.TTF')
            errs = check_font(p, Face(spec, ref))
            print('%s: %s' % (spec[0], 'ok' if not errs else '%d problems: %s' % (len(errs), '; '.join(errs[:8]))))
            bad += bool(errs)
        return 1 if bad else 0
    os.makedirs(a.out, exist_ok=True)
    bad = 0
    for spec in todo:
        t = time.time()
        face = Face(spec, ref)
        data = face.build()
        p = os.path.join(a.out, spec[0] + '.TTF')
        with open(p, 'wb') as fh:
            fh.write(data)
        errs = check_font(p, face)
        print('%s: %d glyphs, %d bytes, %.1f s, %s' % (spec[0], len(face.font.glyphs), len(data),
                                                     time.time() - t, 'ok' if not errs else '; '.join(errs[:6])))
        bad += bool(errs)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
