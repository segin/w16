#!/usr/bin/env python3
"""mkhelpart - w16 Windows Help's artwork, all w16's own (ARCHITECTURE.md
6.8): the program's icon (a blue book with a yellow question mark), the
hand cursor shown over hot spots (a pointing hand), the logo shown with
no file open (a dithered navy-to-black field with "w16 Help" in white
italics) and the annotation mark (a small green paper clip).

usage: mkhelpart.py OUTDIR    writes winhelp.ico, hand.cur, logo1.bmp, logo2.bmp, annot.bmp
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, PAL, BLACK, WHITE, GRAY, SILVER, NAVY, BLUE, YELLOW, OLIVE  # noqa: E402


def icon():
    i = Icon()
    # the book: a cover, a page edge, a spine
    i.poly([(4, 6), (24, 2), (28, 4), (28, 27), (8, 31), (4, 29)], BLUE, BLACK)
    i.rect(8, 28, 27, 29, WHITE)
    i.line(8, 30, 27, 27, BLACK)
    i.line(4, 6, 4, 29, BLACK)
    i.rect(5, 7, 6, 28, NAVY)
    # the question mark
    i.rect(13, 9, 20, 10, YELLOW)
    i.rect(12, 11, 13, 14, YELLOW)
    i.rect(20, 11, 21, 15, YELLOW)
    i.rect(17, 16, 20, 17, YELLOW)
    i.rect(16, 18, 17, 20, YELLOW)
    i.rect(16, 23, 17, 24, YELLOW)
    for x, y in [(12, 15), (14, 15), (21, 16), (18, 21), (18, 25)]:
        i.set(x, y, OLIVE)
    return i.ico()


HAND = [
    "......##........",
    ".....#..#.......",
    ".....#..#.......",
    ".....#..#.......",
    ".....#..###.....",
    ".....#..#..##...",
    ".....#..#..#.##.",
    "..##.#..#..#.#.#",
    ".#..##........#",
    ".#...#........#",
    "..#...........#",
    "..#...........#",
    "...#.........#.",
    "...#.........#.",
    "....#.......#..",
    "....#########..",
]


def cursor():
    """32 x 32 monochrome, hot spot at the fingertip"""
    xor = [[0] * 32 for _ in range(32)]
    andm = [[1] * 32 for _ in range(32)]
    rows = [r.ljust(16, '.') for r in HAND]
    for y, r in enumerate(rows):
        # outline black, inside white: fill each row between its outline
        xs = [x for x, c in enumerate(r) if c == '#']
        if not xs:
            continue
        for x in range(xs[0], xs[-1] + 1):
            andm[y][x] = 0
            xor[y][x] = 0 if r[x] == '#' else 1
    # the fingers' top rows only between their own outlines
    def bits(img):
        out = b''
        for y in range(31, -1, -1):
            v = 0
            for x in range(32):
                v = (v << 1) | img[y][x]
            out += struct.pack('>I', v)
        return out
    xb, ab = bits(xor), bits(andm)
    bih = struct.pack('<IiiHHIIiiII', 40, 32, 64, 1, 1, 0, len(xb) + len(ab), 0, 0, 2, 0)
    pal = struct.pack('<BBBBBBBB', 0, 0, 0, 0, 255, 255, 255, 0)
    img = bih + pal + xb + ab
    head = struct.pack('<HHH', 0, 2, 1)
    entry = struct.pack('<BBBBHHII', 32, 32, 2, 0, 6, 0, len(img), 22)
    return head + entry + img


def bmp4(w, h, px):
    """a 4-bit .BMP in the VGA palette; px[y][x] palette indices"""
    stride = ((w * 4 + 31) // 32) * 4
    data = b''
    for y in range(h - 1, -1, -1):
        row = bytearray(stride)
        for x in range(w):
            if x & 1:
                row[x // 2] |= px[y][x]
            else:
                row[x // 2] |= px[y][x] << 4
        data += bytes(row)
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in PAL)
    bih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 4, 0, len(data), 0, 0, 16, 0)
    fh = struct.pack('<2sIHHI', b'BM', 14 + len(bih) + len(pal) + len(data), 0, 0, 14 + len(bih) + len(pal))
    return fh + bih + pal + data


def bmp1(w, h, bits):
    """a monochrome .BMP; bits[y][x] 0 or 1 (1: the palette's white)"""
    stride = ((w + 31) // 32) * 4
    data = b''
    for y in range(h - 1, -1, -1):
        row = bytearray(stride)
        for x in range(w):
            if bits[y][x]:
                row[x // 8] |= 0x80 >> (x & 7)
        data += bytes(row)
    pal = struct.pack('<BBBBBBBB', 0, 0, 0, 0, 255, 255, 255, 0)
    bih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 1, 0, len(data), 0, 0, 2, 0)
    fh = struct.pack('<2sIHHI', b'BM', 14 + len(bih) + len(pal) + len(data), 0, 0, 14 + len(bih) + len(pal))
    return fh + bih + pal + data


def logo():
    """417 x 430 (the default topic area on VGA): navy fading to black
    through an ordered dither, the name in white italics with a shadow.
    Two monochrome pictures, each well under 64K (the program draws the
    first with navy for its ones and black for its zeros, then ORs the
    second's ones in white): the dither (shadow cut out), the letters."""
    from PIL import Image, ImageDraw, ImageFont
    w, h = 417, 430
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    dither = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            t = 1.0 - min(1.0, max(0.0, (x / w) * 0.9 + (y / h) * 0.6 - 0.2))
            dither[y][x] = 1 if t * 16 > bayer[y & 3][x & 3] else 0
    im = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(im)
    font = None
    for name in ('NotoSans-BoldItalic.ttf', 'DejaVuSans-BoldOblique.ttf'):
        for base in ('/usr/share/fonts/noto', '/usr/share/fonts/TTF', '/usr/share/fonts/truetype/dejavu',
                     '/usr/share/fonts/truetype/noto'):
            q = os.path.join(base, name)
            if os.path.exists(q):
                font = ImageFont.truetype(q, 60)
                break
        if font:
            break
    if font is None:
        font = ImageFont.load_default()
    for text, y in (('w16', 150), ('Help', 215)):
        tw = d.textlength(text, font=font)
        x = (w - tw) / 2 + 20
        d.text((x + 3, y + 3), text, fill=1, font=font)
        d.text((x, y), text, fill=255, font=font)
    letters = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            v = im.getpixel((x, y))
            if v > 128:
                letters[y][x] = 1
                dither[y][x] = 0
            elif v == 1:
                dither[y][x] = 0
    return bmp1(w, h, dither), bmp1(w, h, letters)


def annot():
    """the annotation mark: 14 x 16, a green paper clip leaning right on
    white (w16's own drawing), sitting on the text's baseline"""
    from mkicons import GREEN
    rows = [
        "..............",
        "..............",
        "..........##..",
        ".........#..#.",
        "........#.#.#.",
        ".......#.#..#.",
        "......#.#..#..",
        ".....#.#..#...",
        "....#.#..#....",
        "...#.#..#.....",
        "...#.#.#......",
        "...#..#.......",
        "....##........",
        "..............",
        "..............",
        "..............",
    ]
    px = [[GREEN if c == '#' else WHITE for c in r] for r in rows]
    return bmp4(14, 16, px)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    logo1, logo2 = logo()
    for name, data in (('winhelp.ico', icon()), ('hand.cur', cursor()), ('logo1.bmp', logo1),
                       ('logo2.bmp', logo2), ('annot.bmp', annot())):
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main()
