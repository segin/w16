#!/usr/bin/env python3
"""omfimp - list IMPDEF records (module, ordinal or name, symbol) in an
OMF import library, e.g. OpenWatcom's lib286/win/windows.lib.

Used to build our export ordinal tables. OpenWatcom is open source (Sybase
Open Watcom Public License), so its import libraries are an allowed source
of these facts (see ARCHITECTURE.md, Source and Clean-Room Policy).

usage: omfimp.py library.lib [MODULE]
prints: MODULE ordinal symbol      (or MODULE "name" symbol for by-name)
"""
import struct
import sys

THEADR, COMENT, MODEND, MODEND32, LIBHDR, LIBEND = 0x80, 0x88, 0x8A, 0x8B, 0xF0, 0xF1


def pstr(b, i):
    n = b[i]
    return b[i + 1:i + 1 + n].decode('latin-1'), i + 1 + n


def records(data):
    i, page = 0, 16
    while i + 3 <= len(data):
        rtype = data[i]
        rlen = struct.unpack_from('<H', data, i + 1)[0]
        body = data[i + 3:i + 3 + rlen - 1]          # strip checksum
        if rtype == LIBHDR:
            page = rlen + 3
        yield rtype, body
        i += 3 + rlen
        if rtype in (MODEND, MODEND32, LIBHDR):
            i = (i + page - 1) // page * page       # modules are page aligned
        elif rtype == LIBEND:
            return


def main():
    path = sys.argv[1]
    only = sys.argv[2].upper() if len(sys.argv) > 2 else None
    with open(path, 'rb') as f:
        data = f.read()
    out = []
    for rtype, body in records(data):
        if rtype != COMENT or len(body) < 4 or body[1] != 0xA0 or body[2] != 0x01:
            continue
        byord = body[3]
        sym, i = pstr(body, 4)
        mod, i = pstr(body, i)
        if byord:
            key = struct.unpack_from('<H', body, i)[0]
            ref = str(key)
        else:
            name, i = pstr(body, i)
            key, ref = 0x10000, '"%s"' % (name or sym)
        if only is None or mod.upper() == only:
            out.append((mod.upper(), key, ref, sym))
    for mod, key, ref, sym in sorted(set(out)):
        print(mod, ref, sym)


if __name__ == '__main__':
    main()
