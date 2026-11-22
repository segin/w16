#!/usr/bin/env python3
"""mkpificon - w16 PIF Editor's icon: our own artwork (ARCHITECTURE.md 6.8),
drawn with mkicons.Icon: a form card with ruled entry lines, and over its
lower right corner a small black DOS screen with a grey prompt and cursor.

usage: mkpificon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, WHITE, GRAY, SILVER, NAVY, BLUE, TEAL  # noqa: E402


def pifedit():
    i = Icon()
    # the card
    i.rect(2, 1, 24, 24, WHITE)
    i.frame(1, 0, 25, 25, BLACK)
    i.rect(26, 2, 26, 26, GRAY)
    i.rect(3, 26, 26, 26, GRAY)
    # a title bar and the form's labelled lines
    i.rect(2, 1, 24, 4, NAVY)
    for y in (8, 12, 16):
        i.rect(4, y, 9, y, GRAY)
        i.rect(11, y, 22, y, BLACK)
    i.frame(4, 19, 6, 21, BLACK)
    i.rect(5, 20, 5, 20, BLUE)
    # the DOS screen
    i.rect(13, 14, 30, 30, SILVER)
    i.frame(12, 13, 31, 31, BLACK)
    i.rect(14, 15, 29, 27, BLACK)
    i.rect(16, 17, 17, 18, SILVER)
    i.line(18, 17, 20, 19, SILVER)
    i.line(20, 19, 18, 21, SILVER)
    i.rect(22, 21, 24, 21, TEAL)
    i.rect(15, 29, 28, 29, GRAY)
    return i


if __name__ == '__main__':
    with open(sys.argv[1], 'wb') as fh:
        fh.write(pifedit().ico())
