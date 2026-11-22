#!/usr/bin/env python3
"""mkpics - pictures for w16 WINHELP's own test help file
(tests/winhelp/w16test.hpj, built by OpenWatcom's whc at test time):
w16's own simple drawings, written in the formats a help compiler takes.

  dot.bmp     a 16 x 16 2-colour DIB (a black ring)
  bars.bmp    a 48 x 24 16-colour DIB (eight coloured bars)
  map.shg     a segmented-hypergraphics picture (SHG, "lP"): a 64 x 32
              16-colour DIB with three hot spots: a jump, a popup and a
              macro
  res.mrb     a multi-resolution bitmap (MRB, "lp"): a 32 x 16
              checkerboard at 72 dpi in 2 colours first, then at 96 dpi
              in 16 colours (yellow and navy), so which one is chosen
              shows
  meta.shg    a metafile picture (type 8) in the same container (whc takes
              no .WMF files but passes SHG files through): MM_ANISOTROPIC,
              its window 1000 x 500, its size given as 5080 x 2540 (0.01
              mm: 2 x 1 inches), a red rectangle, a blue ellipse, a line,
              and one hot spot (a jump) over its left half

The SHG and MRB layout is the help file's own picture format (hlp.h's
sources): magic, count, offsets; per picture a type byte (6 DIB), a
packing byte (0), the header in compressed numbers, the colour table, the
bits and the hot spots.

usage: mkpics.py OUTDIR
"""
import os
import struct
import sys

PAL16 = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
         (0, 128, 128), (192, 192, 192), (128, 128, 128), (255, 0, 0), (0, 255, 0),
         (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]


def dib_bits(w, h, bpp, px):
    """bottom-up rows, 32-bit aligned"""
    stride = ((w * bpp + 31) // 32) * 4
    out = b''
    for y in range(h - 1, -1, -1):
        row = bytearray(stride)
        for x in range(w):
            v = px(x, y)
            if bpp == 4:
                row[x // 2] |= v << (4 if x % 2 == 0 else 0)
            else:
                if v:
                    row[x // 8] |= 0x80 >> (x % 8)
        out += bytes(row)
    return out


def palette(bpp):
    if bpp == 4:
        return b''.join(struct.pack('<4B', b, g, r, 0) for r, g, b in PAL16)
    return struct.pack('<8B', 0, 0, 0, 0, 255, 255, 255, 0)


def bmp(w, h, bpp, px):
    bits = dib_bits(w, h, bpp, px)
    pal = palette(bpp)
    bih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, bpp, 0, len(bits), 0, 0, 1 << bpp, 0)
    off = 14 + len(bih) + len(pal)
    return struct.pack('<2sIHHI', b'BM', off + len(bits), 0, 0, off) + bih + pal + bits


def cuw(v):
    return struct.pack('<B', v * 2) if v < 0x80 else struct.pack('<H', v * 2 + 1)


def cul(v):
    return struct.pack('<H', v * 2) if v < 0x8000 else struct.pack('<I', v * 2 + 1)


def ctxhash(s):
    """the context-string hash (hlp.c)"""
    h = 0
    for c in s:
        o = ord(c)
        if 'a' <= c <= 'z':
            v = o - 0x50
        elif 'A' <= c <= 'Z' or '1' <= c <= '9':
            v = o - 0x30
        else:
            v = {'0': 10, '!': 11, '.': 12, '_': 13}[c]
        h = (h * 43 + v) & 0xFFFFFFFF
    return h


def picture(w, h, bpp, dpi, px, hotspots=()):
    """one picture record: type 6 (DIB), packing 0"""
    bits = dib_bits(w, h, bpp, px)
    pal = palette(bpp)
    hot = b''
    if hotspots:
        recs = b''
        macro = b''
        strs = b''
        for kind, x, y, cw, ch, name, target in hotspots:
            if kind == 'macro':
                ids, val = (0xC8, 0, 0), len(macro)
                macro += target.encode() + b'\0'
            else:
                ids, val = ((0xE3 if kind == 'jump' else 0xE2), 0, 0), ctxhash(target)
            recs += struct.pack('<3B4HI', *ids, x, y, cw, ch, val)
            strs += name.encode() + b'\0' + target.encode() + b'\0'
        hot = struct.pack('<BHI', 1, len(hotspots), len(macro)) + recs + macro + strs
    head = bytes([6, 0]) + cul(dpi) + cul(dpi) + cuw(1) + cuw(bpp) + cul(w) + cul(h) + \
        cul(1 << bpp) + cul(0) + cul(len(bits)) + cul(len(hot))
    # then the data's and the hot spots' offsets (plain longs), from the type byte
    fixed = len(head) + 8
    data_off = fixed + len(pal)
    hot_off = data_off + len(bits) if hot else 0
    return head + struct.pack('<II', data_off, hot_off) + pal + bits + hot


def container(magic, pics):
    n = len(pics)
    off = 4 + 4 * n
    offs = []
    for p in pics:
        offs.append(off)
        off += len(p)
    return struct.pack('<HH', magic, n) + b''.join(struct.pack('<I', o) for o in offs) + b''.join(pics)


def wmf():
    """a metafile: METAHEADER, then the records"""
    recs = []

    def rec(func, *params):
        recs.append(struct.pack('<IH', 3 + len(params), func) + b''.join(struct.pack('<h', p) for p in params))
    rec(0x0103, 8)                       # SetMapMode(MM_ANISOTROPIC)
    rec(0x020B, 0, 0)                    # SetWindowOrg(0, 0)
    rec(0x020C, 500, 1000)               # SetWindowExt(1000, 500)
    # CreateBrushIndirect (red), SelectObject, Rectangle
    recs.append(struct.pack('<IHHIH', 7, 0x02FC, 0, 0x000000FF, 0))
    rec(0x012D, 0)
    rec(0x041B, 400, 450, 50, 50)        # Rectangle(50, 50, 450, 400): bottom right top left
    recs.append(struct.pack('<IHHIH', 7, 0x02FC, 0, 0x00FF0000, 0))
    rec(0x012D, 1)
    rec(0x0418, 450, 950, 50, 550)       # Ellipse(550, 50, 950, 450)
    rec(0x0214, 250, 0)                  # MoveTo(0, 250)
    rec(0x0213, 250, 1000)               # LineTo(1000, 250)
    rec(0x01F0, 0)                       # DeleteObject(0)
    rec(0x01F0, 1)
    recs.append(struct.pack('<IH', 3, 0))  # the end
    body = b''.join(recs)
    maxrec = max(len(r) for r in recs) // 2
    mh = struct.pack('<HHHIHIH', 1, 9, 0x300, (18 + len(body)) // 2, 2, maxrec, 0)
    return mh + body


def metapicture(mf, w, h, hotspots):
    """a type-8 picture record: mapping mode, size, sizes, offsets, data"""
    hot = b''
    if hotspots:
        recs = b''
        strs = b''
        for kind, x, y, cw, ch, name, target in hotspots:
            recs += struct.pack('<3B4HI', 0xE3, 0, 0, x, y, cw, ch, ctxhash(target))
            strs += name.encode() + b'\0' + target.encode() + b'\0'
        hot = struct.pack('<BHI', 1, len(hotspots), 0) + recs + strs
    head = bytes([8, 0]) + cuw(8) + struct.pack('<HH', w, h) + cul(len(mf)) + cul(len(mf)) + cul(len(hot))
    data_off = len(head) + 8
    hot_off = data_off + len(mf) if hot else 0
    return head + struct.pack('<II', data_off, hot_off) + mf + hot


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    def ring(x, y):
        d = (x - 7.5) ** 2 + (y - 7.5) ** 2
        return 1 if d < 30 or d > 56 else 0
    files = {
        'dot.bmp': bmp(16, 16, 1, ring),
        'bars.bmp': bmp(48, 24, 4, lambda x, y: (x // 6) + 1 if 2 < y < 21 else 15),
        'map.shg': container(0x506C, [picture(64, 32, 4, 96,
                                              lambda x, y: 12 if x < 21 else (10 if x < 42 else 9),
                                              [('jump', 0, 0, 21, 32, 'left', 'second'),
                                               ('popup', 21, 0, 21, 32, 'middle', 'term'),
                                               ('macro', 42, 0, 22, 32, 'right', 'About()')])]),
        'res.mrb': container(0x706C, [picture(32, 16, 1, 72, lambda x, y: 0 if (x // 8 + y // 8) % 2 else 1),
                                      picture(32, 16, 4, 96, lambda x, y: 4 if (x // 8 + y // 8) % 2 else 11)]),
        'meta.shg': container(0x506C, [metapicture(wmf(), 5080, 2540, [('jump', 0, 0, 2540, 2540, 'shapes', 'second')])]),
    }
    for name, data in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main()
