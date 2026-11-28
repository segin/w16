#!/usr/bin/env python3
# us40bmp.py OUTDIR - the bitmaps of probe US40 (tests/apps/us40.c,
# us40.rc): 8 x 8, 1 bit a pixel, columns 0-3 colour 0 and 4-7 colour 1,
# with the colour tables the probe names
import os, struct, sys

def bmp(name, c0, c1, core=False):
    rows = b"".join(bytes([0x0F, 0, 0, 0]) for _ in range(8))
    if core:
        hdr = struct.pack("<IHHHH", 12, 8, 8, 1, 1)
        ct = bytes([c0[2], c0[1], c0[0], c1[2], c1[1], c1[0]])
    else:
        hdr = struct.pack("<IiiHHIIiiII", 40, 8, 8, 1, 1, 0, len(rows), 0, 0, 2, 0)
        ct = bytes([c0[2], c0[1], c0[0], 0, c1[2], c1[1], c1[0], 0])
    off = 14 + len(hdr) + len(ct)
    fh = struct.pack("<2sIHHI", b"BM", off + len(rows), 0, 0, off)
    with open(os.path.join(sys.argv[1], name), "wb") as f:
        f.write(fh + hdr + ct + rows)

K, W, R, G = (0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 255, 0)
bmp("us40bw.bmp", K, W)
bmp("us40wb.bmp", W, K)
bmp("us40rw.bmp", R, W)
bmp("us40rg.bmp", R, G)
bmp("us40rwc.bmp", R, W, True)
bmp("us40bwc.bmp", K, W, True)
