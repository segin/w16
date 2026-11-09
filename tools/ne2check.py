#!/usr/bin/env python3
"""ne2check - list the imports of an NE file (by ordinal) that Windows/386
2.11 lacks, from the presence columns of docs/exports/*.txt (observed
black-box, tests/apps/texp.c). For the 2.x builds of w16's programs.

usage: ne2check.py FILE.EXE        exit status 1 if any import is missing
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def table(mod):
    p = os.path.join(ROOT, 'docs', 'exports', mod + '.txt')
    t = {}
    if not os.path.exists(p):
        return None
    for l in open(p):
        f = l.split()
        if len(f) >= 5 and f[0].isdigit():
            t[int(f[0])] = (f[2] == 'yes', f[4])
    return t


def main():
    d = open(sys.argv[1], 'rb').read()
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    nseg = struct.unpack_from('<H', d, ne + 0x1C)[0]
    modtab = ne + struct.unpack_from('<H', d, ne + 0x28)[0]
    imptab = ne + struct.unpack_from('<H', d, ne + 0x2A)[0]
    nmods = struct.unpack_from('<H', d, ne + 0x1E)[0]
    segtab = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
    shift = struct.unpack_from('<H', d, ne + 0x32)[0]
    mods = []
    for i in range(nmods):
        o = struct.unpack_from('<H', d, modtab + 2 * i)[0]
        n = d[imptab + o]
        mods.append(d[imptab + o + 1:imptab + o + 1 + n].decode())
    used = set()
    for s in range(nseg):
        off, ln, flags, _ = struct.unpack_from('<HHHH', d, segtab + 8 * s)
        if not (flags & 0x100) or not off:
            continue
        start = off << shift
        size = ln or 0x10000
        r = start + size
        cnt = struct.unpack_from('<H', d, r)[0]
        for k in range(cnt):
            atype, rtype, _, a, b = struct.unpack_from('<BBHHH', d, r + 2 + 8 * k)
            if rtype & 3 == 1:
                used.add((mods[a - 1], b))
    bad = 0
    for mod, o in sorted(used):
        t = table(mod)
        if t is None:
            print('%s.%d: no table' % (mod, o))
            bad = 1
            continue
        ok, name = t.get(o, (False, '?'))
        if not ok:
            print('%s.%d %s: not on Windows/386 2.11' % (mod, o, name))
            bad = 1
    sys.exit(bad)


if __name__ == '__main__':
    main()
