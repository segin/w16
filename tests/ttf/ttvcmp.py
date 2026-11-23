#!/usr/bin/env python3
"""ttvcmp.py - compare what TTVIEW.EXE drew on Windows with the host's
drawing of the same page (tests/ttview.sh).

  usage: ttvcmp.py HOST.PG TTVIEW.PG TTVIEW.SCR [OUT.png]

HOST.PG and TTVIEW.PG: 640 x 480 bits, rows of 80 bytes, set bits the
glyphs' pixels (tests/apps/ttvpage.c). TTVIEW.SCR: the client area read
back from the screen: words width, height, bytes a row, then the rows of
a monochrome bitmap, white 1. Checks that the page TTVIEW rasterised is
the host's bit for bit, and that the screen shows it: every pixel of the
client area black where the page has a glyph pixel and white elsewhere.
OUT.png: the screen capture as an image. Exit status 1 on a difference.
"""
import struct
import sys


def bit(data, stride, x, y):
    return (data[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1


def main():
    host = open(sys.argv[1], 'rb').read()
    page = open(sys.argv[2], 'rb').read()
    scr = open(sys.argv[3], 'rb').read()
    bad = 0
    if len(page) != len(host):
        print('ttvcmp: TTVIEW.PG is %d bytes, not %d' % (len(page), len(host)))
        return 1
    diff = sum(bin(a ^ b).count('1') for a, b in zip(host, page))
    on = sum(bin(a).count('1') for a in host)
    print('page: %s (%d of 640 x 480 pixels differ; %d glyph pixels)'
          % ('identical to the host' if not diff else 'DIFFERENT', diff, on))
    bad |= diff != 0
    w, h, stride = struct.unpack('<HHH', scr[:6])
    rows = scr[6:]
    if len(rows) < stride * h:
        print('ttvcmp: TTVIEW.SCR is short')
        return 1
    sdiff = 0
    for y in range(h):
        for x in range(w):
            if bit(rows, stride, x, y) == bit(host, 80, x, y):    # white 1, glyph 1
                sdiff += 1
    print('screen: the client area, %d x %d: %s (%d pixels differ)'
          % (w, h, 'shows the page' if not sdiff else 'DIFFERENT', sdiff))
    bad |= sdiff != 0
    if len(sys.argv) > 4:
        from PIL import Image
        im = Image.new('1', (w, h))
        im.putdata([bit(rows, stride, x, y) for y in range(h) for x in range(w)])
        im.save(sys.argv[4])
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
