#!/usr/bin/env python3
"""nerename - rename an NE module, or the modules it imports, in place.

For probe US1 (ARCHITECTURE.md 10): a scratch copy of a module gets another
module name, or its imports are pointed at other modules, so that w16's
forwarding modules (tools/mkinterpose.py) can stand between it and its
callers or callees. Only NE header tables are touched (the module name in
the resident-name table, entries of the imported-names table); names keep
their length, so no offsets move. Never run it on the reference install
itself, only on scratch copies.

usage: nerename.py FILE [--module NEW] [--import OLD=NEW]...
"""
import argparse
import struct
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('--module')
    ap.add_argument('--import', dest='imports', action='append', default=[])
    a = ap.parse_args()
    if '.tools/ref' in a.file.replace('\\', '/'):
        sys.exit('refusing to modify the reference install')
    d = bytearray(open(a.file, 'rb').read())
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    assert d[ne:ne + 2] == b'NE'
    resname, modref, impname = struct.unpack_from('<HHH', d, ne + 0x26)
    nref = struct.unpack_from('<H', d, ne + 0x1E)[0]
    if a.module:
        p = ne + resname
        n = d[p]
        old = d[p + 1:p + 1 + n].decode('latin-1')
        if len(a.module) != n:
            sys.exit(f'{a.module}: must have {n} characters like {old}')
        d[p + 1:p + 1 + n] = a.module.upper().encode('latin-1')
    for spec in a.imports:
        old, new = spec.upper().split('=')
        if len(old) != len(new):
            sys.exit(f'{spec}: names must have the same length')
        for i in range(nref):
            off = struct.unpack_from('<H', d, ne + modref + 2 * i)[0]
            p = ne + impname + off
            n = d[p]
            if d[p + 1:p + 1 + n].decode('latin-1').upper() == old:
                d[p + 1:p + 1 + n] = new.encode('latin-1')
                break
        else:
            sys.exit(f'{a.file}: does not import {old}')
    open(a.file, 'wb').write(d)


if __name__ == '__main__':
    main()
