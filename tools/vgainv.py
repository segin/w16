#!/usr/bin/env python3
"""vgainv - inventory of a display driver's graphic resources, from headers
only.

Reads the NE resource table (tools/neres.py) and, for each bitmap, icon
and cursor, only the published header that precedes the pixels: the
BITMAPCOREHEADER or BITMAPINFOHEADER (width, height, planes, bits per
pixel, compression), a cursor's hot spot (the two words before its
header), and the RT_GROUP_ICON / RT_GROUP_CURSOR directories. For a
1-bit bitmap it also says whether the two palette entries are black then
white (the polarity USER relies on when it blits a mono bitmap); for a
4-bit one whether the palette is the standard VGA order. These are format
facts (ARCHITECTURE.md 3, "Format facts versus data"). The pixels are
never read, printed or rendered: w16 draws its own art (drivers/vga/art).

usage: vgainv.py DISPLAY.DRV
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from neres import resources  # noqa: E402

OBM = {32754: 'OBM_CLOSE', 32753: 'OBM_UPARROW', 32752: 'OBM_DNARROW', 32751: 'OBM_RGARROW',
       32750: 'OBM_LFARROW', 32749: 'OBM_REDUCE', 32748: 'OBM_ZOOM', 32747: 'OBM_RESTORE',
       32746: 'OBM_REDUCED', 32745: 'OBM_ZOOMD', 32744: 'OBM_RESTORED', 32743: 'OBM_UPARROWD',
       32742: 'OBM_DNARROWD', 32741: 'OBM_RGARROWD', 32740: 'OBM_LFARROWD',
       32739: 'OBM_MNARROW', 32738: 'OBM_COMBO', 32737: 'OBM_UPARROWI', 32736: 'OBM_DNARROWI',
       32735: 'OBM_RGARROWI', 32734: 'OBM_LFARROWI', 32767: 'OBM_OLD_CLOSE', 32766: 'OBM_SIZE',
       32765: 'OBM_OLD_UPARROW', 32764: 'OBM_OLD_DNARROW', 32763: 'OBM_OLD_RGARROW',
       32762: 'OBM_OLD_LFARROW', 32761: 'OBM_BTSIZE', 32760: 'OBM_CHECK',
       32759: 'OBM_CHECKBOXES', 32758: 'OBM_BTNCORNERS', 32757: 'OBM_OLD_REDUCE',
       32756: 'OBM_OLD_ZOOM', 32755: 'OBM_OLD_RESTORE'}
OCR = {32512: 'OCR_NORMAL', 32513: 'OCR_IBEAM', 32514: 'OCR_WAIT', 32515: 'OCR_CROSS',
       32516: 'OCR_UP', 32640: 'OCR_SIZE', 32641: 'OCR_ICON', 32642: 'OCR_SIZENWSE',
       32643: 'OCR_SIZENESW', 32644: 'OCR_SIZEWE', 32645: 'OCR_SIZENS'}
OIC = {32512: 'OIC_SAMPLE', 32513: 'OIC_HAND', 32514: 'OIC_QUES', 32515: 'OIC_BANG',
       32516: 'OIC_NOTE'}
VGA = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
       (0, 128, 128), (192, 192, 192), (128, 128, 128), (255, 0, 0), (0, 255, 0),
       (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]


def header(d, p):
    """(kind, width, height, planes, bpp, compression, palette (r,g,b)...)"""
    size = struct.unpack_from('<I', d, p)[0]
    if size == 12:
        w, h, pl, bpp = struct.unpack_from('<HHHH', d, p + 4)
        n = 1 << bpp if bpp <= 8 else 0
        pal = [(d[p + 12 + 3 * i + 2], d[p + 12 + 3 * i + 1], d[p + 12 + 3 * i])
               for i in range(n)] if bpp <= 4 else []
        return 'core', w, h, pl, bpp, 0, pal
    w, h, pl, bpp, comp = struct.unpack_from('<iiHHI', d, p + 4)
    used = struct.unpack_from('<I', d, p + 32)[0]
    n = used or (1 << bpp if bpp <= 8 else 0)
    pal = [(d[p + size + 4 * i + 2], d[p + size + 4 * i + 1], d[p + size + 4 * i])
           for i in range(n)] if bpp <= 4 else []
    return 'info', w, h, pl, bpp, comp, pal


def palnote(bpp, pal):
    if bpp == 1:
        return 'black/white' if pal == [(0, 0, 0), (255, 255, 255)] else 'other palette'
    if bpp == 4:
        if pal == VGA:
            return 'VGA palette'
        if pal[:8] == VGA[:8] and len(pal) == 8:
            return 'VGA palette (8)'
        return 'other palette'
    return ''


def main():
    d = open(sys.argv[1], 'rb').read()
    res = resources(d)
    groups = {}
    for t, rid, off, ln, fl in res:
        if t in ('GROUP_ICON', 'GROUP_CURSOR'):
            n = struct.unpack_from('<H', d, off + 4)[0]
            ents = []
            for k in range(n):
                q = off + 6 + 14 * k
                if t == 'GROUP_ICON':
                    w, h, cc = d[q], d[q + 1], d[q + 2]
                    pl, bc, sz, img = struct.unpack_from('<HHIH', d, q + 4)
                    ents.append((img, f'{w}x{h} colours {cc} bpp {bc}'))
                else:
                    w, h, pl, bc, sz, img = struct.unpack_from('<HHHHIH', d, q)
                    ents.append((img, f'{w}x{h} bpp {bc}'))
            groups[(t, rid)] = ents
    for t, rid, off, ln, fl in sorted(res, key=lambda r: (r[0], str(r[1]))):
        loc = f'flags {fl:04X}'
        if t == 'BITMAP':
            k, w, h, pl, bpp, comp, pal = header(d, off)
            print(f'BITMAP {rid} {OBM.get(rid, "?")}: {w}x{h} {bpp} bpp {pl} plane(s) '
                  f'{k} header comp {comp} {palnote(bpp, pal)} {loc}')
        elif t == 'CURSOR':
            hx, hy = struct.unpack_from('<HH', d, off)
            k, w, h, pl, bpp, comp, pal = header(d, off + 4)
            print(f'CURSOR {rid}: {w}x{h // 2} {bpp} bpp hotspot {hx},{hy} {k} header '
                  f'{palnote(bpp, pal)} {loc}')
        elif t == 'ICON':
            k, w, h, pl, bpp, comp, pal = header(d, off)
            print(f'ICON {rid}: {w}x{h // 2} {bpp} bpp {k} header {palnote(bpp, pal)} {loc}')
        elif t == 'GROUP_ICON':
            print(f'GROUP_ICON {rid} {OIC.get(rid, "?")}: images {groups[(t, rid)]} {loc}')
        elif t == 'GROUP_CURSOR':
            print(f'GROUP_CURSOR {rid} {OCR.get(rid, "?")}: images {groups[(t, rid)]} {loc}')
        else:
            print(f'{t} {rid}: {ln} bytes {loc} (not graphics)')


if __name__ == '__main__':
    main()
