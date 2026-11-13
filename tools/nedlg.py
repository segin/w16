#!/usr/bin/env python3
"""nedlg - print an NE file's dialog templates, string tables and menus as
readable text (an .rc-like listing), for observation.

Reads only published formats: the NE resource table and the Win16
resource formats (DLGTEMPLATE / DLGITEMTEMPLATE as documented in the
Windows 3.1 SDK, string tables of 16 length-prefixed strings per block).
The listing describes layouts, IDs, styles and texts; w16 writes its own
resource scripts from such facts (ARCHITECTURE.md 3).

usage: nedlg.py FILE [--dialogs] [--strings] [--menus]   (default: all)
"""
import struct
import sys

RT_MENU, RT_DIALOG, RT_STRING = 4, 5, 6
CLASSES = {0x80: 'Button', 0x81: 'Edit', 0x82: 'Static', 0x83: 'ListBox',
           0x84: 'ScrollBar', 0x85: 'ComboBox'}


def resources(d):
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    rt = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
    if rt == ne + struct.unpack_from('<H', d, ne + 0x26)[0]:
        return
    shift = struct.unpack_from('<H', d, rt)[0]
    p = rt + 2

    def rname(v):
        if v & 0x8000:
            return v & 0x7FFF
        q = rt + v
        return d[q + 1:q + 1 + d[q]].decode('latin-1')
    while True:
        tid, cnt = struct.unpack_from('<HH', d, p)
        if tid == 0:
            break
        p += 8
        for _ in range(cnt):
            off, ln, flags, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            yield rname(tid), rname(rid), d[off << shift:(off << shift) + (ln << shift)]


def cstr(b, p):
    e = b.index(b'\0', p)
    return b[p:e].decode('latin-1'), e + 1


def sz_or_ord(b, p):
    if b[p] == 0xFF:
        return '#%d' % struct.unpack_from('<H', b, p + 1)[0], p + 3
    return cstr(b, p)


def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '""').replace('\t', '\\t') \
        .replace('\n', '\\n').replace('\r', '\\r') + '"'


def dialog(name, b):
    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', b, 0)
    p = 13
    menu, p = sz_or_ord(b, p)
    cls, p = cstr(b, p)
    cap, p = cstr(b, p)
    print('DIALOG %s  %d, %d, %d, %d  style %08X' % (name, x, y, cx, cy, style))
    if menu:
        print('  MENU %s' % menu)
    if cls:
        print('  CLASS %s' % q(cls))
    print('  CAPTION %s' % q(cap))
    if style & 0x40:                    # DS_SETFONT
        pt = struct.unpack_from('<H', b, p)[0]
        face, p = cstr(b, p + 2)
        print('  FONT %d, %s' % (pt, q(face)))
    for _ in range(n):
        ix, iy, icx, icy, iid, ist = struct.unpack_from('<hhhhHI', b, p)
        p += 14
        if b[p] & 0x80:
            ic = CLASSES.get(b[p], '#%02X' % b[p])
            p += 1
        else:
            ic, p = cstr(b, p)
        txt, p = sz_or_ord(b, p)
        extra = b[p]
        p += 1 + extra
        print('  %-10s %-28s id %04X (%5d)  %4d,%4d,%4d,%4d  style %08X%s' % (
            ic, q(txt) if not txt.startswith('#') else txt, iid, iid if iid < 0x8000 else iid - 0x10000,
            ix, iy, icx, icy, ist, '  extra %d' % extra if extra else ''))
    print()


def strings(name, b):
    base = (name - 1) * 16
    p = 0
    for i in range(16):
        if p >= len(b):
            break
        ln = b[p]
        s = b[p + 1:p + 1 + ln].decode('latin-1')
        p += 1 + ln
        if ln:
            print('  %5d  0x%04X  %s' % (base + i, base + i, q(s)))


def menu(name, b):
    print('MENU %s' % name)
    p = 4                               # version, header size
    depth = 1
    while p < len(b) and depth:
        fl = struct.unpack_from('<H', b, p)[0]
        p += 2
        mid = None
        if not fl & 0x10:               # MF_POPUP
            mid = struct.unpack_from('<H', b, p)[0]
            p += 2
        t, p = cstr(b, p)
        print('  ' * depth + '%s %s flags %04X' % (q(t), '' if mid is None else 'id %d' % mid, fl))
        if fl & 0x10:
            depth += 1
        if fl & 0x80:                   # MF_END
            depth -= 1
    print()


def main():
    a = sys.argv[1:]
    want = {o[2:] for o in a if o.startswith('--')} or {'dialogs', 'strings', 'menus'}
    d = open([x for x in a if not x.startswith('--')][0], 'rb').read()
    res = list(resources(d))
    if 'dialogs' in want:
        for t, n, b in res:
            if t == RT_DIALOG:
                dialog(n, b)
    if 'strings' in want:
        print('STRINGTABLE')
        for t, n, b in sorted((r for r in res if r[0] == RT_STRING), key=lambda r: r[1]):
            strings(n, b)
        print()
    if 'menus' in want:
        for t, n, b in res:
            if t == RT_MENU:
                menu(n, b)


if __name__ == '__main__':
    main()
