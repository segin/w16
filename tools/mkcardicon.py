#!/usr/bin/env python3
"""mkcardicon - w16 Cardfile's icon: our own artwork (ARCHITECTURE.md 6.8),
drawn with mkicons.Icon. Three index cards fanned up and to the right,
each with a coloured tab line, the front one with lines of writing.

usage: mkcardicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, WHITE, NAVY, RED, TEAL, GRAY, YELLOW  # noqa: E402


def card(i, x, y, tab):
    """a 20 x 14 card with its top-left at x, y"""
    i.rect(x, y, x + 19, y + 13, WHITE)
    i.frame(x, y, x + 19, y + 13, BLACK)
    i.rect(x + 1, y + 3, x + 18, y + 3, tab)


def make():
    i = Icon()
    card(i, 10, 3, TEAL)
    card(i, 6, 8, YELLOW)
    card(i, 2, 13, RED)
    # the front card's writing and a shadow under the stack
    for k, w in enumerate((14, 11, 13)):
        i.rect(5, 19 + 2 * k, 4 + w, 19 + 2 * k, NAVY)
    i.rect(3, 27, 22, 28, GRAY)
    i.rect(22, 14, 23, 27, GRAY)
    return i


if __name__ == '__main__':
    with open(sys.argv[1], 'wb') as f:
        f.write(make().ico())
