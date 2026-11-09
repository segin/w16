#!/usr/bin/env python3
"""mkcalcicon - draw w16 Calculator's icon (original artwork, ARCHITECTURE.md
6.8) and write it as a Windows 3.x .ICO file (32x32, 16 colours).

The design is our own: a grey calculator body with a dark display showing
a lime digit, and a 4x4 keypad whose keys use the colours of Calculator's
button labels (red operators, blue digits, purple memory).

usage: mkcalcicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import (Icon, BLACK, NAVY, SILVER, GRAY, RED, LIME, BLUE,  # noqa: E402
                     PURPLE, WHITE, TEAL)


def make():
    ic = Icon()
    # body with a shadow on the right and bottom
    ic.rect(7, 2, 25, 29, SILVER)
    ic.frame(6, 1, 26, 30, BLACK)
    ic.rect(27, 3, 27, 31, GRAY)
    ic.rect(8, 31, 27, 31, GRAY)
    ic.rect(7, 2, 25, 2, WHITE)
    ic.rect(7, 2, 7, 29, WHITE)
    # display
    ic.rect(9, 4, 23, 9, BLACK)
    ic.rect(10, 5, 22, 8, NAVY)
    for (x, y) in [(19, 5), (20, 5), (21, 5), (21, 6), (20, 7), (19, 8)]:
        ic.set(x, y, LIME)
    # keypad: 4 columns x 4 rows of 3x3 keys
    colours = [
        [PURPLE, BLUE, BLUE, RED],
        [PURPLE, BLUE, BLUE, RED],
        [PURPLE, BLUE, BLUE, RED],
        [TEAL, BLUE, RED, RED],
    ]
    for r in range(4):
        for c in range(4):
            x, y = 9 + c * 4, 12 + r * 4
            ic.rect(x, y, x + 2, y + 2, colours[r][c])
            ic.rect(x + 1, y + 3, x + 3, y + 3, GRAY)
            ic.rect(x + 3, y + 1, x + 3, y + 3, GRAY)
    return ic


def main():
    with open(sys.argv[1], 'wb') as f:
        f.write(make().ico())


if __name__ == '__main__':
    main()
