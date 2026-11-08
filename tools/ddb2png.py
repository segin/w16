#!/usr/bin/env python3
"""ddb2png - convert SNAP2's .DDB screenshots (Windows 2.x device-dependent
bitmap bits, see tests/apps/snap.c) to PNG.

Layout, as written by SNAP2: header "DDB2", cx, cy, band height,
planes | bitspixel << 8, bytes per plane row (words); then one
GetBitmapBits block per band. For the 4-plane VGA format each scan line
block holds plane 0's rows for the whole band, then plane 1's, and so
on (observed on Windows/386 2.11's VGA driver); a pixel's colour index
has plane n as bit n (blue, green, red, intensity: the EGA/VGA order).
Windows/386 2.11's VGA driver keeps memory bitmaps in 3 planes, 8
colours, plane 0 red, 1 green, 2 blue (observed).

usage: ddb2png.py IN.DDB OUT.PNG
"""
import struct
import sys
from PIL import Image

EGA = [(0, 0, 0), (0, 0, 128), (0, 128, 0), (0, 128, 128), (128, 0, 0), (128, 0, 128),
       (128, 128, 0), (192, 192, 192), (128, 128, 128), (0, 0, 255), (0, 255, 0),
       (0, 255, 255), (255, 0, 0), (255, 0, 255), (255, 255, 0), (255, 255, 255)]


def main():
    d = open(sys.argv[1], 'rb').read()
    if d[:4] != b'DDB2':
        sys.exit('not a SNAP2 DDB file')
    cx, cy, band, pb, wb = struct.unpack_from('<5H', d, 4)
    planes, bpp = pb & 0xFF, pb >> 8
    img = Image.new('RGB', (cx, cy))
    px = img.load()
    off = 14
    for y0 in range(0, cy, band):
        blk = d[off:off + wb * planes * band]
        off += wb * planes * band
        for yy in range(band):
            y = y0 + yy
            if y >= cy:
                break
            for x in range(cx):
                if bpp == 1:
                    v = 0
                    for p in range(planes):
                        b = blk[p * wb * band + yy * wb + x // 8]
                        v |= ((b >> (7 - x % 8)) & 1) << p
                    if planes == 4:
                        px[x, y] = EGA[v]
                    elif planes == 3:           # 8 colours: red, green, blue
                        px[x, y] = (255 * (v & 1), 255 * (v >> 1 & 1), 255 * (v >> 2 & 1))
                    else:
                        px[x, y] = (255, 255, 255) if v else (0, 0, 0)
                else:
                    v = blk[yy * wb + x * bpp // 8]
                    px[x, y] = (v, v, v)
    img.save(sys.argv[2])


if __name__ == '__main__':
    main()
