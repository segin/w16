#!/usr/bin/env python3
"""iconbox.py IMG - the desktop icons at the bottom left of a Windows screen
(tests/boottest.sh case swapesc: the DOS program's icon after Alt+Esc).

Looks at the screen's rectangle 0,400-199,479 (640x480: the first places
of the icon row) and prints one line:
  "icon X0 Y0 X1 Y1 title CX W active N"
icon: the box of the pixels above y 444 (the icon) that are not the
desktop's colour (taken at 639,479); title: the centre and width of those
from y 444 down (the icon's title), "title - 0" when there is none;
active: how many pixels of the active caption's blue (0000aa) the title
has (it is highlighted when the icon is the active window). "none" when
there is no icon.
"""
import sys
from PIL import Image


def main():
    im = Image.open(sys.argv[1]).convert('RGB')
    if im.size != (640, 480):
        print('none size %dx%d' % im.size)
        return
    bg = im.getpixel((639, 479))
    ib = None
    tx0 = tx1 = None
    blue = 0
    for y in range(400, 480):
        for x in range(0, 200):
            p = im.getpixel((x, y))
            if p == bg:
                continue
            if y < 444:
                if ib is None:
                    ib = [x, y, x, y]
                else:
                    ib = [min(ib[0], x), min(ib[1], y), max(ib[2], x), max(ib[3], y)]
            else:
                tx0 = x if tx0 is None else min(tx0, x)
                tx1 = x if tx1 is None else max(tx1, x)
                if p == (0, 0, 0xAA):
                    blue += 1
    if ib is None:
        print('none')
        return
    title = '- 0' if tx0 is None else '%d %d' % ((tx0 + tx1) // 2, tx1 - tx0 + 1)
    print('icon %d %d %d %d title %s active %d' % (ib[0], ib[1], ib[2], ib[3], title, blue))


main()
