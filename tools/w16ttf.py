#!/usr/bin/env python3
"""w16ttf - a small TrueType writer and reader for w16's fonts.

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).

Written from the published TrueType format (Apple's TrueType Reference
Manual and the OpenType specification's table layouts); no font tool's
code is used. Used by tools/ttgen.py (w16's TrueType fonts, ARCHITECTURE.md
6.9.3) and its checks.

Writing: build a Font (glyphs with quadratic contours, metrics, names) and
call Font.build() for the file's bytes. Only what w16 needs:

  - simple glyphs (no composites; instructions as given), loca long;
    optionally prep and cvt tables (given);
  - cmap: one (3,1) format 4 subtable (Unicode BMP);
  - OS/2 version 1, hhea, hmtx, maxp 1.0, head, name (platform 3,
    Unicode, US English), post 2.0 (glyph names), gasp 1, kern 0
    (when pairs are given), hdmx (when device widths are given), VDMX
    version 0 (when per-size ascent and descent are given); the head
    bounding box may be widened to a given box (bbox_min).

Reading: parse(bytes) returns a dict of decoded tables, check(bytes)
lists every structural problem it finds (directory, checksums,
checkSumAdjustment, maxp counts, loca, glyph bounds, hmtx, cmap, name,
post). `w16ttf.py check FILE...` prints them; `w16ttf.py dump FILE` prints
a summary.
"""
import struct
import sys

# --------------------------------------------------------------- helpers


def _pad4(b):
    return b + b'\0' * ((4 - len(b) % 4) % 4)


def checksum(b):
    b = _pad4(b)
    return sum(struct.unpack('>%dI' % (len(b) // 4), b)) & 0xFFFFFFFF


def _fixed(v):
    """16.16 fixed"""
    return int(round(v * 65536)) & 0xFFFFFFFF


class Glyph:
    """contours: list of lists of (x, y, on) with integer x, y; on True for
    on-curve points (TrueType's quadratic outlines). advance: the advance
    width in font units. name: the post table's glyph name."""

    def __init__(self, name, advance, contours=()):
        self.name = name
        self.advance = int(advance)
        self.contours = [list(c) for c in contours if len(c) >= 2]
        self.instructions = b''                 # TrueType bytecode

    def bbox(self):
        pts = [p for c in self.contours for p in c]
        if not pts:
            return (0, 0, 0, 0)
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        return (min(xs), min(ys), max(xs), max(ys))

    def npoints(self):
        return sum(len(c) for c in self.contours)

    def encode(self):
        if not self.contours:
            return b''
        x0, y0, x1, y1 = self.bbox()
        out = struct.pack('>hhhhh', len(self.contours), x0, y0, x1, y1)
        ends = []
        n = 0
        for c in self.contours:
            n += len(c)
            ends.append(n - 1)
        out += struct.pack('>%dH' % len(ends), *ends)
        out += struct.pack('>H', len(self.instructions)) + self.instructions
        flags = []
        xs = b''
        ys = b''
        px = py = 0
        for c in self.contours:
            for (x, y, on) in c:
                dx, dy = x - px, y - py
                px, py = x, y
                f = 1 if on else 0
                if dx == 0:
                    f |= 0x10                  # same x
                elif -255 <= dx <= 255:
                    f |= 0x02
                    if dx > 0:
                        f |= 0x10
                    xs += struct.pack('>B', abs(dx))
                else:
                    xs += struct.pack('>h', dx)
                if dy == 0:
                    f |= 0x20
                elif -255 <= dy <= 255:
                    f |= 0x04
                    if dy > 0:
                        f |= 0x20
                    ys += struct.pack('>B', abs(dy))
                else:
                    ys += struct.pack('>h', dy)
                flags.append(f)
        fb = b''
        i = 0
        while i < len(flags):
            f = flags[i]
            r = 0
            while i + 1 + r < len(flags) and flags[i + 1 + r] == f and r < 255:
                r += 1
            if r >= 1:
                fb += struct.pack('>BB', f | 0x08, r)
            else:
                fb += struct.pack('>B', f)
            i += 1 + r
        return out + fb + xs + ys


class Font:
    def __init__(self):
        self.glyphs = []          # glyph 0 must be .notdef
        self.cmap = {}            # Unicode -> glyph index
        self.upem = 2048
        self.names = {}           # name ID -> string (platform 3, 0x409)
        self.weight = 400
        self.bold = False
        self.italic = False
        self.fixed_pitch = False
        self.italic_angle = 0.0   # degrees, negative leans right
        self.caret = (1, 0)       # hhea caretSlopeRise, Run
        self.hhea = (0, 0, 0)     # ascender, descender, lineGap
        self.typo = (0, 0, 0)
        self.win = (0, 0)         # usWinAscent, usWinDescent
        self.avg_width = None     # OS/2 xAvgCharWidth (None: computed)
        self.panose = bytes(10)
        self.family_class = 0
        self.fs_type = 0
        self.vendor = b'W16 '
        self.subscript = (1434, 1331, 0, 287)    # xSize, ySize, xOff, yOff
        self.superscript = (1434, 1331, 0, 977)
        self.strikeout = (102, 530)              # size, position
        self.underline = (-217, 150)             # post position, thickness
        self.unicode_ranges = (0, 0, 0, 0)
        self.codepages = (1, 0)
        self.first_char = None    # OS/2 usFirstCharIndex (None: from cmap)
        self.last_char = None
        self.lowest_ppem = 8
        self.head_flags = 0x000B
        self.revision = 1.0
        self.created = 0          # seconds since 1904-01-01
        self.kern = {}            # (left glyph, right glyph) -> value
        self.hdmx = {}            # ppem -> list of device widths per glyph
        self.gasp = [(0xFFFF, 0x000F)]
        self.vdmx = {}            # ppem -> (yMax, yMin)
        self.bbox_min = None      # head bbox at least this (x0, y0, x1, y1)
        self.typo_metrics = False
        self.prep = b''            # control value program (bytecode)
        self.cvt = []              # control values (FWORDs)
        self.fpgm = b''            # font program
        self.max_stack = 32

    # ------------------------------------------------------------ tables
    def _head(self, bbox, loca_long):
        x0, y0, x1, y1 = bbox
        style = (1 if self.bold else 0) | (2 if self.italic else 0)
        return struct.pack('>IIIIHHqqhhhhHHhhh',
                           0x00010000, _fixed(self.revision), 0, 0x5F0F3CF5,
                           self.head_flags, self.upem, self.created, self.created,
                           x0, y0, x1, y1, style, self.lowest_ppem, 2,
                           1 if loca_long else 0, 0)

    def _hhea(self, nh):
        adv = [g.advance for g in self.glyphs]
        lsbs, rsbs, ext = [], [], []
        for g in self.glyphs:
            if g.contours:
                x0, _, x1, _ = g.bbox()
                lsbs.append(x0)
                rsbs.append(g.advance - x1)
                ext.append(x1)
        a, d, lg = self.hhea
        return struct.pack('>IhhhHhhhhhhhhhhhH', 0x00010000, a, d, lg, max(adv),
                           min(lsbs or [0]), min(rsbs or [0]), max(ext or [0]),
                           self.caret[0], self.caret[1], 0, 0, 0, 0, 0, 0, nh)

    def _maxp(self):
        return struct.pack('>IHHHHHHHHHHHHHH', 0x00010000, len(self.glyphs),
                           max(g.npoints() for g in self.glyphs),
                           max(len(g.contours) for g in self.glyphs),
                           0, 0, 2, 0, 0, 2 if self.fpgm else 0, 0, self.max_stack,
                           max(len(g.instructions) for g in self.glyphs), 0, 0)

    def _os2(self):
        if self.avg_width is not None:
            avg = self.avg_width
        else:
            ws = [g.advance for g in self.glyphs if g.advance]
            avg = int(round(sum(ws) / len(ws)))
        style = 0
        if self.italic:
            style |= 1
        if self.bold:
            style |= 0x20
        if not (self.italic or self.bold):
            style |= 0x40
        if self.typo_metrics:
            style |= 0x80
        codes = sorted(self.cmap)
        first = self.first_char if self.first_char is not None else min(codes[0], 0xFFFF)
        last = self.last_char if self.last_char is not None else min(codes[-1], 0xFFFF)
        ta, td, tl = self.typo
        sub, sup = self.subscript, self.superscript
        return struct.pack('>HhHHH' + 'h' * 11 + '10sIIII4sHHHhhhHHII',
                           1, avg, self.weight, 5, self.fs_type,
                           sub[0], sub[1], sub[2], sub[3],
                           sup[0], sup[1], sup[2], sup[3],
                           self.strikeout[0], self.strikeout[1], self.family_class,
                           self.panose, *self.unicode_ranges, self.vendor, style,
                           first, last, ta, td, tl, self.win[0], self.win[1],
                           *self.codepages)

    def _hmtx(self):
        # every glyph gets a long metric (no trailing run of equal widths)
        out = b''
        for g in self.glyphs:
            out += struct.pack('>Hh', g.advance, g.bbox()[0])
        return out, len(self.glyphs)

    def _cmap(self):
        codes = sorted(c for c in self.cmap if c < 0xFFFF)
        # segments of consecutive codes
        segs = []
        for c in codes:
            if segs and segs[-1][1] == c - 1:
                segs[-1][1] = c
            else:
                segs.append([c, c])
        segs.append([0xFFFF, 0xFFFF])
        n = len(segs)
        ends, starts, deltas, offs = [], [], [], []
        glyph_ids = []
        for i, (s, e) in enumerate(segs):
            starts.append(s)
            ends.append(e)
            if s == 0xFFFF:
                deltas.append(1)
                offs.append(0)
                continue
            gids = [self.cmap[c] for c in range(s, e + 1)]
            if all(gids[k] - gids[0] == k for k in range(len(gids))):
                deltas.append((gids[0] - s) & 0xFFFF)
                offs.append(0)
            else:
                deltas.append(0)
                # offset from this idRangeOffset entry to the glyph id
                offs.append(2 * (n - i) + 2 * len(glyph_ids))
                glyph_ids.extend(gids)
        p = 1
        lg = 0
        while p * 2 <= n:
            p *= 2
            lg += 1
        search = 2 * p
        body = struct.pack('>HHHH', 2 * n, search, lg, 2 * n - search)
        body += struct.pack('>%dH' % n, *ends) + b'\0\0'
        body += struct.pack('>%dH' % n, *starts)
        body += struct.pack('>%dH' % n, *deltas)
        body += struct.pack('>%dH' % n, *offs)
        body += struct.pack('>%dH' % len(glyph_ids), *glyph_ids)
        sub = struct.pack('>HHH', 4, 6 + len(body), 0) + body
        return struct.pack('>HHHHI', 0, 1, 3, 1, 12) + sub

    def _name(self):
        recs = []
        data = b''
        for nid in sorted(self.names):
            s = self.names[nid].encode('utf-16-be')
            recs.append((3, 1, 0x409, nid, len(s), len(data)))
            data += s
        out = struct.pack('>HHH', 0, len(recs), 6 + 12 * len(recs))
        for r in recs:
            out += struct.pack('>HHHHHH', *r)
        return out + data

    def _post(self):
        std = mac_glyph_names()
        idx = []
        extra = []
        for g in self.glyphs:
            if g.name in std:
                idx.append(std.index(g.name))
            else:
                idx.append(258 + len(extra))
                extra.append(g.name)
        ul_pos, ul_th = self.underline
        out = struct.pack('>IIhhIIIII', 0x00020000, _fixed(self.italic_angle),
                          ul_pos, ul_th, 1 if self.fixed_pitch else 0, 0, 0, 0, 0)
        out += struct.pack('>H', len(self.glyphs))
        out += struct.pack('>%dH' % len(idx), *idx)
        for n in extra:
            b = n.encode('ascii')
            out += struct.pack('>B', len(b)) + b
        return out

    def _gasp(self):
        out = struct.pack('>HH', 1, len(self.gasp))
        for ppem, fl in self.gasp:
            out += struct.pack('>HH', ppem, fl)
        return out

    def _kern(self):
        pairs = sorted(self.kern.items())
        n = len(pairs)
        p = 1
        lg = 0
        while p * 2 <= n:
            p *= 2
            lg += 1
        sub = struct.pack('>HHHH', n, 6 * p, lg, 6 * n - 6 * p)
        for (l, r), v in pairs:
            sub += struct.pack('>HHh', l, r, v)
        sub = struct.pack('>HHH', 0, 6 + len(sub), 0x0001) + sub
        return struct.pack('>HH', 0, 1) + sub

    def _hdmx(self):
        ng = len(self.glyphs)
        rec = ng + 2
        rec += (4 - rec % 4) % 4
        out = struct.pack('>HhI', 0, len(self.hdmx), rec)
        for ppem in sorted(self.hdmx):
            ws = self.hdmx[ppem]
            body = struct.pack('>BB', ppem, max(ws)) + bytes(ws)
            out += body + b'\0' * (rec - len(body))
        return out

    def _vdmx(self):
        """version 0: one group, for the 1:1 aspect ratio with the Windows
        ANSI character set (bCharSet 1; Windows 3.1 ignores the table
        with bCharSet 0 and a 0:0:0 ratio), and the same group again for
        0:0:0 (any ratio)"""
        recs = sorted(self.vdmx.items())
        group = struct.pack('>HBB', len(recs), recs[0][0], recs[-1][0])
        for ppem, (ymax, ymin) in recs:
            group += struct.pack('>Hhh', ppem, ymax, ymin)
        head = struct.pack('>HHH', 0, 1, 2) + struct.pack('>BBBB', 1, 1, 1, 1) + struct.pack('>BBBB', 1, 0, 0, 0)
        off = len(head) + 4
        return head + struct.pack('>HH', off, off) + group

    def build(self):
        glyf = b''
        loca = []
        for g in self.glyphs:
            loca.append(len(glyf))
            glyf += _pad4(g.encode())
        loca.append(len(glyf))
        bbs = [g.bbox() for g in self.glyphs if g.contours]
        bbox = (min(b[0] for b in bbs), min(b[1] for b in bbs),
                max(b[2] for b in bbs), max(b[3] for b in bbs))
        if self.bbox_min:
            m = self.bbox_min
            bbox = (min(bbox[0], m[0]), min(bbox[1], m[1]), max(bbox[2], m[2]), max(bbox[3], m[3]))
        hmtx, nh = self._hmtx()
        tables = {
            b'head': self._head(bbox, True),
            b'hhea': self._hhea(nh),
            b'maxp': self._maxp(),
            b'OS/2': self._os2(),
            b'hmtx': hmtx,
            b'cmap': self._cmap(),
            b'loca': struct.pack('>%dI' % len(loca), *loca),
            b'glyf': glyf,
            b'name': self._name(),
            b'post': self._post(),
        }
        if self.gasp:
            tables[b'gasp'] = self._gasp()
        if self.kern:
            tables[b'kern'] = self._kern()
        if self.hdmx:
            tables[b'hdmx'] = self._hdmx()
        if self.vdmx:
            tables[b'VDMX'] = self._vdmx()
        if self.prep:
            tables[b'prep'] = self.prep
        if self.fpgm:
            tables[b'fpgm'] = self.fpgm
        if self.cvt:
            tables[b'cvt '] = struct.pack('>%dh' % len(self.cvt), *self.cvt)
        return assemble(tables)


def assemble(tables):
    tags = sorted(tables)
    n = len(tags)
    p = 1
    lg = 0
    while p * 2 <= n:
        p *= 2
        lg += 1
    head = struct.pack('>IHHHH', 0x00010000, n, 16 * p, lg, 16 * n - 16 * p)
    off = 12 + 16 * n
    dirs = b''
    body = b''
    for t in tags:
        d = tables[t]
        if t == b'head':
            d = d[:8] + b'\0\0\0\0' + d[12:]     # checkSumAdjustment set below
        dirs += struct.pack('>4sIII', t, checksum(d), off + len(body), len(d))
        body += _pad4(d)
    font = bytearray(head + dirs + body)
    adj = (0xB1B0AFBA - checksum(bytes(font))) & 0xFFFFFFFF
    # head.checkSumAdjustment is at offset 8 in head
    for t, _, o, _ in _dir(bytes(font)):
        if t == b'head':
            struct.pack_into('>I', font, o + 8, adj)
    return bytes(font)


def _dir(b):
    n = struct.unpack_from('>H', b, 4)[0]
    return [struct.unpack_from('>4sIII', b, 12 + 16 * i) for i in range(n)]


_MAC = None


def mac_glyph_names():
    """the 258 standard Macintosh glyph names (post format 2's first 258
    indexes; a published list)"""
    global _MAC
    if _MAC is None:
        _MAC = ('.notdef .null nonmarkingreturn space exclam quotedbl numbersign '
                'dollar percent ampersand quotesingle parenleft parenright asterisk '
                'plus comma hyphen period slash zero one two three four five six '
                'seven eight nine colon semicolon less equal greater question at '
                'A B C D E F G H I J K L M N O P Q R S T U V W X Y Z bracketleft '
                'backslash bracketright asciicircum underscore grave a b c d e f g '
                'h i j k l m n o p q r s t u v w x y z braceleft bar braceright '
                'asciitilde Adieresis Aring Ccedilla Eacute Ntilde Odieresis '
                'Udieresis aacute agrave acircumflex adieresis atilde aring '
                'ccedilla eacute egrave ecircumflex edieresis iacute igrave '
                'icircumflex idieresis ntilde oacute ograve ocircumflex odieresis '
                'otilde uacute ugrave ucircumflex udieresis dagger degree cent '
                'sterling section bullet paragraph germandbls registered copyright '
                'trademark acute dieresis notequal AE Oslash infinity plusminus '
                'lessequal greaterequal yen mu partialdiff summation product pi '
                'integral ordfeminine ordmasculine Omega ae oslash questiondown '
                'exclamdown logicalnot radical florin approxequal Delta '
                'guillemotleft guillemotright ellipsis nonbreakingspace Agrave '
                'Atilde Otilde OE oe endash emdash quotedblleft quotedblright '
                'quoteleft quoteright divide lozenge ydieresis Ydieresis fraction '
                'currency guilsinglleft guilsinglright fi fl daggerdbl '
                'periodcentered quotesinglbase quotedblbase perthousand '
                'Acircumflex Ecircumflex Aacute Edieresis Egrave Iacute '
                'Icircumflex Idieresis Igrave Oacute Ocircumflex apple Ograve '
                'Uacute Ucircumflex Ugrave dotlessi circumflex tilde macron breve '
                'dotaccent ring cedilla hungarumlaut ogonek caron Lslash lslash '
                'Scaron scaron Zcaron zcaron brokenbar Eth eth Yacute yacute '
                'Thorn thorn minus multiply onesuperior twosuperior threesuperior '
                'onehalf onequarter threequarters franc Gbreve gbreve Idotaccent '
                'Scedilla scedilla Cacute cacute Ccaron ccaron dcroat').split()
        assert len(_MAC) == 258, len(_MAC)
    return _MAC


# --------------------------------------------------------------- reading


def parse(b):
    t = {}
    for tag, cs, off, ln in _dir(b):
        t[tag.decode('latin-1')] = (off, ln, cs)
    out = {'_dir': t}
    g = lambda tag: b[t[tag][0]:t[tag][0] + t[tag][1]]
    h = g('head')
    out['head'] = dict(zip(
        'version revision adj magic flags upem created modified xmin ymin xmax ymax '
        'macstyle lowestppem dirhint locafmt glyfmt'.split(),
        struct.unpack('>IIIIHHqqhhhhHHhhh', h[:54])))
    m = g('maxp')
    out['maxp'] = dict(zip('version numglyphs maxpoints maxcontours'.split(),
                           struct.unpack('>IHHH', m[:10])))
    hh = g('hhea')
    out['hhea'] = dict(zip(
        'version asc desc gap advmax minlsb minrsb maxext rise run off r1 r2 r3 r4 '
        'fmt nhm'.split(), struct.unpack('>IhhhHhhhhhhhhhhhH', hh[:36])))
    ng = out['maxp']['numglyphs']
    nh = out['hhea']['nhm']
    mt = g('hmtx')
    adv = []
    lsb = []
    for i in range(ng):
        if i < nh:
            a, l = struct.unpack_from('>Hh', mt, 4 * i)
        else:
            a = adv[-1]
            l = struct.unpack_from('>h', mt, 4 * nh + 2 * (i - nh))[0]
        adv.append(a)
        lsb.append(l)
    out['advance'] = adv
    out['lsb'] = lsb
    lo = g('loca')
    if out['head']['locafmt']:
        loca = list(struct.unpack('>%dI' % (ng + 1), lo[:4 * (ng + 1)]))
    else:
        loca = [2 * v for v in struct.unpack('>%dH' % (ng + 1), lo[:2 * (ng + 1)])]
    out['loca'] = loca
    gl = g('glyf')
    glyphs = []
    for i in range(ng):
        d = gl[loca[i]:loca[i + 1]]
        glyphs.append(decode_glyph(d))
    out['glyphs'] = glyphs
    out['cmap'] = decode_cmap(g('cmap'))
    out['name'] = decode_name(g('name'))
    out['post'] = decode_post(g('post'), ng)
    o = g('OS/2')
    out['os2'] = dict(zip(
        'version avg weight width fstype subx suby subdx subdy supx supy supdx supdy '
        'strsize strpos family'.split(), struct.unpack('>HhHHHhhhhhhhhhhh', o[:32])))
    out['os2']['panose'] = o[32:42]
    out['os2'].update(dict(zip(
        'ur1 ur2 ur3 ur4 vendor sel first last tasc tdesc tgap wasc wdesc'.split(),
        struct.unpack('>IIII4sHHHhhhHH', o[42:78]))))
    if 'kern' in t:
        k = g('kern')
        n = struct.unpack_from('>H', k, 10)[0]
        out['kern'] = {}
        for i in range(n):
            l, r, v = struct.unpack_from('>HHh', k, 18 + 6 * i)
            out['kern'][(l, r)] = v
    if 'hdmx' in t:
        d = g('hdmx')
        _, nrec, size = struct.unpack_from('>HhI', d, 0)
        out['hdmx'] = {}
        for i in range(nrec):
            o0 = 8 + i * size
            out['hdmx'][d[o0]] = list(d[o0 + 2:o0 + 2 + ng])
    return out


def decode_glyph(d):
    if not d:
        return {'contours': [], 'bbox': (0, 0, 0, 0)}
    nc, x0, y0, x1, y1 = struct.unpack_from('>hhhhh', d, 0)
    if nc < 0:
        return {'composite': True, 'contours': [], 'bbox': (x0, y0, x1, y1)}
    ends = struct.unpack_from('>%dH' % nc, d, 10)
    p = 10 + 2 * nc
    il = struct.unpack_from('>H', d, p)[0]
    p += 2 + il
    n = ends[-1] + 1 if nc else 0
    flags = []
    while len(flags) < n:
        f = d[p]
        p += 1
        flags.append(f)
        if f & 8:
            r = d[p]
            p += 1
            flags.extend([f] * r)
    def coords(short, same):
        nonlocal p
        v = 0
        out = []
        for f in flags:
            if f & short:
                dv = d[p]
                p += 1
                v += dv if f & same else -dv
            elif not f & same:
                v += struct.unpack_from('>h', d, p)[0]
                p += 2
            out.append(v)
        return out
    xs = coords(2, 0x10)
    ys = coords(4, 0x20)
    cs = []
    s = 0
    for e in ends:
        cs.append([(xs[i], ys[i], bool(flags[i] & 1)) for i in range(s, e + 1)])
        s = e + 1
    return {'contours': cs, 'bbox': (x0, y0, x1, y1), 'instructions': il}


def decode_cmap(c):
    ver, n = struct.unpack_from('>HH', c, 0)
    out = {}
    for i in range(n):
        pid, eid, off = struct.unpack_from('>HHI', c, 4 + 8 * i)
        fmt = struct.unpack_from('>H', c, off)[0]
        m = {}
        if fmt == 4:
            segx2 = struct.unpack_from('>H', c, off + 6)[0]
            ns = segx2 // 2
            ends = struct.unpack_from('>%dH' % ns, c, off + 14)
            starts = struct.unpack_from('>%dH' % ns, c, off + 16 + segx2)
            deltas = struct.unpack_from('>%dH' % ns, c, off + 16 + 2 * segx2)
            ro = off + 16 + 3 * segx2
            offs = struct.unpack_from('>%dH' % ns, c, ro)
            for k in range(ns):
                for code in range(starts[k], ends[k] + 1):
                    if code == 0xFFFF:
                        continue
                    if offs[k] == 0:
                        gid = (code + deltas[k]) & 0xFFFF
                    else:
                        a = ro + 2 * k + offs[k] + 2 * (code - starts[k])
                        gid = struct.unpack_from('>H', c, a)[0]
                        if gid:
                            gid = (gid + deltas[k]) & 0xFFFF
                    if gid:
                        m[code] = gid
        out[(pid, eid, fmt)] = m
    return out


def decode_name(b):
    fmt, n, so = struct.unpack_from('>HHH', b, 0)
    out = {}
    for i in range(n):
        pid, eid, lid, nid, ln, off = struct.unpack_from('>HHHHHH', b, 6 + 12 * i)
        s = b[so + off:so + off + ln]
        out[(pid, eid, lid, nid)] = s.decode('utf-16-be') if pid in (0, 3) else s.decode('latin-1')
    return out


def decode_post(b, ng):
    ver, ang, up, ut, fixed = struct.unpack_from('>IihhI', b, 0)
    out = {'version': ver, 'italic_angle': ang / 65536.0, 'ul_pos': up,
           'ul_thick': ut, 'fixed': fixed}
    if ver == 0x00020000:
        n = struct.unpack_from('>H', b, 32)[0]
        idx = struct.unpack_from('>%dH' % n, b, 34)
        p = 34 + 2 * n
        extra = []
        while p < len(b):
            ln = b[p]
            extra.append(b[p + 1:p + 1 + ln].decode('latin-1'))
            p += 1 + ln
        std = mac_glyph_names()
        out['names'] = [std[i] if i < 258 else extra[i - 258] for i in idx]
    return out


def signed_area(c):
    a = 0
    for i in range(len(c)):
        x0, y0 = c[i][0], c[i][1]
        x1, y1 = c[i - 1][0], c[i - 1][1]
        a += (x1 - x0) * (y1 + y0)
    return a / 2.0


def check(b, expect_glyphs=None):
    """structural checks; returns a list of problem strings"""
    errs = []
    try:
        n = struct.unpack_from('>H', b, 4)[0]
        dirs = _dir(b)
    except struct.error:
        return ['truncated directory']
    if struct.unpack_from('>I', b, 0)[0] != 0x00010000:
        errs.append('sfnt version is not 1.0')
    tags = [d[0] for d in dirs]
    if tags != sorted(tags):
        errs.append('table directory not sorted')
    for need in (b'cmap', b'glyf', b'head', b'hhea', b'hmtx', b'loca', b'maxp',
                 b'name', b'post', b'OS/2'):
        if need not in tags:
            errs.append('missing table ' + need.decode())
    for tag, cs, off, ln in dirs:
        if off % 4:
            errs.append('%s not 4-byte aligned' % tag)
        if off + ln > len(b):
            errs.append('%s past end of file' % tag)
            continue
        d = bytearray(b[off:off + ln])
        if tag == b'head':
            d[8:12] = b'\0\0\0\0'
        if checksum(bytes(d)) != cs:
            errs.append('%s checksum wrong' % tag.decode())
    if checksum(b) != 0xB1B0AFBA:
        errs.append('checkSumAdjustment wrong')
    if errs:
        return errs
    f = parse(b)
    hd = f['head']
    if hd['magic'] != 0x5F0F3CF5:
        errs.append('head magic')
    ng = f['maxp']['numglyphs']
    if expect_glyphs is not None and ng != expect_glyphs:
        errs.append('numGlyphs %d, expected %d' % (ng, expect_glyphs))
    loca = f['loca']
    if any(loca[i] > loca[i + 1] for i in range(ng)):
        errs.append('loca not ascending')
    if loca[-1] != f['_dir']['glyf'][1]:
        errs.append('loca end %d != glyf length %d' % (loca[-1], f['_dir']['glyf'][1]))
    mp = mc = 0
    bx = [99999, 99999, -99999, -99999]
    for i, g in enumerate(f['glyphs']):
        if g.get('composite'):
            errs.append('glyph %d is composite' % i)
            continue
        cs = g['contours']
        pts = [p for c in cs for p in c]
        mp = max(mp, len(pts))
        mc = max(mc, len(cs))
        if pts:
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            real = (min(xs), min(ys), max(xs), max(ys))
            if real != g['bbox']:
                errs.append('glyph %d bbox %s, points give %s' % (i, g['bbox'], real))
            if f['lsb'][i] != real[0]:
                errs.append('glyph %d lsb %d != xMin %d' % (i, f['lsb'][i], real[0]))
            bx = [min(bx[0], real[0]), min(bx[1], real[1]), max(bx[2], real[2]), max(bx[3], real[3])]
            for c in cs:
                if len(c) < 3:
                    errs.append('glyph %d has a contour of %d points' % (i, len(c)))
    if hd['xmin'] > bx[0] or hd['ymin'] > bx[1] or hd['xmax'] < bx[2] or hd['ymax'] < bx[3]:
        errs.append('head bbox %s does not hold the glyphs %s' % ((hd['xmin'], hd['ymin'], hd['xmax'], hd['ymax']), bx))
    if f['maxp']['maxpoints'] != mp or f['maxp']['maxcontours'] != mc:
        errs.append('maxp points/contours %d/%d, glyphs %d/%d' % (
            f['maxp']['maxpoints'], f['maxp']['maxcontours'], mp, mc))
    if f['hhea']['advmax'] != max(f['advance']):
        errs.append('hhea advanceWidthMax')
    if f['glyphs'][0]['contours'] == [] and f['post'].get('names', ['.notdef'])[0] != '.notdef':
        errs.append('glyph 0 is not .notdef')
    if (3, 1, 4) not in f['cmap']:
        errs.append('no (3,1) format 4 cmap')
    else:
        for code, gid in f['cmap'][(3, 1, 4)].items():
            if gid >= ng:
                errs.append('cmap U+%04X -> glyph %d out of range' % (code, gid))
    for nid in (1, 2, 3, 4, 5, 6):
        if (3, 1, 0x409, nid) not in f['name']:
            errs.append('name ID %d missing' % nid)
    if f['post'].get('names') and len(f['post']['names']) != ng:
        errs.append('post names count')
    return errs


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'check':
        bad = 0
        for fn in sys.argv[2:]:
            e = check(open(fn, 'rb').read())
            print('%s: %s' % (fn, 'ok' if not e else '; '.join(e)))
            bad += bool(e)
        sys.exit(1 if bad else 0)
    if len(sys.argv) == 3 and sys.argv[1] == 'dump':
        f = parse(open(sys.argv[2], 'rb').read())
        for k in ('head', 'hhea', 'maxp', 'os2'):
            print(k, f[k])
        print('post', {k: v for k, v in f['post'].items() if k != 'names'})
        for k, v in sorted(f['name'].items()):
            print('name', k, v)
        print('cmap', {k: len(v) for k, v in f['cmap'].items()})
        return
    print(__doc__)
    sys.exit(2)


if __name__ == '__main__':
    main()
