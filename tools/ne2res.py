#!/usr/bin/env python3
"""ne2res - copy an NE file's resources into a Win16 .RES file (for tests).

Reads the NE resource table (a published file format, ARCHITECTURE.md 3)
and writes every resource, with its type, name and memory flags, as a
16-bit .RES file that wlink can link into another module. Used only at
test time on scratch copies, e.g. to give the logging stand-in DISPLAY
driver GD1 the resources of the driver it stands in front of (GDI and
USER load the display driver's resources by module handle); the output
never goes into the repository.

usage: ne2res.py IN.EXE OUT.RES
"""
import struct
import sys


def main():
    d = open(sys.argv[1], 'rb').read()
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    rt = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
    if rt == ne + struct.unpack_from('<H', d, ne + 0x26)[0]:
        open(sys.argv[2], 'wb').close()          # no resources
        return
    shift = struct.unpack_from('<H', d, rt)[0]
    p = rt + 2

    def rname(v):
        if v & 0x8000:
            return b'\xff' + struct.pack('<H', v & 0x7FFF)
        q = rt + v
        return d[q + 1:q + 1 + d[q]].upper() + b'\0'
    out = bytearray()
    while True:
        tid, cnt = struct.unpack_from('<HH', d, p)
        if tid == 0:
            break
        p += 8
        for _ in range(cnt):
            off, ln, flags, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            data = d[off << shift:(off << shift) + (ln << shift)]
            out += rname(tid) + rname(rid) + struct.pack('<HI', flags & 0x1070, len(data)) + data
    open(sys.argv[2], 'wb').write(out)


if __name__ == '__main__':
    main()
