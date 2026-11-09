#!/usr/bin/env python3
"""fontspec - specimen sheets of w16's raster fonts (PNG).

Renders every character of a font, 16 to a row with the codes alongside,
and sample text, at 1x and at a larger scale. The font comes from a glyph
source (fonts/*.txt, through tools/fontc.py) or from a built .FNT/.FON.

Clean room (ARCHITECTURE.md 3, 6.9): this tool draws glyphs, so it only
accepts w16's own fonts. A .FON/.FNT whose dfCopyright does not start with
"w16" is refused: Microsoft's glyph bitmaps are never rendered.

usage: fontspec.py OUT.png SOURCE.txt|FILE.FON|FILE.FNT [--scale N]
       (a .FON with several fonts gives one sheet per font: OUT-<points>.png)
"""
import argparse
import os
import struct
import sys

from PIL import Image, ImageDraw

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fontc  # noqa: E402

SAMPLES = [
    'The quick brown fox jumps over the lazy dog. 0123456789',
    'PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS! (w16) [x] {y} <z>',
    'File  Edit  Search  Help    Program Manager    OK  Cancel',
]


class Loaded:
    """a font as code -> rows, from a .FNT image (w16's own only)"""

    def __init__(self, data, name):
        ver = struct.unpack_from('<H', data, 0)[0]
        copyright = data[6:66].split(b'\0')[0]
        if not copyright.startswith(b'w16'):
            raise SystemExit('%s: not a w16 font (dfCopyright); refusing to render it' % name)
        self.points = struct.unpack_from('<H', data, 68)[0]
        self.height = struct.unpack_from('<H', data, 88)[0]
        self.ascent = struct.unpack_from('<H', data, 74)[0]
        first, last = data[95], data[96]
        face = struct.unpack_from('<I', data, 105)[0]
        self.face = data[face:data.index(b'\0', face)].decode('latin-1')
        self.first, self.last = first, last
        self.default = first + data[97]
        hsize, esize = (118, 4) if ver < 0x300 else (148, 6)
        self.glyphs = {}
        for i, c in enumerate(range(first, last + 1)):
            p = hsize + esize * i
            if ver < 0x300:
                w, off = struct.unpack_from('<HH', data, p)
            else:
                w, off = struct.unpack_from('<HI', data, p)
            rows = []
            for y in range(self.height):
                r = ''
                for x in range(w):
                    b = data[off + (x // 8) * self.height + y]
                    r += '#' if b & (0x80 >> (x % 8)) else '.'
                rows.append(r)
            self.glyphs[c] = rows


def from_source(path):
    f = fontc.Font(path)
    o = Loaded.__new__(Loaded)
    o.points, o.height, o.ascent = f.hdr['points'], f.hdr['height'], f.hdr['ascent']
    o.face, o.first, o.last, o.default = f.hdr['face'], f.hdr['first'], f.hdr['last'], f.hdr['default']
    o.glyphs = {c: g.rows for c, g in f.glyphs.items()}
    return o


def from_file(path):
    data = open(path, 'rb').read()
    if data[:2] != b'MZ':
        return [Loaded(data, path)]
    lfa = struct.unpack_from('<I', data, 0x3C)[0]
    rt = lfa + struct.unpack_from('<H', data, lfa + 0x24)[0]
    shift = struct.unpack_from('<H', data, rt)[0]
    p = rt + 2
    out = []
    while True:
        typ, cnt = struct.unpack_from('<HH', data, p)
        if typ == 0:
            break
        p += 8
        for _ in range(cnt):
            off, ln = struct.unpack_from('<HH', data, p)
            p += 12
            if typ == 0x8008:
                out.append(Loaded(data[off << shift:(off + ln) << shift], path))
    return out


def text_width(f, s):
    return sum(len(f.glyphs.get(ord(ch), f.glyphs[f.default])[0]) for ch in s)


def draw_text(img, f, x, y, s, ink=(0, 0, 0)):
    for ch in s:
        rows = f.glyphs.get(ord(ch), f.glyphs[f.default])
        for yy, r in enumerate(rows):
            for xx, c in enumerate(r):
                if c == '#':
                    img.putpixel((x + xx, y + yy), ink)
        x += len(rows[0])
    return x


def sheet(f, scale):
    """all characters in a 16-column table, then the samples"""
    cellw = max(len(r[0]) for r in f.glyphs.values()) + 4
    cellh = f.height + 4
    label = 26
    cols = 16
    first_row = f.first // 16
    nrows = f.last // 16 - first_row + 1
    samples_h = len(SAMPLES) * (f.height + 4) + 8
    w = max(label + cols * cellw + 4, max(text_width(f, s) for s in SAMPLES) + 16)
    h = 18 + nrows * cellh + samples_h + 4
    img = Image.new('RGB', (w, h), (255, 255, 255))
    d = ImageDraw.Draw(img)
    d.text((4, 2), '%s %dpt (%d px, ascent %d)' % (f.face, f.points, f.height, f.ascent),
           fill=(0, 0, 128))
    y0 = 18
    for r in range(nrows):
        row = first_row + r
        y = y0 + r * cellh
        d.text((2, y + (cellh - 10) // 2), '%02X' % (row * 16), fill=(128, 128, 128))
        for c in range(cols):
            code = row * 16 + c
            x = label + c * cellw
            if code not in f.glyphs:
                continue
            rows = f.glyphs[code]
            gw = len(rows[0])
            # the advance width, shaded; the baseline, marked
            d.rectangle([x + 2, y + 2, x + 2 + gw - 1, y + 2 + f.height - 1], fill=(232, 240, 255))
            d.line([x + 2, y + 2 + f.ascent, x + 2 + max(gw - 1, 0), y + 2 + f.ascent],
                   fill=(255, 210, 210))
            draw_text(img, f, x + 2, y + 2, chr(code))
    y = y0 + nrows * cellh + 8
    for s in SAMPLES:
        draw_text(img, f, 8, y, s)
        y += f.height + 4
    if scale > 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('font')
    ap.add_argument('--scale', type=int, default=1)
    a = ap.parse_args()
    if a.font.lower().endswith('.txt'):
        fonts = [from_source(a.font)]
    else:
        fonts = from_file(a.font)
    for f in fonts:
        out = a.out
        if len(fonts) > 1:
            base, ext = os.path.splitext(a.out)
            out = '%s-%d%s' % (base, f.points, ext)
        sheet(f, a.scale).save(out)


if __name__ == '__main__':
    main()
