#!/usr/bin/env python3
"""ne2bmp - write one BITMAP resource of an NE file as a .BMP file (for
tests only: tests/wrmsres.sh links it into a scratch copy of a w16
program; ARCHITECTURE.md 3, test-only combinations). Copies the bytes
without looking at them; the output never goes into the repository.

usage: ne2bmp.py IN.EXE ID OUT.BMP
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nerc  # noqa: E402


def main():
    d = open(sys.argv[1], 'rb').read()
    want = int(sys.argv[2])
    for t, r, _, b in nerc.resources(d):
        if t == 2 and r == want:
            hs, _, _, _, bpp = struct.unpack_from('<IiiHH', b, 0)
            ncol = struct.unpack_from('<I', b, 32)[0] or (1 << bpp if bpp <= 8 else 0)
            off = 14 + hs + 4 * ncol
            open(sys.argv[3], 'wb').write(b'BM' + struct.pack('<IHHI', 14 + len(b), 0, 0, off) + b)
            return
    sys.exit('no bitmap %d' % want)


if __name__ == '__main__':
    main()
