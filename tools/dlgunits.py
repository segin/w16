#!/usr/bin/env python3
"""dlgunits - turn a probe's window dump into dialog-template coordinates.

The SH probes (tests/apps/shprobe.h, dump_win) log each control of a
running dialog as
    WIN   Class "text" style=XXXXXXXX id=XXXX rect=l,t,r,b font=...
with the rectangle in the dialog's client coordinates. For a dialog whose
font gives the base units BX, BY (MS Sans Serif 8 bold: 7 and 13), this
prints, per control, the dialog units that USER maps back to exactly
those pixels (x = MulDiv(dx, BX, 4), y = MulDiv(dy, BY, 8), rounded), so
that a w16 template reproduces a layout observed on Windows 3.1 without
reading Microsoft's resources.

usage: dlgunits.py LOG MARKER [BX BY]
    MARKER: the line that starts the dump (e.g. "TEMPLATE 0064")
"""
import re
import sys


def muldiv(a, b, c):
    # Windows' MulDiv rounds half away from zero
    n = a * b
    q, r = divmod(abs(n), c)
    if 2 * r >= c:
        q += 1
    return q if n >= 0 else -q


def units(p, base, div):
    for d in sorted(range(-2000, 2000), key=lambda d: abs(d * base / div - p)):
        if muldiv(d, base, div) == p:
            return d
    return None


def main():
    log, marker = sys.argv[1], sys.argv[2]
    bx, by = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (7, 13)
    lines = open(log, encoding='latin-1').read().split('\n')
    i = next(k for k, l in enumerate(lines) if l.startswith(marker))
    pat = re.compile(r'WIN   (\S+) "(.*?)" style=(\w+) id=(\w+) rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
    text = ''
    for l in lines[i + 2:]:
        if not l.startswith('WIN   ') and not text:
            if l.startswith('WIN') or l.startswith('snap') or l.startswith('OBS'):
                break
        full = text + l if text else l
        m = pat.match(full)
        if not m and full.startswith('WIN   ') and '" style=' not in full:
            text = full + '\\n'
            continue
        text = ''
        if not m:
            break
        cls, t, style, cid, left, top, right, bottom = m.groups()
        left, top, right, bottom = map(int, (left, top, right, bottom))
        x, y = units(left, bx, 4), units(top, by, 8)
        cx, cy = units(right - left, bx, 4), units(bottom - top, by, 8)
        print('%-9s id=%s style=%s  %s, %s, %s, %s   "%s"' % (cls, cid, style, x, y, cx, cy, t))


if __name__ == '__main__':
    main()
