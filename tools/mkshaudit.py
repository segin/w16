#!/usr/bin/env python3
"""mkshaudit - the damaged files probe SH15 (tests/apps/sh15.c) hands to
SHELL.DLL: registration databases and icon files, each a well-formed one
with one thing wrong. Everything is made here from scratch or from w16's
own build (no Microsoft file is read).

  R0.DAT    a good REG.DAT (the format of tools/regdat.py, ARCHITECTURE.md
            13.2): .classes with .w16 = w16file, w16file = W16 Document,
            w16file\\shell\\open\\command = C:\\SH5.EXE %1
  R1.DAT    empty (0 bytes)
  R2.DAT    the signature wrong ("XHCC3.10")
  R3.DAT    cut after 48 bytes (the header and two entries)
  R4.DAT    the text area's size 100h bytes more than the file holds
  R5.DAT    .classes' first child an entry past the table (FFF0h)
  R6.DAT    .w16's name an entry past the table
  R7.DAT    the string "w16file" (.w16's value) FFF0h bytes long
  R8.DAT    the first free entry past the table
  R9.DAT    0 hash buckets
  RC.DAT    .w16 its own next sibling (a cycle)
  RT.DAT    the first string's WORD an even FFFEh (a free block of 64K)
  NEOK.EXE  w16's PROGMAN.EXE (46 group icons) as it is
  NECUT.EXE the same, cut after the first RT_GROUP_ICON resource entry
  NECUT2.EXE cut where the resident-name table starts (the resource
            table whole, no resource data)
  NECUT3.EXE cut one byte before that
  ICOFF.ICO an .ICO file whose header counts FFFFh images (one there)
  ICOCUT.ICO an .ICO file counting 3 images that ends after the first
            directory entry

usage: mkshaudit.py PROGMAN.EXE STOCK.ICO OUTDIR
"""
import os
import struct
import sys

NB = 37


class Db:
    def __init__(self):
        self.ents = [[0, 0, 0, 0]] + [[i, 0, 0, 0] for i in range(1, NB + 1)]
        self.text = b''
        self.strs = {}

    def string(self, s):
        if s in self.strs:
            i = self.strs[s]
            self.ents[i][1] += 1
            return i
        i = len(self.ents)
        b = s.encode('latin-1')
        self.text += struct.pack('<H', 2 * i + 1) + b
        bucket = 1 + sum(c for c in s.upper()[:39].encode('latin-1')) % NB
        p = bucket
        while self.ents[p][0] != bucket:
            p = self.ents[p][0]
        self.ents.append([bucket, 1, len(b), len(self.text) - len(b)])
        self.ents[p][0] = i
        self.strs[s] = i
        return i

    def key(self, parent, name, value=None):
        n = self.string(name)
        v = self.string(value) if value else 0
        k = len(self.ents)
        self.ents.append([self.ents[parent][1], 0, n, v])
        self.ents[parent][1] = k
        return k

    def build(self, free=None, nb=NB, tsize=None):
        first = len(self.ents)
        ents = [list(e) for e in self.ents]
        for i in range(16):
            ents.append([first + i + 1 if i < 15 else 0, 0, 0, 0])
        n = len(ents)
        text = self.text
        hdr = b'SHCC3.10' + struct.pack('<5I2H', 0x20, 0x20, n, 0x20 + 8 * n,
                                        len(text) if tsize is None else tsize,
                                        nb, first if free is None else free)
        return hdr + b''.join(struct.pack('<4H', *e) for e in ents) + text


def good():
    d = Db()
    classes = d.key(0, '.classes')
    w16 = d.key(classes, '.w16', 'w16file')
    f = d.key(classes, 'w16file', 'W16 Document')
    s = d.key(f, 'shell')
    o = d.key(s, 'open')
    d.key(o, 'command', 'C:\\SH5.EXE %1')
    return d, classes, w16


def patch(b, off, fmt, *v):
    b = bytearray(b)
    struct.pack_into(fmt, b, off, *v)
    return bytes(b)


def registry(out):
    d, classes, w16 = good()
    ok = d.build()
    ent = lambda i, w: 0x20 + 8 * i + 2 * w            # noqa: E731
    files = {
        'R0': ok,
        'R1': b'',
        'R2': b'X' + ok[1:],
        'R3': ok[:48],
        'R4': patch(ok, 0x18, '<I', len(d.text) + 0x100),
        'R5': patch(ok, ent(classes, 1), '<H', 0xFFF0),
        'R6': patch(ok, ent(w16, 2), '<H', 0xFFF0),
        'R7': patch(ok, ent(d.strs['w16file'], 2), '<H', 0xFFF0),
        'R8': d.build(free=0xFFF0),
        'R9': d.build(nb=0),
        'RC': patch(ok, ent(w16, 0), '<H', w16),
        'RT': patch(ok, 0x20 + 8 * struct.unpack_from('<I', ok, 0x10)[0], '<H', 0xFFFE),
    }
    for name, data in files.items():
        open(os.path.join(out, name + '.DAT'), 'wb').write(data)


def icons(progman, ico, out):
    ne = open(progman, 'rb').read()
    open(os.path.join(out, 'NEOK.EXE'), 'wb').write(ne)
    h = struct.unpack_from('<I', ne, 0x3C)[0]
    p = h + struct.unpack_from('<H', ne, h + 0x24)[0] + 2
    while True:
        t, n = struct.unpack_from('<HH', ne, p)
        assert t, 'no RT_GROUP_ICON'
        if t == 0x800E:
            break
        p += 8 + 12 * n
    open(os.path.join(out, 'NECUT.EXE'), 'wb').write(ne[:p + 8 + 12])
    names = h + struct.unpack_from('<H', ne, h + 0x26)[0]
    open(os.path.join(out, 'NECUT2.EXE'), 'wb').write(ne[:names])
    open(os.path.join(out, 'NECUT3.EXE'), 'wb').write(ne[:names - 1])
    one = open(ico, 'rb').read()
    assert struct.unpack_from('<3H', one) == (0, 1, 1)
    open(os.path.join(out, 'ICOFF.ICO'), 'wb').write(patch(one, 4, '<H', 0xFFFF))
    open(os.path.join(out, 'ICOCUT.ICO'), 'wb').write(patch(one, 4, '<H', 3)[:22])


def main():
    progman, ico, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    registry(out)
    icons(progman, ico, out)


if __name__ == '__main__':
    main()
