#!/usr/bin/env python3
"""mkaboutlogo - the 64x64 picture SHELL.DLL's About box shows when the
caller gives no icon (w16 original artwork: the emblem of the boot splash,
tools/mklogo.py, three stacked slabs, over the "w16" wordmark).

Windows 3.1 shows its own logo there (a 64x64 bitmap, probe SH8); w16
draws this one in the same place instead.

usage: mkaboutlogo.py OUT.BMP [preview.png]
Requires Pillow and the Noto Sans fonts (Arch: noto-fonts).
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mklogo  # noqa: E402  (the palette, the slab drawing, the fonts)

W = H = 64


def draw():
    img = Image.new('P', (W, H), mklogo.WHITE)
    img.putpalette([c for rgb in mklogo.VGA for c in rgb] + [0] * (768 - 48))
    d = ImageDraw.Draw(img)
    d.fontmode = '1'
    p = mklogo.iso(30, 23, 2.0)
    slabs = [
        (0.0, 0.0, mklogo.BLUE, mklogo.DKBLUE, mklogo.BLACK, mklogo.DKBLUE),
        (1.6, 3.3, mklogo.DKCYAN, mklogo.DKBLUE, mklogo.BLACK, mklogo.DKBLUE),
        (3.2, 6.6, mklogo.LTGRAY, mklogo.DKGRAY, mklogo.BLACK, mklogo.BLACK),
    ]
    for sx, z, top, left, right, edge in slabs:
        mklogo.slab(d, p, sx, -sx, z, 10, 10, 1.8, top, left, right, edge)
    f = mklogo.font('NotoSans-Black.ttf', 21)
    tw = d.textlength('w16', font=f)
    d.text(((W - tw) // 2, 36), 'w16', font=f, fill=mklogo.DKBLUE)
    return img


def write_bmp(img, path):
    pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in mklogo.VGA)
    px = img.load()
    rows = bytearray()
    for y in range(H - 1, -1, -1):
        rows += bytes((px[x, y] << 4) | px[x + 1, y] for x in range(0, W, 2))
    off = 14 + 40 + len(pal)
    info = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 4, 0, len(rows), 0, 0, 16, 16)
    head = struct.pack('<2sIHHI', b'BM', off + len(rows), 0, 0, off)
    open(path, 'wb').write(head + info + pal + rows)


def main():
    img = draw()
    write_bmp(img, sys.argv[1])
    if len(sys.argv) > 2:
        img.convert('RGB').resize((W * 4, H * 4), Image.NEAREST).save(sys.argv[2])


if __name__ == '__main__':
    main()
