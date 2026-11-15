#!/usr/bin/env python3
"""deskstamp - the desktop version stamp as w16's USER draws it (user/deskver.c).

USER paints "<Mode> Mode w16 v<version> Debug Release <build>" in the
bottom right corner of the desktop ([w16] DesktopVersion=1, the default):
the System font, slanted one pixel for every SLANT rows above the cell's
bottom row (synthesised italic), a black copy one pixel right and down
(the shadow), then the text in white, both transparent. This tool draws
the same from w16's own System font (a glyph source or w16's VGASYS.FON;
tools/fontspec.py refuses any other font), for tests/boottest.sh's case
deskver: the stamp composited over the same boot with DesktopVersion=0
must equal the boot with DesktopVersion=1, pixel for pixel.

usage: deskstamp.py FONT TEXT BASE.png OUT.png   (OUT = BASE with the stamp)
       deskstamp.py FONT TEXT --box W H          (print the stamp's box:
                                                  x y width height)
       deskstamp.py --text MODE                  (the text for MODE:
                                                  Standard, "386 Enhanced",
                                                  Real; from VERSION and
                                                  build/w16ver.h)
The geometry constants below are user/deskver.c's; keep them equal.
"""
import os
import re
import sys

from PIL import Image

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fontspec  # noqa: E402

SLANT = 3          # rows per pixel of slant (DV_SLANT)
MARGIN_X = 4       # the box (text and shadow) from the screen's right edge
MARGIN_Y = 2       # and from its bottom edge


def load_font(path):
    if path.endswith('.txt'):
        return fontspec.from_source(path)
    fonts = fontspec.from_file(path)
    if len(fonts) != 1:
        raise SystemExit('%s: expected one font' % path)
    return fonts[0]


def mask(f, text):
    """the slanted text: a set of (x, y) in a box of (width, height)"""
    h = f.height
    w = fontspec.text_width(f, text)
    pts = set()
    x0 = 0
    for ch in text:
        rows = f.glyphs.get(ord(ch), f.glyphs[f.default])
        for y, r in enumerate(rows):
            for x, c in enumerate(r):
                if c == '#':
                    pts.add((x0 + x + (h - 1 - y) // SLANT, y))
        x0 += len(rows[0])
    return pts, w + (h - 1) // SLANT, h


def box(f, text, sw, sh):
    _, mw, mh = mask(f, text)
    bw, bh = mw + 1, mh + 1
    return sw - MARGIN_X - bw, sh - MARGIN_Y - bh, bw, bh


def stamp(f, text, img):
    pts, _, _ = mask(f, text)
    bx, by, _, _ = box(f, text, img.width, img.height)
    img = img.convert('RGB')
    for (x, y) in pts:
        img.putpixel((bx + x + 1, by + y + 1), (0, 0, 0))
    for (x, y) in pts:
        img.putpixel((bx + x, by + y), (255, 255, 255))
    return img


def text_for(mode, root):
    ver = open(os.path.join(root, 'VERSION')).read().strip()
    hdr = open(os.path.join(root, 'build', 'w16ver.h')).read()
    build = re.search(r'#define W16_BUILD "([^"]*)"', hdr).group(1)
    return '%s Mode w16 v%s Debug Release %s' % (mode, ver, build)


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == '--text':
        print(text_for(a[1], os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
        return
    if len(a) == 5 and a[2] == '--box':
        print('%d %d %d %d' % box(load_font(a[0]), a[1], int(a[3]), int(a[4])))
        return
    if len(a) != 4:
        sys.exit(__doc__)
    f = load_font(a[0])
    stamp(f, a[1], Image.open(a[2])).save(a[3])


if __name__ == '__main__':
    main()
