#!/usr/bin/env python3
"""nemovsel - drop wlink's unnamed movable entries for selector fixups, in place.

A selector-only fixup (relocation type 2) that targets a movable segment,
such as the "mov ax, DGROUP" a __loadds function begins with in a DLL
whose DGROUP is movable, is linked by wlink as an internal reference
through a movable entry (0FFh, ordinal) that it adds to the entry table:
an unnamed, unexported entry after the module's last export (w16's
USER.EXE had one, 533, at 1:01A4, the start of the segment its target
lies in). A selector fixup needs no entry: the NE internal reference by
segment number gives the segment's selector whether it is fixed or
movable, and Windows 3.1's own USER.EXE refers to its movable data
segments that way (its relocation records: type 2, internal, segment 44
and 45; the format only, read with tools/neinfo.py). w16's KERNEL reads
it so (kernel/loader.c, target()).

This rewrites every such fixup to refer to the segment by number and
removes the entries nothing else refers to (unnamed, not exported, and no
other relocation names them), so that the entry table holds the module's
exports only. The entry table is rebuilt in place (shorter; the space
freed at its end is zeroed) and the header's movable entry count updated.

usage: nemovsel.py FILE
"""
import struct
import sys


def main():
    path = sys.argv[1]
    d = bytearray(open(path, 'rb').read())
    ne = struct.unpack_from('<H', d, 0x3C)[0]
    ent_off = ne + struct.unpack_from('<H', d, ne + 4)[0]
    ent_len = struct.unpack_from('<H', d, ne + 6)[0]
    nseg = struct.unpack_from('<H', d, ne + 0x1C)[0]
    segtab = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
    restab = ne + struct.unpack_from('<H', d, ne + 0x26)[0]
    nonres = struct.unpack_from('<I', d, ne + 0x2C)[0]
    shift = struct.unpack_from('<H', d, ne + 0x32)[0]

    # the entry table: ordinal -> (indicator, raw entry bytes)
    entries, p, o = {}, ent_off, 1
    while True:
        n, ind = d[p], d[p + 1] if d[p] else 0
        if n == 0:
            break
        p += 2
        if ind == 0:
            o += n
            continue
        size = 6 if ind == 0xFF else 3
        for _ in range(n):
            entries[o] = (ind, bytes(d[p:p + size]))
            p += size
            o += 1

    def names(at):
        out, q = set(), at
        while d[q]:
            n = d[q]
            out.add(struct.unpack_from('<H', d, q + 1 + n)[0])
            q += 3 + n
        return out
    named = names(restab) | (names(nonres) if nonres else set())

    # relocations
    relocs = []                         # (file offset, type, flags, t1, t2)
    for i in range(nseg):
        off, ln, fl, _ = struct.unpack_from('<4H', d, segtab + 8 * i)
        if not fl & 0x100 or not off:
            continue
        p = (off << shift) + (ln or 0x10000)
        for j in range(struct.unpack_from('<H', d, p)[0]):
            q = p + 2 + 8 * j
            st, flg, _, t1, t2 = struct.unpack_from('<BBHHH', d, q)
            relocs.append((q, st, flg, t1, t2))

    drop = set()
    for o, (ind, raw) in entries.items():
        if ind != 0xFF or raw[0] & 1 or o in named:
            continue
        refs = [r for r in relocs if r[2] & 3 == 0 and r[3] == 0xFF and r[4] == o]
        if all(r[1] == 2 for r in refs):
            drop.add(o)
            for q, *_ in refs:          # internal reference by segment number
                struct.pack_into('<HH', d, q + 4, raw[3], 0)
    if not drop:
        return

    # rebuild the entry table without them
    keep = sorted(o for o in entries if o not in drop)
    out, o, i = bytearray(), 1, 0
    while i < len(keep):
        if keep[i] > o:                 # a gap: unused ordinals
            gap = min(keep[i] - o, 255)
            out += bytes([gap, 0])
            o += gap
            continue
        ind = entries[keep[i]][0]
        j = i
        while (j < len(keep) and j - i < 255 and keep[j] == o + (j - i)
               and entries[keep[j]][0] == ind):
            j += 1
        out += bytes([j - i, ind])
        for k in keep[i:j]:
            out += entries[k][1]
        o += j - i
        i = j
    out += b'\0'
    assert len(out) <= ent_len
    d[ent_off:ent_off + ent_len] = out + bytes(ent_len - len(out))
    struct.pack_into('<H', d, ne + 6, len(out))
    struct.pack_into('<H', d, ne + 0x30, sum(1 for k in keep if entries[k][0] == 0xFF))
    open(path, 'wb').write(d)
    print(f'{path}: dropped unnamed movable entries {sorted(drop)}')


if __name__ == '__main__':
    main()
