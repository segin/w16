#!/usr/bin/env python3
"""mkregicon - w16 Registration Info Editor's icon: our own artwork
(ARCHITECTURE.md 6.8), drawn with mkicons.Icon: three blocks joined by the
lines of a tree, the registration database's keys, over a white card.

usage: mkregicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, WHITE, GRAY, SILVER, NAVY, BLUE, TEAL, CYAN, MAROON, RED, OLIVE, YELLOW  # noqa: E402


def block(i, x, y, face, top, side):
    """a 9 x 9 block (a cube seen from above and the right) at x, y"""
    i.rect(x + 2, y, x + 9, y + 1, top)
    i.rect(x, y + 2, x + 7, y + 9, face)
    i.rect(x + 8, y + 1, x + 9, y + 8, side)
    i.frame(x, y + 2, x + 7, y + 9, BLACK)
    i.line(x, y + 2, x + 2, y, BLACK)
    i.line(x + 2, y, x + 9, y, BLACK)
    i.line(x + 9, y, x + 9, y + 7, BLACK)
    i.line(x + 7, y + 2, x + 9, y, BLACK)
    i.line(x + 7, y + 9, x + 9, y + 7, BLACK)


def regedit():
    i = Icon()
    # the card
    i.rect(2, 2, 28, 29, WHITE)
    i.frame(1, 1, 29, 30, BLACK)
    i.rect(30, 3, 30, 31, GRAY)
    i.rect(3, 31, 30, 31, GRAY)
    # the tree's lines
    i.rect(8, 13, 8, 25, BLACK)
    i.rect(8, 16, 13, 16, BLACK)
    i.rect(8, 25, 13, 25, BLACK)
    # the keys
    block(i, 4, 3, BLUE, CYAN, NAVY)
    block(i, 14, 11, RED, YELLOW, MAROON)
    block(i, 14, 20, TEAL, CYAN, NAVY)
    return i


if __name__ == '__main__':
    with open(sys.argv[1], 'wb') as fh:
        fh.write(regedit().ico())
