#!/usr/bin/env python3
"""mkfot.py - write the .FOT for a TrueType file, as w16's
CreateScalableFontResource (gdi/ttfont.c) writes it at run time, so that
`make install-tree` can install TrueType fonts the way 3.1's Setup does:
the .TTF and its .FOT in SYSTEM, a WIN.INI [fonts] line naming the .FOT
(ARCHITECTURE.md 11.12: the .FOT's layout and every FONTDIR field).

  usage: tools/mkfot.py FONT.TTF OUT.FOT [TTFNAME]

TTFNAME is the name stored in the .FOT's CCh resource, which GDI finds
with OpenFile's search (default: FONT.TTF's base name, upper-cased). The
module name is OUT.FOT's base name. The output is byte for byte what
w16's CreateScalableFontResource(0, OUT.FOT, TTFNAME, NULL) writes
(checked by tests/gd16.sh).
"""
import os
import struct
import sys

FOT_NE = 0x80
FOT_SIZE = 0x600
STUB = bytes([0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21,
              0xB8, 0x01, 0x4C, 0xCD, 0x21])
MSG = b"w16 TrueType font resource, not a program.\r\n$\0"


def u16(d, o):
    return struct.unpack_from('>H', d, o)[0]


def i16(d, o):
    return struct.unpack_from('>h', d, o)[0]


def u32(d, o):
    return struct.unpack_from('>I', d, o)[0]


def tables(d):
    n = u16(d, 4)
    t = {}
    for i in range(n):
        r = 12 + 16 * i
        t[d[r:r + 4].decode('latin-1')] = u32(d, r + 8)
    return t


def name(d, nm, nid):
    """a name record as ANSI, as gdi/ttfont.c tt_name: Microsoft Unicode
    US English, else Microsoft Symbol, else Macintosh Roman; 63 at most"""
    count, soff = u16(d, nm + 2), nm + u16(d, nm + 4)

    def find(p, e, lang):
        for i in range(count):
            r = nm + 6 + 12 * i
            if (u16(d, r), u16(d, r + 2), u16(d, r + 4), u16(d, r + 6)) == (p, e, lang, nid):
                ln, off = u16(d, r + 8), u16(d, r + 10)
                return d[soff + off:soff + off + ln]
        return None
    s = find(3, 1, 0x409)
    if s is None:
        s = find(3, 0, 0x409)
    if s is not None:
        s = s[:128]
        out = ''.join('?' if s[i] else chr(s[i + 1]) for i in range(0, len(s) - 1, 2))
    else:
        s = find(1, 0, 0) or b''
        out = s[:128].decode('latin-1')
    return out[:63]


def symbol_cmap(d, cmap):
    """stb_truetype's choice (gdi/ttf/ttfx.c): a Microsoft Unicode or a
    Unicode-platform subtable if there is one, else Microsoft Symbol"""
    uni = sym = False
    for i in range(u16(d, cmap + 2)):
        p, e = u16(d, cmap + 4 + 8 * i), u16(d, cmap + 6 + 8 * i)
        if (p == 3 and e in (1, 10)) or p == 0:
            uni = True
        elif p == 3 and e == 0:
            sym = True
    return sym and not uni


def mkfot(ttf, fotname, ttfname):
    d = open(ttf, 'rb').read()
    t = tables(d)
    head, hhea, os2, post = t['head'], t['hhea'], t.get('OS/2'), t.get('post')
    upem = u16(d, head + 18)
    asc, desc, gap = i16(d, hhea + 4), i16(d, hhea + 6), i16(d, hhea + 8)
    if os2 is not None:
        win_a, win_d, xavg = i16(d, os2 + 74), i16(d, os2 + 76), i16(d, os2 + 2)
    else:
        win_a, win_d, xavg = asc, -desc, 0
    gap = max(0, gap - ((win_a + win_d) - (asc - desc)))
    bboxw = i16(d, head + 40) - i16(d, head + 36)
    symbol = symbol_cmap(d, t['cmap'])
    family, full, style = (name(d, t['name'], i) for i in (1, 4, 2))
    base = os.path.basename(fotname)
    module = base.split('.')[0][:8].upper()
    ttfb = ttfname.encode('latin-1')

    b = bytearray(FOT_SIZE)

    def p16(o, v):
        struct.pack_into('<H', b, o, v & 0xFFFF)

    def p32(o, v):
        struct.pack_into('<I', b, o, v & 0xFFFFFFFF)
    b[0:2] = b'MZ'
    p16(2, FOT_SIZE % 512)
    p16(4, (FOT_SIZE + 511) // 512)
    p16(8, 4)
    p16(0x0C, 0xFFFF)
    p16(0x10, 0xB8)
    p16(0x18, 0x40)
    p32(0x3C, FOT_NE)
    b[0x40:0x40 + len(STUB)] = STUB
    b[0x40 + len(STUB):0x40 + len(STUB) + len(MSG)] = MSG
    ne = FOT_NE
    b[ne:ne + 4] = b'NE\x05\x0a'
    p16(ne + 0x0C, 0x8000)
    p16(ne + 0x22, 0x40)
    p16(ne + 0x24, 0x40)
    p16(ne + 0x32, 4)
    p16(ne + 0x34, 2)
    b[ne + 0x36] = 2
    p16(ne + 0x3E, 0x0300)
    rt = ne + 0x40
    p16(rt, 4)
    ttlen = max(128, (len(ttfb) + 1 + 15) & ~15)
    off = 0x400
    p16(rt + 2, 0x8007)
    p16(rt + 4, 1)
    p16(rt + 10, (off + ttlen) >> 4)
    fdir = off + ttlen
    p16(rt + 14, 0x0C50)
    p16(rt + 22, 0x80CC)
    p16(rt + 24, 1)
    p16(rt + 30, off >> 4)
    p16(rt + 32, ttlen >> 4)
    p16(rt + 34, 0x0C50)
    p16(rt + 36, 0x8001)
    p16(rt + 42, 0)
    p16(rt + 16, 44)
    b[rt + 44] = 7
    b[rt + 45:rt + 52] = b'FONTDIR'
    b[rt + 52] = 0
    q = rt + 53
    p16(ne + 0x26, q - ne)
    mb = module.encode('latin-1')
    b[q] = len(mb)
    b[q + 1:q + 1 + len(mb)] = mb
    q += 1 + len(mb) + 2
    b[q] = 0
    q += 1
    p16(ne + 0x28, q - ne)
    p16(ne + 0x2A, q - ne)
    b[q] = 0
    q += 1
    p16(ne + 0x04, q - ne)
    p16(ne + 0x06, 2)
    q += 2
    p32(ne + 0x2C, q)
    fb = family.encode('latin-1')
    nr = 8 + len(fb)
    b[q] = nr & 0xFF
    b[q + 1:q + 9] = b'FONTRES:'
    b[q + 9:q + 9 + len(fb)] = fb
    q += 1 + nr + 2
    b[q] = 0
    q += 1
    p16(ne + 0x20, 1 + nr + 2 + 1)
    b[off:off + len(ttfb)] = ttfb
    q = fdir
    p16(q, 1)
    p16(q + 2, 0)
    e = q + 4
    p16(e, 0x200)
    p32(e + 2, 0x95)
    b[e + 6:e + 32] = b'Windows! Windows! Windows!'
    b[e + 33:e + 37] = b'\x10\x03\x01\x01'
    fssel = d[os2 + 63] if os2 is not None else 0
    p16(e + 66, fssel << 8 | 3)
    p16(e + 68, upem)
    p16(e + 70, 72)
    p16(e + 72, 72)
    p16(e + 74, win_a)
    p16(e + 76, win_a + win_d - upem)
    p16(e + 78, gap)
    b[e + 80] = 0xFF if fssel & 1 else 0
    p16(e + 83, u16(d, os2 + 4) if os2 is not None else 400)
    b[e + 85] = 2 if symbol else 0
    p16(e + 88, win_a + win_d)
    pan1 = d[os2 + 33] if os2 is not None else 0
    pan3 = d[os2 + 35] if os2 is not None else 0
    fixed = post is not None and u32(d, post + 12) != 0
    fam = (0x30 if fixed or pan3 == 9 else 0x20 if 11 <= pan1 <= 13 else
           0x00 if pan1 <= 1 else 0x10)
    b[e + 90] = fam | (0 if fixed else 1) | 6
    p16(e + 91, xavg)
    p16(e + 93, bboxw)
    b[e + 95:e + 99] = b'\x1e\xff\x01\x02'
    p32(e + 105, 0x76)
    p32(e + 109, (0xF0000000 if symbol else 0) | u16(d, head + 46))
    q = e + 113
    s = b'\0' + fb + b'\0' + full.encode('latin-1') + b'\0' + style.encode('latin-1') + b'\0'
    b[q:q + len(s)] = s
    q += len(s)
    fdlen = (q - fdir + 15) & ~15
    p16(rt + 12, fdlen >> 4)
    return bytes(b[:fdir + fdlen])


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    ttf, fot = sys.argv[1], sys.argv[2]
    ttfname = sys.argv[3] if len(sys.argv) == 4 else os.path.basename(ttf).upper()
    data = mkfot(ttf, fot, ttfname)
    with open(fot, 'wb') as f:
        f.write(data)


if __name__ == '__main__':
    main()
