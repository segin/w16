#!/usr/bin/env python3
"""mkgrp - build a Program Manager group file (.GRP) from a small text
definition, taking each item's icon from a program's own icon resource.

w16's default groups (assets/progman/*.txt -> assets/progman/*.GRP) are
made with it from the programs w16 builds, so the group files hold only
w16's data: the titles, commands and layout of the definition and the
icons of w16's own executables.

The .GRP format is the one Windows 3.1 Setup and Program Manager write.
Microsoft Windows Programmer's Reference, Volume 4: Resources (1992),
ch. 5, documents it; where the book and 3.1's own files disagree, this
follows the files (tests/grpref.py rebuilds the four groups 3.1 Setup
wrote byte for byte; shell/progman/group.c's header comment,
tools/grpdump.py):
  - header: "PMCC", the checksum, cbGroup, nCmdShow, the normal
    rectangle, the minimized position, the group name's offset, then
    32, 32 (the icon size; the book calls them wLogPixelsX/Y), bits per
    pixel 1 and planes 4 as BYTES (the book: two WORDs), a reserved zero
    word, cItems and the item offsets (an array of offsets, 0 for an
    empty slot; the book calls it an array of ITEMDATA);
  - the checksum makes the sum of every word of the file zero (an odd
    last byte padded with zero);
  - an item's pt is its icon's TOP-left corner (the book: lower-left);
  - the icon header's size field is 12, the size of the cached header
    written (the book: the resource's size in the executable).
Then the group name and, for each item in order, its 24-byte record,
title, command, icon path, the 12-byte icon header (hotspot 16,16, 32 x
32, 4 bytes per row, 4 planes, 1 bit per pixel), the AND mask and the
XOR bits in the VGA driver's memory-bitmap format (top row first, each
scan line's four planes one after the other, plane n being bit n of the
colour index: red, green, blue, intensity; ARCHITECTURE.md 11.2). No
tags (hotkeys, run minimized) are written, so cbGroup is the file size.

The icon: the n-th RT_GROUP_ICON of an NE file (Volume 4, ch. 6-7: the
NE header and its resource table), and of that group the 32 x 32 image
with 16 colours (bColorCount 16, or 0 with wBitCount 4). The directory
entries inside an icon resource are the .ICO file's (Volume 4, ch. 1)
with a WORD resource id in place of the file offset (14 bytes each;
observed in 3.1's and w16's executables). Unlike ExtractIcon, which
takes the closest image of any colour count, only a 32 x 32 4-bit image
is accepted: the cached bits are 4-plane VGA bits. The colour table is
always 16 entries (ch. 1: in an icon only biSize..biBitCount and
biSizeImage are used); a colour that is not one of the VGA's 16 maps to
the nearest of them.

Definition (one group per file; '#' starts a comment line; text is
Windows ANSI, read as latin-1: write non-ASCII titles in cp1252, not
UTF-8):
    file  MAIN.GRP              the output's name (informational)
    name  Main                  the group's title
    show  1                     nCmdShow (1 normal, 2 minimized, 3 maximized)
    rect  0 0 416 202           normal rectangle in the MDI client
    min   -1 -1                 minimized position (-1 -1: none)
    item  Title | command line | ICONFILE.EXE [index] [@ x y]
An item line is split on its LAST two '|', so a title may contain '|'.
Items without "@ x y" are placed on Program Manager's grid in order: x =
21 + 75 * column, y = 72 * row, five to a row (as Setup lays out its
groups in a 416-pixel-wide window: 3.1's Main and Accessories), skipping
grid places that an item with "@ x y" already takes. An empty group keeps
one empty item slot (3.1's StartUp has cItems 1).

usage: mkgrp.py [-d DIR]... DEF.txt OUT.GRP
       -d DIR   where to look for the icon files (default: .); the first
                directory holding the file is used, an exact-case match
                before any other
"""
import os
import struct
import sys

X_FIRST, CX_SPACING, CY_SPACING, COLS = 21, 75, 72, 5
RT_ICON, RT_GROUP_ICON = 3, 14
MAX_GROUP = 0xFFF0                       # as shell/progman/group.c

# The VGA driver's 16 colours by device index (plane n = bit n of the
# index: red, green, blue, intensity; 7 is dark gray, 8 light gray).
# Found by converting icons both ways and comparing with the bits Windows
# 3.1's Program Manager caches: tests/grpref.py rebuilds Setup's
# Accessories group, whose cached icons use all 16 indices, byte for byte.
VGA = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
       (0, 128, 128), (128, 128, 128), (192, 192, 192), (255, 0, 0), (0, 255, 0),
       (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]


def need(d, off, n, what):
    """Raise a clear error unless d[off:off + n] lies inside d."""
    if off < 0 or off + n > len(d):
        raise ValueError('%s at %d (%d bytes) lies outside the %d-byte file' % (what, off, n, len(d)))


def ne_resources(d):
    """{type: [(id, bytes)]} in resource table order; a named type or id
    is its name."""
    need(d, 0, 0x40, 'the MZ header')
    if d[:2] != b'MZ':
        raise ValueError('not an executable')
    if struct.unpack_from('<H', d, 0x18)[0] < 0x40:
        raise ValueError('not a new-style executable (MZ relocation table at < 40h)')
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    need(d, ne, 0x40, 'the NE header')
    if d[ne:ne + 2] != b'NE':
        raise ValueError('not an NE file')
    resoff, resnames = struct.unpack_from('<HH', d, ne + 0x24)
    if resoff == resnames:
        return {}                                # no resource table
    base = ne + resoff

    def name(at):
        need(d, base + at, 1, 'a resource name')
        k = d[base + at]
        need(d, base + at + 1, k, 'a resource name')
        return d[base + at + 1:base + at + 1 + k].decode('latin-1')

    need(d, base, 2, 'the resource table')
    shift = struct.unpack_from('<H', d, base)[0]
    p = base + 2
    out = {}
    while True:
        need(d, p, 2, 'the resource table')
        t = struct.unpack_from('<H', d, p)[0]
        if t == 0:
            break
        need(d, p, 8, 'a resource type entry')
        n = struct.unpack_from('<H', d, p + 2)[0]
        p += 8
        key = t & 0x7FFF if t & 0x8000 else name(t)
        for _ in range(n):
            need(d, p, 12, 'a resource entry')
            off, ln, _fl, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            rid = rid & 0x7FFF if rid & 0x8000 else name(rid)
            need(d, off << shift, ln << shift, 'a resource')
            out.setdefault(key, []).append((rid, d[off << shift:(off << shift) + (ln << shift)]))
    return out


def icon_dib(exe, index):
    """The 32 x 32 16-colour DIB image of the index-th icon of an NE file."""
    res = ne_resources(exe)
    groups = res.get(RT_GROUP_ICON, [])
    if index >= len(groups):
        raise ValueError('no icon %d (%d icons)' % (index, len(groups)))
    g = groups[index][1]
    need(g, 0, 6, 'the icon directory')
    _r, _t, count = struct.unpack_from('<3H', g, 0)
    need(g, 6, 14 * count, 'the icon directory')
    images = dict(res.get(RT_ICON, []))
    for i in range(count):
        e = 6 + 14 * i
        w, h, colours = g[e], g[e + 1], g[e + 2]
        bits = struct.unpack_from('<H', g, e + 6)[0]
        nid = struct.unpack_from('<H', g, e + 12)[0]
        if w == 32 and h == 32 and (colours == 16 or (colours == 0 and bits == 4)) and nid in images:
            return images[nid]
    raise ValueError('icon %d has no 32 x 32 16-colour image' % index)


def nearest(rgb):
    """The VGA device index nearest to rgb (exact when it is one of them)."""
    return min(range(16), key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb, VGA[i])))


def device_icon(dib):
    """A DIB icon image as Program Manager caches it: 12-byte header, AND
    mask, XOR bits (VGA format)."""
    need(dib, 0, 16, 'the icon image header')
    size, w, h2, _planes, bpp = struct.unpack_from('<IiiHH', dib, 0)
    if w != 32 or h2 != 64 or bpp != 4:
        raise ValueError('not a 32 x 32 4-bit image')
    ncol = 16                                    # biClrUsed is not used in icons
    need(dib, size, 4 * ncol, 'the icon colour table')
    dev = [nearest(tuple(dib[size + 4 * i + 2 - k] for k in range(3))) for i in range(ncol)]
    xor_at = size + 4 * ncol
    and_at = xor_at + 16 * 32                    # 16 bytes per 4-bit row
    need(dib, xor_at, 16 * 32 + 4 * 32, 'the icon bits')
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
        if not os.path.isdir(d):
            continue
        files = sorted(os.listdir(d))
        if name in files:
            return os.path.join(d, name)
        for f in files:
            if f.upper() == name.upper():
                return os.path.join(d, f)
    raise FileNotFoundError('%s not found in %s' % (name, ', '.join(dirs)))


def numbers(path, ln, s, n):
    v = s.split()
    if len(v) != n:
        raise ValueError('%s: %d numbers needed: %s' % (path, n, ln))
    try:
        return tuple(int(x) for x in v)
    except ValueError:
        raise ValueError('%s: not a number: %s' % (path, ln)) from None


def parse(path):
    g = {'show': 1, 'rect': (0, 0, 0, 0), 'min': (-1, -1), 'items': []}
    for ln in open(path, encoding='latin-1'):
        ln = ln.strip()
        if not ln or ln.startswith('#'):
            continue
        parts = ln.split(None, 1)
        key, rest = parts[0], (parts[1].strip() if len(parts) > 1 else '')
        if key in ('file', 'name'):
            g[key] = rest
        elif key == 'show':
            g['show'] = numbers(path, ln, rest, 1)[0]
        elif key == 'rect':
            g['rect'] = numbers(path, ln, rest, 4)
        elif key == 'min':
            g['min'] = numbers(path, ln, rest, 2)
        elif key == 'item':
            f = [s.strip() for s in rest.rsplit('|', 2)]
            if len(f) != 3:
                raise ValueError('%s: item needs "title | command | icon": %s' % (path, ln))
            icon, _, at = f[2].partition('@')
            icon = icon.split()
            if not 1 <= len(icon) <= 2:
                raise ValueError('%s: icon needs "FILE [index]": %s' % (path, ln))
            pos = numbers(path, ln, at, 2) if at else None
            g['items'].append({'name': f[0], 'cmd': f[1], 'iconfile': icon[0],
                               'index': numbers(path, ln, icon[1], 1)[0] if len(icon) > 1 else 0,
                               'at': pos})
        else:
            raise ValueError('%s: unknown key %r' % (path, key))
    if 'name' not in g:
        raise ValueError('%s: no name' % path)
    return g


def place(items):
    """Each item's position: its own "@ x y", else the next grid place
    that no item with "@ x y" takes."""
    taken = {it['at'] for it in items if it['at']}
    out, slot = [], 0
    for it in items:
        if it['at']:
            out.append(it['at'])
            continue
        while True:
            xy = (X_FIRST + CX_SPACING * (slot % COLS), CY_SPACING * (slot // COLS))
            slot += 1
            if xy not in taken:
                break
        out.append(xy)
    return out


def build(g, dirs):
    n = len(g['items'])
    slots = max(n, 1)
    name = g['name'].encode('latin-1') + b'\0'
    at = 0x22 + 2 * slots + len(name)
    if at > MAX_GROUP:
        raise ValueError('group too large for the format (%d items)' % n)
    body = bytearray(name)
    offsets = []
    for it, (x, y) in zip(g['items'], place(g['items'])):
        exe = open(find(dirs, it['iconfile']), 'rb').read()
        hdr, andm, xor = device_icon(icon_dib(exe, it['index']))
        strs = [s.encode('latin-1') + b'\0' for s in (it['name'], it['cmd'], it['iconfile'])]
        size = 24 + sum(map(len, strs)) + len(hdr) + len(andm) + len(xor)
        if at + size > MAX_GROUP:
            raise ValueError('group too large for the format (at item %r)' % it['name'])
        p_name = at + 24
        p_cmd = p_name + len(strs[0])
        p_icon = p_cmd + len(strs[1])
        p_hdr = p_icon + len(strs[2])
        p_and = p_hdr + len(hdr)
        p_xor = p_and + len(andm)
        rec = struct.pack('<2h10H', x, y, it['index'], len(hdr), len(andm), len(xor),
                          p_hdr, p_and, p_xor, p_name, p_cmd, p_icon)
        offsets.append(at)
        body += rec + b''.join(strs) + hdr + andm + xor
        at += size
    offsets += [0] * (slots - n)
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
        if len(args) < 2:
            sys.exit('mkgrp: -d needs a directory')
        dirs.append(args[1])
        args = args[2:]
    if len(args) != 2:
        sys.exit(__doc__.split('usage: ')[1])
    data = build(parse(args[0]), dirs or ['.'])
    with open(args[1], 'wb') as f:
        f.write(data)


if __name__ == '__main__':
    main()
