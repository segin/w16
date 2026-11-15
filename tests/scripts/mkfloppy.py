#!/usr/bin/env python3
"""mkfloppy - a blank, formatted 1.44 MB floppy image (FAT12, as DOS's
FORMAT lays it out: 512-byte sectors, 18 per track, 2 heads, 80 tracks,
2 FATs of 9 sectors, 224 root entries, media byte F0h), for DOSBox's
imgmount (tests/sysprobe.sh).

usage: mkfloppy.py IMAGE
"""
import struct
import sys

SECT = 512
TOTAL = 2880
boot = bytearray(SECT)
boot[0:3] = b'\xEB\x3C\x90'
boot[3:11] = b'W16     '
struct.pack_into('<HBHBHHBHHHII', boot, 11,
                 SECT, 1, 1, 2, 224, TOTAL, 0xF0, 9, 18, 2, 0, 0)
struct.pack_into('<BBBI11s8s', boot, 36, 0, 0, 0x29, 0x16161616, b'NO NAME    ', b'FAT12   ')
boot[510:512] = b'\x55\xAA'
img = bytearray(TOTAL * SECT)
img[0:SECT] = boot
for f in range(2):
    off = SECT * (1 + 9 * f)
    img[off:off + 3] = b'\xF0\xFF\xFF'
open(sys.argv[1], 'wb').write(img)
