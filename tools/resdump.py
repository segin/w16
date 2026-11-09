#!/usr/bin/env python3
"""resdump - an NE module's dialogs, menus, string tables and accelerator
tables printed as text (for writing w16's own .rc files to the same layout).

Reads the NE resource table and the Win16 resource formats (DIALOG, MENU,
STRING, ACCELERATOR: published file formats, ARCHITECTURE.md 3); no code is
read. The output is for looking at, at test time; it is never committed.

usage: resdump.py FILE [TYPE...]     TYPE: DIALOG MENU STRING ACCELERATOR
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from neimports import NE  # noqa: E402

CLS = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX',
       0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}


def cstr(b, p):
    e = b.index(b'\0', p)
    return b[p:e].decode('latin-1'), e + 1


def nameord(b, p):
    if b[p] == 0xFF:
        return '#%d' % struct.unpack_from('<H', b, p + 1)[0], p + 3
    return cstr(b, p)


def dialog(blob):
    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', blob, 0)
    p = 13
    menu, p = nameord(blob, p)
    cls, p = cstr(blob, p)
    cap, p = cstr(blob, p)
    print(f'  DIALOG {x}, {y}, {cx}, {cy} style={style:08X} menu="{menu}" class="{cls}"')
    print(f'  CAPTION "{cap}"')
    if style & 0x40:  # DS_SETFONT
        pt = struct.unpack_from('<H', blob, p)[0]
        face, p = cstr(blob, p + 2)
        print(f'  FONT {pt}, "{face}"')
    for _ in range(n):
        x, y, cx, cy, cid, st = struct.unpack_from('<hhhhHI', blob, p)
        p += 14
        if blob[p] & 0x80:
            c = CLS.get(blob[p], '#%02X' % blob[p])
            p += 1
        else:
            c, p = cstr(blob, p)
        if blob[p] == 0xFF:
            t = '#%d' % struct.unpack_from('<H', blob, p + 1)[0]
            p += 3
        else:
            t, p = cstr(blob, p)
        extra = blob[p]
        p += 1 + extra
        print(f'    {c:10} {cid:5} {x:4},{y:4},{cx:4},{cy:4} style={st:08X} "{t}"')


def menu_tree(blob):
    def walk(p, depth):
        while True:
            fl = struct.unpack_from('<H', blob, p)[0]
            p += 2
            if fl & 0x10:
                t, p = cstr(blob, p)
                print('  ' * depth + f'POPUP "{t}" flags={fl:04X}')
                p = walk(p, depth + 1)
            else:
                mid = struct.unpack_from('<H', blob, p)[0]
                t, p = cstr(blob, p + 2)
                print('  ' * depth + f'ITEM {mid:5} "{t}" flags={fl:04X}')
            if fl & 0x80:
                return p
    walk(4, 1)


def strings(rid, blob):
    p, k = 0, 0
    base = (rid - 1) * 16
    while p < len(blob):
        ln = blob[p]
        if ln:
            print(f'  {base + k:5} "{blob[p + 1:p + 1 + ln].decode("latin-1")}"')
        p += 1 + ln
        k += 1


def accel(blob):
    for p in range(0, len(blob), 5):
        fl, key, cmd = struct.unpack_from('<BHH', blob, p)
        print(f'  key={key:04X} cmd={cmd} flags={fl:02X}')
        if fl & 0x80:
            break


def main():
    ne = NE(sys.argv[1])
    want = set(a.upper() for a in sys.argv[2:])
    for t, i, size, fl, blob in ne.resources():
        if want and t not in want:
            continue
        if t == 'DIALOG':
            print(f'DIALOG {i}')
            dialog(blob)
        elif t == 'MENU':
            print(f'MENU {i}')
            menu_tree(blob)
        elif t == 'STRING':
            strings(i, blob)
        elif t == 'ACCELERATOR':
            print(f'ACCELERATOR {i}')
            accel(blob)


if __name__ == '__main__':
    main()
