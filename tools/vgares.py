#!/usr/bin/env python3
"""vgares - the resources of w16's VGA.DRV as a 16-bit .RES file for wlink.

Every resource Windows 3.1's VGA.DRV has (drivers/vga/resources.md: type,
ID, memory flags, and for icons and cursors the image IDs their groups
name), with w16's contents:

- the bitmaps, icons and cursors: w16's own art, built by tools/vgaart.py
  (make vgaart) into ARTDIR as .bmp, .ico and .cur files;
- OEMBIN 1, the configuration resource (DDK: CONFIG.BIN, 98 bytes; the
  reference's 112 is that rounded up to the NE file's 16-byte resource
  alignment): the scroll-bar thumb, icon, cursor and border sizes and the
  20 default system colours,
  as USER shows them with Windows 3.1's driver (probe VG1: GetSystemMetrics
  and GetSysColor with no WIN.INI [colors]; ARCHITECTURE.md 10.4 US2);
- OEMBIN FONTS, the stock-font resource: for OEM_FIXED_FONT,
  ANSI_FIXED_FONT and ANSI_VAR_FONT the LOGFONT up to the face name (18
  bytes) and the face name, NUL-terminated, as GDI gives them back with
  Windows 3.1's driver (VG1: GetObject of each stock font);
- OEMBIN 2, STRINGS 1000 and 1010: w16's driver needs none of them and no
  other module was seen to read them (UNVERIFIED); present so that the
  resource table has the same entries, holding a short note;
- VERSION 1: w16's own version information (3.10).

usage: vgares.py ARTDIR OUT.RES
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fontc import vs_node  # noqa: E402

RT_CURSOR, RT_BITMAP, RT_ICON = 1, 2, 3
RT_GROUP_CURSOR, RT_GROUP_ICON, RT_VERSION = 12, 14, 16

BITMAPS = [  # name, id, flags
    ('obm_close', 32754, 0x0C70), ('obm_uparrow', 32753, 0x0C70),
    ('obm_dnarrow', 32752, 0x0C70), ('obm_rgarrow', 32751, 0x0C70),
    ('obm_lfarrow', 32750, 0x0C70), ('obm_uparrowd', 32743, 0x0C70),
    ('obm_dnarrowd', 32742, 0x0C70), ('obm_rgarrowd', 32741, 0x0C70),
    ('obm_lfarrowd', 32740, 0x0C70), ('obm_uparrowi', 32737, 0x0C70),
    ('obm_dnarrowi', 32736, 0x0C70), ('obm_rgarrowi', 32735, 0x0C70),
    ('obm_lfarrowi', 32734, 0x0C70), ('obm_reduce', 32749, 0x0C70),
    ('obm_zoom', 32748, 0x0C70), ('obm_restore', 32747, 0x0C70),
    ('obm_reduced', 32746, 0x0C70), ('obm_zoomd', 32745, 0x0C70),
    ('obm_restored', 32744, 0x0C70), ('obm_mnarrow', 32739, 0x0C70),
    ('obm_combo', 32738, 0x0C70), ('obm_check', 32760, 0x0C70),
    ('obm_checkboxes', 32759, 0x0C70), ('obm_btsize', 32761, 0x0C70),
    ('obm_size', 32766, 0x0C70), ('obm_btncorners', 32758, 0x0C70),
    ('obm_old_close', 32767, 0x0C30), ('obm_old_uparrow', 32765, 0x0C30),
    ('obm_old_dnarrow', 32764, 0x0C30), ('obm_old_rgarrow', 32763, 0x0C30),
    ('obm_old_lfarrow', 32762, 0x0C30), ('obm_old_reduce', 32757, 0x0C30),
    ('obm_old_zoom', 32756, 0x0C30), ('obm_old_restore', 32755, 0x0C30),
]

CURSORS = [  # name, group id, image id, group flags, image flags
    ('ocr_normal', 32512, 1, 0x0C40, 0x0C40), ('ocr_ibeam', 32513, 7, 0x1C50, 0x1C50),
    ('ocr_wait', 32514, 2, 0x0C40, 0x0C40), ('ocr_cross', 32515, 10, 0x1C30, 0x1C10),
    ('ocr_up', 32516, 8, 0x1C50, 0x1C50), ('ocr_size', 32640, 11, 0x1C30, 0x1C10),
    ('ocr_icon', 32641, 9, 0x1C50, 0x1C50), ('ocr_sizenwse', 32642, 3, 0x1C50, 0x1C50),
    ('ocr_sizenesw', 32643, 4, 0x1C50, 0x1C50), ('ocr_sizewe', 32644, 5, 0x1C50, 0x1C50),
    ('ocr_sizens', 32645, 6, 0x1C50, 0x1C50),
]

ICONS = [  # name, group id, image ids in the .ico's order, group flags, image flags
    ('oic_sample', 32512, [24], 0x1C30, 0x1C10),
    ('oic_hand', 32513, [12, 13, 14], 0x1C50, 0x1C50),
    ('oic_ques', 32514, [15, 16, 17], 0x1C50, 0x1C50),
    ('oic_bang', 32515, [18, 19, 20], 0x1C50, 0x1C50),
    ('oic_note', 32516, [21, 22, 23], 0x1C50, 0x1C50),
]


def rgb(c):
    """a colour from 0xRRGGBB: red, green, blue, 0 (a COLORREF: the DDK
    calls these RGBQUADs, but USER reads red first; VG1's GetSysColor of
    the active caption, navy, came out maroon with blue first)"""
    return struct.pack('BBBB', c >> 16, (c >> 8) & 255, c & 255, 0)


def config_bin():
    """CONFIG.BIN (DDK): nine shorts, then 20 colours in COLOR_* order 0-19
    (scroll bar ... inactive caption text)"""
    metrics = struct.pack('<9h',
                          17, 17,       # thumb height, width (SM_CYVTHUMB, SM_CXHTHUMB)
                          32, 32,       # icon width, height (SM_CXICON, SM_CYICON)
                          32, 32,       # cursor width, height (SM_CXCURSOR, ...)
                          0,            # reserved
                          1, 1)         # border width, height (SM_CXBORDER, ...)
    colors = [0xC0C0C0, 0xC0C0C0, 0x000080, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF,
              0x000000, 0x000000, 0x000000, 0xFFFFFF, 0xC0C0C0, 0xC0C0C0,
              0xFFFFFF, 0x000080, 0xFFFFFF, 0xC0C0C0, 0x808080, 0xC0C0C0,
              0x000000, 0x000000]
    return metrics + b''.join(rgb(c) for c in colors)


def fonts_bin():
    """the stock fonts: LOGFONT's first 18 bytes, then the face name"""
    out = b''
    for h, w, charset, pitch, face in ((12, 8, 255, 1, 'Terminal'),
                                       (12, 9, 0, 1, 'Courier'),
                                       (12, 9, 0, 2, 'Helv')):
        out += struct.pack('<5h8B', h, w, 0, 0, 0, 0, 0, 0, charset, 0, 2, 2, pitch)
        out += face.encode('latin-1') + b'\0'
    return out


def note(s):
    return s.encode('latin-1') + b'\0'


def version():
    fixed = struct.pack('<13I', 0xFEEF04BD, 0x00010000, 0x00030000 | 10, 0, 0x00030000 | 10, 0,
                        0x3F, 0, 0x00010001, 3, 4, 0, 0)   # VFT_DRV, VFT2_DRV_DISPLAY
    info = [('CompanyName', 'the w16 contributors'),
            ('FileDescription', 'w16 VGA display driver'),
            ('FileVersion', '3.10'),
            ('InternalName', 'DISPLAY'),
            ('LegalCopyright', 'Copyright (c) 2026 the w16 contributors (MIT licence)'),
            ('OriginalFilename', 'VGA.DRV'),
            ('ProductName', 'w16'),
            ('ProductVersion', '3.10')]
    strings = b''.join(vs_node(k, v.encode('latin-1') + b'\0') for k, v in info)
    sfi = vs_node('StringFileInfo', b'', vs_node('040904E4', b'', strings))
    vfi = vs_node('VarFileInfo', b'', vs_node('Translation', struct.pack('<HH', 0x0409, 0x04E4)))
    return vs_node('VS_VERSION_INFO', fixed, sfi + vfi)


def rname(v):
    if isinstance(v, int):
        return b'\xff' + struct.pack('<H', v)
    return v.upper().encode('latin-1') + b'\0'


def entry(t, n, flags, data):
    return rname(t) + rname(n) + struct.pack('<HI', flags, len(data)) + data


def main():
    art, outp = sys.argv[1], sys.argv[2]
    out = b''
    for name, rid, fl in BITMAPS:
        d = open(os.path.join(art, name + '.bmp'), 'rb').read()
        out += entry(RT_BITMAP, rid, fl, d[14:])
    for name, gid, iid, gfl, ifl in CURSORS:
        d = open(os.path.join(art, name + '.cur'), 'rb').read()
        w, h, _, _, hx, hy, size, off = struct.unpack_from('<BBBBHHII', d, 6)
        img = struct.pack('<HH', hx, hy) + d[off:off + size]
        out += entry(RT_CURSOR, iid, ifl, img)
        grp = struct.pack('<HHH', 0, 2, 1) + struct.pack('<HHHHIH', w, h * 2, 1, 1, len(img), iid)
        out += entry(RT_GROUP_CURSOR, gid, gfl, grp)
    for name, gid, iids, gfl, ifl in ICONS:
        d = open(os.path.join(art, name + '.ico'), 'rb').read()
        n = struct.unpack_from('<H', d, 4)[0]
        assert n == len(iids), name
        grp = struct.pack('<HHH', 0, 1, n)
        for k in range(n):
            w, h, cc, rs, pl, bpp, size, off = struct.unpack_from('<BBBBHHII', d, 6 + 16 * k)
            out += entry(RT_ICON, iids[k], ifl, d[off:off + size])
            grp += struct.pack('<BBBBHHIH', w, h, cc, rs, pl, bpp, size, iids[k])
        out += entry(RT_GROUP_ICON, gid, gfl, grp)
    out += entry('OEMBIN', 1, 0x0C70, config_bin())
    out += entry('OEMBIN', 'FONTS', 0x0C70, fonts_bin())
    out += entry('OEMBIN', 2, 0x0C30, note('w16: not used'))
    out += entry('STRINGS', 1000, 0x0C30, note('w16: not used'))
    out += entry('STRINGS', 1010, 0x0C30, note('w16: not used'))
    out += entry(RT_VERSION, 1, 0x0C30, version())
    open(outp, 'wb').write(out)


if __name__ == '__main__':
    main()
