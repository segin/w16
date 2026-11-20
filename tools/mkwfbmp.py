#!/usr/bin/env python3
"""mkwfbmp - w16 File Manager's small pictures: our own artwork
(ARCHITECTURE.md 6.8), one 4-bit .BMP strip of 208 x 46 pixels.

The strip's layout is the one File Manager's drawing code expects
(shell/winfile/bitmap.c), so that a test copy can carry 3.1's strip in
its place (tests/wfmsres.sh) and screens compare the drawing, not the art:
  rows 0-15   thirteen 16 x 16 pictures on pure blue (drawn on a selected
              line: the blue becomes the highlight colour),
  rows 16-31  the same pictures on pure magenta (the window colour),
  rows 32-45  six 27 x 14 drive pictures, then magenta.
The pictures, left to right: program, document, other file, hidden or
system file, the parent directory (".."), closed folder, closed folder
with "+", open folder, open with "+", open with "-", closed with "-",
shared folder closed, shared folder open; drives: CD-ROM, floppy, hard
disk, RAM disk, network, network (not connected).

usage: mkwfbmp.py OUT.BMP
"""
import struct
import sys

PAL = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
    (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
    (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
    (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]
BLACK, MAROON, GREEN, OLIVE, NAVY, PURPLE, TEAL, SILVER = range(8)
GRAY, RED, LIME, YELLOW, BLUE, FUCHSIA, CYAN, WHITE = range(8, 16)

W, H = 208, 46
px = [[FUCHSIA] * W for _ in range(H)]


def put(x, y, c):
    if 0 <= x < W and 0 <= y < H:
        px[y][x] = c


def rect(x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(x, y, c)


def frame(x0, y0, x1, y1, c):
    rect(x0, y0, x1, y0, c)
    rect(x0, y1, x1, y1, c)
    rect(x0, y0, x0, y1, c)
    rect(x1, y0, x1, y1, c)


# ---- the 16 x 16 pictures, drawn at (ox, oy) ----

def program(ox, oy):
    # a small application window: black frame, navy title, white client
    # with a silver menu line
    frame(ox + 1, oy + 2, ox + 14, oy + 13, BLACK)
    rect(ox + 2, oy + 3, ox + 13, oy + 4, NAVY)
    rect(ox + 2, oy + 5, ox + 13, oy + 12, WHITE)
    rect(ox + 2, oy + 6, ox + 13, oy + 6, SILVER)
    put(ox + 12, oy + 3, WHITE)


def page(ox, oy):
    # a sheet with its top right corner turned down
    frame(ox + 3, oy + 1, ox + 12, oy + 14, BLACK)
    rect(ox + 4, oy + 2, ox + 11, oy + 13, WHITE)
    for k in range(3):
        put(ox + 10 + k, oy + 1, FUCHSIA if px[oy + 1][ox + 10 + k] != BLUE else BLUE)
    # the fold
    rect(ox + 10, oy + 1, ox + 12, oy + 3, None)
    put(ox + 10, oy + 1, BLACK)
    put(ox + 10, oy + 2, BLACK)
    put(ox + 10, oy + 3, BLACK)
    put(ox + 11, oy + 3, BLACK)
    put(ox + 12, oy + 3, BLACK)
    put(ox + 11, oy + 2, BLACK)
    put(ox + 12, oy + 4, BLACK)


def document(ox, oy):
    page(ox, oy)
    for y in (5, 7, 9, 11):
        rect(ox + 5, oy + y, ox + (9 if y == 5 else 10), oy + y, TEAL)


def hidden(ox, oy):
    page(ox, oy)
    # a red diamond: a file to leave alone
    for d in range(3):
        rect(ox + 7 - d, oy + 6 + d, ox + 8 + d, oy + 6 + d, RED)
        rect(ox + 7 - d, oy + 11 - d, ox + 8 + d, oy + 11 - d, RED)


def updir(ox, oy):
    # a hooked arrow pointing up, and two dots
    rect(ox + 3, oy + 5, ox + 4, oy + 12, GREEN)
    rect(ox + 3, oy + 11, ox + 8, oy + 12, GREEN)
    rect(ox + 2, oy + 4, ox + 5, oy + 4, GREEN)
    rect(ox + 1, oy + 5, ox + 1, oy + 5, GREEN)
    rect(ox + 6, oy + 5, ox + 6, oy + 5, GREEN)
    rect(ox + 3, oy + 3, ox + 4, oy + 3, GREEN)
    put(ox + 11, oy + 12, BLACK)
    put(ox + 14, oy + 12, BLACK)


def folder(ox, oy, mark=None, shared=False):
    # tab on the left, body below
    rect(ox + 2, oy + 3, ox + 6, oy + 3, BLACK)
    put(ox + 1, oy + 4, BLACK)
    put(ox + 7, oy + 4, BLACK)
    frame(ox + 1, oy + 5, ox + 14, oy + 13, BLACK)
    rect(ox + 2, oy + 4, ox + 6, oy + 4, YELLOW)
    rect(ox + 2, oy + 6, ox + 13, oy + 12, YELLOW)
    rect(ox + 2, oy + 6, ox + 13, oy + 6, WHITE)
    rect(ox + 2, oy + 12, ox + 13, oy + 12, OLIVE)
    marks(ox, oy, mark, 7)
    if shared:
        hand(ox, oy)


def folder_open(ox, oy, mark=None, shared=False):
    # the back of the folder, and its front leaning out
    rect(ox + 2, oy + 2, ox + 6, oy + 2, BLACK)
    put(ox + 1, oy + 3, BLACK)
    put(ox + 7, oy + 3, BLACK)
    frame(ox + 1, oy + 4, ox + 12, oy + 13, BLACK)
    rect(ox + 2, oy + 3, ox + 6, oy + 3, OLIVE)
    rect(ox + 2, oy + 5, ox + 11, oy + 12, OLIVE)
    # the front: a parallelogram from (4,7)-(15,7) to (1,13)-(12,13)
    for i, y in enumerate(range(7, 14)):
        x0 = ox + 4 - (i * 3) // 6
        x1 = ox + 15 - (i * 3) // 6
        rect(x0, oy + y, x1, oy + y, YELLOW)
        put(x0, oy + y, BLACK)
        put(x1, oy + y, BLACK)
    rect(ox + 4, oy + 7, ox + 15, oy + 7, BLACK)
    rect(ox + 1, oy + 13, ox + 12, oy + 13, BLACK)
    marks(ox, oy, mark, 10)
    if shared:
        hand(ox, oy)


def marks(ox, oy, mark, cy):
    if mark is None:
        return
    rect(ox + 6, oy + cy, ox + 10, oy + cy, BLACK)
    if mark == '+':
        rect(ox + 8, oy + cy - 2, ox + 8, oy + cy + 2, BLACK)


def hand(ox, oy):
    # a shared folder: a small navy bar under it
    rect(ox + 4, oy + 14, ox + 11, oy + 15, NAVY)


# ---- the 27 x 14 drive pictures ----

def drive_box(ox, oy, face=SILVER):
    frame(ox, oy, ox + 26, oy + 13, BLACK)
    rect(ox + 1, oy + 1, ox + 25, oy + 12, face)
    rect(ox + 1, oy + 1, ox + 25, oy + 1, WHITE)
    rect(ox + 1, oy + 1, ox + 1, oy + 12, WHITE)
    rect(ox + 1, oy + 12, ox + 25, oy + 12, GRAY)
    rect(ox + 25, oy + 1, ox + 25, oy + 12, GRAY)


def cdrom(ox, oy):
    drive_box(ox, oy)
    rect(ox + 4, oy + 5, ox + 22, oy + 6, BLACK)
    # the disc
    for y, (a, b) in enumerate(((10, 16), (8, 18), (8, 18), (10, 16)), start=4):
        rect(ox + a, oy + y, ox + b, oy + y, CYAN)
    put(ox + 13, oy + 5, BLACK)
    put(ox + 13, oy + 6, BLACK)
    put(ox + 21, oy + 9, GREEN)


def floppy(ox, oy):
    drive_box(ox, oy)
    rect(ox + 4, oy + 6, ox + 22, oy + 7, BLACK)
    rect(ox + 11, oy + 5, ox + 15, oy + 8, GRAY)
    put(ox + 21, oy + 10, GREEN)


def hard(ox, oy):
    drive_box(ox, oy)
    rect(ox + 4, oy + 4, ox + 22, oy + 4, GRAY)
    rect(ox + 4, oy + 7, ox + 22, oy + 7, GRAY)
    rect(ox + 4, oy + 9, ox + 5, oy + 10, RED)


def ram(ox, oy):
    drive_box(ox, oy)
    rect(ox + 7, oy + 4, ox + 19, oy + 9, BLACK)
    for x in range(8, 20, 2):
        put(ox + x, oy + 3, BLACK)
        put(ox + x, oy + 10, BLACK)


def network(ox, oy, gone=False):
    drive_box(ox, oy)
    c = GRAY if gone else BLACK
    rect(ox + 6, oy + 3, ox + 20, oy + 7, c)
    rect(ox + 7, oy + 4, ox + 19, oy + 6, SILVER if gone else TEAL)
    rect(ox + 13, oy + 8, ox + 13, oy + 10, c)
    rect(ox + 4, oy + 10, ox + 22, oy + 10, c)


def main():
    for row, bg in ((0, BLUE), (16, FUCHSIA)):
        rect(0, row, W - 1, row + 15, bg)
        pics = [program, document, page, hidden, updir,
                folder, lambda x, y: folder(x, y, '+'),
                folder_open, lambda x, y: folder_open(x, y, '+'),
                lambda x, y: folder_open(x, y, '-'), lambda x, y: folder(x, y, '-'),
                lambda x, y: folder(x, y, None, True),
                lambda x, y: folder_open(x, y, None, True)]
        for i, f in enumerate(pics):
            f(16 * i, row)
        # page()'s corner cut: what it marked None is background
        for y in range(row, row + 16):
            for x in range(W):
                if px[y][x] is None:
                    px[y][x] = bg
    for i, f in enumerate((cdrom, floppy, hard, ram, network,
                           lambda x, y: network(x, y, True))):
        f(27 * i, 32)
    # 4-bit bottom-up BMP
    stride = ((W * 4 + 31) // 32) * 4
    bits = bytearray()
    for y in range(H - 1, -1, -1):
        row = bytearray(stride)
        for x in range(W):
            if x & 1:
                row[x // 2] |= px[y][x]
            else:
                row[x // 2] |= px[y][x] << 4
        bits += row
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in PAL)
    hdr = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 4, 0, len(bits), 0, 0, 16, 0)
    off = 14 + len(hdr) + len(pal)
    with open(sys.argv[1], 'wb') as f:
        f.write(b'BM' + struct.pack('<IHHI', off + len(bits), 0, 0, off) + hdr + pal + bits)


if __name__ == '__main__':
    main()
