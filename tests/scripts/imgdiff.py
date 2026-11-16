#!/usr/bin/env python3
"""imgdiff - compare two screenshots pixel by pixel (tests/grabber3.sh).

usage: imgdiff.py A.png B.png [DIFF.png]
Prints "same" or "N pixels differ in (x0,y0)-(x1,y1)"; with DIFF.png the
differing pixels are written red on a dimmed copy of A. Exit status 0 when
the images are the same, 1 when they differ, 2 when one is missing or the
sizes differ.
"""
import sys

from PIL import Image


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    try:
        a = Image.open(sys.argv[1]).convert('RGB')
        b = Image.open(sys.argv[2]).convert('RGB')
    except OSError as e:
        print('missing: %s' % e)
        sys.exit(2)
    if a.size != b.size:
        print('sizes differ: %dx%d %dx%d' % (a.size + b.size))
        sys.exit(2)
    pa, pb = a.load(), b.load()
    w, h = a.size
    n = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    diff = []
    for y in range(h):
        for x in range(w):
            if pa[x, y] != pb[x, y]:
                n += 1
                diff.append((x, y))
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    if not n:
        print('same')
        sys.exit(0)
    print('%d pixels differ in (%d,%d)-(%d,%d)' % (n, x0, y0, x1, y1))
    if len(sys.argv) > 3:
        d = a.point(lambda v: v // 3)
        pd = d.load()
        for x, y in diff:
            pd[x, y] = (255, 0, 0)
        d.save(sys.argv[3])
    sys.exit(1)


if __name__ == '__main__':
    main()
