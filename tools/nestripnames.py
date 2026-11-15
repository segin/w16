#!/usr/bin/env python3
"""nestripnames - make exports ordinal-only in an NE file.

Removes from the non-resident name table (NE format, a published file
format) every name that starts with PREFIX, leaving the entries
themselves, so that the ordinals are exported without a name. wlink has
no way to export by ordinal alone; w16's OLECLI exports Windows 3.1's
unnamed ordinals as OLECLI_<n> and strips the names here. The table
shrinks in place (its size field in the NE header is updated; the freed
bytes at its end are zeroed).

usage: nestripnames.py FILE PREFIX
"""
import struct
import sys


def main():
    path, prefix = sys.argv[1], sys.argv[2].encode()
    d = bytearray(open(path, 'rb').read())
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    size = struct.unpack_from('<H', d, ne + 0x20)[0]
    off = struct.unpack_from('<I', d, ne + 0x2C)[0]
    p, end = off, off + size
    keep = bytearray()
    removed = 0
    while p < end and d[p]:
        n = d[p]
        name = bytes(d[p + 1:p + 1 + n])
        rec = d[p:p + 1 + n + 2]
        if name.startswith(prefix) and p != off:
            removed += 1
        else:
            keep += rec
        p += 1 + n + 2
    keep += b'\0'
    d[off:end] = keep + b'\0' * (end - off - len(keep))
    struct.pack_into('<H', d, ne + 0x20, len(keep))
    open(path, 'wb').write(d)
    print('nestripnames: %d names removed' % removed)


main()
