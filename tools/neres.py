#!/usr/bin/env python3
"""neres - list an NE module's resources and decode the textual ones.

Reads the published NE resource table and the published Windows 3.x
resource formats: RT_STRING tables, RT_DIALOG templates, RT_MENU,
RT_VERSION (strings), RT_GROUP_ICON directories. Images (icons, bitmaps,
cursors) are only listed with their size and dimensions: w16 draws its own
artwork (ARCHITECTURE.md 6.8). Never looks at code.

usage: neres.py FILE...
"""
import struct
import sys

TYPES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING',
         7: 'FONTDIR', 8: 'FONT', 9: 'ACCELERATOR', 10: 'RCDATA', 12: 'GROUP_CURSOR',
         14: 'GROUP_ICON', 16: 'VERSION'}
CLASSES = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX',
           0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}


def cstr(d, p):
    e = d.index(b'\0', p)
    return d[p:e].decode('latin-1'), e + 1


def show(s):
    return repr(s)


def resources(d):
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    resoff, rnoff = struct.unpack_from('<HH', d, ne + 0x24)
    p = ne + resoff
    shift = struct.unpack_from('<H', d, p)[0]
    p += 2
    out = []
    while True:
        t = struct.unpack_from('<H', d, p)[0]
        if t == 0:
            break
        n = struct.unpack_from('<H', d, p + 2)[0]
        p += 8
        for _ in range(n):
            off, ln, fl, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            if not t & 0x8000:
                k = d[ne + resoff + t]
                tname = d[ne + resoff + t + 1:ne + resoff + t + 1 + k].decode('latin-1')
            else:
                tname = TYPES.get(t & 0x7FFF, str(t & 0x7FFF))
            if rid & 0x8000:
                rname = rid & 0x7FFF
            else:
                k = d[ne + resoff + rid]
                rname = d[ne + resoff + rid + 1:ne + resoff + rid + 1 + k].decode('latin-1')
            out.append((tname, rname, off << shift, ln << shift, fl))
    return out


def strings(d, rid, p, ln):
    base = (rid - 1) * 16
    for i in range(16):
        n = d[p]
        if n:
            print(f'    {base + i:5}: {show(d[p + 1:p + 1 + n].decode("latin-1"))}')
        p += 1 + n


def dialog(d, p):
    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', d, p)
    p += 13
    if d[p] == 0xFF:
        menu = struct.unpack_from('<H', d, p + 1)[0]
        p += 3
    else:
        menu, p = cstr(d, p)
    cls, p = cstr(d, p)
    title, p = cstr(d, p)
    print(f'    style {style:08X} at {x},{y} size {cx}x{cy} menu {menu!r} class {cls!r} title {show(title)}')
    if style & 0x40:            # DS_SETFONT
        pt = struct.unpack_from('<H', d, p)[0]
        face, p = cstr(d, p + 2)
        print(f'    font {pt} {face!r}')
    for _ in range(n):
        x, y, cx, cy, cid, st = struct.unpack_from('<hhhhHI', d, p)
        p += 14
        if d[p] & 0x80:
            c = CLASSES.get(d[p], hex(d[p]))
            p += 1
        else:
            c, p = cstr(d, p)
        if d[p] == 0xFF:
            text = f'#{struct.unpack_from("<H", d, p + 1)[0]}'
            p += 3
        else:
            text, p = cstr(d, p)
        extra = d[p]
        p += 1 + extra
        print(f'      {c:9} id {cid:5} {x:4},{y:4} {cx:4}x{cy:<4} style {st:08X} {show(text)}')


def menu(d, p, end):
    p += 4                      # version, header size
    depth = 0
    while p < end and depth >= 0:
        fl = struct.unpack_from('<H', d, p)[0]
        p += 2
        mid = None
        if not fl & 0x10:       # MF_POPUP
            mid = struct.unpack_from('<H', d, p)[0]
            p += 2
        text, p = cstr(d, p)
        print(f'    {"  " * depth}{fl:04X} {mid if mid is not None else "popup"} {show(text)}')
        if fl & 0x10:
            depth += 1
        if fl & 0x80:           # MF_END
            depth -= 1
            if fl & 0x10:
                pass


def version(d, p, ln):
    blob = d[p:p + ln]
    i = 0
    while True:
        i = blob.find(b'\0', i)
        if i < 0:
            break
        i += 1
    # print the printable runs
    for run in [r for r in blob.split(b'\0') if len(r) > 2]:
        try:
            s = run.decode('latin-1')
        except Exception:
            continue
        if all(32 <= ord(c) < 127 for c in s):
            print(f'    {s!r}')


def main():
    for path in sys.argv[1:]:
        d = open(path, 'rb').read()
        print(f'== {path}')
        for tname, rname, off, ln, fl in resources(d):
            print(f'{tname} {rname}: offset {off:X} length {ln} flags {fl:04X}')
            if tname == 'STRING':
                strings(d, rname, off, ln)
            elif tname == 'DIALOG':
                dialog(d, off)
            elif tname == 'MENU':
                menu(d, off, off + ln)
            elif tname == 'VERSION':
                version(d, off, ln)
            elif tname == 'GROUP_ICON':
                _, typ, cnt = struct.unpack_from('<HHH', d, off)
                for k in range(cnt):
                    w, h, col, _, planes, bits, size, rid = struct.unpack_from('<BBBBHHIH', d, off + 6 + 14 * k)
                    print(f'    icon {rid}: {w}x{h} {col or 256} colours {bits} bpp, {size} bytes')
            elif tname == 'ICON':
                hs, w, h, planes, bits = struct.unpack_from('<IiiHH', d, off)
                print(f'    {w}x{h // 2}, {bits} bpp')


if __name__ == '__main__':
    main()
