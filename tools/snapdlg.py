#!/usr/bin/env python3
"""snapdlg - turn a SNAP.EXE "dump" of a running dialog (tests/apps/snap.c:
class, text, style, ID and screen rectangle of each window) into a dialog
template for a w16 resource script, so that a layout observed on Windows
3.1 is reproduced without reading the program's resources (ARCHITECTURE.md
3).

The dialog's client area starts 5 pixels in and 24 down from its window
(a DS_MODALFRAME dialog with a caption on VGA: measured on 3.1's File
Manager dialogs, whose widths and heights come out whole in dialog units
only so); dialog units are MS Sans Serif 8 bold's, 7 x 13 (x = MulDiv(dx,
7, 4), y = MulDiv(dy, 13, 8)). For each pixel value the dialog unit that
USER maps back to exactly it is chosen.

The template's place assumes that USER puts the dialog's client area at
x, y of the owner's client area (OWNERX, OWNERY: its screen position;
default 4, 42, File Manager's frame on VGA).

usage: snapdlg.py LOG "Dialog title" [NAME [OWNERX OWNERY]]
"""
import re
import sys

LINE = re.compile(r'^WIN( +)class="([^"]*)" text="(.*)" style=([0-9A-F]{8}) '
                  r'exstyle=([0-9A-F]{8}) id=([0-9A-F]{4}) rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')


def muldiv(a, b, c):
    n = a * b
    q, r = divmod(abs(n), c)
    if 2 * r >= c:
        q += 1
    return q if n >= 0 else -q


def units(p, base, div):
    for d in sorted(range(-2000, 2000), key=lambda d: abs(d * base / div - p)):
        if muldiv(d, base, div) == p:
            return d
    return round(p * div / base)


def rcstr(s):
    return '"' + s.replace('"', '""').replace('\t', '\\t') + '"'


def main():
    log, title = sys.argv[1], sys.argv[2]
    name = sys.argv[3] if len(sys.argv) > 3 else 'DLG'
    ox, oy = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (4, 42)
    lines = open(log, encoding='latin-1').read().replace('\r', '').split('\n')
    start = None
    for i, l in enumerate(lines):
        m = LINE.match(l)
        if m and m.group(2) == '#32770' and m.group(3) == title:
            start = i
            break
    if start is None:
        sys.exit('no dialog "%s"' % title)
    m = LINE.match(lines[start])
    wl, wt, wr, wb = map(int, m.group(7, 8, 9, 10))
    cx = units(wr - wl - 10, 7, 4)
    cy = units(wb - wt - 29, 13, 8)
    px = units(wl + 5 - ox, 7, 4)
    py = units(wt + 24 - oy, 13, 8)
    print('%s DIALOG %d, %d, %d, %d' % (name, px, py, cx, cy))
    print('STYLE 0x%sL' % m.group(4))
    print('CAPTION %s' % rcstr(title))
    print('FONT 8, "Helv"')
    print('BEGIN')
    depth = len(m.group(1))
    for l in lines[start + 1:]:
        if l.lstrip(' ').startswith(('WIN') ) and 'entry "' in l:
            continue                    # list and combo box entries
        c = LINE.match(l)
        if not c or len(c.group(1)) <= depth:
            break
        if len(c.group(1)) != depth + 2:
            continue                    # a control's own children
        cls, text, style, exs, cid = c.group(2), c.group(3), c.group(4), c.group(5), c.group(6)
        l0, t0, r0, b0 = map(int, c.group(7, 8, 9, 10))
        x = units(l0 - wl - 5, 7, 4)
        y = units(t0 - wt - 24, 13, 8)
        w = units(r0 - l0, 7, 4)
        h = units(b0 - t0, 13, 8)
        st = int(style, 16) & ~0x50000000      # WS_CHILD | WS_VISIBLE are implied
        hidden = not (int(style, 16) & 0x10000000)
        if hidden:
            st |= 0
        ident = int(cid, 16)
        if ident == 0xFFFF:
            ident = -1
        print('    CONTROL %s, %d, %s, 0x%04X%s, %d, %d, %d, %d' % (
            rcstr(text), ident, rcstr(cls), st & 0xFFFF,
            (' | 0x%08XL' % (st & 0xFFFF0000)) if st & 0xFFFF0000 else '',
            x, y, w, h) + ('    /* not visible */' if hidden else ''))
    print('END')


if __name__ == '__main__':
    main()
