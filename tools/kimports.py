#!/usr/bin/env python3
"""kimports - which KERNEL exports do the modules Windows 3.1 boots import?

Reads the NE import data (module reference table, imported-names table
and the relocation records of every segment; a published file format,
see ARCHITECTURE.md 3) of the modules a Windows install loads at boot,
and lists every KERNEL entry point they import, then diffs that against
the exports of w16's KERNEL (kernel/kernel.lnk).

Modules read: those SYSTEM.INI [boot] names (system.drv, keyboard.drv,
mouse.drv, display.drv, sound.drv, comm.drv, the fonts, drivers=, shell=,
...), GDI.EXE, USER.EXE, SHELL.DLL, MMSYSTEM.DLL, plus any extra files
given with -m. A module that is not an NE file (or not found) is noted.

Names for ordinals come from docs/exports/KERNEL.txt (tools/exports.py).

usage: kimports.py [-m FILE]... [--lnk kernel/kernel.lnk] WINDOWS_DIR
       e.g. tools/kimports.py .tools/ref/win31/cdrive/WINDOWS
Output: per module, the KERNEL ordinals it imports; then the union with
names, each marked "ok" (w16 exports it) or "MISSING", and a summary.
"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ini_section(path, section):
    """key -> value of [section] in an INI file (keys lower-cased; the
    first occurrence wins, as GetPrivateProfileString does)."""
    out, cur = [], None
    with open(path, 'rb') as f:
        for raw in f.read().decode('latin-1').splitlines():
            line = raw.strip()
            if line.startswith('['):
                cur = line[1:line.index(']')].strip().lower() if ']' in line else None
                continue
            if cur == section.lower() and '=' in line:
                k, v = line.split('=', 1)
                out.append((k.strip().lower(), v.strip()))
    return out


def find_file(windir, name):
    """Search the Windows directory and SYSTEM, case-insensitively."""
    for d in (os.path.join(windir, 'SYSTEM'), windir):
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            if f.lower() == name.lower():
                return os.path.join(d, f)
    return None


class NE:
    def __init__(self, path):
        self.path = path
        d = self.data = open(path, 'rb').read()
        if d[:2] != b'MZ':
            raise ValueError('not an MZ file')
        ne = self.ne = struct.unpack_from('<I', d, 0x3C)[0]
        if d[ne:ne + 2] != b'NE':
            raise ValueError('not an NE file')
        (self.seg_count, self.modref_count) = struct.unpack_from('<HH', d, ne + 0x1C)
        (self.seg_off, self.res_off, self.resname_off, self.modref_off,
         self.impname_off) = struct.unpack_from('<HHHHH', d, ne + 0x22)
        self.shift = struct.unpack_from('<H', d, ne + 0x32)[0] or 9
        self.flags = struct.unpack_from('<H', d, ne + 0x0C)[0]
        n = d[ne + self.resname_off]
        self.name = d[ne + self.resname_off + 1:ne + self.resname_off + 1 + n].decode('latin-1')

    def impname(self, off):
        p = self.ne + self.impname_off + off
        n = self.data[p]
        return self.data[p + 1:p + 1 + n].decode('latin-1')

    def modrefs(self):
        out = []
        for i in range(self.modref_count):
            off = struct.unpack_from('<H', self.data, self.ne + self.modref_off + 2 * i)[0]
            out.append(self.impname(off))
        return out

    def imports(self):
        """(module, ordinal or name, segment number, offset) per import
        relocation record."""
        d, refs, out = self.data, self.modrefs(), []
        for i in range(self.seg_count):
            sector, flen, flags, _ = struct.unpack_from(
                '<HHHH', d, self.ne + self.seg_off + 8 * i)
            if not sector or not (flags & 0x100):
                continue
            start = sector << self.shift
            flen = flen or 0x10000
            p = start + flen
            count = struct.unpack_from('<H', d, p)[0]
            p += 2
            for k in range(count):
                src, rflags, off, a, b = struct.unpack_from('<BBHHH', d, p + 8 * k)
                kind = rflags & 3
                if kind == 1:
                    out.append((refs[a - 1], b, i + 1, off))
                elif kind == 2:
                    out.append((refs[a - 1], self.impname(b), i + 1, off))
        return out


def kernel_names():
    """ordinal -> name, and name -> ordinal, from docs/exports/KERNEL.txt."""
    byord, byname = {}, {}
    p = os.path.join(ROOT, 'docs', 'exports', 'KERNEL.txt')
    for line in open(p):
        if line.startswith('#') or not line.strip():
            continue
        f = line.split()
        o = int(f[0])
        name = f[-1] if len(f) >= 5 or (len(f) == 4 and not f[3].islower()) else ''
        if name and not name.isdigit() and name not in ('yes', 'no'):
            byord[o] = name
            byname[name.upper()] = o
    return byord, byname


def lnk_exports(path):
    out = {}
    for line in open(path):
        m = re.match(r'\s*export\s+([^.\s]+)\.(\d+)', line, re.I)
        if m:
            out[int(m.group(2))] = m.group(1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('windir')
    ap.add_argument('-m', action='append', default=[], help='extra module file')
    ap.add_argument('--lnk', default=os.path.join(ROOT, 'kernel', 'kernel.lnk'))
    ap.add_argument('--missing-only', action='store_true')
    a = ap.parse_args()

    names = []
    sysini = os.path.join(a.windir, 'SYSTEM.INI')
    if os.path.exists(sysini):
        for k, v in ini_section(sysini, 'boot'):
            # shell=, drivers= and the module keys; grabbers are loaded
            # by WINOLDAP, not at boot, but read them too (harmless)
            for w in v.replace(',', ' ').split():
                if re.search(r'\.(drv|dll|fon|exe|fot)$', w, re.I) or k == 'drivers':
                    if '.' not in w:
                        continue        # drivers= aliases (none in 3.1)
                    names.append(w)
    names += ['GDI.EXE', 'USER.EXE', 'SHELL.DLL', 'MMSYSTEM.DLL']
    seen, mods = set(), []
    for n in names + a.m:
        p = n if os.path.exists(n) else find_file(a.windir, n)
        key = (p or n).lower()
        if key in seen:
            continue
        seen.add(key)
        mods.append((n, p))

    byord, byname = kernel_names()
    have = lnk_exports(a.lnk)
    union = {}                  # ordinal -> set of module names
    for n, p in mods:
        if not p:
            print(f'# {n}: not found')
            continue
        try:
            ne = NE(p)
        except ValueError as e:
            print(f'# {n}: {e}')
            continue
        imps = ne.imports()
        kord = set()
        for mod, what, seg, off in imps:
            if mod.upper() != 'KERNEL':
                continue
            if isinstance(what, str):
                o = byname.get(what.upper())
                if o is None:
                    print(f'# {ne.name}: imports KERNEL.{what} by name (unknown ordinal)')
                    continue
                what = o
            kord.add(what)
        others = sorted(set(m.upper() for m, *_ in imps) - {'KERNEL'})
        print(f'{os.path.basename(p)} ({ne.name}): imports {", ".join(["KERNEL"] + others) if kord else ", ".join(others) or "nothing"}')
        if kord:
            print('  KERNEL ' + ' '.join(str(o) for o in sorted(kord)))
        for o in kord:
            union.setdefault(o, set()).add(ne.name)
    print()
    print('ordinal  name                        w16      imported by')
    nmiss = 0
    for o in sorted(union):
        ok = o in have
        nmiss += not ok
        if a.missing_only and ok:
            continue
        print(f'{o:7}  {byord.get(o, "?"):26}  {"ok" if ok else "MISSING":7}  {" ".join(sorted(union[o]))}')
    print(f'\n{len(union)} KERNEL entry points imported, {len(union) - nmiss} exported by w16, {nmiss} missing')


if __name__ == '__main__':
    main()
