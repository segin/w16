#!/usr/bin/env python3
"""mkhelpart - w16 Windows Help's artwork, all w16's own (ARCHITECTURE.md
6.8): the program's icon (a blue book with a yellow question mark), the
hand cursor shown over hot spots (a pointing hand), the name shown with
no file open ("w16 Help" in white italics, over a navy-to-black dither
the program draws at any size) and the annotation mark (a small green paper clip).

usage: mkhelpart.py OUTDIR    writes winhelp.ico, hand.cur, logo.bmp, annot.bmp
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
    """the name for the no-file screen: "w16" over "Help" in white
    italics, a monochrome picture cropped to the letters (1: letter); the
    program draws the navy-to-black dither itself at any window size,
    then the letters' shadow 3 pixels down and right in black, then the
    letters in white, centred"""
    from PIL import Image, ImageDraw, ImageFont
    w, h = 400, 200
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
    for text, y in (('w16', 10), ('Help', 75)):
        tw = d.textlength(text, font=font)
        d.text(((w - tw) / 2 + 20, y), text, fill=255, font=font)
    box = im.point(lambda v: 255 if v > 128 else 0).getbbox()
    im = im.crop(box)
    bits = [[1 if im.getpixel((x, y)) > 128 else 0 for x in range(im.width)] for y in range(im.height)]
    return bmp1(im.width, im.height, bits)


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
    for name, data in (('winhelp.ico', icon()), ('hand.cur', cursor()), ('logo.bmp', logo()),
                       ('annot.bmp', annot())):
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main()
