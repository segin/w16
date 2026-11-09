#!/usr/bin/env python3
"""nerename - change the module name of an NE file (a copy, for tests).

The module name is the first entry of the NE resident-name table (a
published file format, ARCHITECTURE.md 3). The new name must have the
same length, so nothing else in the file moves. Used to load a second
copy of a module next to a logging stand-in that takes its name (e.g.
VGA.DRV as VGAREAL behind GD1, tests/gdiboot.sh), only ever in scratch
copies of a Windows install.

usage: nerename.py IN OUT NEWNAME
"""
import struct
import sys


def main():
    src, dst, new = sys.argv[1], sys.argv[2], sys.argv[3].upper().encode()
    d = bytearray(open(src, 'rb').read())
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    assert d[ne:ne + 2] == b'NE', 'not an NE file'
    p = ne + struct.unpack_from('<H', d, ne + 0x26)[0]
    n = d[p]
    if n != len(new):
        sys.exit(f'nerename: {d[p + 1:p + 1 + n].decode()} has {n} letters, {new.decode()} {len(new)}')
    d[p + 1:p + 1 + n] = new
    open(dst, 'wb').write(d)


if __name__ == '__main__':
    main()
