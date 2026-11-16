#!/usr/bin/env python3
"""nenoname - make NE exports ordinal-only, in place.

wlink gives every export a name; some Windows modules export ordinals with
no name at all (SHELL.DLL's 104-128, docs/exportdb). This removes the
names of the given ordinals from the resident and non-resident name
tables (the tables shrink; the space freed at their end is zeroed) and
clears their entries' "exported" flags.

usage: nenoname.py FILE ORDINAL[-ORDINAL]...
"""
import struct
import sys


def strip(d, start, size_avail, drop):
    """rewrite the name table at start without the dropped ordinals;
    returns the new length (including the terminating 0)"""
    p, out = start, bytearray()
    while True:
        n = d[p]
        if n == 0:
            break
        name = d[p + 1:p + 1 + n]
        ordinal = struct.unpack_from('<H', d, p + 1 + n)[0]
        if not (ordinal in drop and p != start):
            out += bytes([n]) + name + struct.pack('<H', ordinal)
        p += 3 + n
    old = p + 1 - start
    out += b'\0'
    d[start:start + old] = out + b'\0' * (old - len(out))
    return len(out)


def main():
    path = sys.argv[1]
    if '.tools/ref' in path.replace('\\', '/'):
        sys.exit('refusing to modify the reference install')
    drop = set()
    for a in sys.argv[2:]:
        lo, _, hi = a.partition('-')
        drop.update(range(int(lo), int(hi or lo) + 1))
    d = bytearray(open(path, 'rb').read())
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    assert d[ne:ne + 2] == b'NE'
    resname = ne + struct.unpack_from('<H', d, ne + 0x26)[0]
    strip(d, resname, None, drop)
    nonres = struct.unpack_from('<I', d, ne + 0x2C)[0]
    n = strip(d, nonres, None, drop)
    struct.pack_into('<H', d, ne + 0x20, n)
    # their entry flags: not marked exported (as Windows 3.1's SHELL has
    # them: flags 0, docs/exportdb)
    p = ne + struct.unpack_from('<H', d, ne + 0x04)[0]
    ordinal = 1
    while d[p]:
        count, kind = d[p], d[p + 1]
        p += 2
        if kind == 0:
            ordinal += count
            continue
        size = 6 if kind == 0xFF else 3
        for _ in range(count):
            if ordinal in drop:
                d[p] = 0
            p += size
            ordinal += 1
    open(path, 'wb').write(d)


if __name__ == '__main__':
    main()
