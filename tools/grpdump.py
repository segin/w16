#!/usr/bin/env python3
"""grpdump - print a Program Manager .GRP file: header, items, tags.
Format as observed (see shell/progman/group.c)."""
import struct
import sys


def s(d, p):
    return d[p:d.index(b'\0', p)].decode('latin-1') if p else ''


def main():
    d = open(sys.argv[1], 'rb').read()
    (ck, cb, show, l, t, r, b, mx, my, pname, cx, cy, b1, b2, w1e, n) = \
        struct.unpack_from('<2xxxHHH4h2hHHH2BHH', d, 0)
    pad = d + b'\0' if len(d) % 2 else d
    total = sum(struct.unpack('<%dH' % (len(pad) // 2), pad)) & 0xFFFF
    print('file %d bytes, cbGroup %d, checksum %04x (sum %s)' % (len(d), cb, ck, 'ok' if total == 0 else 'BAD'))
    print('name %r show %d rc (%d,%d,%d,%d) min (%d,%d) icon %dx%d bytes %d,%d w1e %d items %d'
          % (s(d, pname), show, l, t, r, b, mx, my, cx, cy, b1, b2, w1e, n))
    for i, off in enumerate(struct.unpack_from('<%dH' % n, d, 0x22)):
        if not off:
            print('  [%d] (empty)' % i)
            continue
        it = struct.unpack_from('<2h10H', d, off)
        print('  [%d] @%04x x=%d y=%d icon=%d name=%r cmd=%r iconpath=%r hdr=%s' % (
            i, off, it[0], it[1], it[2], s(d, it[9]), s(d, it[10]), s(d, it[11]),
            d[it[6]:it[6] + it[3]].hex()))
    p = cb
    while p + 6 <= len(d):
        tid, item, c = struct.unpack_from('<HHH', d, p)
        print('  tag %04x item %d cb %d data %r' % (tid, item if item != 0xFFFF else -1, c, d[p + 6:p + c]))
        if tid == 0xFFFF or c < 6:
            break
        p += c


if __name__ == '__main__':
    main()
