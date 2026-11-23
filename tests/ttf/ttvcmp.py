#!/usr/bin/env python3
"""ttvcmp.py - compare what TTVIEW.EXE drew on Windows with the host's
drawing of the same pages (tests/ttview.sh).

  usage: ttvcmp.py HOST.PG TTVIEW.PG HOSTU.PG TTVIEWU.PG TTVIEW.SCR SHOWN [OUT.png]

HOST.PG, TTVIEW.PG: the page hinted (w16's autohinter, gdi/ttf/ttfhint.c);
HOSTU.PG, TTVIEWU.PG: unhinted. Each 640 x 480 bits, rows of 80 bytes,
set bits the glyphs' pixels (tests/apps/ttvpage.c). TTVIEW.SCR: the
client area read back from the screen: words width, height, bytes a row,
then the rows of a monochrome bitmap, white 1. SHOWN: what TTVIEW showed,
h (the hinted page), u (unhinted) or g (Windows' own rendering through
TextOut: not compared). Checks that both pages TTVIEW rasterised are the
host's bit for bit, and that the screen shows the one shown: every pixel
of the client area black where the page has a glyph pixel and white
elsewhere. OUT.png: the screen capture as an image. Exit status 1 on a
difference.
"""
import struct
import sys


def bit(data, stride, x, y):
    return (data[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1


def cmp_page(what, host, page):
    if len(page) != len(host):
        print('ttvcmp: %s is %d bytes, not %d' % (what, len(page), len(host)))
        return 1
    diff = sum(bin(a ^ b).count('1') for a, b in zip(host, page))
    on = sum(bin(a).count('1') for a in host)
    print('%s: %s (%d of 640 x 480 pixels differ; %d glyph pixels)'
          % (what, 'identical to the host' if not diff else 'DIFFERENT', diff, on))
    return diff != 0


def main():
    if len(sys.argv) < 7:
        print(__doc__)
        return 2
    host = open(sys.argv[1], 'rb').read()
    hostu = open(sys.argv[3], 'rb').read()
    shown = sys.argv[6]
    bad = cmp_page('page, hinted', host, open(sys.argv[2], 'rb').read())
    bad |= cmp_page('page, unhinted', hostu, open(sys.argv[4], 'rb').read())
    scr = open(sys.argv[5], 'rb').read()
    w, h, stride = struct.unpack('<HHH', scr[:6])
    rows = scr[6:]
    if len(rows) < stride * h:
        print('ttvcmp: TTVIEW.SCR is short')
        return 1
    if shown == 'g':
        print('screen: the client area, %d x %d: Windows\' own rendering (not compared)' % (w, h))
    else:
        ref = host if shown == 'h' else hostu
        sdiff = 0
        for y in range(h):
            for x in range(w):
                if bit(rows, stride, x, y) == bit(ref, 80, x, y):    # white 1, glyph 1
                    sdiff += 1
        print('screen: the client area, %d x %d: %s (%d pixels differ)'
              % (w, h, 'shows the %s page' % ('hinted' if shown == 'h' else 'unhinted')
                 if not sdiff else 'DIFFERENT', sdiff))
        bad |= sdiff != 0
    if len(sys.argv) > 7:
        from PIL import Image
        im = Image.new('1', (w, h))
        im.putdata([bit(rows, stride, x, y) for y in range(h) for x in range(w)])
        im.save(sys.argv[7])
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
