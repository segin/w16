#!/usr/bin/env python3
"""resdump - print an NE file's resources as readable text.

Decodes the published Win16 resource formats (NE resource table; menu,
dialog, string table, accelerator and version templates, as documented
in the Windows 3.1 SDK) of a module, so that a program's UI can be
observed as data (ARCHITECTURE.md 3: resources are data, not code).
Bitmaps, icons and cursors are listed with their size only.

usage: resdump.py FILE.EXE [TYPE...]     (TYPE: menu dialog string accel
                                          or a number; default: all)
"""
import struct
import sys

TYPES = {1: 'cursor', 2: 'bitmap', 3: 'icon', 4: 'menu', 5: 'dialog',
         6: 'string', 7: 'fontdir', 8: 'font', 9: 'accel', 10: 'rcdata',
         12: 'groupcursor', 14: 'groupicon', 16: 'version'}


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


def cstr(b, p):
    e = b.index(b'\0', p)
    return b[p:e].decode('latin-1'), e + 1


def menu(b):
    out = []
    pos = [4]

    def level(depth):
        while pos[0] < len(b):
            fl = struct.unpack_from('<H', b, pos[0])[0]
            pos[0] += 2
            if fl & 0x10:
                s, pos[0] = cstr(b, pos[0])
                out.append('  ' * depth + 'POPUP %r flags=%04X' % (s, fl))
                level(depth + 1)
            else:
                mid = struct.unpack_from('<H', b, pos[0])[0]
                s, pos[0] = cstr(b, pos[0] + 2)
                out.append('  ' * depth + 'ITEM %r id=%d flags=%04X' % (s, mid, fl))
            if fl & 0x80:
                return
    level(0)
    return '\n'.join(out)


def dialog(b):
    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', b, 0)
    p = 13
    out = []

    def sz(p):
        if b[p] == 0xFF:
            return '#%d' % struct.unpack_from('<H', b, p + 1)[0], p + 3
        return cstr(b, p)
    mn, p = sz(p)
    cl, p = cstr(b, p)
    cap, p = cstr(b, p)
    line = 'DIALOG %d,%d,%d,%d style=%08X menu=%r class=%r caption=%r' % (
        x, y, cx, cy, style, mn, cl, cap)
    if style & 0x40:
        pt = struct.unpack_from('<H', b, p)[0]
        f, p = cstr(b, p + 2)
        line += ' font=%d,%r' % (pt, f)
    out.append(line)
    for _ in range(n):
        x, y, cx, cy, cid, st = struct.unpack_from('<hhhhHI', b, p)
        p += 14
        if b[p] & 0x80:
            cls = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX',
                   0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}.get(b[p], hex(b[p]))
            p += 1
        else:
            cls, p = cstr(b, p)
        if b[p] == 0xFF:
            txt = '#%d' % struct.unpack_from('<H', b, p + 1)[0]
            p += 3
        else:
            txt, p = cstr(b, p)
        extra = b[p]
        p += 1 + extra
        out.append('  %-9s %r id=%d %d,%d,%d,%d style=%08X' % (cls, txt, cid, x, y, cx, cy, st))
    return '\n'.join(out)


def strings(b, rid):
    out = []
    p = 0
    for i in range(16):
        if p >= len(b):
            break
        n = b[p]
        if n:
            out.append('  %d %r' % ((rid - 1) * 16 + i, b[p + 1:p + 1 + n].decode('latin-1')))
        p += 1 + n
    return '\n'.join(out)


def accel(b):
    out = []
    for p in range(0, len(b) - 4, 5):
        fl, key, cmd = struct.unpack_from('<BHH', b, p)
        out.append('  key=%04X id=%d flags=%02X' % (key, cmd, fl))
        if fl & 0x80:
            break
    return '\n'.join(out)


def main():
    d = open(sys.argv[1], 'rb').read()
    want = sys.argv[2:]
    for t, r, fl, b in resources(d):
        tn = TYPES.get(t, t)
        if want and str(tn) not in want and str(t) not in want:
            continue
        print('== %s %r flags=%04X size=%d' % (tn, r, fl, len(b)))
        try:
            if t == 4:
                print(menu(b))
            elif t == 5:
                print(dialog(b))
            elif t == 6:
                print(strings(b, r))
            elif t == 9:
                print(accel(b))
            elif t in (10, 16):
                print('  ' + b.hex())
        except (IndexError, ValueError, struct.error) as e:
            print('  (decode error: %s)' % e)


if __name__ == '__main__':
    main()
