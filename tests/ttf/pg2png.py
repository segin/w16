#!/usr/bin/env python3
"""pg2png.py - TTVIEW pages (tests/apps/ttvpage.c: 640 x 480 bits, rows of
80 bytes, set bits the glyphs' pixels) as images: black on white, side by
side or stacked, with a caption above each, scaled up by a whole factor.

  usage: pg2png.py [-z N] [-v] OUT.png CAPTION=PAGE.PG|IMAGE.png ...

A .png input (e.g. TTVIEW's screen capture, TTVIEW.SCR as an image) is
taken as it is, thresholded to black and white. -v stacks the pages
vertically (default: side by side). -z: the scale (default 2).
"""
import sys
from PIL import Image, ImageDraw


def page(path):
    if path.lower().endswith('.png'):
        im = Image.open(path).convert('L').point(lambda v: 255 if v >= 128 else 0)
        return im.convert('1')
    data = open(path, 'rb').read()
    im = Image.frombytes('1', (640, 480), data)
    # set bits are glyph pixels: black on white
    return Image.eval(im.convert('L'), lambda v: 255 - v).convert('1')


def main(argv):
    zoom, vert = 2, False
    while argv and argv[0].startswith('-'):
        if argv[0] == '-z':
            zoom = int(argv[1])
            argv = argv[2:]
        elif argv[0] == '-v':
            vert = True
            argv = argv[1:]
        else:
            break
    out, items = argv[0], argv[1:]
    ims = []
    for it in items:
        cap, path = it.split('=', 1)
        im = page(path).convert('L')
        im = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)
        c = Image.new('L', (im.width, im.height + 20), 255)
        c.paste(im, (0, 20))
        ImageDraw.Draw(c).text((4, 4), cap, fill=0)
        ims.append(c)
    gap = 8
    if vert:
        w = max(i.width for i in ims)
        h = sum(i.height for i in ims) + gap * (len(ims) - 1)
    else:
        w = sum(i.width for i in ims) + gap * (len(ims) - 1)
        h = max(i.height for i in ims)
    sheet = Image.new('L', (w, h), 160)
    x = y = 0
    for i in ims:
        sheet.paste(i, (x, y))
        if vert:
            y += i.height + gap
        else:
            x += i.width + gap
    sheet.save(out)


if __name__ == '__main__':
    main(sys.argv[1:])
