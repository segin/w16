#!/usr/bin/env python3
"""szdd - expand a file compressed with Microsoft's COMPRESS (the "SZDD"
format of the Windows 3.1 Setup disks: a 14-byte header, then LZSS with a
4096-byte window primed with spaces), as EXPAND.EXE does. A published,
widely documented format; used by tests to put printer drivers from the
reference Setup disks into scratch installs.

usage: szdd.py IN OUT
"""
import struct
import sys


def expand(d):
    if d[:8] != b'SZDD\x88\xf0\x27\x33':
        raise ValueError('not an SZDD file')
    size = struct.unpack_from('<I', d, 10)[0]
    win = bytearray(b' ' * 4096)
    pos = 4096 - 16
    out = bytearray()
    p = 14
    while p < len(d) and len(out) < size:
        flags = d[p]
        p += 1
        for bit in range(8):
            if p >= len(d) or len(out) >= size:
                break
            if flags & (1 << bit):
                c = d[p]
                p += 1
                out.append(c)
                win[pos] = c
                pos = (pos + 1) & 4095
            else:
                if p + 1 >= len(d):
                    break
                off = d[p] | ((d[p + 1] & 0xF0) << 4)
                ln = (d[p + 1] & 0x0F) + 3
                p += 2
                for i in range(ln):
                    c = win[(off + i) & 4095]
                    out.append(c)
                    win[pos] = c
                    pos = (pos + 1) & 4095
    return bytes(out[:size])


if __name__ == '__main__':
    open(sys.argv[2], 'wb').write(expand(open(sys.argv[1], 'rb').read()))
