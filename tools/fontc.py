#!/usr/bin/env python3
"""fontc - w16's raster font compiler (ARCHITECTURE.md 6.9).

Compiles text glyph sources (fonts/*.txt, drawn by hand as '#'/'.' grids)
into Windows .FNT raster fonts and packages them as .FON files: resource-
only NE DLLs with a font directory (RT_FONTDIR), the fonts (RT_FONT) and a
version resource (RT_VERSION).

    fontc.py fnt SRC OUT.FNT
    fontc.py fon OUT.FON --module NAME --desc TEXT [--file NAME]
                 [--info KEY=VALUE ...] ID=SRC [ID=SRC ...]
    fontc.py check SRC...          parse and check sources only

Formats: the published Windows 3.x font file format (.FNT; SDK "Font
Files", versions 2.0 and 3.0) and the NE executable format; the .FON
layout (module and description names, resource flags and IDs, the font
directory being each font's first 113 header bytes followed by the device
and face names) was observed from the header fields of Windows 3.1's own
.FON files with tools/fonhdr.py. No glyph data was ever read from them.

Limits are the formats' own (ZOI): a version 2.0 font must fit 64 KB
(16-bit glyph offsets), a version 3.0 font 4 GB; character codes are 0-255
(byte-sized dfFirstChar/dfLastChar); dfCopyright holds 60 bytes; an NE
resource's offset and length are 16-bit counts of 2^shift-byte units, so
fontc picks the smallest alignment shift that fits the file.

Source format (one font per file):

    ; comment                          anywhere; also after a value
    face System                        header lines: 'key value'
    points 10
    ...
    widths 32: 4 4 6 8 ...             expected advance widths (optional),
                                       from the code given, checked
    piece grave_cap                    a named part for composed glyphs:
    ..#..                              a grid of any width, 'height' rows
    ...
    char 0x41 A                        a glyph: code (decimal, 0x.., or a
    ..##..                             quoted 'c'), then 'height' rows; the
    .#..#.                             row length is the advance width
    ...
    char 0xC0 = 0x41 + grave_cap       composed: a base glyph with pieces
    char 0xC5 = 0x41 + ring@1,-1         or other glyphs ORed over it,
                                       centred, moved by @dx[,dy]
    char 0x80 = 0x7F                   a copy
    char 0xEC = dotless + grave        the base may be a piece too

Header keys: face, version (2 or 3), copyright, type, points, vertres,
horizres, height (dfPixHeight), ascent, internal-leading, external-leading,
italic, underline, strikeout, weight, charset, pitch-family, avgwidth,
maxwidth (checked), first, last, default and break (absolute codes; the
file stores them relative to first), sentinel-width (width of the extra
character-table entry after the last character), device.
"""
import argparse
import os
import re
import struct
import sys

HDR2 = 118          # .FNT header size, version 2.0 (with dfReserved)
HDR3 = 148          # version 3.0 (dfFlags ... dfReserved1[16])
INT_KEYS = {
    'version': 2, 'type': 0, 'points': None, 'vertres': 96, 'horizres': 96,
    'height': None, 'ascent': None, 'internal-leading': 0,
    'external-leading': 0, 'italic': 0, 'underline': 0, 'strikeout': 0,
    'weight': 400, 'charset': 0, 'pitch-family': None, 'avgwidth': None,
    'maxwidth': None, 'first': 32, 'last': 255, 'default': None,
    'break': 32, 'sentinel-width': None,
}
STR_KEYS = {'face': None, 'copyright': '', 'device': ''}


class FontError(Exception):
    pass


def num(s):
    s = s.strip()
    if len(s) == 3 and s[0] == s[2] == "'":
        return ord(s[1])
    return int(s, 0)


class Glyph:
    def __init__(self, rows):
        self.rows = rows            # list of strings of '#'/'.'

    @property
    def width(self):
        return len(self.rows[0]) if self.rows else 0


class Font:
    """A parsed source file."""

    def __init__(self, path):
        self.path = path
        self.hdr = dict(INT_KEYS)
        self.hdr.update(STR_KEYS)
        self.glyphs = {}            # code -> Glyph
        self.pieces = {}            # name -> Glyph
        self.widths = {}            # code -> expected width
        self.parse()

    def err(self, line, msg):
        raise FontError('%s:%d: %s' % (self.path, line, msg))

    def parse(self):
        lines = open(self.path, encoding='utf-8').read().split('\n')
        compose = []                # (line, code, base, [(name, dx, dy)])
        i = 0
        n = len(lines)
        while i < n:
            raw = lines[i]
            ln = i + 1
            i += 1
            line = raw.split(';', 1)[0].strip()
            if not line:
                continue
            if re.fullmatch(r'[#.]+', line):
                self.err(ln, 'grid row outside a glyph (wrong number of rows above?)')
            word, _, rest = line.partition(' ')
            rest = rest.strip()
            if word in ('char', 'piece'):
                m = re.match(r"(\S+)\s*(.*)$", rest)
                if not m:
                    self.err(ln, 'char/piece needs a code or name')
                key, tail = m.group(1), m.group(2)
                if word == 'char':
                    try:
                        code = num(key)
                    except ValueError:
                        self.err(ln, 'bad character code %r' % key)
                    if not 0 <= code <= 255:
                        self.err(ln, 'character codes are 0-255 (format limit)')
                    if code in self.glyphs or any(c[1] == code for c in compose):
                        self.err(ln, 'character 0x%02X defined twice' % code)
                if word == 'char' and re.match(r'=\s*\S', tail):
                    parts = [p.strip() for p in tail[1:].split('+')]
                    try:
                        base = num(parts[0])
                    except ValueError:
                        base = parts[0]             # a piece
                    over = []
                    for p in parts[1:]:
                        m2 = re.fullmatch(r"([^@\s]+)(?:@(-?\d+)(?:,(-?\d+))?)?", p)
                        if not m2:
                            self.err(ln, 'bad part %r' % p)
                        over.append((m2.group(1), int(m2.group(2) or 0), int(m2.group(3) or 0)))
                    compose.append((ln, code, base, over))
                    continue
                h = self.hdr['height']
                if h is None:
                    self.err(ln, 'height must come before the glyphs')
                rows = []
                while len(rows) < h:
                    if i >= n:
                        self.err(ln, 'glyph ends early (%d of %d rows)' % (len(rows), h))
                    r = lines[i].split(';', 1)[0].strip()
                    i += 1
                    if not re.fullmatch(r'[#.]+', r):
                        self.err(i, 'expected a grid row of # and . (got %r)' % lines[i - 1])
                    rows.append(r)
                if len({len(r) for r in rows}) != 1:
                    self.err(ln, 'rows of different lengths')
                g = Glyph(rows)
                if word == 'char':
                    self.glyphs[code] = g
                else:
                    if key in self.pieces:
                        self.err(ln, 'piece %s defined twice' % key)
                    self.pieces[key] = g
            elif word == 'widths':
                m = re.fullmatch(r"(\S+):\s*(.*)", rest)
                if not m:
                    self.err(ln, 'widths CODE: W W ...')
                c = num(m.group(1))
                for w in m.group(2).split():
                    self.widths[c] = int(w)
                    c += 1
            elif word in INT_KEYS:
                try:
                    self.hdr[word] = num(rest)
                except ValueError:
                    self.err(ln, 'bad number %r' % rest)
            elif word in STR_KEYS:
                if len(rest) >= 2 and rest[0] == rest[-1] == '"':
                    rest = rest[1:-1]
                self.hdr[word] = rest
            else:
                self.err(ln, 'unknown key %r' % word)
        # composed glyphs, in order (a composition may use an earlier one)
        for ln, code, base, over in compose:
            if isinstance(base, str):
                if base not in self.pieces:
                    self.err(ln, 'no piece %r' % base)
                b = self.pieces[base]
            elif base not in self.glyphs:
                self.err(ln, 'base 0x%02X is not defined (yet)' % base)
            else:
                b = self.glyphs[base]
            grid = [list(r) for r in b.rows]
            for name, dx, dy in over:
                if name in self.pieces:
                    p = self.pieces[name]
                else:
                    try:
                        p = self.glyphs[num(name)]
                    except (ValueError, KeyError):
                        self.err(ln, 'no piece or glyph %r' % name)
                x0 = (b.width - p.width) // 2 + dx
                for y, row in enumerate(p.rows):
                    for x, c in enumerate(row):
                        if c != '#':
                            continue
                        yy, xx = y + dy, x0 + x
                        if not (0 <= yy < len(grid) and 0 <= xx < b.width):
                            self.err(ln, '%s falls outside the glyph' % name)
                        grid[yy][xx] = '#'
            self.glyphs[code] = Glyph([''.join(r) for r in grid])
        self.check()

    def check(self):
        h = self.hdr
        for k in ('face', 'points', 'height', 'ascent', 'pitch-family', 'avgwidth'):
            if h[k] is None:
                raise FontError('%s: %s not given' % (self.path, k))
        if h['default'] is None:
            h['default'] = h['first']
        if not 0 <= h['first'] <= h['last'] <= 255:
            raise FontError('%s: first/last out of order' % self.path)
        for k in ('default', 'break'):
            if not h['first'] <= h[k] <= h['last']:
                raise FontError('%s: %s char 0x%X outside first..last' % (self.path, k, h[k]))
        if len(h['copyright'].encode('latin-1')) > 60:
            raise FontError('%s: copyright longer than 60 bytes (format limit)' % self.path)
        missing = [c for c in range(h['first'], h['last'] + 1) if c not in self.glyphs]
        if missing:
            raise FontError('%s: no glyph for %s' % (self.path, ', '.join('0x%02X' % c for c in missing)))
        extra = [c for c in self.glyphs if not h['first'] <= c <= h['last']]
        if extra:
            raise FontError('%s: glyphs outside first..last: %s' % (self.path, extra))
        bad = []
        for c, w in sorted(self.widths.items()):
            if h['first'] <= c <= h['last'] and self.glyphs[c].width != w:
                bad.append('0x%02X is %d wide, expected %d' % (c, self.glyphs[c].width, w))
        if bad:
            raise FontError('%s: widths differ from the observed ones:\n  %s'
                            % (self.path, '\n  '.join(bad)))
        mw = max(g.width for c, g in self.glyphs.items())
        if h['maxwidth'] is not None and h['maxwidth'] != mw:
            raise FontError('%s: maxwidth %d given, widest glyph is %d' % (self.path, h['maxwidth'], mw))
        h['maxwidth'] = mw
        if h['sentinel-width'] is None:
            h['sentinel-width'] = self.glyphs[h['break']].width

    def fixed(self):
        ws = {g.width for g in self.glyphs.values()}
        return len(ws) == 1

    def fnt(self):
        """the .FNT file (bytes)"""
        h = self.hdr
        ver = h['version']
        if ver not in (2, 3):
            raise FontError('%s: version must be 2 or 3' % self.path)
        hsize = HDR2 if ver == 2 else HDR3
        esize = 4 if ver == 2 else 6
        codes = list(range(h['first'], h['last'] + 1))
        height = h['height']
        entries = [self.glyphs[c] for c in codes] + [Glyph(['.' * h['sentinel-width']] * height)]
        bits_off = hsize + esize * len(entries)
        table = b''
        bits = b''
        width_bytes = 0
        for g in entries:
            off = bits_off + len(bits)
            if ver == 2:
                if off > 0xFFFF:
                    raise FontError('%s: a version 2.0 font must fit 64 KB (16-bit '
                                    'glyph offsets, format limit); use version 3' % self.path)
                table += struct.pack('<HH', g.width, off)
            else:
                table += struct.pack('<HI', g.width, off)
            cols = (g.width + 7) // 8
            width_bytes += cols
            for cb in range(cols):
                for row in g.rows:
                    v = 0
                    for b in range(8):
                        x = cb * 8 + b
                        if x < g.width and row[x] == '#':
                            v |= 0x80 >> b
                    bits += bytes([v])
        face_off = bits_off + len(bits)
        dev = h['device'].encode('latin-1')
        dev_off = 0
        tail = b''
        if dev:
            dev_off = face_off
            tail += dev + b'\0'
            face_off += len(dev) + 1
        tail += h['face'].encode('latin-1') + b'\0'
        size = bits_off + len(bits) + len(tail)
        if ver == 2 and size > 0xFFFF + 1:
            raise FontError('%s: larger than 64 KB (version 2.0 format limit)' % self.path)
        pixwidth = entries[0].width if self.fixed() else 0
        hdr = struct.pack('<HI60sHHHHHHHBBBHBHHBHHBBBBHIIIIB',
                          ver << 8, size, h['copyright'].encode('latin-1'), h['type'],
                          h['points'], h['vertres'], h['horizres'], h['ascent'],
                          h['internal-leading'], h['external-leading'], h['italic'],
                          h['underline'], h['strikeout'], h['weight'], h['charset'],
                          pixwidth, height, h['pitch-family'], h['avgwidth'],
                          h['maxwidth'], h['first'], h['last'],
                          h['default'] - h['first'], h['break'] - h['first'],
                          width_bytes, dev_off, face_off, 0, bits_off, 0)
        assert len(hdr) == HDR2
        if ver == 3:
            # UNVERIFIED: version 3.0 follows the published layout (dfFlags
            # DFF_FIXED 1 / DFF_PROPORTIONAL 2) but no version 3.0 font has
            # been tried on Windows 3.1; the VGA set is version 2.0.
            hdr += struct.pack('<IHHHI16s', 1 if self.fixed() else 2, 0, 0, 0, 0, b'')
            assert len(hdr) == HDR3
        return hdr + table + bits + tail


# ---------------------------------------------------------------- .FON

def stub():
    """w16's own DOS stub: says what the file is and exits with 1"""
    msg = b'This is a w16 font resource file; it needs Windows.\r\n$'
    code = bytes([0x0E, 0x1F,                       # push cs / pop ds
                  0xBA, 0x0E, 0x00,                 # mov dx, msg (offset 0Eh)
                  0xB4, 0x09, 0xCD, 0x21,           # mov ah,9 / int 21h
                  0xB8, 0x01, 0x4C, 0xCD, 0x21])    # mov ax,4C01h / int 21h
    body = code + msg
    size = 0x40 + len(body)
    size = (size + 15) & ~15
    hdr = struct.pack('<2sHHHHHHHHHHHHH', b'MZ', size % 512, (size + 511) // 512, 0, 4,
                      0x20, 0xFFFF, 0, 0x200, 0, 0, 0, 0x40, 0)
    hdr = hdr.ljust(0x3C, b'\0')
    return (hdr + b'\0\0\0\0' + body).ljust(size, b'\0')


def vs_node(key, value=b'', children=b''):
    """a 16-bit VERSIONINFO block: cbBlock, cbValue, szKey, the value and
    the children, each starting on a 4-byte boundary; cbBlock does not
    count the padding after the block"""
    node = struct.pack('<HH', 0, len(value)) + key.encode('latin-1') + b'\0'
    node += b'\0' * ((-len(node)) % 4) + value
    if children:
        node += b'\0' * ((-len(node)) % 4) + children
    node = struct.pack('<H', len(node)) + node[2:]
    return node + b'\0' * ((-len(node)) % 4)


def version_resource(info):
    """VS_VERSION_INFO with a fixed part and English (US), Windows ANSI
    strings"""
    def vnum(s):
        p = [int(x) for x in (s.split('.') + ['0', '0', '0'])[:4]]
        return (p[0] << 16) | p[1], (p[2] << 16) | p[3]
    fv = vnum(info.get('FileVersion', '1.0'))
    pv = vnum(info.get('ProductVersion', info.get('FileVersion', '1.0')))
    fixed = struct.pack('<13I', 0xFEEF04BD, 0x00010000, fv[0], fv[1], pv[0], pv[1],
                        0x3F, 0, 0x00010001, 4, int(info.pop('_subtype', '1')), 0, 0)
    strings = b''
    for k, v in info.items():
        strings += vs_node(k, v.encode('latin-1') + b'\0')
    table = vs_node('040904E4', b'', strings)
    sfi = vs_node('StringFileInfo', b'', table)
    vfi = vs_node('VarFileInfo', b'', vs_node('Translation', struct.pack('<HH', 0x0409, 0x04E4)))
    return vs_node('VS_VERSION_INFO', fixed, sfi + vfi)


def fon(fonts, module, desc, info):
    """fonts: [(id, fnt bytes)]; returns the .FON file"""
    for rid, _ in fonts:
        if not 1 <= rid <= 0x7FFF:
            raise FontError('resource IDs are 1-32767 (format limit)')
    # the font directory: count, then per font its ID, its first 113
    # header bytes and its device and face names
    fdir = struct.pack('<H', len(fonts))
    for rid, f in fonts:
        dev_off, face_off = struct.unpack_from('<II', f, 101)
        dev = f[dev_off:f.index(b'\0', dev_off) + 1] if dev_off else b'\0'
        face = f[face_off:f.index(b'\0', face_off) + 1]
        fdir += struct.pack('<H', rid) + f[:113] + dev + face
    ver = version_resource(dict(info))
    # resources: (type, name-or-id, flags, data)
    res = [(0x8007, 'FONTDIR', 0x0C50, fdir)]
    res += [(0x8008, rid, 0x1C30, f) for rid, f in fonts]
    res += [(0x8010, 1, 0x0C30, ver)]

    st = stub()
    lfa = len(st)
    # NE tables after the 64-byte header: (empty segment table), resource
    # table, resident names, (empty module references, imported names),
    # entry table; then the non-resident names
    types = []
    for t, name, fl, data in res:
        if not types or types[-1][0] != t:
            types.append((t, []))
        types[-1][1].append((name, fl, data))
    rt_len = 2 + sum(8 + 12 * len(items) for _, items in types) + 2
    names = b''
    name_off = {}
    for t, items in types:
        for name, fl, data in items:
            if isinstance(name, str):
                name_off[name] = rt_len + len(names)
                names += bytes([len(name)]) + name.encode('latin-1')
    names += b'\0'
    resnames = bytes([len(module)]) + module.encode('latin-1') + b'\0\0' + b'\0'
    entry = b'\0\0'
    nonres = bytes([len(desc)]) + desc.encode('latin-1') + b'\0\0' + b'\0'
    if len(module) > 255 or len(desc) > 255:
        raise FontError('module name and description are at most 255 bytes (format limit)')
    off_rt = 0x40
    off_resn = off_rt + rt_len + len(names)
    off_modref = off_resn + len(resnames)
    off_imp = off_modref
    off_entry = off_imp
    tables_end = off_entry + len(entry)
    nonres_off = lfa + tables_end
    data_start = nonres_off + len(nonres)
    # alignment: the smallest shift whose 16-bit offset/length fields fit
    shift = 4
    while True:
        unit = 1 << shift
        pos = (data_start + unit - 1) & ~(unit - 1)
        placed = []
        ok = True
        for t, name, fl, data in res:
            ln = (len(data) + unit - 1) // unit
            if pos // unit > 0xFFFF or ln > 0xFFFF:
                ok = False
                break
            placed.append((pos, ln))
            pos += ln * unit
        if ok:
            break
        shift += 1
        if shift > 15:
            raise FontError('too large for an NE resource table (format limit)')
    rt = struct.pack('<H', shift)
    k = 0
    for t, items in types:
        rt += struct.pack('<HHI', t, len(items), 0)
        for name, fl, data in items:
            pos, ln = placed[k]
            k += 1
            rid = name_off[name] if isinstance(name, str) else (0x8000 | name)
            rt += struct.pack('<HHHHHH', pos >> shift, ln, fl, rid, 0, 0)
    rt += b'\0\0'
    assert len(rt) == rt_len
    ne = struct.pack('<2sBBHHIHHHHIIHHHHHHHHIHHHBBHHHH',
                     b'NE', 5, 1, off_entry, len(entry), 0, 0x8300, 0, 0, 0, 0, 0, 0, 0,
                     len(nonres), off_rt, off_rt, off_resn, off_modref, off_imp,
                     nonres_off, 0, shift, 0, 2, 0, 0, 0, 0, 0x030A)
    assert len(ne) == 64
    out = bytearray(st + ne + rt + names + resnames + entry + nonres)
    struct.pack_into('<I', out, 0x3C, lfa)
    for (t, name, fl, data), (pos, ln) in zip(res, placed):
        out += b'\0' * (pos - len(out))
        out += data
        out += b'\0' * (ln * (1 << shift) - len(data))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description='w16 raster font compiler')
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('fnt')
    a.add_argument('src')
    a.add_argument('out')
    a = sub.add_parser('fon')
    a.add_argument('out')
    a.add_argument('--module', required=True)
    a.add_argument('--desc', required=True)
    a.add_argument('--info', action='append', default=[], help='version string KEY=VALUE')
    a.add_argument('fonts', nargs='+', help='ID=SOURCE')
    a = sub.add_parser('check')
    a.add_argument('srcs', nargs='+')
    args = ap.parse_args()
    try:
        if args.cmd == 'fnt':
            data = Font(args.src).fnt()
            open(args.out, 'wb').write(data)
        elif args.cmd == 'check':
            for s in args.srcs:
                f = Font(s)
                print('%s: %s %dpt, %d glyphs, %d bytes' % (s, f.hdr['face'], f.hdr['points'],
                                                           len(f.glyphs), len(f.fnt())))
        else:
            fonts = []
            for spec in args.fonts:
                rid, _, src = spec.partition('=')
                fonts.append((int(rid, 0), Font(src).fnt()))
            info = {}
            for kv in args.info:
                k, _, v = kv.partition('=')
                info[k] = v
            data = fon(fonts, args.module, args.desc, info)
            tmp = args.out + '.tmp'
            open(tmp, 'wb').write(data)
            os.replace(tmp, args.out)
    except FontError as e:
        print('fontc: %s' % e, file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
