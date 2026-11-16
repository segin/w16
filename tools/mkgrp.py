#!/usr/bin/env python3
"""mkgrp - build a Program Manager group file (.GRP) from a small text
definition, taking each item's icon from a program's own icon resource.

w16's default groups (assets/progman/*.txt -> assets/progman/*.GRP) are
made with it from the programs w16 builds, so the group files hold only
w16's data: the titles, commands and layout of the definition and the
icons of w16's own executables.

The .GRP format is the one Windows 3.1 Setup and Program Manager write
(observed; shell/progman/group.c's header comment, tools/grpdump.py):
header "PMCC", checksum, cbGroup, nCmdShow, the normal rectangle, the
minimized position, the group name's offset, 32, 32, bytes 01 04, a zero
word, cItems and the item offsets; then the group name and, for each item
in order, its 24-byte record, title, command, icon path, the 12-byte icon
header (hotspot 16,16, 32 x 32, 4 bytes per row, 4 planes, 1 bit per
pixel), the AND mask and the XOR bits in the VGA driver's memory-bitmap
format (top row first, each scan line's four planes one after the other,
plane n being bit n of the colour index: red, green, blue, intensity;
ARCHITECTURE.md 11.2). No tags (hotkeys, run minimized) are written.

The icon is taken as Program Manager's ExtractIcon takes it: the n-th
RT_GROUP_ICON of an NE file, the 32 x 32 16-colour image of it (the
published NE resource table and icon resource formats).

Definition (one group per file; '#' starts a comment line):
    file  MAIN.GRP              the output's name (informational)
    name  Main                  the group's title
    show  1                     nCmdShow (1 normal, 2 minimized, 3 maximized)
    rect  0 0 416 202           normal rectangle in the MDI client
    min   -1 -1                 minimized position (-1 -1: none)
    item  Title | command line | ICONFILE.EXE [index] [@ x y]
Items without "@ x y" are placed on Program Manager's grid in order: x =
21 + 75 * column, y = 72 * row, five to a row (as Setup lays out its
groups in a 416-pixel-wide window: 3.1's Main and Accessories). An empty
group keeps one empty item slot (3.1's StartUp has cItems 1).

usage: mkgrp.py [-d DIR]... DEF.txt OUT.GRP
       -d DIR   where to look for the icon files (default: .); the first
                directory holding the file (any case) is used
"""
import os
import struct
import sys

X_FIRST, CX_SPACING, CY_SPACING, COLS = 21, 75, 72, 5
RT_ICON, RT_GROUP_ICON = 3, 14

# The VGA driver's 16 colours by device index (plane n = bit n of the
# index: red, green, blue, intensity; 7 is dark gray, 8 light gray).
# Found by converting icons both ways and comparing with the bits Windows
# 3.1's Program Manager caches (format observation; tests note it).
VGA = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
       (0, 128, 128), (128, 128, 128), (192, 192, 192), (255, 0, 0), (0, 255, 0),
       (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]


def ne_resources(d):
    """{type: [(id, bytes)]} in resource table order (integer types and
    ids only; named ones get their name)."""
    if d[:2] != b'MZ':
        raise ValueError('not an executable')
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    if d[ne:ne + 2] != b'NE':
        raise ValueError('not an NE file')
    resoff = struct.unpack_from('<H', d, ne + 0x24)[0]
    p = ne + resoff
    shift = struct.unpack_from('<H', d, p)[0]
    p += 2
    out = {}
    while True:
        t = struct.unpack_from('<H', d, p)[0]
        if t == 0:
            break
        n = struct.unpack_from('<H', d, p + 2)[0]
        p += 8
        for _ in range(n):
            off, ln, _fl, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            if rid & 0x8000:
                rid &= 0x7FFF
            else:
                k = d[ne + resoff + rid]
                rid = d[ne + resoff + rid + 1:ne + resoff + rid + 1 + k].decode('latin-1')
            out.setdefault(t & 0x7FFF if t & 0x8000 else None, []).append(
                (rid, d[off << shift:(off << shift) + (ln << shift)]))
    return out


def icon_dib(exe, index):
    """The 32 x 32 16-colour DIB image of the index-th icon of an NE file."""
    res = ne_resources(exe)
    groups = res.get(RT_GROUP_ICON, [])
    if index >= len(groups):
        raise ValueError('no icon %d (%d icons)' % (index, len(groups)))
    g = groups[index][1]
    _r, _t, count = struct.unpack_from('<3H', g, 0)
    images = dict(res.get(RT_ICON, []))
    for i in range(count):
        w, h, colours = g[6 + 14 * i], g[7 + 14 * i], g[8 + 14 * i]
        nid = struct.unpack_from('<H', g, 6 + 14 * i + 12)[0]
        if w == 32 and h == 32 and colours == 16 and nid in images:
            return images[nid]
    raise ValueError('icon %d has no 32 x 32 16-colour image' % index)


def device_icon(dib):
    """A DIB icon image as Program Manager caches it: 12-byte header, AND
    mask, XOR bits (VGA format)."""
    size, w, h2, _planes, bpp = struct.unpack_from('<IiiHH', dib, 0)
    if w != 32 or h2 != 64 or bpp != 4:
        raise ValueError('not a 32 x 32 4-bit image')
    ncol = struct.unpack_from('<I', dib, 32)[0] or 16
    pal = [tuple(dib[size + 4 * i + 2 - k] for k in range(3)) for i in range(ncol)]
    dev = []
    for rgb in pal:
        if rgb not in VGA:
            raise ValueError('colour %r is not one of the 16 VGA colours' % (rgb,))
        dev.append(VGA.index(rgb))
    xor_at = size + 4 * ncol
    and_at = xor_at + 16 * 32                    # 16 bytes per 4-bit row
    xor = bytearray()
    andm = bytearray()
    for y in range(32):
        src = 31 - y                             # DIBs are bottom-up
        row = dib[xor_at + 16 * src:xor_at + 16 * src + 16]
        idx = [dev[(row[x >> 1] >> (0 if x & 1 else 4)) & 15] for x in range(32)]
        for plane in range(4):
            for b in range(4):
                v = 0
                for bit in range(8):
                    if idx[8 * b + bit] >> plane & 1:
                        v |= 0x80 >> bit
                xor.append(v)
        andm += dib[and_at + 4 * src:and_at + 4 * src + 4]
    hdr = struct.pack('<5H2B', 16, 16, 32, 32, 4, 4, 1)
    return hdr, bytes(andm), bytes(xor)


def find(dirs, name):
    name = name.replace('\\', '/').split('/')[-1]      # the file, wherever named
    for d in dirs:
        for f in os.listdir(d):
            if f.upper() == name.upper():
                return os.path.join(d, f)
    raise FileNotFoundError('%s not found in %s' % (name, ', '.join(dirs)))


def parse(path):
    g = {'show': 1, 'rect': (0, 0, 0, 0), 'min': (-1, -1), 'items': []}
    for ln in open(path, encoding='latin-1'):
        ln = ln.strip()
        if not ln or ln.startswith('#'):
            continue
        key, _, rest = ln.partition(' ')
        rest = rest.strip()
        if key in ('file', 'name'):
            g[key] = rest
        elif key == 'show':
            g['show'] = int(rest)
        elif key in ('rect', 'min'):
            g[key] = tuple(int(v) for v in rest.split())
        elif key == 'item':
            f = [s.strip() for s in rest.split('|')]
            if len(f) != 3:
                raise ValueError('%s: item needs "title | command | icon": %s' % (path, ln))
            icon, _, at = f[2].partition('@')
            icon = icon.split()
            pos = tuple(int(v) for v in at.split()) if at else None
            g['items'].append({'name': f[0], 'cmd': f[1], 'iconfile': icon[0],
                               'index': int(icon[1]) if len(icon) > 1 else 0, 'at': pos})
        else:
            raise ValueError('%s: unknown key %r' % (path, key))
    if 'name' not in g:
        raise ValueError('%s: no name' % path)
    return g


def build(g, dirs):
    n = len(g['items'])
    slots = max(n, 1)
    name = g['name'].encode('latin-1') + b'\0'
    at = 0x22 + 2 * slots + len(name)
    body = bytearray(name)
    offsets = []
    for i, it in enumerate(g['items']):
        exe = open(find(dirs, it['iconfile']), 'rb').read()
        hdr, andm, xor = device_icon(icon_dib(exe, it['index']))
        x, y = it['at'] or (X_FIRST + CX_SPACING * (i % COLS), CY_SPACING * (i // COLS))
        strs = [s.encode('latin-1') + b'\0' for s in (it['name'], it['cmd'], it['iconfile'])]
        p_name = at + 24
        p_cmd = p_name + len(strs[0])
        p_icon = p_cmd + len(strs[1])
        p_hdr = p_icon + len(strs[2])
        p_and = p_hdr + len(hdr)
        p_xor = p_and + len(andm)
        rec = struct.pack('<2h10H', x, y, it['index'], len(hdr), len(andm), len(xor),
                          p_hdr, p_and, p_xor, p_name, p_cmd, p_icon)
        blob = rec + b''.join(strs) + hdr + andm + xor
        offsets.append(at)
        body += blob
        at += len(blob)
    offsets += [0] * (slots - n)
    if at > 0xFFF0:
        raise ValueError('group too large for the format')
    l, t, r, b = g['rect']
    head = struct.pack('<4sHHH4h2hH2H2BH', b'PMCC', 0, at, g['show'], l, t, r, b,
                       g['min'][0], g['min'][1], 0x22 + 2 * slots, 32, 32, 1, 4, 0)
    data = bytearray(head + struct.pack('<H', slots) + struct.pack('<%dH' % slots, *offsets) + body)
    pad = data + b'\0' if len(data) & 1 else data
    total = sum(struct.unpack('<%dH' % (len(pad) // 2), pad)) & 0xFFFF
    struct.pack_into('<H', data, 4, -total & 0xFFFF)
    return bytes(data)


def main():
    args = sys.argv[1:]
    dirs = []
    while args and args[0] == '-d':
        dirs.append(args[1])
        args = args[2:]
    if len(args) != 2:
        sys.exit(__doc__.split('usage: ')[1])
    data = build(parse(args[0]), dirs or ['.'])
    with open(args[1], 'wb') as f:
        f.write(data)


if __name__ == '__main__':
    main()
