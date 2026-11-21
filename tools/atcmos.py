#!/usr/bin/env python3
"""atcmos - write the CMOS image (an 86Box .nvr file) of an AMI 386 BIOS
(1990, as on 86Box's "asus386", ASUS ISA-386C) set up to boot from an IDE
hard disk without stopping in the BIOS: test tooling for tests/86box.sh.

usage: atcmos.py OUT.NVR --cyl N --heads H --sectors S [--mem KB] [--no87]

The standard AT layout (IBM AT Technical Reference): RTC status registers
A-D, no floppy drives (10h), hard disk 0 of type 47 (12h, 19h) with the
given geometry in the user type block (1Bh-23h), the equipment byte (14h:
EGA/VGA, a coprocessor unless --no87), base memory 640 KB (15h),
extended memory (17h, 30h), the checksum of 10h-2Dh (2Eh, big-endian).
The AMI-specific bytes (11h, 13h, 2Dh, 34h-3Dh with their checksum at
3Eh) are what that BIOS's own setup saved after "Auto configuration with
BIOS defaults", "Numeric Processor: Present" and "Wait for <F1> if any
error: Disabled" (observed in 86Box, 2026-11-21).
"""
import argparse


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--cyl', type=int, required=True)
    ap.add_argument('--heads', type=int, required=True)
    ap.add_argument('--sectors', type=int, required=True)
    ap.add_argument('--mem', type=int, default=8192)
    ap.add_argument('--no87', action='store_true')
    a = ap.parse_args()

    c = bytearray(b'\xff' * 128)
    c[0:0x10] = bytes(0x10)
    # the clock: 10:00:00, 15 June 1992 (a Monday), in BCD
    c[0x00], c[0x02], c[0x04] = 0x00, 0x00, 0x10
    c[0x06], c[0x07], c[0x08], c[0x09] = 0x02, 0x15, 0x06, 0x92
    c[0x0A], c[0x0B], c[0x0D] = 0x26, 0x02, 0x80
    c[0x10:0x34] = bytes(0x24)
    c[0x10] = 0x00                          # no floppy drives
    c[0x11] = 0x3D                          # AMI
    c[0x12] = 0xF0                          # disk 0: type in 19h
    c[0x13] = 0x3B                          # AMI
    c[0x14] = 0x0D if a.no87 else 0x0F      # EGA/VGA, 87 (bit 1)
    c[0x15], c[0x16] = 0x80, 0x02           # 640 KB
    ext = max(0, a.mem - 1024)
    c[0x17], c[0x18] = ext & 0xFF, ext >> 8
    c[0x19] = 47
    c[0x1B], c[0x1C] = a.cyl & 0xFF, a.cyl >> 8
    c[0x1D] = a.heads
    c[0x1E], c[0x1F] = 0xFF, 0xFF           # no write precompensation
    c[0x20] = 0x08 if a.heads > 8 else 0x00
    c[0x21], c[0x22] = a.cyl & 0xFF, a.cyl >> 8
    c[0x23] = a.sectors
    c[0x2D] = 0x18                          # AMI
    s = sum(c[0x10:0x2E])
    c[0x2E], c[0x2F] = s >> 8, s & 0xFF
    c[0x30], c[0x31] = ext & 0xFF, ext >> 8
    c[0x32] = 0x19                          # century
    c[0x34:0x3E] = bytes([0x00, 0x0F, 0x11, 0x80, 0x1B, 0x7B, 0x21, 0, 0, 0])
    s = sum(c[0x34:0x3E])
    c[0x3E], c[0x3F] = s >> 8, s & 0xFF
    with open(a.out, 'wb') as f:
        f.write(bytes(c))


if __name__ == '__main__':
    main()
