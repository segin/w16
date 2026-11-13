#!/usr/bin/env python3
"""mkcdart - w16 COMMDLG's pictures (w16 originals, MIT).

Writes into OUTDIR:
  drives.bmp     bitmap 576: eight 16x16 cells for the file dialogs: open
                 folder, current open folder, closed folder, floppy drive,
                 fixed disk, CD-ROM, network drive, RAM disk; blue
                 (0,0,255) is the background the dialog replaces
  fonttype.bmp   bitmap 38: the font list's 20x12 marks (TrueType, printer
                 font), on blue (normal) and magenta (selected) rows
  cross.bmp      bitmap 584: the colour field's 9x9 cross
  arrow.bmp      bitmap 30: the luminosity arrow, 8x10
  portrait.ico, landscape.ico   icons 528, 529 (Print Setup)
The layout (cell sizes, positions, key colours) is the original's, read
from its resources as data; every picture is drawn here anew.
"""
import os
import struct
import sys

# the 16 VGA colours, indexed by the letters of the drawings below
PAL = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
       (0, 128, 128), (128, 128, 128), (192, 192, 192), (255, 0, 0), (0, 255, 0),
       (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]
LETTER = {'#': 0, 'r': 1, 'g': 2, 'y': 3, 'n': 4, 'm': 5, 'c': 6, '=': 7, '-': 8,
          'R': 9, 'G': 10, 'Y': 11, '.': 12, 'M': 13, 'C': 14, 'W': 15}

# ---- the file dialogs' strip: 8 cells of 16x16 ('.' = background) ----
OPEN = """
................
................
..####..........
.#WWWW#.........
.#WYYYY######...
.#WYYYYYYYYY#...
.#WY############
.#WY#YYYYYYYYYY#
.#W#YYYYYYYYYY#.
.#W#YYYYYYYYYY#.
.##YYYYYYYYYY#..
.##YYYYYYYYYY#..
.#############..
................
................
................
"""
OPENCUR = """
................
................
..####..........
.#WWWW#.........
.#WYYYY######...
.#WYYYYYYYYY#...
.#WY############
.#WY#yYyYyYyYyY#
.#W#yYyYyYyYyY#.
.#W#YyYyYyYyYy#.
.##yYyYyYyYyY#..
.##YyYyYyYyYy#..
.#############..
................
................
................
"""
CLOSED = """
................
................
..####..........
.#WWWW#.........
.#WYYYY#######..
.#WYYYYYYYYYYY#.
.#WYYYYYYYYYYY#.
.#WYYYYYYYYYYY#.
.#WYYYYYYYYYYY#.
.#WYYYYYYYYYYY#.
.#WYYYYYYYYYYY#.
.#=YYYYYYYYYYY#.
..############..
................
................
................
"""
FLOPPY = """
................
................
................
................
.##############.
#--------------#
#-WWWWWWWWWWWW-#
#-W##########W-#
#-WWWWWWWWWWWW-#
#------------=-#
#--------===---#
.##############.
................
................
................
................
"""
HARD = """
................
................
................
................
.##############.
#WWWWWWWWWWWWWW#
#W-------------#
#W--=-=-=-=----#
#W-------------#
#W---------GG--#
#==============#
.##############.
................
................
................
................
"""
CDROM = """
................
................
................
.##############.
#WWWWWWWWWWWWWW#
#W---------RR--#
#W-------------#
#W-##########--#
#W-#WWWWWWWW#--#
#W-##########--#
#==============#
.##############.
................
................
................
................
"""
NET = """
................
................
................
.##############.
#WWWWWWWWWWWWWW#
#W---------GG--#
#W-------------#
#W-------------#
#==============#
.######..######.
......#..#......
......#..#......
.....######.....
.....#CCCC#.....
.....######.....
................
"""
RAM = """
................
................
................
.....#.#.#.#....
....##########..
....#--------#..
...##-#----#-##.
....#--------#..
...##-#----#-##.
....#--------#..
...##-#----#-##.
....#--------#..
....##########..
.....#.#.#.#....
................
................
"""

# ---- font marks: 20x12, TrueType (an outlined T over a shadow) and
# printer (a small printer); two rows: key blue, key magenta ----
TT = """
....................
..##########........
..#WWWWWWWW#........
..####WW####........
.....#WW#=====......
.....#WW#=#==#......
.....#WW#..#=#......
.....#WW#..#=#......
.....#WW#..#=#......
....##WW##.#=#......
....#WWWW#####......
....######..........
"""
PRN = """
....................
.......######.......
.......#WWWW#.......
.......#W##W#.......
.....##########.....
....#----------#....
....#--------GG#....
....#==========#....
....#=WWWWWWWW=#....
.....#W######W#.....
......#WWWWWW#......
......########......
"""
CROSS = """
....#....
....#....
.........
.........
##.....##
.........
.........
....#....
....#....
"""
ARROW = """
.......#
......##
.....#=#
....#==#
...#===#
...#===#
....#==#
.....#=#
......##
.......#
"""


def grid(art, w, h):
    rows = [r for r in art.strip('\n').split('\n')]
    assert len(rows) == h, (len(rows), h)
    for r in rows:
        assert len(r) == w, (r, w)
    return [[LETTER[c] for c in r] for r in rows]


def bmp4(path, pix):
    """a 4-bit Windows 3.x DIB file of pix (rows of palette indices)"""
    h, w = len(pix), len(pix[0])
    stride = ((w * 4 + 31) // 32) * 4
    data = bytearray()
    for row in reversed(pix):
        b = bytearray(stride)
        for x, v in enumerate(row):
            b[x // 2] |= v << (4 if x % 2 == 0 else 0)
        data += b
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 4, 0, len(data), 0, 0, 16, 16)
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in PAL)
    off = 14 + len(hdr) + len(pal)
    with open(path, 'wb') as f:
        f.write(b'BM' + struct.pack('<IHHI', off + len(data), 0, 0, off))
        f.write(hdr + pal + data)


def side_by_side(*cells):
    return [sum((c[y] for c in cells), []) for y in range(len(cells[0]))]


def ico(path, pix, mask):
    """a 32x32 16-colour icon file"""
    w = h = 32
    xor = bytearray()
    for row in reversed(pix):
        b = bytearray(16)
        for x, v in enumerate(row):
            b[x // 2] |= v << (4 if x % 2 == 0 else 0)
        xor += b
    andm = bytearray()
    for row in reversed(mask):
        b = bytearray(4)
        for x, v in enumerate(row):
            if v:
                b[x // 8] |= 0x80 >> (x % 8)
        andm += b
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 4, 0, len(xor) + len(andm), 0, 0, 16, 16)
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in PAL)
    img = hdr + pal + xor + andm
    with open(path, 'wb') as f:
        f.write(struct.pack('<HHH', 0, 1, 1))
        f.write(struct.pack('<BBBBHHII', w, h, 16, 0, 1, 4, len(img), 22))
        f.write(img)


def page(landscape):
    """a sheet of paper with lines of text and a corner fold"""
    W, H = (28, 20) if landscape else (20, 28)
    x0, y0 = (32 - W) // 2, (32 - H) // 2
    pix = [[0] * 32 for _ in range(32)]      # transparent: AND 1, XOR 0
    mask = [[1] * 32 for _ in range(32)]
    for y in range(H):
        for x in range(W):
            X, Y = x0 + x, y0 + y
            fold = x - (W - 6) > y               # the folded top right corner
            if fold:
                continue
            mask[Y][X] = 0
            edge = x == 0 or y == H - 1 or x == W - 1 or y == 0 or x - (W - 6) == y
            pix[Y][X] = 0 if edge else 15
            if not edge and y > 6 and y < H - 3 and y % 3 == 1 and 2 < x < W - 3:
                pix[Y][X] = 4                    # lines of "text"
    for y in range(6):                           # the fold's back
        for x in range(W - 6 + y, W - 6 + 6):
            if x - (W - 6) >= y:
                X, Y = x0 + x, y0 + y
                mask[Y][X] = 0
                pix[Y][X] = 7 if x - (W - 6) > y else 0
    return pix, mask


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    cells = [grid(a, 16, 16) for a in (OPEN, OPENCUR, CLOSED, FLOPPY, HARD, CDROM, NET, RAM)]
    strip = side_by_side(*cells)
    bmp4(os.path.join(out, 'drives.bmp'), strip)
    tt, prn = grid(TT, 20, 12), grid(PRN, 20, 12)
    top = side_by_side(tt, prn)
    bot = [[13 if v == 12 else v for v in row] for row in top]
    bmp4(os.path.join(out, 'fonttype.bmp'), top + bot)
    bmp4(os.path.join(out, 'cross.bmp'), grid(CROSS, 9, 9))
    bmp4(os.path.join(out, 'arrow.bmp'), grid(ARROW, 8, 10))
    p, m = page(False)
    ico(os.path.join(out, 'portrait.ico'), p, m)
    p, m = page(True)
    ico(os.path.join(out, 'landscape.ico'), p, m)


if __name__ == '__main__':
    main()
