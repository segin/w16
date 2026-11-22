#!/usr/bin/env python3
"""mkbadgrp - malformed and edge-case Program Manager group files for the
Program Manager audit probes (tests/scripts/dde/grp*.txt, run by
tests/ddescenario.sh on the reference Windows 3.1 with Microsoft's and
with w16's PROGMAN.EXE).

    usage: tools/mkbadgrp.py OUTDIR

Writes OUTDIR/<NAME>.GRP for each case below. Every file is w16's own
data: the layout is the .GRP format (shell/progman/group.c's header
comment, tools/mkgrp.py), the icons a plain pattern made here. Each file
starts from one valid group of two items (Clock and Notepad, cached VGA
icons, tags: a hotkey on the first, a working-directory path and "run
minimized" on the second) and breaks one thing:

  GOOD      nothing (the reference: what both read from a sound file)
  BADSIG    "PMCX" for "PMCC"
  BADSUM    a wrong checksum
  CBBIG     cbGroup past the file's end
  CBSMALL   cbGroup smaller than the header
  NAMEOFF   the group name's offset past cbGroup
  NAMEEND   the group name without its NUL before cbGroup
  ITEMWRAP  an item offset (FFF0h) whose record wraps past 64K
  ITEMOFF   an item offset past cbGroup
  ITEMSTR   an item's title offset past cbGroup
  ICONWRAP  an item's three icon blocks of 5800h bytes each, so that
            their sum passes 64K (the file itself stays small enough)
  ICONHDR   an icon header claiming 256 x 256 for a 32 x 32 icon's bits
  TAGWRAP   a tag whose length runs past 64K from its offset
  TAGSMALL  a tag of length 3
  TAGPATH   a working-directory tag (8101h) without its NUL
  TAGNOEND  the tags without their closing FFFFh record
  TRUNC     the file cut short in the middle of an item
  EMPTY     a file of no bytes
  MANY      300 items with 100-character commands and no cached icons
  HOTKEYS   twelve items with Ctrl+Alt shortcut keys (Home, Insert,
            Delete, Page Up, Left, keypad 7, keypad /, keypad *, F5, 1,
            =, Pause), as Program Item Properties names them
"""
import os
import struct
import sys

HDR = 0x22
ITEM = 24


def icon_bits():
    """A 32 x 32 icon in the VGA driver's format: header, AND, XOR."""
    hdr = struct.pack('<HHHHHBB', 16, 16, 32, 32, 4, 4, 1)
    andm = bytes(128)                       # all opaque
    xor = bytearray()
    for y in range(32):
        for plane in range(4):              # a diagonal band, colours by row
            row = 0
            for x in range(32):
                if (x + y) // 8 % 2 and (y >> plane) & 1:
                    row |= 0x80000000 >> x
            xor += struct.pack('>I', row)
    return hdr, andm, bytes(xor)


def build(items, name='Bad', tags=None, cbgroup=None, cslots=None, raw_items=None):
    """A group file. items: (title, command, iconpath, (hdr, and, xor) or
    None). tags: list of (id, item, data bytes) or raw bytes."""
    n = cslots if cslots is not None else len(items)
    out = bytearray(HDR + 2 * n)
    out[0:4] = b'PMCC'
    struct.pack_into('<HhhhhhhH', out, 8, 1, 0, 0, 300, 200, -1, -1, 0)
    struct.pack_into('<HHBBH', out, 0x18, 32, 32, 1, 4, 0)
    struct.pack_into('<H', out, 0x20, n)
    struct.pack_into('<H', out, 0x16, len(out))
    out += name.encode('ascii') + b'\0'
    for i, (title, cmd, ipath, icon) in enumerate(items):
        rec = len(out)
        out += bytes(ITEM)
        x, y = 21 + 75 * (i % 5), 72 * (i // 5)
        pn = len(out)
        out += title.encode('ascii') + b'\0'
        pc = len(out)
        out += cmd.encode('ascii') + b'\0'
        pi = len(out)
        out += ipath.encode('ascii') + b'\0'
        h, a, xo = icon if icon else (b'', b'', b'')
        ph = len(out)
        out += h
        pa = len(out)
        out += a
        px = len(out)
        out += xo
        struct.pack_into('<hhHHHHHHHHHH', out, rec, x, y, 0, len(h), len(a), len(xo),
                         ph, pa, px, pn, pc, pi)
        struct.pack_into('<H', out, HDR + 2 * i, rec)
    size = len(out)
    struct.pack_into('<H', out, 6, cbgroup if cbgroup is not None else size)
    if tags is not None:
        if isinstance(tags, (bytes, bytearray)):
            out += tags
        else:
            out += struct.pack('<HHH', 0x8000, 0xFFFF, 10) + b'PMCC'
            for tid, it, data in tags:
                out += struct.pack('<HHH', tid, it, 6 + len(data)) + data
            out += struct.pack('<HHH', 0xFFFF, 0xFFFF, 0)
    return out


def checksum(b):
    b = bytearray(b)
    b[4:6] = b'\0\0'
    s = 0
    for i in range(0, len(b) - 1, 2):
        s += b[i] | b[i + 1] << 8
    if len(b) & 1:
        s += b[-1]
    struct.pack_into('<H', b, 4, -s & 0xFFFF)
    return b


def std_tags():
    return [(0x8102, 0, struct.pack('<H', 0x0624)),     # Ctrl+Alt+Home
            (0x8103, 1, b''),
            (0x8101, 1, b'C:\\WINDOWS\\\0')]


def std_items(icon):
    return [('Clock', 'CLOCK.EXE', 'CLOCK.EXE', icon),
            ('Notepad', 'C:\\TMP\\NOTEPAD.EXE', 'NOTEPAD.EXE', icon)]


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    icon = icon_bits()
    files = {}

    def good(name='Good'):
        return build(std_items(icon), name=name, tags=std_tags())

    files['GOOD'] = checksum(good())
    b = checksum(good('Bad Sig'))
    b[0:4] = b'PMCX'
    files['BADSIG'] = b
    b = checksum(good('Bad Sum'))
    b[4] ^= 0x55
    files['BADSUM'] = b
    b = good('Cb Big')
    struct.pack_into('<H', b, 6, len(b) + 0x100)
    files['CBBIG'] = checksum(b)
    b = good('Cb Small')
    struct.pack_into('<H', b, 6, 0x10)
    files['CBSMALL'] = checksum(b)
    b = good('Name Off')
    struct.pack_into('<H', b, 0x16, struct.unpack_from('<H', b, 6)[0] + 4)
    files['NAMEOFF'] = checksum(b)
    # the name runs to cbGroup without a NUL: a group of no items
    b = build([], name='Name End' + 'X' * 7)
    b = b[:-1]                              # drop the name's NUL
    struct.pack_into('<H', b, 6, len(b))
    files['NAMEEND'] = checksum(b)
    b = good('Item Wrap')
    struct.pack_into('<H', b, HDR, 0xFFF0)
    files['ITEMWRAP'] = checksum(b)
    b = good('Item Off')
    struct.pack_into('<H', b, HDR, struct.unpack_from('<H', b, 6)[0] + 2)
    files['ITEMOFF'] = checksum(b)
    b = good('Item Str')
    rec = struct.unpack_from('<H', b, HDR)[0]
    struct.pack_into('<H', b, rec + 18, struct.unpack_from('<H', b, 6)[0] + 2)
    files['ITEMSTR'] = checksum(b)
    # three icon blocks of 5800h bytes over one region: each in the file,
    # their sum past 64K
    big = bytes(icon[0]) + bytes(0x5800 - 12)
    b = build([('Wrap', 'CLOCK.EXE', 'CLOCK.EXE', (big, b'', b''))], name='Icon Wrap')
    rec = struct.unpack_from('<H', b, HDR)[0]
    ph = struct.unpack_from('<H', b, rec + 12)[0]
    struct.pack_into('<HHHHHH', b, rec + 6, 0x5800, 0x5800, 0x5800, ph, ph, ph)
    files['ICONWRAP'] = checksum(b)
    h, a, xo = icon
    h2 = struct.pack('<HHHHHBB', 128, 128, 256, 256, 32, 4, 1)
    files['ICONHDR'] = checksum(build([('Hdr', 'CLOCK.EXE', 'CLOCK.EXE', (h2, a, xo))],
                                      name='Icon Hdr'))
    b = build(std_items(icon), name='Tag Wrap', tags=[])
    at = len(b) - 6                         # before the closing record
    tag = struct.pack('<HHH', 0x8102, 0, (0x10000 - at + 2) & 0xFFFF) + b'\x24\x06'
    files['TAGWRAP'] = checksum(b[:at] + tag + b[at:])
    b = build(std_items(icon), name='Tag Small', tags=[])
    at = len(b) - 6
    files['TAGSMALL'] = checksum(b[:at] + struct.pack('<HHH', 0x8102, 0, 3) + b'\x24\x06' +
                                 b[at:])
    files['TAGPATH'] = checksum(build(std_items(icon), name='Tag Path',
                                      tags=[(0x8101, 1, b'C:\\WINDOWS\\SYSTEM')]))
    b = build(std_items(icon), name='Tag No End', tags=std_tags())
    files['TAGNOEND'] = checksum(b[:-6])
    b = checksum(good('Trunc'))
    rec = struct.unpack_from('<H', b, HDR + 2)[0]
    files['TRUNC'] = b[:rec + 10]
    files['EMPTY'] = b''
    many = [('Item%03d' % i, 'C:\\' + ('D%03d\\' % i) * 19 + 'X.EXE', '', None)
            for i in range(300)]
    files['MANY'] = checksum(build(many, name='Many'))
    keys = [0x24, 0x2D, 0x2E, 0x21, 0x25, 0x67, 0x6F, 0x6A, 0x74, 0x31, 0xBB, 0x13]
    hk = [('K%02X' % k, 'CLOCK.EXE', 'CLOCK.EXE', icon) for k in keys]
    files['HOTKEYS'] = checksum(build(hk, name='Hot Keys', tags=[
        (0x8102, i, struct.pack('<H', 0x0600 | k)) for i, k in enumerate(keys)]))
    for k, v in files.items():
        assert len(v) <= 0xFFF0, k
        with open(os.path.join(outdir, k + '.GRP'), 'wb') as f:
            f.write(bytes(v))


if __name__ == '__main__':
    main()
