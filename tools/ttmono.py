#!/usr/bin/env python3
"""ttmono - host-side monochrome specimens of TrueType fonts, rendered with
FreeType's TrueType bytecode interpreter (through Pillow), black and white
as Windows 3.1 draws them, at 8-24 pt on a 96 dpi screen.

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).

    ttmono.py OUT.png FONT.TTF [FONT.TTF ...]

Each font gets a label and one line per size. FREETYPE_PROPERTIES=
"truetype:interpreter-version=35" selects FreeType's full (v35)
interpreter, which honours x-direction hints too; it is set here unless
the environment sets it.
"""
import os
import sys

os.environ.setdefault('FREETYPE_PROPERTIES', 'truetype:interpreter-version=35')
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

SIZES = (8, 9, 10, 11, 12, 14, 16, 18, 20, 24)
TEXT = 'The quick brown fox jumps over the lazy dog. HOMBURG 0123456789 éàüß'


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out, fonts = sys.argv[1], sys.argv[2:]
    W = 1100
    rows = []
    for fn in fonts:
        rows.append(('label', fn))
        for pt in SIZES:
            rows.append((pt, fn))
    H = sum(16 if k == 'label' else int(k * 96 / 72 * 1.35) + 2 for k, _ in rows) + 10
    img = Image.new('1', (W, H), 1)
    d = ImageDraw.Draw(img)
    d.fontmode = '1'
    y = 4
    lab = ImageFont.load_default()
    for k, fn in rows:
        if k == 'label':
            d.text((4, y), os.path.basename(fn), font=lab, fill=0)
            y += 16
            continue
        px = round(k * 96 / 72)
        fo = ImageFont.truetype(fn, px)
        d.text((4, y), '%2d' % k, font=lab, fill=0)
        d.text((28, y), TEXT, font=fo, fill=0)
        y += int(px * 1.35) + 2
    img.save(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
