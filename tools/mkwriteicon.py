#!/usr/bin/env python3
"""mkwriteicon - w16 Write's icon (our own artwork, ARCHITECTURE.md 6.8):
a sheet with lines of text, a pen across its corner. Drawn with
mkicons.Icon.

usage: mkwriteicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, WHITE, NAVY, GRAY, YELLOW, OLIVE, TEAL  # noqa: E402


def make():
    i = Icon()
    i.page(4, 2, 23, 29, fold=5)
    for y, w in ((9, 15), (12, 13), (15, 15), (18, 11), (21, 14), (24, 8)):
        i.rect(7, y, 7 + w - 1 if 7 + w - 1 < 21 else 20, y, NAVY)
    # the pen: a teal barrel from top right down to a nib
    i.poly([(29, 6), (31, 8), (18, 25), (16, 23)], TEAL, BLACK)
    i.poly([(16, 23), (18, 25), (14, 27)], YELLOW, BLACK)
    i.set(14, 27, BLACK)
    i.line(27, 8, 29, 10, OLIVE)
    i.rect(25, 26, 28, 26, GRAY)
    return i


def main():
    with open(sys.argv[1], 'wb') as f:
        f.write(make().ico())


if __name__ == '__main__':
    main()
