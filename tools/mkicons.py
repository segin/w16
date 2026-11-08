#!/usr/bin/env python3
"""mkicons - draw w16's original icons and write them as Windows 3.x .ICO
files (one 32x32 16-colour image each: BITMAPINFOHEADER, palette, XOR
bits, AND mask).

The artwork is our own (ARCHITECTURE.md 6.8: artwork, logos and games get
original designs). The shapes are deliberately simple and recognisable
for their purpose; they borrow the three-slab motif of the w16 boot
splash (assets/boot).

usage: mkicons.py OUTDIR      writes progman.ico, group.ico, defitem.ico
"""
import os
import struct
import sys

# VGA palette in Windows system order
PAL = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
    (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
    (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
    (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]
BLACK, MAROON, GREEN, OLIVE, NAVY, PURPLE, TEAL, SILVER = range(8)
GRAY, RED, LIME, YELLOW, BLUE, FUCHSIA, CYAN, WHITE = range(8, 16)
CLEAR = None


class Icon:
    def __init__(self):
        self.px = [[CLEAR] * 32 for _ in range(32)]

    def rect(self, x0, y0, x1, y1, c):
        """filled, inclusive"""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.px[y][x] = c

    def frame(self, x0, y0, x1, y1, c):
        self.rect(x0, y0, x1, y0, c)
        self.rect(x0, y1, x1, y1, c)
        self.rect(x0, y0, x0, y1, c)
        self.rect(x1, y0, x1, y1, c)

    def window(self, x0, y0, x1, y1, title=NAVY, body=WHITE):
        """a small window: black outline, title bar, body"""
        self.rect(x0, y0, x1, y1, body)
        self.rect(x0, y0, x1, y0 + 4, title)
        self.frame(x0, y0, x1, y1, BLACK)
        self.rect(x0, y0 + 5, x1, y0 + 5, BLACK)

    def slab(self, x, y, w, face, top, side_fill, edge=BLACK):
        """one slab, as in the boot splash: a box seen from above and to
        the front-left. Rows y..y+3 hold the top face, slanting 1 pixel
        per row; rows y+4..y+7 the front face (x..x+w); the right side
        face joins the front's right edge to the back corner (x+w+4, y)."""
        for r in range(8):
            # top face (rows 0..3) or front face (rows 4..7)
            if r < 4:
                x0, x1, c = x + 4 - r, x + 4 - r + w, top
            else:
                x0, x1, c = x, x + w, face
            self.rect(x0, y + r, x1, y + r, c)
            # right side face: from the front's right edge to the back edge
            back = x + w + 4 if r < 4 else x + w + 7 - r
            if back > x1:
                self.rect(x1 + 1, y + r, back, y + r, side_fill)
                self.px[y + r][back] = edge
            # left outline
            self.px[y + r][x0] = edge
            if r == 4 or r == 7:
                self.rect(x0, y + r, x1, y + r, edge)
            else:
                self.px[y + r][x1] = edge
        self.rect(x + 4, y, x + 4 + w, y, edge)    # back edge of the top

    def ico(self):
        rows = []
        mask = []
        for y in range(31, -1, -1):            # bottom-up
            row = bytearray()
            for x in range(0, 32, 2):
                a, b = self.px[y][x], self.px[y][x + 1]
                row.append(((a or 0) << 4) | (b or 0))
            rows.append(bytes(row))
            m = 0
            for x in range(32):
                m = (m << 1) | (1 if self.px[y][x] is CLEAR else 0)
            mask.append(struct.pack('>I', m))
        xor = b''.join(rows)
        andm = b''.join(mask)
        bih = struct.pack('<IiiHHIIiiII', 40, 32, 64, 1, 4, 0,
                          len(xor) + len(andm), 0, 0, 16, 0)
        pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in PAL)
        img = bih + pal + xor + andm
        head = struct.pack('<HHH', 0, 1, 1)
        entry = struct.pack('<BBBBHHII', 32, 32, 16, 0, 1, 4, len(img), 6 + 16)
        return head + entry + img


def progman():
    """the shell: a window holding the w16 slab stack"""
    i = Icon()
    i.window(1, 2, 30, 29, NAVY, SILVER)
    # bottom first, so each slab rests on the one below
    i.slab(5, 18, 17, WHITE, SILVER, GRAY)
    i.slab(5, 14, 17, TEAL, CYAN, NAVY)
    i.slab(5, 10, 17, BLUE, CYAN, NAVY)
    return i


def group():
    """a program group: a window with three small program tiles"""
    i = Icon()
    i.window(1, 4, 30, 27, NAVY, WHITE)
    for n, (fill, edge) in enumerate(((BLUE, NAVY), (CYAN, TEAL), (SILVER, GRAY))):
        x = 5 + n * 8
        i.rect(x, 13, x + 5, 18, fill)
        i.frame(x, 13, x + 5, 18, edge)
        i.rect(x, 21, x + 5, 21, GRAY)
    return i


def defitem():
    """a program with no icon of its own: a plain application window"""
    i = Icon()
    i.window(2, 4, 29, 27, TEAL, WHITE)
    i.rect(3, 10, 28, 10, SILVER)
    for y in (14, 17, 20, 23):
        i.rect(6, y, 22 if y != 23 else 15, y, GRAY)
    return i


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, fn in (('progman', progman), ('group', group), ('defitem', defitem)):
        with open(os.path.join(out, name + '.ico'), 'wb') as f:
            f.write(fn().ico())


if __name__ == '__main__':
    main()
