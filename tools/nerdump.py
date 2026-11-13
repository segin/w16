#!/usr/bin/env python3
"""nerdump - print an NE file's menu, dialog, string table and accelerator
resources as text (black-box observation of resource data, a published
file format: ARCHITECTURE.md 3). Code is never looked at.

usage: nerdump.py FILE.EXE
"""
import struct
import sys

TYPES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING',
         7: 'FONTDIR', 8: 'FONT', 9: 'ACCELERATORS', 10: 'RCDATA', 12: 'GROUP_CURSOR',
         14: 'GROUP_ICON', 16: 'VERSION'}


def resources(d):
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    rt = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
    shift = struct.unpack_from('<H', d, rt)[0]
    p = rt + 2

    def name(v):
        if v & 0x8000:
            return v & 0x7FFF
        n = d[rt + v]
        return d[rt + v + 1:rt + v + 1 + n].decode('latin-1')
    while True:
        t = struct.unpack_from('<H', d, p)[0]
        if not t:
            break
        cnt = struct.unpack_from('<H', d, p + 2)[0]
        p += 8
        for _ in range(cnt):
            off, ln, fl, rid = struct.unpack_from('<HHHH', d, p)
            yield name(t), name(rid), fl, d[off << shift:(off << shift) + (ln << shift)]
            p += 12


def cstr(b, i):
    e = b.index(b'\0', i)
    return b[i:e].decode('latin-1'), e + 1


def menu(b, win2=False):
    """win2: the Windows 2.x format (no header, byte flags)."""
    out = []
    fw = 1 if win2 else 2

    def level(i, depth):
        while True:
            fl = b[i] if win2 else struct.unpack_from('<H', b, i)[0]
            i += fw
            if fl & 0x10:       # MF_POPUP
                s, i = cstr(b, i)
                out.append('  ' * depth + 'POPUP %r flags=%04X' % (s, fl))
                i = level(i, depth + 1)
            else:
                mid = struct.unpack_from('<H', b, i)[0]
                s, i = cstr(b, i + 2)
                out.append('  ' * depth + 'ITEM %r id=%d flags=%04X' % (s, mid, fl))
            if fl & 0x80:       # MF_END
                return i
    level(0 if win2 else 4, 0)  # 3.x: after the version and header size
    return out


def dialog(b):
    style, n, x, y, cx, cy = struct.unpack_from('<IBHHHH', b, 0)
    i = 13

    def sz(i):
        if b[i] == 0xFF:
            return '#%d' % struct.unpack_from('<H', b, i + 1)[0], i + 3
        return cstr(b, i)
    mn, i = sz(i)
    cls, i = cstr(b, i)
    cap, i = cstr(b, i)
    out = ['DIALOG %d,%d,%d,%d style=%08X menu=%r class=%r caption=%r' % (x, y, cx, cy, style, mn, cls, cap)]
    if style & 0x40:            # DS_SETFONT
        pt = struct.unpack_from('<H', b, i)[0]
        f, i = cstr(b, i + 2)
        out.append('  FONT %d %r' % (pt, f))
    for _ in range(n):
        x, y, cx, cy, cid, st = struct.unpack_from('<HHHHHI', b, i)
        i += 14
        if b[i] & 0x80:
            cl = {0x80: 'Button', 0x81: 'Edit', 0x82: 'Static', 0x83: 'ListBox',
                  0x84: 'ScrollBar', 0x85: 'ComboBox'}.get(b[i], '#%02X' % b[i])
            i += 1
        else:
            cl, i = cstr(b, i)
        if b[i] == 0xFF:
            t = '#%d' % struct.unpack_from('<H', b, i + 1)[0]
            i += 3
        else:
            t, i = cstr(b, i)
        extra = b[i]
        i += 1 + extra
        out.append('  %-9s id=%-5d %3d,%3d,%3d,%3d style=%08X %r' % (cl, cid if cid < 0x8000 else cid - 0x10000, x, y, cx, cy, st, t))
    return out


def strings(rid, b):
    out = []
    i = 0
    for k in range(16):
        n = b[i]
        if n:
            out.append('  %5d %r' % ((rid - 1) * 16 + k, b[i + 1:i + 1 + n].decode('latin-1')))
        i += 1 + n
    return out


def accel(b):
    out = []
    for i in range(0, len(b), 5):
        fl, key, cmd = struct.unpack_from('<BHH', b, i)
        out.append('  key=%04X cmd=%d flags=%02X' % (key, cmd, fl))
        if fl & 0x80:
            break
    return out


def main():
    d = open(sys.argv[1], 'rb').read()
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    win2 = d[ne + 0x3F] < 3        # expected Windows version (major)
    for t, rid, fl, b in resources(d):
        tn = TYPES.get(t, t)
        print('%s %s (flags %04X, %d bytes)' % (tn, rid, fl, len(b)))
        if tn == 'MENU':
            print('\n'.join(menu(b, win2)))
        elif tn == 'DIALOG':
            print('\n'.join(dialog(b)))
        elif tn == 'STRING':
            print('\n'.join(strings(rid, b)))
        elif tn == 'ACCELERATORS':
            print('\n'.join(accel(b)))


if __name__ == '__main__':
    main()
