#!/usr/bin/env python3
"""snapicon - SNAP.EXE's icon (w16's test driver, tests/apps/snap.c;
original art, ARCHITECTURE.md 6.8): a camera taking a snapshot.

A silver camera body with a black outline and a gray grip band, a
viewfinder and a red shutter button on top, a yellow flash firing at the
top left, and a large lens: black barrel, gray ring, navy glass with a
teal centre and a white highlight. The camera sits on the slab motif of
the w16 boot splash, as Clock's dial does (tools/clockicon.py).

usage: snapicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import (Icon, BLACK, GRAY, SILVER, WHITE, TEAL, CYAN, RED,  # noqa: E402
                     NAVY, YELLOW, OLIVE, MAROON)


def make():
    i = Icon()
    i.slab(4, 23, 20, NAVY, CYAN, TEAL)          # the stand
    # the body
    i.rect(2, 9, 29, 25, SILVER)
    i.rect(2, 20, 29, 23, GRAY)                  # the grip band
    i.frame(2, 9, 29, 25, BLACK)
    i.rect(3, 24, 28, 24, GRAY)                  # shading along the bottom
    i.rect(3, 10, 28, 10, WHITE)                 # light along the top
    # the top: viewfinder hump and shutter button
    i.rect(11, 5, 20, 9, SILVER)
    i.frame(11, 5, 20, 9, BLACK)
    i.rect(12, 6, 19, 6, WHITE)
    i.rect(14, 7, 17, 8, NAVY)                   # viewfinder window
    i.rect(23, 6, 26, 8, RED)                    # shutter button
    i.frame(23, 6, 26, 9, BLACK)
    i.rect(24, 6, 25, 6, WHITE)
    # the flash window, firing
    i.rect(4, 12, 8, 14, YELLOW)
    i.frame(3, 11, 9, 15, BLACK)
    # its light: three rays from above the window
    for x, y in ((2, 6), (3, 7), (6, 4), (6, 5), (6, 6), (8, 7), (9, 6)):
        i.set(x, y, YELLOW)
    i.set(1, 5, OLIVE)
    i.set(6, 3, OLIVE)
    i.set(10, 5, OLIVE)
    # the lens
    cx, cy = 16, 17
    i.disc(cx, cy, 7, BLACK)
    i.disc(cx, cy, 6, GRAY)
    i.disc(cx, cy, 5, BLACK)
    i.disc(cx, cy, 4, NAVY)
    i.disc(cx, cy, 2, TEAL)
    i.set(cx, cy, CYAN)
    i.rect(cx - 2, cy - 3, cx - 1, cy - 3, WHITE)   # highlight
    i.set(cx - 3, cy - 2, WHITE)
    # a red light: the picture is being taken
    i.rect(25, 12, 26, 13, MAROON)
    i.set(25, 12, RED)
    return i


def main():
    with open(sys.argv[1], 'wb') as f:
        f.write(make().ico())


if __name__ == '__main__':
    main()
