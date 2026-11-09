#!/usr/bin/env python3
"""mkshfiles - the test files probe SH6 (tests/apps/sh6.c) extracts icons
from, made from w16's own icon art (build/shell/res, tools/mkicons.py):

  SHI.ICO   a one-image icon file (Program Manager's stock icon 0)
  SHI2.ICO  three images: mono 32x32, 16-colour 16x16, 16-colour 32x32
            (stock icon 1), to see which one ExtractIcon picks
  SHX.XYZ   a file with no association

usage: mkshfiles.py ICONDIR OUTDIR
"""
import os
import struct
import sys


def bih(w, h, bpp):
    return struct.pack('<IiiHHIIiiII', 40, w, 2 * h, 1, bpp, 0, 0, 0, 0, 0, 0)


def main():
    res, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    one = open(os.path.join(res, 'stock00.ico'), 'rb').read()
    open(os.path.join(out, 'SHI.ICO'), 'wb').write(one)
    src = open(os.path.join(res, 'stock01.ico'), 'rb').read()
    off = struct.unpack_from('<I', src, 6 + 12)[0]
    size = struct.unpack_from('<I', src, 6 + 8)[0]
    img32 = src[off:off + size]
    mono = bih(32, 32, 1) + b'\0\0\0\0\xff\xff\xff\0' + bytes([0xAA, 0x55, 0xAA, 0x55] * 32) + bytes(128)
    pal = src[off + 40:off + 40 + 64]
    small = bih(16, 16, 4) + pal + bytes([0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0] * 16) + bytes(64)
    imgs = [(32, 32, 2, 1, mono), (16, 16, 16, 4, small), (32, 32, 16, 4, img32)]
    o = 6 + 16 * len(imgs)
    dirs = data = b''
    for w, h, c, bpp, d in imgs:
        dirs += struct.pack('<BBBBHHII', w, h, c, 0, 1, bpp, len(d), o)
        o += len(d)
        data += d
    open(os.path.join(out, 'SHI2.ICO'), 'wb').write(struct.pack('<3H', 0, 1, len(imgs)) + dirs + data)
    open(os.path.join(out, 'SHX.XYZ'), 'wb').write(b'x')


if __name__ == '__main__':
    main()
