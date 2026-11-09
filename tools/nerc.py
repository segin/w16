#!/usr/bin/env python3
"""nerc - print an NE file's menus, dialogs, string tables and
accelerators as .rc-like text (for reading resource templates as data).

The NE resource table and the 16-bit resource formats (MENU, DIALOG,
STRINGTABLE, ACCELERATORS) are published formats (Windows 3.1 SDK; the
resource formats are documented in the SDK's "Resource formats" notes).
Only resource data is decoded here, never code (ARCHITECTURE.md 3).

usage: nerc.py FILE.EXE [type...]     type: menu dialog string accel
       nerc.py --rc FILE.EXE NAME...   the named dialogs as compilable
                                       DIALOG statements (layout data for
                                       a w16 .rc; check every string)
"""
import struct
import sys


def resources(d):
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    rt = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
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


def q(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '""').replace('\t', '\\t') \
        .replace('\x08', '\\b').replace('\n', '\\n').replace('\r', '\\r') + '"'


def cstr(b, p):
    e = b.index(b'\0', p)
    return b[p:e].decode('cp1252', 'replace'), e + 1


def menu(b):
    out = []
    p = 4                               # version, header size

    def items(p, depth):
        while True:
            fl = struct.unpack_from('<H', b, p)[0]
            p += 2
            if fl & 0x10:               # MF_POPUP
                s, p = cstr(b, p)
                out.append('  ' * depth + 'POPUP %s  /* flags %04X */' % (q(s), fl))
                p = items(p, depth + 1)
            else:
                mid = struct.unpack_from('<H', b, p)[0]
                s, p = cstr(b, p + 2)
                if not s and not mid and not (fl & ~0x80):
                    out.append('  ' * depth + 'MENUITEM SEPARATOR')
                else:
                    out.append('  ' * depth + 'MENUITEM %s, %d  /* flags %04X */' % (q(s), mid, fl))
            if fl & 0x80:               # MF_END
                return p
    items(p, 0)
    return '\n'.join(out)


CLS = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX',
       0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}


def dialog(b):
    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', b, 0)
    p = 13

    def sz(p):
        if b[p] == 0xFF:
            return '#%d' % struct.unpack_from('<H', b, p + 1)[0], p + 3
        return cstr(b, p)
    mn, p = sz(p)
    cl, p = cstr(b, p)
    cap, p = cstr(b, p)
    out = ['DIALOG %d, %d, %d, %d  STYLE %08X  MENU %s  CLASS %s' % (x, y, cx, cy, style, q(mn), q(cl)),
           'CAPTION %s' % q(cap)]
    if style & 0x40:                    # DS_SETFONT
        pt = struct.unpack_from('<H', b, p)[0]
        f, p = cstr(b, p + 2)
        out.append('FONT %d, %s' % (pt, q(f)))
    for _ in range(n):
        x, y, cx, cy, cid, st = struct.unpack_from('<hhhhHI', b, p)
        p += 14
        if b[p] & 0x80:
            c = CLS.get(b[p], '#%02X' % b[p])
            p += 1
        else:
            c, p = cstr(b, p)
        if b[p] == 0xFF:
            t = '#%d' % struct.unpack_from('<H', b, p + 1)[0]
            p += 3
        else:
            t, p = cstr(b, p)
            t = q(t)
        extra = b[p]
        p += 1 + extra
        out.append('  CONTROL %s, %d, %s, %08X, %d, %d, %d, %d' % (t, cid, c, st, x, y, cx, cy))
    return '\n'.join(out)


def dialog_rc(name, b):
    """one dialog as an .rc DIALOG statement: every control as CONTROL
    with its exact class, style, id and rectangle"""
    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', b, 0)
    p = 13

    def sz(p):
        if b[p] == 0xFF:
            return None, p + 3
        return cstr(b, p)
    mn, p = sz(p)
    cl, p = cstr(b, p)
    cap, p = cstr(b, p)
    out = ['%s DIALOG %d, %d, %d, %d' % (name, x, y, cx, cy),
           'STYLE 0x%08XL' % style, 'CAPTION %s' % q(cap)]
    if style & 0x40:
        pt = struct.unpack_from('<H', b, p)[0]
        f, p = cstr(b, p + 2)
        out.append('FONT %d, %s' % (pt, q(f)))
    out.append('BEGIN')
    for _ in range(n):
        x, y, cx, cy, cid, st = struct.unpack_from('<hhhhHI', b, p)
        p += 14
        if b[p] & 0x80:
            c = q(CLS.get(b[p], '#%02X' % b[p]).capitalize())
            p += 1
        else:
            c, p = cstr(b, p)
            c = q(c)
        if b[p] == 0xFF:
            t = struct.unpack_from('<H', b, p + 1)[0]
            p += 3
            out.append('    ICON %d, %d, %d, %d, %d, %d' % (t, cid if cid != 0xFFFF else -1, x, y, cx, cy))
        else:
            t, p = cstr(b, p)
            out.append('    CONTROL %s, %d, %s, 0x%08XL, %d, %d, %d, %d' % (
                q(t), cid if cid != 0xFFFF else -1, c, st, x, y, cx, cy))
        p += 1 + b[p]
    out.append('END')
    return '\n'.join(out)


def strings(b, block):
    out = []
    p = 0
    for i in range(16):
        if p >= len(b):
            break
        n = b[p]
        if n:
            out.append('  %d, %s' % ((block - 1) * 16 + i, q(b[p + 1:p + 1 + n].decode('cp1252'))))
        p += 1 + n
    return '\n'.join(out)


def accel(b):
    out = []
    p = 0
    while p + 5 <= len(b):
        fl, key, cmd = struct.unpack_from('<BHH', b, p)
        p += 5
        out.append('  key %04X (%s), %d, flags %02X' % (key, repr(chr(key)) if 32 < key < 127 else '', cmd, fl))
        if fl & 0x80:
            break
    return '\n'.join(out)


def main():
    if sys.argv[1] == '--rc':
        d = open(sys.argv[2], 'rb').read()
        want = sys.argv[3:]
        for t, n, fl, b in resources(d):
            if t == 5 and str(n) in want:
                print(dialog_rc(n, b))
                print()
        return
    d = open(sys.argv[1], 'rb').read()
    want = set(sys.argv[2:]) or {'menu', 'dialog', 'string', 'accel'}
    kinds = {4: 'menu', 5: 'dialog', 6: 'string', 9: 'accel'}
    for t, n, fl, b in resources(d):
        k = kinds.get(t)
        if k not in want:
            continue
        print('/* %s %s (flags %04X) */' % (k, n, fl))
        if k == 'menu':
            print(menu(b))
        elif k == 'dialog':
            print(dialog(b))
        elif k == 'string':
            print(strings(b, n))
        else:
            print(accel(b))
        print()


if __name__ == '__main__':
    main()
