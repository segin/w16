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
    # a pencil lying across the page, point down to the left: drawn row by
    # row along the diagonal x = 38 - y, so its outline and stripes run
    # unbroken from the eraser to the point
    def row(y, left, right, *colours):
        """the pencil's row y: black edges at left-1 and right+1, colours
        filling left..right"""
        i.set(left - 1, y, BLACK)
        i.set(right + 1, y, BLACK)
        for k, c in enumerate(colours):
            i.set(left + k, y, c)
    for y in range(9, 30):
        cx, t = 38 - y, y - 9
        if t == 0:                                  # the eraser's end
            for x in range(cx - 1, cx + 2):
                i.set(x, y - 1, BLACK)
            row(y, cx - 1, cx + 1, RED, RED, RED)
        elif t == 1:
            row(y, cx - 1, cx + 1, RED, RED, RED)
        elif t == 2:                                # the metal band
            row(y, cx - 1, cx + 1, SILVER, WHITE, GRAY)
        elif t <= 14:                               # the painted body
            row(y, cx - 1, cx + 1, OLIVE, YELLOW, YELLOW)
        elif t == 15:                               # sharpened wood
            row(y, cx - 1, cx + 1, SILVER, WHITE, WHITE)
        elif t == 16:
            row(y, cx - 1, cx, SILVER, WHITE)
        elif t == 17:                               # the lead
            row(y, cx - 1, cx - 1, GRAY)
        elif t == 18:
            i.set(cx - 1, y, BLACK)
            break
    return i


def main():
    with open(sys.argv[1], 'wb') as f:
        f.write(notepad().ico())


if __name__ == '__main__':
    main()
