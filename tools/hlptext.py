#!/usr/bin/env python3
"""hlptext - print the topic text of a Windows 3.x .HLP file.

For reading a program's own user documentation as text (facts about its
behaviour; never copied into w16). The .HLP layout used here is the
published one (internal file system with a B-tree directory, |SYSTEM,
|Phrases, |TOPIC blocks with LZ77 compression and phrase replacement), as
described in "Windows Undocumented File Formats" (Davis/Wallace) and the
helpdeco documentation. Formatting codes are dropped; only text strings
are printed, a blank line between records.

usage: hlptext.py FILE.HLP
"""
import struct
import sys


def lz77(b, limit=None):
    out = bytearray()
    p = 0
    while p < len(b):
        flags = b[p]
        p += 1
        for i in range(8):
            if p >= len(b):
                break
            if flags & (1 << i):
                w = struct.unpack_from('<H', b, p)[0]
                p += 2
                n, d = (w >> 12) + 3, (w & 0xFFF) + 1
                for _ in range(n):
                    out.append(out[-d])
            else:
                out.append(b[p])
                p += 1
        if limit and len(out) >= limit:
            break
    return bytes(out)


def files(d):
    dirofs = struct.unpack_from('<I', d, 4)[0]
    p = dirofs + 9
    pagesize = struct.unpack_from('<H', d, p + 4)[0]
    root, _, total, levels = struct.unpack_from('<HhHH', d, p + 26)
    pages = p + 38
    page = root
    for _ in range(levels - 1):        # down the leftmost branch
        page = struct.unpack_from('<H', d, pages + page * pagesize + 4)[0]
    res = {}
    while page != 0xFFFF:
        q = pages + page * pagesize
        n = struct.unpack_from('<H', d, q + 2)[0]
        nxt = struct.unpack_from('<H', d, q + 6)[0]
        q += 8
        for _ in range(n):
            e = d.index(b'\0', q)
            name = d[q:e].decode('latin-1')
            res[name] = struct.unpack_from('<I', d, e + 1)[0]
            q = e + 5
        page = nxt
    return res


def body(d, ofs):
    used = struct.unpack_from('<I', d, ofs + 4)[0]
    return d[ofs + 9:ofs + 9 + used]


def main():
    d = open(sys.argv[1], 'rb').read()
    fs = files(d)
    sysf = body(d, fs['|SYSTEM'])
    minor, flags = struct.unpack_from('<H', sysf, 2)[0], struct.unpack_from('<H', sysf, 10)[0]
    compressed = minor > 16 and flags in (4, 8)
    bsize = 2048 if flags == 8 else 4096
    phrases = []
    if '|Phrases' in fs:
        ph = body(d, fs['|Phrases'])
        num = struct.unpack_from('<H', ph, 0)[0]
        if minor > 16:
            size = struct.unpack_from('<I', ph, 4)[0]
            offs = struct.unpack_from('<%dH' % (num + 1), ph, 8)
            text = lz77(ph[8 + 2 * (num + 1):], size)
            base = offs[0]
        else:
            offs = struct.unpack_from('<%dH' % (num + 1), ph, 4)
            text = ph[4:]
            base = 0
            offs = [o - 4 + 4 for o in offs]
        for i in range(num):
            phrases.append(text[offs[i] - base:offs[i + 1] - base])
    topic = body(d, fs['|TOPIC'])
    stream = bytearray()
    for b in range(0, len(topic), bsize):
        blk = topic[b + 12:b + bsize]
        stream += lz77(blk, 16384) if compressed else blk
    p = 0
    while p + 21 <= len(stream):
        size, len2, _, _, len1, rtype = struct.unpack_from('<IIIIIB', stream, p)
        if size < 21:
            break
        ld2 = bytes(stream[p + len1:p + size])
        if len2 > len(ld2) and phrases:
            x = bytearray()
            i = 0
            while i < len(ld2):
                c = ld2[i]
                if 0 < c < 0x10 and i + 1 < len(ld2):
                    n = ((c - 1) << 8) | ld2[i + 1]
                    x += phrases[n >> 1]
                    if n & 1:
                        x += b' '
                    i += 2
                else:
                    x.append(c)
                    i += 1
            ld2 = bytes(x)
        if rtype in (0x02, 0x20, 0x23):
            parts = [s.decode('latin-1') for s in ld2.split(b'\0') if s]
            if parts:
                print(('TOPIC: ' if rtype == 2 else '') + ' | '.join(parts))
                print()
        p += size


if __name__ == '__main__':
    main()
