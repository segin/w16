#!/usr/bin/env python3
"""clockicon - draw w16 Clock's icon (our own artwork, ARCHITECTURE.md 6.8)
and write it as a Windows 3.x .ICO file.

A round dial on a stand: a gray rim, a white face with four teal hour
marks, black hands at ten past ten and a red second hand; the stand is
the slab motif of the w16 boot splash, as in tools/mkicons.py.

usage: clockicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkicons  # noqa: E402
from mkicons import Icon, BLACK, GRAY, SILVER, WHITE, TEAL, CYAN, RED, NAVY  # noqa: E402


def make():
    i = Icon()
    cx, cy = 15, 13
    i.slab(5, 23, 18, NAVY, CYAN, TEAL)         # the stand
    i.disc(cx, cy, 13, BLACK)
    i.disc(cx, cy, 12, GRAY)
    i.disc(cx, cy, 10, SILVER)
    i.disc(cx, cy, 9, WHITE)
    for x, y in ((cx, cy - 8), (cx + 8, cy), (cx, cy + 8), (cx - 8, cy)):
        i.rect(x - 1, y - 1, x, y, TEAL)
    i.line(cx, cy, cx - 5, cy - 3, BLACK)        # hour hand: ten
    i.line(cx, cy - 1, cx - 5, cy - 4, BLACK)
    i.line(cx, cy, cx + 5, cy - 6, BLACK)        # minute hand: two
    i.line(cx + 1, cy, cx + 6, cy - 6, BLACK)
    i.line(cx, cy, cx - 2, cy + 7, RED)          # second hand
    i.rect(cx, cy, cx, cy, BLACK)
    return i


def main():
    with open(sys.argv[1], 'wb') as f:
        f.write(make().ico())


if __name__ == '__main__':
    main()
