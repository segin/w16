#!/usr/bin/env python3
"""crdcmp - compare two Cardfile .CRD files byte for byte, except where the
original writes uninitialised memory: a 3.0 (MGC) picture that Cardfile
3.1 made from an object is a bitmap one pixel wider and higher than the
object's rectangle, and that last column, last row and the row padding
are not drawn (docs/crd-format.md). Bits there may differ.

Exit status 0 when equal (prints "same" or "same except undrawn bits"),
1 when not (prints the first differing offset).

usage: crdcmp.py A.CRD B.CRD
"""
import struct
import sys


def undrawn(d):
    """offset -> mask of the bits that may differ, for an MGC file"""
    if d[:3] != b'MGC':
        return {}
    n = struct.unpack_from('<H', d, 3)[0]
    free = {}
    for i in range(n):
        off = struct.unpack_from('<I', d, 5 + 52 * i + 6)[0]
        size = struct.unpack_from('<H', d, off)[0]
        if not size:
            continue
        w, h = struct.unpack_from('<HH', d, off + 2)
        bits = off + 10
        wb = size // h if h else 0
        for r in range(h):
            for x in range(wb * 8):
                if x >= w - 1 or r == h - 1:
                    o = bits + r * wb + x // 8
                    free[o] = free.get(o, 0) | (0x80 >> (x % 8))
    return free


def main():
    a = open(sys.argv[1], 'rb').read()
    b = open(sys.argv[2], 'rb').read()
    if a == b:
        print('same')
        return 0
    if len(a) != len(b):
        print('differ: sizes %d and %d' % (len(a), len(b)))
        return 1
    free = undrawn(a)
    for i, (x, y) in enumerate(zip(a, b)):
        if (x ^ y) & ~free.get(i, 0):
            print('differ at offset %d' % i)
            return 1
    print('same except undrawn bits')
    return 0


if __name__ == '__main__':
    sys.exit(main())
