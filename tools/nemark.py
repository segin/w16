#!/usr/bin/env python3
"""nemark - set the expected Windows version in an NE executable's header
(offset 3Eh of the NE header: minor in the low byte, major in the high).

The Windows 2.x builds of w16 programs are linked like the 3.x ones and
then marked 2.x, since Windows 2.x will not load a program that says it
needs 3.0.

usage: nemark.py FILE.EXE MAJOR.MINOR      e.g. nemark.py PROGMAN.EXE 2.0
"""
import struct
import sys

path, ver = sys.argv[1], sys.argv[2]
major, minor = (int(x) for x in ver.split('.'))
with open(path, 'r+b') as f:
    data = bytearray(f.read())
    if data[:2] != b'MZ':
        sys.exit('%s: not an MZ executable' % path)
    ne = struct.unpack_from('<I', data, 0x3C)[0]
    if data[ne:ne + 2] != b'NE':
        sys.exit('%s: no NE header' % path)
    struct.pack_into('<BB', data, ne + 0x3E, minor, major)
    f.seek(0)
    f.write(data)
