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

    def set(self, x, y, c):
        if 0 <= x < 32 and 0 <= y < 32:
            self.px[y][x] = c

    def line(self, x0, y0, x1, y1, c):
        """Bresenham, inclusive"""
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.set(x0, y0, c)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def disc(self, cx, cy, r, c):
        """filled circle"""
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r:
                    self.set(x, y, c)

    def ring(self, cx, cy, r, c):
        """circle outline"""
        for y in range(cy - r - 1, cy + r + 2):
            for x in range(cx - r - 1, cx + r + 2):
                d = (x - cx) ** 2 + (y - cy) ** 2
                if r * r - r <= d <= r * r + r:
                    self.set(x, y, c)

    def poly(self, pts, c, edge=None):
        """filled polygon (even-odd, pixel centres), optional outline"""
        ys = [p[1] for p in pts]
        for y in range(max(0, min(ys)), min(31, max(ys)) + 1):
            xs = []
            for (xa, ya), (xb, yb) in zip(pts, pts[1:] + pts[:1]):
                if (ya <= y < yb) or (yb <= y < ya):
                    xs.append(xa + (y - ya) * (xb - xa) / (yb - ya))
            xs.sort()
            for a, b in zip(xs[::2], xs[1::2]):
                for x in range(int(round(a)), int(round(b)) + 1):
                    self.set(x, y, c)
        if edge is not None:
            for (xa, ya), (xb, yb) in zip(pts, pts[1:] + pts[:1]):
                self.line(xa, ya, xb, yb, edge)

    # a 3x5 pixel font for the few letters the icons need
    FONT = {
        'C': ['111', '100', '100', '100', '111'], ':': ['000', '010', '000', '010', '000'],
        '\\': ['100', '100', '010', '001', '001'], '>': ['100', '010', '001', '010', '100'],
        '_': ['000', '000', '000', '000', '111'], 'W': ['101', '101', '101', '111', '101'],
        '1': ['010', '110', '010', '010', '111'], '6': ['111', '100', '111', '101', '111'],
    }

    def text(self, x, y, s, c):
        for ch in s:
            for r, bits in enumerate(self.FONT[ch]):
                for k, b in enumerate(bits):
                    if b == '1':
                        self.set(x + k, y + r, c)
            x += 4

    def page(self, x0, y0, x1, y1, fold=4, body=WHITE):
        """a sheet of paper with a folded top-right corner"""
        self.poly([(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)], body, BLACK)
        self.line(x1 - fold, y0, x1 - fold, y0 + fold, BLACK)
        self.line(x1 - fold, y0 + fold, x1, y0 + fold, BLACK)

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


def main():
    """stock00.ico .. stock45.ico: PROGMAN.EXE's stock icons (stockicons.py);
    its own, group and default item icons are stock 0, 7 and 2"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import stockicons
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for n in range(len(stockicons.STOCK)):
        with open(os.path.join(out, 'stock%02d.ico' % n), 'wb') as f:
            f.write(stockicons.make(n).ico())


if __name__ == '__main__':
    main()
