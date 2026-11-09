#!/usr/bin/env python3
"""cplicons - w16's original icons for Control Panel (CONTROL.EXE) and its
applets (MAIN.CPL ...), in the style of the stock set (tools/stockicons.py,
tools/mkicons.py): black outlines, the 16 VGA colours, simple shapes.

usage: cplicons.py OUTDIR     writes <name>.ico for each icon below
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import (Icon, BLACK, MAROON, GREEN, OLIVE, NAVY, PURPLE, TEAL,  # noqa: E402
                     SILVER, GRAY, RED, LIME, YELLOW, BLUE, FUCHSIA, CYAN, WHITE)


def slider(i, x, y0, y1, knob):
    """a vertical slider: a groove and a knob at row knob"""
    i.rect(x, y0, x, y1, BLACK)
    i.rect(x + 1, y0, x + 1, y1, WHITE)
    i.rect(x - 2, knob, x + 3, knob + 3, SILVER)
    i.frame(x - 2, knob, x + 3, knob + 3, BLACK)
    i.rect(x - 1, knob + 1, x + 2, knob + 1, WHITE)


def control(i):
    """Control Panel: a console with three sliders and a dial"""
    i.poly([(2, 9), (29, 9), (29, 27), (2, 27)], SILVER, BLACK)
    i.rect(3, 26, 29, 26, GRAY)
    i.rect(28, 10, 28, 26, GRAY)
    i.rect(3, 10, 27, 10, WHITE)
    i.rect(3, 10, 3, 25, WHITE)
    slider(i, 7, 13, 24, 15)
    slider(i, 13, 13, 24, 19)
    slider(i, 19, 13, 24, 13)
    i.disc(25, 18, 2, TEAL)
    i.ring(25, 18, 2, BLACK)
    i.set(25, 16, WHITE)
    # a small screen on top
    i.rect(9, 2, 22, 8, NAVY)
    i.frame(8, 1, 23, 8, BLACK)
    i.rect(10, 3, 15, 3, CYAN)
    i.rect(10, 5, 18, 5, CYAN)


ICONS = {
    'control': control,
}


def make(name):
    i = Icon()
    ICONS[name](i)
    return i


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name in ICONS:
        with open(os.path.join(out, name + '.ico'), 'wb') as f:
            f.write(make(name).ico())


if __name__ == '__main__':
    main()
