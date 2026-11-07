#!/usr/bin/env python3
"""mklogo - generate the w16 boot splash (original artwork).

Produces a 640x480, 16-colour image in the standard VGA palette, written as
Windows BMP files:
  W16LOGO.BMP   4 bpp, uncompressed (BI_RGB)
  W16LOGOR.BMP  4 bpp, run-length encoded (BI_RLE4), the compact form
                that the boot logo code is meant to display

usage: mklogo.py OUTDIR [preview.png]
Requires Pillow and the Noto Sans fonts (Arch: noto-fonts).
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 640, 480

# Standard VGA 16-colour palette, in Windows' system palette order.
VGA = [
    (0x00, 0x00, 0x00), (0x80, 0x00, 0x00), (0x00, 0x80, 0x00), (0x80, 0x80, 0x00),
    (0x00, 0x00, 0x80), (0x80, 0x00, 0x80), (0x00, 0x80, 0x80), (0xC0, 0xC0, 0xC0),
    (0x80, 0x80, 0x80), (0xFF, 0x00, 0x00), (0x00, 0xFF, 0x00), (0xFF, 0xFF, 0x00),
    (0x00, 0x00, 0xFF), (0xFF, 0x00, 0xFF), (0x00, 0xFF, 0xFF), (0xFF, 0xFF, 0xFF),
]
BLACK, DKRED, DKGREEN, DKYELLOW, DKBLUE, DKMAGENTA, DKCYAN, LTGRAY = range(8)
DKGRAY, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE = range(8, 16)

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

FONT_DIR = '/usr/share/fonts/noto/'


def font(name, size):
    return ImageFont.truetype(FONT_DIR + name, size)


def dither(img, box, ca, cb, t_of_xy):
    """Ordered-dither between colours ca (t=0) and cb (t=1) inside box."""
    px = img.load()
    x0, y0, x1, y1 = box
    for y in range(y0, y1):
        for x in range(x0, x1):
            t = t_of_xy(x, y)
            px[x, y] = cb if (BAYER4[y & 3][x & 3] + 0.5) / 16 < t else ca


def iso(cx, cy, s):
    """Isometric projection: (x, y, z) in units -> screen point."""
    def p(x, y, z):
        return (cx + (x - y) * 0.866 * s, cy + (x + y) * 0.5 * s - z * s)
    return p


def slab(d, p, x, y, z, w, dpt, h, top, left, right, edge):
    """Draw a box from (x,y,z) of size w x dpt x h with three visible faces."""
    t = [p(x, y, z + h), p(x + w, y, z + h), p(x + w, y + dpt, z + h), p(x, y + dpt, z + h)]
    lf = [p(x, y + dpt, z + h), p(x + w, y + dpt, z + h), p(x + w, y + dpt, z), p(x, y + dpt, z)]
    rt = [p(x + w, y, z + h), p(x + w, y + dpt, z + h), p(x + w, y + dpt, z), p(x + w, y, z)]
    for face, colour in ((lf, left), (rt, right), (t, top)):
        d.polygon(face, fill=colour, outline=edge)


def draw():
    img = Image.new('P', (W, H), BLACK)
    img.putpalette([c for rgb in VGA for c in rgb] + [0] * (768 - 48))

    # Background: black at the top, dithered into dark blue at the bottom.
    dither(img, (0, 0, W, H), BLACK, DKBLUE, lambda x, y: 0.15 + 0.8 * y / H)

    d = ImageDraw.Draw(img)
    d.fontmode = '1'                        # no anti-aliasing: pure palette

    # Emblem: three stacked, offset slabs, like segments in a memory map.
    p = iso(184, 196, 8.5)
    slabs = [  # (x shift, z, top, left, right, edge)
        (0.0, 0.0, BLUE, DKBLUE, BLACK, CYAN),
        (1.6, 3.3, DKCYAN, DKBLUE, BLACK, CYAN),
        (3.2, 6.6, WHITE, LTGRAY, DKGRAY, WHITE),
    ]
    for sx, z, top, left, right, edge in slabs:
        slab(d, p, sx, -sx, z, 10, 10, 1.8, top, left, right, edge)
    # Wordmark and tagline.
    x0 = 332
    d.text((x0, 112), 'w16', font=font('NotoSans-Black.ttf', 132), fill=WHITE)
    d.line([(x0 + 6, 282), (604, 282)], fill=CYAN)
    d.line([(x0 + 6, 283), (604, 283)], fill=DKCYAN)
    d.text((x0 + 6, 294), '16-bit operating environment',
           font=font('NotoSans-Regular.ttf', 21), fill=LTGRAY)

    # Footer.
    foot = 'Version 0.1  ·  Clean-room implementation'
    f = font('NotoSans-Regular.ttf', 14)
    tw = d.textlength(foot, font=f)
    d.text(((W - tw) // 2, 440), foot, font=f, fill=DKGRAY)
    return img


# ---- BMP writers (4 bpp) --------------------------------------------------

def bmp(pixels_bytes, compression, img):
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in VGA)
    off = 14 + 40 + len(pal)
    info = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 4, compression,
                       len(pixels_bytes), 3780, 3780, 16, 16)   # 96 dpi
    head = struct.pack('<2sIHHI', b'BM', off + len(pixels_bytes), 0, 0, off)
    return head + info + pal + pixels_bytes


def rows_bottom_up(img):
    px = img.load()
    for y in range(H - 1, -1, -1):
        yield [px[x, y] for x in range(W)]


def raw4(img):
    out = bytearray()
    for row in rows_bottom_up(img):
        line = bytearray((row[i] << 4) | row[i + 1] for i in range(0, W, 2))
        line += b'\0' * (-len(line) % 4)
        out += line
    return bytes(out)


def rle4(img):
    """BI_RLE4: encoded runs where they pay off, absolute runs elsewhere."""
    out = bytearray()

    def absolute(pix):
        while pix:
            chunk, pix = pix[:252], pix[252:]
            if len(chunk) < 3:              # absolute mode needs >= 3 pixels
                for c in chunk:
                    out.extend((1, c << 4))
                continue
            out.extend((0, len(chunk)))
            data = bytearray()
            for i in range(0, len(chunk), 2):
                hi = chunk[i]
                lo = chunk[i + 1] if i + 1 < len(chunk) else 0
                data.append((hi << 4) | lo)
            data += b'\0' * (len(data) & 1)     # word-align
            out.extend(data)

    for row in rows_bottom_up(img):
        i, lit = 0, []
        while i < W:
            # Run of a repeating 2-colour pattern (covers solid and dither).
            a = row[i]
            b = row[i + 1] if i + 1 < W else a
            n = 2 if i + 1 < W else 1
            while i + n < W and n < 255 and row[i + n] == (a if n % 2 == 0 else b):
                n += 1
            if n >= 6:
                absolute(lit)
                lit = []
                out.extend((n, (a << 4) | b))
                i += n
            else:
                lit.append(row[i])
                i += 1
        absolute(lit)
        out.extend((0, 0))                  # end of line
    out[-2:] = b'\0\1'                      # end of bitmap replaces last EOL
    return bytes(out)


def main():
    outdir = sys.argv[1]
    img = draw()
    assert max(img.tobytes()) < 16
    with open(outdir + '/W16LOGO.BMP', 'wb') as f:
        f.write(bmp(raw4(img), 0, img))
    with open(outdir + '/W16LOGOR.BMP', 'wb') as f:
        f.write(bmp(rle4(img), 2, img))
    if len(sys.argv) > 2:
        img.convert('RGB').save(sys.argv[2])


if __name__ == '__main__':
    main()
