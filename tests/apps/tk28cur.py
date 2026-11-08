#!/usr/bin/env python3
"""tk28cur.py OUT.CUR - a 32x32 monochrome cursor file (a black triangle,
hot spot 1,2) for TK28's resources."""
import struct
import sys

w = h = 32
xor = bytearray(128)
andm = bytearray(b'\xff' * 128)
for y in range(16):                     # a triangle: black inside
    for x in range(y + 1):
        r = h - 1 - y                   # DIB rows go bottom up
        andm[r * 4 + x // 8] &= ~(0x80 >> (x % 8)) & 0xFF
bih = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 1, 0, 0, 0, 0, 2, 0)
pal = bytes([0, 0, 0, 0, 255, 255, 255, 0])
img = bih + pal + bytes(xor) + bytes(andm)
hdr = struct.pack('<HHH', 0, 2, 1)
ent = struct.pack('<BBBBHHII', w, h, 0, 0, 1, 2, len(img), 6 + 16)
open(sys.argv[1], 'wb').write(hdr + ent + img)
