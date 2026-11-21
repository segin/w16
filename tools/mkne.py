#!/usr/bin/env python3
"""mkne - write a small NE module that holds only resources, for tests.

The module is a library with no segments and no entry points, like a
font resource file (.FON): KERNEL's LoadLibrary loads it (nothing to
initialise), and programs read its resources by handle or from the file.
The VER.DLL tests (tests/ver/mkinputs.py) use it for files with version
resources of every kind, well formed and not.

Layout (the NE format: Windows 3.1 SDK, Programmer's Reference Vol. 4,
executable-file header format): an MZ header with w16's own DOS stub,
the 64-byte NE header, the resource table (alignment shift, per type a
TYPEINFO and its NAMEINFOs, names after the table), the resident-name
table (the module name), empty module-reference, imported-name and entry
tables, the non-resident names (the description), then the resources,
each at a multiple of 1 << shift.

usage: mkne.py OUT --module NAME [--desc TEXT] [--shift N] [--flags HEX]
               [--expver M.N] RES...
RES is TYPE:NAME:FILE[:LEN] - TYPE and NAME a number (an integer
resource) or a string (a named one, upper-cased as rc does), FILE the
resource's bytes; LEN, when given, is written as the resource's length
in the table (in units of 1 << shift) instead of the data's own, to make
a table that disagrees with the data.
--stubonly writes just the MZ stub (an MS-DOS program, no NE header).
--entries N gives it N exported constant entries (ordinals 1 to N, each
value its ordinal), in bundles of 255: KERNEL keeps 5 bytes an entry
and 6 a bundle, the file 3 and 2, so a table that fits the file can be
too large for the module table (TK62, the KERNEL audit's C5).
--entry-len N writes N as the entry table's length in the NE header
instead of its own (a bad file: TK62, the audit's H9).
--dgroup N gives it one data segment, its automatic data segment, of N
bytes in the file (no heap, no stack): over FFF0h it cannot be loaded
as one selector's DGROUP (TK62, the audit's H7).
--type-name-off N writes N as every named resource type's name offset
in the resource table instead of its own (a bad file whose name runs
past the module table: TK66, the audit's medium 16).
"""
import argparse
import struct


def stub():
    """w16's own MS-DOS stub: prints a line and exits with 1"""
    msg = b'This is a w16 test module; it needs Windows.\r\n$'
    code = bytes([0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21,
                  0xB8, 0x01, 0x4C, 0xCD, 0x21])
    body = code + msg
    size = (0x40 + len(body) + 15) & ~15
    hdr = struct.pack('<2sHHHHHHHHHHHHH', b'MZ', size % 512, (size + 511) // 512, 0, 4,
                      0x20, 0xFFFF, 0, 0x200, 0, 0, 0, 0x40, 0)
    return (hdr.ljust(0x3C, b'\0') + b'\0\0\0\0' + body).ljust(size, b'\0')


def rkey(s):
    try:
        return int(s, 0)
    except ValueError:
        return s.upper()


def entry_table(n):
    """n constant entries (type FEh), ordinal i's value i"""
    out, i = b'', 1
    while i <= n:
        k = min(255, n - i + 1)
        out += bytes([k, 0xFE])
        for j in range(i, i + k):
            out += struct.pack('<BH', 1, j & 0xFFFF)
        i += k
    return out + b'\0\0'


def build(module, desc, res, shift=4, flags=0x8300, expver=0x030A, entries=0,
          entry_len=None, dgroup=0, type_name_off=None):
    """res: [(type, name, data, length-override or None)]"""
    st = stub()
    lfa = len(st)
    types = []
    for t, name, data, ovr in res:
        if not types or types[-1][0] != t:
            types.append((t, []))
        types[-1][1].append((name, data, ovr))
    # no resources: no table at all (its offset is the resident names')
    rt_len = 2 + sum(8 + 12 * len(items) for _, items in types) + 2 if res else 0
    names = b''
    name_off = {}
    for t, items in types:
        for key in [t] + [n for n, _, _ in items]:
            if isinstance(key, str) and key not in name_off:
                name_off[key] = rt_len + len(names)
                names += bytes([len(key)]) + key.encode('latin-1')
    names += b'\0' if res else b''
    resnames = bytes([len(module)]) + module.encode('latin-1') + b'\0\0' + b'\0'
    entry = entry_table(entries) if entries else b'\0\0'
    nonres = bytes([len(desc)]) + desc.encode('latin-1') + b'\0\0' + b'\0'
    off_rt = 0x40 + (8 if dgroup else 0)       # after the segment table
    off_resn = off_rt + rt_len + len(names)
    off_imp = off_resn + len(resnames)
    off_entry = off_imp
    nonres_off = lfa + off_entry + len(entry)
    unit = 1 << shift
    pos = (nonres_off + len(nonres) + unit - 1) & ~(unit - 1)
    rt = struct.pack('<H', shift)
    placed = []
    for t, items in types:
        tid = name_off[t] if isinstance(t, str) else (0x8000 | t)
        if isinstance(t, str) and type_name_off is not None:
            tid = type_name_off
        rt += struct.pack('<HHI', tid, len(items), 0)
        for name, data, ovr in items:
            ln = (len(data) + unit - 1) // unit
            rid = name_off[name] if isinstance(name, str) else (0x8000 | name)
            rt += struct.pack('<HHHHHH', pos >> shift, ln if ovr is None else ovr,
                              0x0C30, rid, 0, 0)
            placed.append((pos, data, ln))
            pos += ln * unit
    rt = rt + b'\0\0' if res else b''
    segtab = b''
    if dgroup:
        # one data segment (flags 0001h), its file data at pos
        segtab = struct.pack('<HHHH', pos >> shift, dgroup & 0xFFFF, 0x0001,
                             dgroup & 0xFFFF)
        placed.append((pos, bytes([0x5A]) * dgroup, (dgroup + unit - 1) // unit))
    ne = struct.pack('<2sBBHHIHHHHIIHHHHHHHHIHHHBBHHHH',
                     b'NE', 5, 1, off_entry, len(entry) if entry_len is None else entry_len,
                     0, flags, 1 if dgroup else 0, 0, 0, 0, 0, 1 if dgroup else 0, 0,
                     len(nonres), 0x40, off_rt, off_resn, off_imp, off_imp,
                     nonres_off, 0, shift, 0, 2, 0, 0, 0, 0, expver)
    out = bytearray(st + ne + segtab + rt + names + resnames + entry + nonres)
    struct.pack_into('<I', out, 0x3C, lfa)
    for p, data, ln in placed:
        out += b'\0' * (p - len(out))
        out += data + b'\0' * (ln * unit - len(data))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--module', default='TESTMOD')
    ap.add_argument('--desc', default='w16 test module')
    ap.add_argument('--shift', type=int, default=4)
    ap.add_argument('--flags', default='8300')
    ap.add_argument('--expver', default='3.10')
    ap.add_argument('--stubonly', action='store_true')
    ap.add_argument('--entries', type=int, default=0)
    ap.add_argument('--entry-len', type=lambda v: int(v, 0), default=None)
    ap.add_argument('--dgroup', type=lambda v: int(v, 0), default=0)
    ap.add_argument('--type-name-off', type=lambda v: int(v, 0), default=None)
    ap.add_argument('res', nargs='*')
    a = ap.parse_args()
    if a.stubonly:
        open(a.out, 'wb').write(stub())
        return
    res = []
    for r in a.res:
        p = r.split(':')
        data = open(p[2], 'rb').read()
        res.append((rkey(p[0]), rkey(p[1]), data, int(p[3]) if len(p) > 3 else None))
    mj, mn = (int(x) for x in a.expver.split('.'))
    open(a.out, 'wb').write(build(a.module, a.desc, res, a.shift, int(a.flags, 16),
                                  (mj << 8) | mn, a.entries, a.entry_len, a.dgroup,
                                  a.type_name_off))


if __name__ == '__main__':
    main()
