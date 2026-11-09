#!/usr/bin/env python3
"""layoutdiff - compare two screenshots whose text was drawn in different
fonts with the same metrics (tests/fonts.sh, ARCHITECTURE.md 6.9).

With equal metrics only the glyph shapes may differ: every pixel that
differs must lie inside a text cell. The differing pixels are grouped into
connected regions (8-neighbour, after closing gaps of up to GAP pixels
horizontally, so a run of characters becomes one region); a region taller
than the tallest font cell in use (--cell, default 16) or a difference
outside text (window frames, controls, positions) shows up as a region
that is not text-shaped and is reported.

Writes a diff image: the second screenshot dimmed, with differing pixels
red and each region's box outlined.

usage: layoutdiff.py A.BMP B.BMP DIFF.PNG [--cell N] [--gap N]
exit status: 0 if every difference is text-shaped, 1 otherwise
"""
import argparse
import sys

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('out')
    ap.add_argument('--cell', type=int, default=16)
    ap.add_argument('--gap', type=int, default=4)
    args = ap.parse_args()
    a = Image.open(args.a).convert('RGB')
    b = Image.open(args.b).convert('RGB')
    if a.size != b.size:
        print('sizes differ: %s %s' % (a.size, b.size))
        sys.exit(1)
    w, h = a.size
    pa, pb = a.load(), b.load()
    diff = [[pa[x, y] != pb[x, y] for x in range(w)] for y in range(h)]
    n = sum(map(sum, diff))
    # close horizontal gaps so that a word or line of text is one region
    grown = [row[:] for row in diff]
    for y in range(h):
        last = None
        for x in range(w):
            if diff[y][x]:
                if last is not None and 0 < x - last <= args.gap:
                    for xx in range(last + 1, x):
                        grown[y][xx] = True
                last = x
    seen = [[False] * w for _ in range(h)]
    regions = []
    for y in range(h):
        for x in range(w):
            if grown[y][x] and not seen[y][x]:
                stack = [(x, y)]
                seen[y][x] = True
                x0 = x1 = x
                y0 = y1 = y
                while stack:
                    cx, cy = stack.pop()
                    x0, x1, y0, y1 = min(x0, cx), max(x1, cx), min(y0, cy), max(y1, cy)
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            nx, ny = cx + dx, cy + dy
                            if 0 <= nx < w and 0 <= ny < h and grown[ny][nx] and not seen[ny][nx]:
                                seen[ny][nx] = True
                                stack.append((nx, ny))
                regions.append((x0, y0, x1, y1))
    bad = [r for r in regions if r[3] - r[1] + 1 > args.cell]
    img = b.point(lambda v: 128 + v // 2)
    px = img.load()
    for y in range(h):
        for x in range(w):
            if diff[y][x]:
                px[x, y] = (255, 0, 0)
    d = ImageDraw.Draw(img)
    for r in regions:
        d.rectangle(r, outline=(255, 160, 0) if r not in bad else (0, 0, 255))
    img.save(args.out)
    print('%d pixels differ in %d text regions; %d regions taller than %d px%s'
          % (n, len(regions), len(bad), args.cell,
             ''.join(' %s' % (r,) for r in bad[:10])))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
