#!/usr/bin/env python3
"""mkiterated - make a copy of an NE program whose automatic data segment
is stored iterated (segment flag 0008h, Microsoft's NE format: records of
a WORD repeat count, a WORD byte count and that many bytes, the bytes
repeated count times).

The segment's data moves to the end of the file (sector-aligned), with
its relocation records after it, as iterated records: one record for
each stretch of plain bytes, and one repeated record for PATTERN (a byte
string that must occur in the data, made of a 2-byte unit repeated: its
length / 2 copies of the unit). The expanded data is the same as the
original, so the program runs as before when the loader expands it.

usage: mkiterated.py IN.EXE OUT.EXE PATTERN
"""
import struct
import sys


def main():
    src, dst, pat = sys.argv[1], sys.argv[2], sys.argv[3].encode('latin-1')
    if len(pat) < 4 or len(pat) % 2 or pat != pat[:2] * (len(pat) // 2):
        sys.exit('mkiterated: PATTERN must be a 2-byte unit repeated')
    d = bytearray(open(src, 'rb').read())
    ne = struct.unpack_from('<H', d, 0x3C)[0]
    if d[ne:ne + 2] != b'NE':
        sys.exit('mkiterated: not an NE file')
    seg_off, = struct.unpack_from('<H', d, ne + 0x22)
    auto, = struct.unpack_from('<H', d, ne + 0x0E)
    shift, = struct.unpack_from('<H', d, ne + 0x32)
    shift = shift or 9
    ent = ne + seg_off + 8 * (auto - 1)
    sector, flen, flags, minalloc = struct.unpack_from('<HHHH', d, ent)
    flen = flen or 0x10000
    start = sector << shift
    data = bytes(d[start:start + flen])
    relocs = b''
    if flags & 0x0100:
        n, = struct.unpack_from('<H', d, start + flen)
        relocs = bytes(d[start + flen:start + flen + 2 + 8 * n])
    at = data.find(pat)
    if at < 0:
        sys.exit('mkiterated: PATTERN not in the data segment')
    out = b''
    if at:
        out += struct.pack('<HH', 1, at) + data[:at]
    out += struct.pack('<HH', len(pat) // 2, 2) + pat[:2]
    rest = data[at + len(pat):]
    if rest:
        out += struct.pack('<HH', 1, len(rest)) + rest
    align = 1 << shift
    while len(d) % align:
        d.append(0)
    new_sector = len(d) >> shift
    d += out + relocs
    struct.pack_into('<HHH', d, ent, new_sector, len(out), flags | 0x0008)
    open(dst, 'wb').write(d)


if __name__ == '__main__':
    main()
