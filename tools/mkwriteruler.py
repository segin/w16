#!/usr/bin/env python3
"""mkwriteruler - w16 Write's ruler art (our own artwork, ARCHITECTURE.md
6.8): monochrome .BMP strips with the cell sizes Write 3.1's ruler uses on
VGA (format facts: its bitmap resources' dimensions, ARCHITECTURE.md 15.6).

  buttons: 18 cells of 22 x 15 in one strip, 396 x 15 (resource 102): the
           nine ruler buttons (left tab, decimal tab, single, 1 1/2 and
           double spacing, left, centered, right, justified), up, then the
           same nine down
  marks:   5 cells of 12 x 11, 60 x 11 (resource 202): first-line indent,
           left indent, right indent, left tab, decimal tab; a mark's
           position is the cell's column 6

Black ink on white. The glyphs are w16's own drawings.

usage: mkwriteruler.py buttons|marks OUT.BMP
"""
import struct
import sys

BW, BH = 22, 15

# button glyphs, 14 x 9, drawn centred in the button ('#' ink)
GLYPHS = [
    # left tab: a stem with a flag to the right
    ["..............",
     "......#.......",
     "......##......",
     "......###.....",
     "......#.......",
     "......#.......",
     "......#.......",
     ".....#####....",
     ".............."],
    # decimal tab: the same stem, a point beside it
    ["..............",
     "......#.......",
     "......##......",
     "......###.....",
     "......#.......",
     "......#.......",
     "......#...##..",
     ".....###..##..",
     ".............."],
    # single spacing
    ["..............",
     "..##########..",
     "..............",
     "..##########..",
     "..............",
     "..##########..",
     "..............",
     "..............",
     ".............."],
    # 1 1/2 spacing
    ["..............",
     "..##########..",
     "..............",
     "..............",
     "..##########..",
     "..............",
     "..............",
     "..##########..",
     ".............."],
    # double spacing
    ["..............",
     "..............",
     "..##########..",
     "..............",
     "..............",
     "..............",
     "..##########..",
     "..............",
     ".............."],
    # left
    ["..............",
     "..##########..",
     "..............",
     "..######......",
     "..............",
     "..#########...",
     "..............",
     "..#####.......",
     ".............."],
    # centered
    ["..............",
     "..##########..",
     "..............",
     "....######....",
     "..............",
     "...########...",
     "..............",
     ".....####.....",
     ".............."],
    # right
    ["..............",
     "..##########..",
     "..............",
     "......######..",
     "..............",
     "...#########..",
     "..............",
     ".......#####..",
     ".............."],
    # justified
    ["..............",
     "..##########..",
     "..............",
     "..##########..",
     "..............",
     "..##########..",
     "..............",
     "..######......",
     ".............."],
]

MARKS = [
    # first-line indent: a small block
    ["............", "............", "............", "............", "......##....",
     "......##....", "......##....", "............", "............", "............",
     "............"],
    # left indent: a wedge pointing right from column 6
    ["......#.....", "......##....", "......###...", "......####..", "......#####.",
     "......######", "......#####.", "......####..", "......###...", "......##....",
     "......#....."],
    # right indent: a wedge pointing left, ending at column 6
    [".....#......", "....##......", "...###......", "..####......", ".#####......",
     "######......", ".#####......", "..####......", "...###......", "....##......",
     ".....#......"],
    # left tab: a corner, the stem at column 6
    ["............", "............", "......#.....", "......#.....", "......#.....",
     "......#.....", "......#.....", "......#.....", "......#####.", "............",
     "............"],
    # decimal tab: an upturned T with a point
    ["............", "............", "......#.....", "......#.....", "......#.....",
     "......#.....", "......#..##.", "......#..##.", "....#####...", "............",
     "............"],
]


def button(up, g):
    """One 22 x 15 button: a rounded outline (filled when down)."""
    rows = [[0] * BW for _ in range(BH)]
    for y in range(1, BH - 1):
        for x in range(0, BW):
            # a 1-pixel frame with its corners cut by two pixels
            cut = (y in (1, BH - 2) and x in (0, 1, BW - 2, BW - 1)) or \
                  (y in (2, BH - 3) and x in (0, BW - 1))
            if cut:
                continue
            edge = y in (1, BH - 2) or x in (0, BW - 1)
            rows[y][x] = 1 if (edge or not up) else 0
    for gy, line in enumerate(g):
        for gx, c in enumerate(line):
            if c == '#':
                rows[3 + gy][4 + gx] ^= 1
    return rows


def strip(cells):
    h = len(cells[0])
    return [sum((c[y] for c in cells), []) for y in range(h)]


def bmp(rows):
    """A monochrome DIB file, 1 = black ink (palette 0 white, 1 black)."""
    h, w = len(rows), len(rows[0])
    stride = (w + 31) // 32 * 4
    bits = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray(stride)
        for x in range(w):
            if not rows[y][x]:
                line[x // 8] |= 0x80 >> (x % 8)     # bit set = white
        bits += line
    pal = bytes([0, 0, 0, 0, 255, 255, 255, 0])     # 0 black, 1 white
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 1, 0, len(bits), 0, 0, 2, 2)
    off = 14 + len(info) + len(pal)
    return b'BM' + struct.pack('<IHHI', off + len(bits), 0, 0, off) + info + pal + bytes(bits)


def main():
    kind, out = sys.argv[1], sys.argv[2]
    if kind == 'buttons':
        rows = strip([button(True, g) for g in GLYPHS] + [button(False, g) for g in GLYPHS])
        assert len(rows[0]) == 396 and len(rows) == BH
    else:
        rows = strip([[[1 if c == '#' else 0 for c in r] for r in m] for m in MARKS])
        assert len(rows[0]) == 60 and len(rows) == 11
    with open(out, 'wb') as f:
        f.write(bmp(rows))


if __name__ == '__main__':
    main()
