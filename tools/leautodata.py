#!/usr/bin/env python3
"""leautodata.py FILE: make a VxD that wlink linked from C load under
Windows 3.1's WIN386: the automatic data object number (the LE header's
field at 94h, which wlink sets from DGROUP) cleared, and every object
executable (wlink's single object of code and data, tests/vxd/vxm.c,
comes out READ WRITE without EXEC). Windows 3.1 did not call the probe
VxD VXM.386 otherwise; its own VxDs and w16's in assembly have 0 and
EXEC."""
import struct
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
le = struct.unpack_from('<I', d, 0x3C)[0]
assert d[le:le + 2] == b'LE', 'not an LE module'
struct.pack_into('<I', d, le + 0x94, 0)
objtab, nobj = struct.unpack_from('<II', d, le + 0x40)
for i in range(nobj):
    o = le + objtab + i * 24 + 8
    struct.pack_into('<I', d, o, struct.unpack_from('<I', d, o)[0] | 4)
open(p, 'wb').write(d)
