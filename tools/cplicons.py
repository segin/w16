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


def color(i):
    """Color: a painter's palette with five paints and a brush"""
    i.poly([(3, 12), (9, 6), (20, 5), (28, 9), (29, 17), (25, 20), (20, 19),
            (17, 22), (19, 26), (14, 29), (6, 27), (2, 20)], OLIVE, BLACK)
    i.poly([(4, 13), (9, 7), (19, 6), (26, 9)], YELLOW)
    i.disc(9, 12, 2, RED)
    i.disc(15, 9, 2, LIME)
    i.disc(22, 11, 2, BLUE)
    i.disc(7, 20, 2, FUCHSIA)
    i.disc(12, 25, 2, CYAN)
    i.disc(23, 15, 1, SILVER)
    i.line(30, 1, 18, 17, MAROON)
    i.line(31, 2, 19, 18, MAROON)
    i.line(17, 18, 15, 20, BLACK)
    i.line(18, 19, 16, 21, BLACK)


def fonts(i):
    """Fonts: a large serif A over a small italic-ish b, two type styles"""
    i.poly([(3, 26), (11, 3), (15, 3), (23, 26), (18, 26), (16, 20), (10, 20), (8, 26)], NAVY, BLACK)
    i.poly([(11, 16), (13, 9), (15, 16)], WHITE, BLACK)
    i.rect(2, 26, 10, 27, BLACK)
    i.rect(16, 26, 24, 27, BLACK)
    i.rect(22, 14, 24, 28, RED)
    i.disc(27, 23, 4, RED)
    i.disc(27, 23, 2, WHITE)
    i.rect(22, 13, 25, 13, RED)


def ports(i):
    """Ports: a 9-pin serial connector with its cable"""
    i.poly([(3, 9), (26, 9), (23, 21), (6, 21)], SILVER, BLACK)
    i.poly([(6, 11), (23, 11), (21, 19), (8, 19)], GRAY, BLACK)
    for x in (9, 12, 15, 18, 21):
        i.set(x, 14, YELLOW)
    for x in (11, 14, 17, 20):
        i.set(x, 17, YELLOW)
    i.disc(1, 15, 1, GRAY)
    i.rect(26, 14, 30, 16, GRAY)
    i.frame(26, 14, 30, 16, BLACK)
    i.rect(13, 21, 16, 24, GRAY)
    i.frame(13, 21, 16, 24, BLACK)
    i.line(14, 25, 14, 31, BLACK)
    i.line(15, 25, 15, 31, BLACK)


def mouse(i):
    """Mouse: a two-button mouse seen from above, with its cord"""
    i.poly([(9, 8), (22, 8), (24, 12), (24, 26), (20, 30), (11, 30), (7, 26), (7, 12)], WHITE, BLACK)
    i.line(8, 16, 23, 16, BLACK)
    i.line(15, 8, 15, 16, BLACK)
    i.rect(9, 10, 14, 15, SILVER)
    i.rect(16, 10, 22, 15, SILVER)
    i.line(15, 7, 15, 4, BLACK)
    i.line(15, 4, 19, 1, BLACK)
    i.line(19, 1, 24, 2, BLACK)
    i.rect(8, 28, 23, 28, GRAY)


def desktop(i):
    """Desktop: a monitor showing a patterned desktop and a small window"""
    i.rect(2, 3, 29, 23, SILVER)
    i.frame(2, 3, 29, 23, BLACK)
    i.rect(4, 5, 27, 20, TEAL)
    for y in range(5, 21):
        for x in range(4, 28):
            if (x + y) % 4 == 0:
                i.set(x, y, CYAN)
    i.window(13, 8, 25, 17, BLUE, WHITE)
    i.rect(5, 18, 8, 19, YELLOW)
    i.poly([(10, 24), (21, 24), (24, 28), (7, 28)], GRAY, BLACK)


def keyboard(i):
    """Keyboard: a keyboard in perspective, rows of keys"""
    i.poly([(4, 12), (27, 12), (31, 26), (0, 26)], SILVER, BLACK)
    for r, (y, x0, x1) in enumerate(((14, 6, 25), (17, 5, 26), (20, 4, 27))):
        for x in range(x0, x1, 3):
            i.rect(x, y, x + 1, y + 1, WHITE)
            i.set(x + 1, y + 1, GRAY)
    i.rect(9, 23, 22, 24, WHITE)
    i.line(16, 11, 16, 6, BLACK)
    i.line(16, 6, 20, 3, BLACK)


def printers(i):
    """Printers: a printer with a sheet coming out"""
    i.page(9, 2, 23, 14)
    i.line(11, 6, 19, 6, GRAY)
    i.line(11, 9, 20, 9, GRAY)
    i.poly([(3, 13), (28, 13), (30, 24), (1, 24)], SILVER, BLACK)
    i.rect(2, 22, 29, 24, GRAY)
    i.line(1, 24, 30, 24, BLACK)
    i.rect(8, 15, 23, 16, BLACK)
    i.rect(24, 18, 26, 19, LIME)
    i.page(8, 25, 23, 30, fold=0)


def intl(i):
    """International: a globe with meridians on a stand"""
    i.disc(15, 13, 11, BLUE)
    i.poly([(9, 5), (15, 4), (17, 8), (13, 12), (14, 17), (10, 20), (6, 14), (5, 9)], GREEN)
    i.poly([(18, 13), (23, 10), (25, 15), (21, 21), (18, 19)], GREEN)
    i.ring(15, 13, 11, BLACK)
    i.line(15, 2, 15, 24, CYAN)
    i.line(4, 13, 26, 13, CYAN)
    i.rect(14, 25, 16, 27, OLIVE)
    i.rect(9, 28, 21, 29, OLIVE)
    i.frame(9, 28, 21, 29, BLACK)


def datetime(i):
    """Date/Time: a calendar sheet with a clock in front"""
    i.rect(2, 4, 21, 25, WHITE)
    i.rect(2, 4, 21, 9, RED)
    i.frame(2, 4, 21, 25, BLACK)
    i.line(2, 9, 21, 9, BLACK)
    for y in (12, 16, 20):
        for x in (4, 9, 14):
            i.rect(x, y, x + 2, y + 1, GRAY)
    i.disc(22, 21, 8, YELLOW)
    i.ring(22, 21, 8, BLACK)
    i.disc(22, 21, 6, WHITE)
    i.line(22, 21, 22, 16, BLACK)
    i.line(22, 21, 26, 21, BLACK)
    for x, y in ((22, 14), (29, 21), (22, 28), (15, 21)):
        i.set(x, y, BLACK)


def drivers(i):
    """Drivers: a plug-in card with chips and a gold edge"""
    i.poly([(2, 6), (29, 6), (29, 22), (2, 22)], GREEN, BLACK)
    i.rect(6, 9, 12, 14, BLACK)
    i.rect(15, 9, 25, 12, BLACK)
    i.rect(15, 15, 19, 18, BLACK)
    i.rect(5, 23, 26, 26, YELLOW)
    for x in range(6, 26, 3):
        i.rect(x, 23, x, 26, OLIVE)
    i.rect(22, 15, 26, 19, SILVER)
    i.frame(22, 15, 26, 19, BLACK)


def sound(i):
    """Sound: a loudspeaker with sound waves"""
    i.poly([(3, 12), (9, 12), (17, 5), (17, 27), (9, 20), (3, 20)], SILVER, BLACK)
    i.rect(4, 13, 8, 19, GRAY)
    for r, c in ((5, NAVY), (9, BLUE)):
        for y in range(16 - r, 17 + r):
            for x in range(18, 31):
                d = (x - 16) ** 2 + (y - 16) ** 2
                if r * r - r <= d <= r * r + r:
                    i.set(x, y, c)


def enh386(i):
    """386 Enhanced: a processor chip with its pins"""
    i.rect(7, 7, 24, 24, GRAY)
    i.frame(7, 7, 24, 24, BLACK)
    i.rect(10, 10, 21, 21, SILVER)
    i.text(11, 13, '16', NAVY)
    for k in range(9, 24, 3):
        for (x, y) in ((k, 4), (k, 25), (4, k), (25, k)):
            i.rect(x, y, x + 1, y + 2 if y in (4, 25) else y + 1, YELLOW)
            if x in (4, 25):
                i.rect(x, y, x + 2, y + 1, YELLOW)


ICONS = {
    'control': control,
    'color': color, 'fonts': fonts, 'ports': ports, 'mouse': mouse,
    'desktop': desktop, 'keyboard': keyboard, 'printers': printers,
    'intl': intl, 'datetime': datetime,
    'drivers': drivers, 'sound': sound, 'enh386': enh386,
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
