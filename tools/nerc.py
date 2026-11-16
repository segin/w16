#!/usr/bin/env python3
"""nerc - list an NE file's menus, dialogs, string tables and accelerators
as readable text (for observation; ARCHITECTURE.md 3 allows resources,
dialog and menu templates as data).

Reads the NE resource table (published format) and decodes the 16-bit
resource formats documented in the Windows 3.1 SDK (MENUITEMTEMPLATE,
DLGTEMPLATE, string tables of 16 counted strings, ACCELTABLEENTRY).
Windows 2.x files use the same formats. Nothing here is copied into
w16's sources: it is a reading aid; w16 writes its own .rc files.

usage: nerc.py FILE.EXE [menu|dialog|string|accel|list]...
"""
import struct
import sys

TYPES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING',
         7: 'FONTDIR', 8: 'FONT', 9: 'ACCELERATORS', 10: 'RCDATA', 12: 'GROUP_CURSOR',
         14: 'GROUP_ICON', 16: 'VERSION'}
CLASSES = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX',
           0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}


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
            yield rname(tid), rname(rid), flags, d[off << shift:(off << shift) + (ln << shift)]


def cstr(b, i):
    j = b.index(b'\0', i)
    return b[i:j].decode('latin-1'), j + 1


def q(s):
    return '"' + s.replace('\t', '\\t').replace('\b', '\\b').replace('"', '""') + '"'


def menu(b):
    out = []
    i = 4                                       # version, header size
    depth = [1 << 30]

    def walk(i, ind):
        while True:
            fl = struct.unpack_from('<H', b, i)[0]
            i += 2
            if fl & 0x10:                       # MF_POPUP
                s, i = cstr(b, i)
                out.append('  ' * ind + 'POPUP %s flags=%04X' % (q(s), fl))
                i = walk(i, ind + 1)
            else:
                mid = struct.unpack_from('<H', b, i)[0]
                i += 2
                s, i = cstr(b, i)
                out.append('  ' * ind + ('MENUITEM %s, %d flags=%04X' % (q(s), mid, fl) if s or mid
                                          else 'MENUITEM SEPARATOR'))
            if fl & 0x80:                       # MF_END
                return i
    walk(i, 0)
    return out


def dialog(b):
    out = []
    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', b, 0)
    i = 13
    if b[i] == 0xFF:
        mn = 'id %d' % struct.unpack_from('<H', b, i + 1)[0]
        i += 3
    else:
        mn, i = cstr(b, i)
    cls, i = cstr(b, i)
    cap, i = cstr(b, i)
    out.append('DIALOG %d, %d, %d, %d style=%08X menu=%s class=%s caption=%s' %
               (x, y, cx, cy, style, mn, q(cls), q(cap)))
    if style & 0x40:                            # DS_SETFONT
        pt = struct.unpack_from('<H', b, i)[0]
        face, i = cstr(b, i + 2)
        out.append('FONT %d, %s' % (pt, q(face)))
    for _ in range(n):
        x, y, cx, cy, cid, st = struct.unpack_from('<hhhhHI', b, i)
        i += 14
        if b[i] & 0x80:
            c = CLASSES.get(b[i], '%02X' % b[i])
            i += 1
        else:
            c, i = cstr(b, i)
        if b[i] == 0xFF:
            t = '#%d' % struct.unpack_from('<H', b, i + 1)[0]
            i += 3
        else:
            t, i = cstr(b, i)
            t = q(t)
        extra = b[i]
        i += 1 + extra
        out.append('  CONTROL %s, %d, %s, %08X, %d, %d, %d, %d' % (t, cid, c, st, x, y, cx, cy))
    return out


def strings(b, rid):
    out = []
    base = (rid - 1) * 16
    i = 0
    for k in range(16):
        if i >= len(b):
            break
        n = b[i]
        s = b[i + 1:i + 1 + n].decode('latin-1')
        i += 1 + n
        if n:
            out.append('  %d %s' % (base + k, q(s)))
    return out


def accel(b):
    out = []
    for i in range(0, len(b) - 4, 5):
        fl, key, cmd = struct.unpack_from('<BHH', b, i)
        out.append('  key=%04X cmd=%d flags=%02X' % (key, cmd, fl))
        if fl & 0x80:
            break
    return out


def main():
    d = open(sys.argv[1], 'rb').read()
    want = set(sys.argv[2:]) or {'menu', 'dialog', 'string', 'accel', 'list'}
    for t, rid, fl, b in resources(d):
        tn = TYPES.get(t, t)
        if 'list' in want:
            print('%s %s flags=%04X size=%d' % (tn, rid, fl, len(b)))
        if tn == 'MENU' and 'menu' in want:
            print('MENU %s' % rid)
            print('\n'.join(menu(b)))
        elif tn == 'DIALOG' and 'dialog' in want:
            print('DIALOG %s' % rid)
            print('\n'.join(dialog(b)))
        elif tn == 'STRING' and 'string' in want:
            print('\n'.join(strings(b, rid)))
        elif tn == 'ACCELERATORS' and 'accel' in want:
            print('ACCELERATORS %s' % rid)
            print('\n'.join(accel(b)))


if __name__ == '__main__':
    main()
