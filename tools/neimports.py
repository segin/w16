#!/usr/bin/env python3
"""neimports - a module's interface, read from NE file headers.

Everything here comes from the NE tables of the files of a Windows install
(entry table, resident and non-resident names, module reference and
imported-names tables, segment relocation records, the resource table):
a published file format (ARCHITECTURE.md 3). No code is read.

usage: neimports.py [options] MODULE WINDOWS_DIR
  MODULE        a module name (USER, GDI, KERNEL, ...); its file is found
                by module name among the NE files of WINDOWS_DIR and
                WINDOWS_DIR/SYSTEM, or given with --file
  --exports     the module's entry table: ordinal, name, resident or
                non-resident name, code/data/constant, segment class
  --importers   every NE file that imports MODULE: per file, the
                ordinals; then each ordinal ranked by importer count
  --imports     what MODULE itself imports, per module, with names
  --resources   MODULE's resources (type, id, size; bitmap, icon and
                cursor dimensions; dialog size and control count)
  (no option: all four)

Names for ordinals come from docs/exports/<MODULE>.txt (tools/exports.py:
OpenWatcom import libraries, then Wine's X11-licensed spec files), else
from the module's own name tables.
"""
import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NE_EXT = ('.exe', '.dll', '.drv', '.cpl', '.scr', '.mod', '.tsk', '.fon',
          '.fot', '.acm', '.386')

RT = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING',
      7: 'FONTDIR', 8: 'FONT', 9: 'ACCELERATOR', 10: 'RCDATA', 12: 'GROUP_CURSOR',
      14: 'GROUP_ICON', 16: 'VERSION'}


class NE:
    def __init__(self, path):
        self.path = path
        d = self.data = open(path, 'rb').read()
        if d[:2] != b'MZ' or len(d) < 0x40:
            raise ValueError('not an MZ file')
        ne = self.ne = struct.unpack_from('<I', d, 0x3C)[0]
        if d[ne:ne + 2] != b'NE':
            raise ValueError('not an NE file')
        (self.entry_off, self.entry_len) = struct.unpack_from('<HH', d, ne + 4)
        self.flags = struct.unpack_from('<H', d, ne + 0x0C)[0]
        self.autods = struct.unpack_from('<H', d, ne + 0x0E)[0]
        (self.seg_count, self.modref_count, self.nonres_len) = struct.unpack_from('<HHH', d, ne + 0x1C)
        (self.seg_off, self.rsrc_off, self.resname_off, self.modref_off,
         self.impname_off) = struct.unpack_from('<HHHHH', d, ne + 0x22)
        self.nonres_pos = struct.unpack_from('<I', d, ne + 0x2C)[0]
        self.shift = struct.unpack_from('<H', d, ne + 0x32)[0] or 9
        self.res_names = self._names(ne + self.resname_off)
        self.nonres_names = self._names(self.nonres_pos) if self.nonres_pos else []
        self.name = self.res_names[0][0] if self.res_names else '?'
        self.segs = []
        for i in range(self.seg_count):
            self.segs.append(struct.unpack_from('<HHHH', d, ne + self.seg_off + 8 * i))

    def _names(self, p):
        out, d = [], self.data
        while p < len(d) and d[p]:
            n = d[p]
            s = d[p + 1:p + 1 + n].decode('latin-1')
            o = struct.unpack_from('<H', d, p + 1 + n)[0]
            out.append((s, o))
            p += n + 3
        return out

    def impname(self, off):
        p = self.ne + self.impname_off + off
        n = self.data[p]
        return self.data[p + 1:p + 1 + n].decode('latin-1')

    def modrefs(self):
        return [self.impname(struct.unpack_from('<H', self.data, self.ne + self.modref_off + 2 * i)[0])
                for i in range(self.modref_count)]

    def imports(self):
        """(module, ordinal or name) per import relocation record."""
        d, refs, out = self.data, self.modrefs(), []
        for i, (sector, flen, flags, _) in enumerate(self.segs):
            if not sector or not (flags & 0x100):
                continue
            p = (sector << self.shift) + (flen or 0x10000)
            count = struct.unpack_from('<H', d, p)[0]
            p += 2
            for k in range(count):
                src, rflags, off, a, b = struct.unpack_from('<BBHHH', d, p + 8 * k)
                kind = rflags & 3
                if kind == 1:
                    out.append((refs[a - 1].upper(), b))
                elif kind == 2:
                    out.append((refs[a - 1].upper(), self.impname(b)))
        return out

    def entries(self):
        """ordinal -> (segment or 'const', flags byte); segment 0 gaps skipped."""
        d, p, end, o, out = self.data, self.ne + self.entry_off, self.ne + self.entry_off + self.entry_len, 1, {}
        while p < end:
            count, typ = d[p], d[p + 1] if p + 1 < end else 0
            p += 2
            if count == 0:
                break
            if typ == 0:
                o += count
                continue
            for _ in range(count):
                if typ == 0xFF:
                    fl, _int, seg, off = struct.unpack_from('<BHBH', d, p)
                    p += 6
                else:
                    fl, off = struct.unpack_from('<BH', d, p)
                    seg = 'const' if typ == 0xFE else typ
                    p += 3
                out[o] = (seg, fl)
                o += 1
        return out

    def resources(self):
        """[(type, id, size, bytes)]"""
        d = self.data
        if self.rsrc_off == self.resname_off:
            return []
        p = self.ne + self.rsrc_off
        shift = struct.unpack_from('<H', d, p)[0]
        p += 2
        out = []
        while True:
            tid = struct.unpack_from('<H', d, p)[0]
            if tid == 0:
                break
            cnt = struct.unpack_from('<H', d, p + 2)[0]
            p += 8
            for _ in range(cnt):
                off, ln, fl, rid = struct.unpack_from('<HHHH', d, p)
                p += 12
                t = RT.get(tid & 0x7FFF, tid & 0x7FFF) if tid & 0x8000 else self._rname(tid)
                i = rid & 0x7FFF if rid & 0x8000 else self._rname(rid)
                blob = d[off << shift:(off << shift) + (ln << shift)]
                out.append((t, i, ln << shift, fl, blob))
        return out

    def _rname(self, off):
        p = self.ne + self.rsrc_off + off
        n = self.data[p]
        return '"' + self.data[p + 1:p + 1 + n].decode('latin-1') + '"'


def ord_names(mod):
    out = {}
    p = os.path.join(ROOT, 'docs', 'exports', mod.upper() + '.txt')
    if os.path.exists(p):
        for line in open(p):
            f = line.split()
            if f and f[0].isdigit() and f[-1] != '?':
                out[int(f[0])] = f[-1]
    return out


def ne_files(windir):
    out = []
    for d in (windir, os.path.join(windir, 'SYSTEM')):
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if os.path.splitext(f)[1].lower() in NE_EXT:
                try:
                    out.append(NE(os.path.join(d, f)))
                except (ValueError, struct.error, IndexError):
                    pass
    return out


def seg_class(ne, seg):
    if seg == 'const':
        return 'const'
    flags = ne.segs[seg - 1][2]
    kind = 'DATA' if flags & 1 else 'CODE'
    attr = 'fixed' if not flags & 0x10 else ('discardable' if flags & 0x1000 else 'moveable')
    return f'{kind} {seg:2} {attr}{" preload" if flags & 0x40 else ""}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('windir')
    ap.add_argument('--file')
    for o in ('exports', 'importers', 'imports', 'resources'):
        ap.add_argument('--' + o, action='store_true')
    a = ap.parse_args()
    if not (a.exports or a.importers or a.imports or a.resources):
        a.exports = a.importers = a.imports = a.resources = True
    mod = a.module.upper()
    files = ne_files(a.windir)
    me = NE(a.file) if a.file else next((n for n in files if n.name.upper() == mod), None)
    if not me:
        sys.exit(f'{mod}: no NE file with that module name in {a.windir}')
    names = ord_names(mod)
    own = {o: (s, 'res') for s, o in me.res_names[1:]}
    own.update({o: (s, 'nonres') for s, o in me.nonres_names[1:]})
    ents = me.entries()
    rel = lambda p: os.path.relpath(p, a.windir).replace(os.sep, '\\').upper()

    if a.exports:
        print(f'# {mod} ({rel(me.path)}): entry table, {len(ents)} entries; flags {me.flags:04X}, '
              f'automatic data segment {me.autods}')
        print('# ordinal  name  where-named  segment  entry-flags (1 exported, 2 shared data)')
        for o in sorted(ents):
            seg, fl = ents[o]
            nm, where = own.get(o, (names.get(o, '?'), '-'))
            print(f'{o:5}  {nm:28} {where:6}  {seg_class(me, seg):28} {fl:02X}')
        print()

    if a.imports:
        print(f'# {mod} imports (ordinal name):')
        byref = {}
        for m, w in me.imports():
            byref.setdefault(m, set()).add(w)
        for m in sorted(byref):
            mn = ord_names(m)
            items = sorted(byref[m], key=lambda w: (isinstance(w, str), w))
            print(f'{m} ({len(items)}):')
            for w in items:
                print(f'  {w:5}  {mn.get(w, "?") if isinstance(w, int) else "(by name)"}')
        print()

    if a.importers:
        usage, per = {}, []
        for ne in files:
            if ne is me:
                continue
            ords = sorted({w for m, w in ne.imports() if m == mod}, key=lambda w: (isinstance(w, str), w))
            if not ords:
                continue
            per.append((rel(ne.path), ne.name, ords))
            for w in ords:
                usage.setdefault(w, []).append(ne.name)
        print(f'# importers of {mod}: {len(per)} files')
        for f, n, ords in per:
            print(f'{f} ({n}): {len(ords)}: ' + ' '.join(str(w) for w in ords))
        print()
        print(f'# {mod} ordinals by importer count: {len(usage)} of {len(ents)} entries imported')
        print('# ordinal  name  count  importers')
        for w in sorted(usage, key=lambda w: (-len(usage[w]), str(type(w)), w)):
            nm = names.get(w) if isinstance(w, int) else w
            print(f'{w:5}  {nm or "?":28} {len(usage[w]):3}  {" ".join(sorted(usage[w]))}')
        unused = [o for o in sorted(ents) if o not in usage]
        print(f'\n# not imported by any file of the install ({len(unused)}): ' + ' '.join(map(str, unused)))
        print()

    if a.resources:
        rs = me.resources()
        print(f'# {mod} resources: {len(rs)}')
        for t, i, size, fl, blob in rs:
            info = ''
            try:
                if t == 'BITMAP':
                    hs, w, h, pl, bpp = struct.unpack_from('<IiiHH', blob, 0)
                    info = f'{w}x{h} {bpp}bpp' if hs == 40 else f'header {hs}'
                    if hs == 12:
                        w, h, pl, bpp = struct.unpack_from('<HHHH', blob, 4)
                        info = f'{w}x{h} {bpp}bpp (core)'
                elif t in ('ICON', 'CURSOR'):
                    o = 4 if t == 'CURSOR' else 0
                    hs, w, h, pl, bpp = struct.unpack_from('<IiiHH', blob, o)
                    info = f'{w}x{h // 2} {bpp}bpp' if hs == 40 else 'old format'
                    if t == 'CURSOR' and hs == 40:
                        info += ' hotspot %d,%d' % struct.unpack_from('<HH', blob, 0)
                elif t in ('GROUP_ICON', 'GROUP_CURSOR'):
                    n = struct.unpack_from('<H', blob, 4)[0]
                    ims = []
                    for k in range(n):
                        e = blob[6 + 14 * k:20 + 14 * k]
                        if t == 'GROUP_ICON':
                            ims.append(f'{e[0]}x{e[1]}/{struct.unpack_from("<H", e, 6)[0]}bpp#{struct.unpack_from("<H", e, 12)[0]}')
                        else:
                            ims.append(f'{struct.unpack_from("<H", e, 0)[0]}x{struct.unpack_from("<H", e, 2)[0] // 2}#{struct.unpack_from("<H", e, 12)[0]}')
                    info = ' '.join(ims)
                elif t == 'DIALOG':
                    style, n, x, y, cx, cy = struct.unpack_from('<IBhhhh', blob, 0)
                    info = f'style {style:08X} {n} controls, {cx}x{cy} dialog units'
                elif t == 'STRING':
                    p, k = 0, 0
                    while p < len(blob):
                        ln = blob[p]
                        p += 1 + ln
                        k += ln > 0
                    info = f'{k} strings'
            except struct.error:
                info = '(short)'
            flag = ('moveable ' if fl & 0x10 else '') + ('preload ' if fl & 0x40 else '') + \
                   ('discardable' if fl & 0x1000 else '')
            print(f'{t!s:13} {i!s:8} {size:6}  {flag:32} {info}')


if __name__ == '__main__':
    main()
