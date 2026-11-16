#!/usr/bin/env python3
"""crddump - print a Cardfile .CRD file's structure (docs/crd-format.md):
the signature, the object counter, each index entry (with the bytes after
the line's NUL), and each card's body: picture or object (its number,
OLE stream class and size, rectangle, type) and text.

usage: crddump.py FILE.CRD...
"""
import struct
import sys


def ole_end(d, p):
    """the end of the OLE 1.0 stream at p (KB Q99340's parsing)"""
    def lstr(p):
        n = struct.unpack_from('<I', d, p)[0]
        return d[p + 4:p + 4 + n], p + 4 + n

    def sized(p):
        n = struct.unpack_from('<I', d, p)[0]
        return p + 4 + n

    def pres(p):
        ver, fid = struct.unpack_from('<II', d, p)
        p += 8
        if fid == 0:
            return p
        cls, p = lstr(p)
        if cls.rstrip(b'\0') in (b'METAFILEPICT', b'BITMAP', b'DIB'):
            return sized(p + 8)
        cf = struct.unpack_from('<I', d, p)[0]
        p += 4
        if cf == 0:
            _, p = lstr(p)
        return sized(p)
    ver, fmt = struct.unpack_from('<II', d, p)
    cls, q = lstr(p + 8)
    if fmt == 3:
        w, h = struct.unpack_from('<ii', d, q)
        return sized(q + 8), cls, fmt, (w, h)
    if fmt == 2:
        _, q = lstr(q)
        _, q = lstr(q)
        return pres(sized(q)), cls, fmt, None
    if fmt == 1:
        _, q = lstr(q)
        _, q = lstr(q)
        _, q = lstr(q)
        return pres(q + 8), cls, fmt, None
    raise ValueError('object format %d' % fmt)


def dump(path):
    d = open(path, 'rb').read()
    sig = d[:3].decode('latin-1')
    if sig == 'RRG':
        nxt, n = struct.unpack_from('<IH', d, 3)
        p = 9
        print('%s: RRG next-object=%d cards=%d size=%d' % (path, nxt, n, len(d)))
    else:
        n = struct.unpack_from('<H', d, 3)[0]
        p = 5
        print('%s: %s cards=%d size=%d' % (path, sig, n, len(d)))
    for i in range(n):
        e = d[p:p + 52]
        off = struct.unpack_from('<I', e, 6)[0]
        field = e[11:51]
        line = field.split(b'\0')[0]
        rest = field[len(line) + 1:]
        extra = ('' if not rest.strip(b'\0') else ' after-NUL=%r' % rest)
        odd = '' if e[:6] == b'\0' * 6 and e[10] == 0 and e[51] == 0 else ' reserved=%s' % e.hex()
        print('  card %d @%d %r%s%s' % (i, off, line.decode('latin-1'), extra, odd))
        q = off
        if sig == 'RRG':
            flag = struct.unpack_from('<H', d, q)[0]
            q += 2
            if flag:
                oid = struct.unpack_from('<I', d, q)[0]
                end, cls, fmt, wh = ole_end(d, q + 4)
                cx, cy, l, t, r, b, ty = struct.unpack_from('<7h', d, end)
                print('    object #%d %s class=%r stream=%d bytes himetric=%s char=%dx%d rect=%d,%d,%d,%d type=%d'
                      % (oid, {1: 'linked', 2: 'embedded', 3: 'static'}[fmt], cls.rstrip(b'\0').decode(),
                         end - q - 4, wh, cx, cy, l, t, r, b, ty))
                q = end + 14
        else:
            size = struct.unpack_from('<H', d, q)[0]
            q += 2
            if size:
                w, h, x, y = struct.unpack_from('<4H', d, q)
                print('    bitmap %dx%d at %d,%d, %d bytes' % (w, h, x, y, size))
                q += 8 + size
        tl = struct.unpack_from('<H', d, q)[0]
        print('    text %r' % d[q + 2:q + 2 + tl].decode('latin-1'))
        p += 52


if __name__ == '__main__':
    for f in sys.argv[1:]:
        dump(f)
