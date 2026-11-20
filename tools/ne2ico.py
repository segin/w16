#!/usr/bin/env python3
"""ne2ico - write one GROUP_ICON resource of an NE file, with its images,
as a .ICO file (for tests only: tests/wfmsres.sh links it into a scratch
copy of a w16 program; ARCHITECTURE.md 3, test-only combinations). Copies
the bytes without looking at them; the output never goes into the
repository. The .ICO layout is the Windows 3.1 SDK's (Programmer's
Reference vol. 4, ch. 1, icon resources and files).

usage: ne2ico.py IN.EXE ID OUT.ICO
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nerc  # noqa: E402


def main():
    d = open(sys.argv[1], 'rb').read()
    want = int(sys.argv[2])
    res = list(nerc.resources(d))
    icons = {r: b for t, r, _, b in res if t == 3}
    for t, r, _, b in res:
        if t == 14 and r == want:
            n = struct.unpack_from('<H', b, 4)[0]
            head = struct.pack('<HHH', 0, 1, n)
            dirs, data = b'', b''
            off = 6 + 16 * n
            for i in range(n):
                w, h, cc, rs, pl, bc, size, iid = struct.unpack_from('<BBBBHHIH', b, 6 + 14 * i)
                img = icons[iid]
                dirs += struct.pack('<BBBBHHII', w, h, cc, rs, pl, bc, len(img), off + len(data))
                data += img
            open(sys.argv[3], 'wb').write(head + dirs + data)
            return
    sys.exit('no icon group %d' % want)


if __name__ == '__main__':
    main()
