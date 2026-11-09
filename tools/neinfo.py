#!/usr/bin/env python3
"""neinfo - an NE module's interface: header, segments, exports, imports.

Reads only the published NE file format (ARCHITECTURE.md 3): the header,
segment table, resident and non-resident name tables, entry table,
module reference and imported-name tables, and each segment's relocation
records (to list the imports). Never looks at code bytes.

Import names come from docs/exports/<MODULE>.txt where w16 has one.

usage: neinfo.py FILE.EXE...
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def export_names(mod):
    p = os.path.join(ROOT, 'docs', 'exports', mod.upper() + '.txt')
    out = {}
    if not os.path.exists(p):
        return out
    for line in open(p):
        if line.startswith('#') or not line.strip():
            continue
        f = line.split()
        name = f[-1]
        if len(f) >= 4 and not name.isdigit() and name not in ('yes', 'no'):
            out[int(f[0])] = name
    return out


class NE:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        if d[:2] != b'MZ':
            raise ValueError('not an MZ file')
        ne = self.ne = struct.unpack_from('<I', d, 0x3C)[0]
        if d[ne:ne + 2] != b'NE':
            raise ValueError('not an NE file')
        h = self.h = {}
        (h['linkver'], h['linkrev'], h['entoff'], h['entlen'], h['crc'],
         h['flags'], h['autodata'], h['heap'], h['stack'], h['ip'], h['cs'],
         h['sp'], h['ss'], h['nseg'], h['nmod'], h['nrnlen'], h['segoff'],
         h['resoff'], h['rnoff'], h['modoff'], h['impoff'], h['nrnoff'],
         h['nmov'], h['shift'], h['nres'], h['os'], h['flags2'], h['gangoff'],
         h['ganglen'], h['swaparea'], h['expver']) = struct.unpack_from(
            '<BBHHIHHHHHHHHHHHHHHHHIHHHBBHHHH', d, ne + 2)
        self.shift = h['shift'] or 9

    def pstr(self, p):
        n = self.d[p]
        return self.d[p + 1:p + 1 + n].decode('latin-1'), p + 1 + n

    def names(self, p, end=None):
        out = []
        while (end is None or p < end) and self.d[p]:
            s, p = self.pstr(p)
            out.append((s, struct.unpack_from('<H', self.d, p)[0]))
            p += 2
        return out

    def resnames(self):
        return self.names(self.ne + self.h['rnoff'])

    def nonresnames(self):
        return self.names(self.h['nrnoff'], self.h['nrnoff'] + self.h['nrnlen'])

    def segments(self):
        out = []
        for i in range(self.h['nseg']):
            out.append(struct.unpack_from('<HHHH', self.d, self.ne + self.h['segoff'] + 8 * i))
        return out

    def entries(self):
        d, p, out, o = self.d, self.ne + self.h['entoff'], {}, 1
        end = p + self.h['entlen']
        while p < end:
            cnt, seg = d[p], d[p + 1]
            p += 2
            if cnt == 0:
                break
            for _ in range(cnt):
                if seg == 0:
                    pass
                elif seg == 0xFF:
                    fl, _, sn, off = struct.unpack_from('<BHBH', d, p)
                    out[o] = ('movable', fl, sn, off)
                    p += 6
                else:
                    fl, off = struct.unpack_from('<BH', d, p)
                    out[o] = ('fixed', fl, seg if seg != 0xFE else 'const', off)
                    p += 3
                o += 1
        return out

    def impname(self, off):
        return self.pstr(self.ne + self.h['impoff'] + off)[0]

    def modrefs(self):
        return [self.impname(struct.unpack_from('<H', self.d, self.ne + self.h['modoff'] + 2 * i)[0])
                for i in range(self.h['nmod'])]

    def imports(self):
        d, refs, out = self.d, self.modrefs(), []
        for i, (sector, flen, flags, _) in enumerate(self.segments()):
            if not sector or not (flags & 0x100):
                continue
            p = (sector << self.shift) + (flen or 0x10000)
            count = struct.unpack_from('<H', d, p)[0]
            p += 2
            for k in range(count):
                src, rflags, off, a, b = struct.unpack_from('<BBHHH', d, p + 8 * k)
                kind = rflags & 3
                if kind == 1:
                    out.append((refs[a - 1], b, i + 1, off, src))
                elif kind == 2:
                    out.append((refs[a - 1], self.impname(b), i + 1, off, src))
        return out


def main():
    for path in sys.argv[1:]:
        ne = NE(path)
        h = ne.h
        rn = ne.resnames()
        print(f'== {path}')
        print(f'module {rn[0][0]}  description {ne.nonresnames()[0][0]!r}')
        print(f'flags {h["flags"]:04X}  flags2 {h["flags2"]:02X}  os {h["os"]}  '
              f'expver {h["expver"]:04X}  linker {h["linkver"]}.{h["linkrev"]}')
        print(f'autodata seg {h["autodata"]}  heap {h["heap"]:04X}  stack {h["stack"]:04X}  '
              f'CS:IP {h["cs"]}:{h["ip"]:04X}  SS:SP {h["ss"]}:{h["sp"]:04X}  align shift {ne.shift}')
        print('segments (n: file length, flags, min alloc):')
        for i, (sec, flen, fl, mn) in enumerate(ne.segments()):
            kind = 'DATA' if fl & 1 else 'CODE'
            attrs = [kind] + [n for b, n in ((0x10, 'MOVEABLE'), (0x20, 'PURE'), (0x40, 'PRELOAD'),
                                             (0x80, 'RO/EO'), (0x100, 'RELOCS'), (0x1000, 'DISCARD'))
                              if fl & b]
            print(f'  {i + 1}: {flen:5X} {fl:04X} {mn:5X}  {" ".join(attrs)}')
        ents = ne.entries()
        exps = {o: n for n, o in rn[1:] + ne.nonresnames()[1:]}
        print('exports (ordinal: name, entry):')
        for o in sorted(set(ents) | set(exps)):
            e = ents.get(o)
            es = f'{e[0]} flags {e[1]:02X} {e[2]}:{e[3]:04X}' if e else '(no entry)'
            res = ' resident' if o in dict((x[1], 1) for x in rn[1:]) else ''
            print(f'  {o:4}: {exps.get(o, "-"):24} {es}{res}')
        print('imports:')
        bymod = {}
        for mod, what, seg, off, src in ne.imports():
            bymod.setdefault(mod.upper(), {}).setdefault(what, []).append(seg)
        for mod in sorted(bymod):
            names = export_names(mod)
            print(f'  {mod}:')
            for what in sorted(bymod[mod], key=lambda w: (isinstance(w, str), w)):
                segs = sorted(set(bymod[mod][what]))
                nm = names.get(what, '?') if isinstance(what, int) else '(by name)'
                print(f'    {what!s:>5} {nm:28} seg {",".join(map(str, segs))}')
        print()


if __name__ == '__main__':
    main()
