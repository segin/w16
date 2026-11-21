#!/usr/bin/env python3
"""starsicon - draw the icon of w16's Starfield Simulation screen saver
(SSSTARS.SCR; our own artwork, ARCHITECTURE.md 6.8) and write it as a
Windows 3.x .ICO file.

A monitor showing a starfield: a gray case on the slab motif of the w16
boot splash (as in tools/mkicons.py), a black screen with white stars,
bigger towards the edges as if flying out of the middle.

usage: starsicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, GRAY, SILVER, WHITE, TEAL, CYAN, NAVY  # noqa: E402


def make():
    i = Icon()
    i.slab(5, 24, 18, NAVY, CYAN, TEAL)         # the stand
    i.rect(2, 2, 29, 22, BLACK)                 # the case
    i.rect(3, 3, 28, 21, SILVER)
    i.frame(3, 3, 28, 21, GRAY)
    i.rect(5, 5, 26, 19, BLACK)                 # the screen
    for x, y in ((15, 12), (17, 11), (14, 10), (19, 13), (12, 13)):
        i.set(x, y, GRAY)                       # far: dim points
    for x, y in ((10, 8), (21, 9), (11, 16), (20, 16), (16, 7)):
        i.set(x, y, WHITE)                      # nearer: points
    for x, y in ((6, 6), (24, 6), (6, 17), (24, 17)):
        i.rect(x, y, x + 1, y + 1, WHITE)       # near: squares
    return i


def main():
    with open(sys.argv[1], 'wb') as f:
        f.write(make().ico())


if __name__ == '__main__':
    main()
