#!/usr/bin/env python3
"""npicon - w16 Notepad's icon (original art, ARCHITECTURE.md 6.8): a
spiral-bound writing pad with ruled lines and a pencil across it.

usage: npicon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, NAVY, SILVER, GRAY, YELLOW, OLIVE, WHITE, RED, CYAN  # noqa: E402


def notepad():
    i = Icon()
    # the pad: a white sheet with a black outline and a shadow
    i.rect(6, 5, 26, 29, GRAY)              # shadow, offset by one
    i.rect(5, 4, 25, 28, WHITE)
    i.frame(5, 4, 25, 28, BLACK)
    # the binding: a navy band with spiral rings
    i.rect(5, 4, 25, 7, NAVY)
    i.frame(5, 4, 25, 7, BLACK)
    for x in range(7, 25, 3):
        i.rect(x, 2, x, 5, SILVER)
        i.set(x, 1, GRAY)
    # ruled lines, the left margin in red
    for y in range(11, 27, 3):
        i.rect(7, y, 23, y, CYAN)
    i.rect(9, 9, 9, 27, RED)
    # a pencil lying across the page, point down to the left
    for k in range(13):
        x, y = 27 - k, 12 + k
        i.set(x, y, YELLOW)
        i.set(x + 1, y, OLIVE)
        i.set(x - 1, y, BLACK)
        i.set(x + 2, y, BLACK)
    i.set(14, 25, WHITE)
    i.set(13, 26, SILVER)
    i.set(12, 27, BLACK)
    i.set(28, 11, RED)
    i.set(29, 11, BLACK)
    i.set(28, 10, BLACK)
    return i


def main():
    with open(sys.argv[1], 'wb') as f:
        f.write(notepad().ico())


if __name__ == '__main__':
    main()
