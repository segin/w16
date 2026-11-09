#!/usr/bin/env python3
"""modimports - who imports which entry points of a module?

Reads the NE import data (module reference table, imported-names table,
the relocation records of every segment: a published file format, see
ARCHITECTURE.md 3) of every NE file in a Windows install (WINDOWS and
WINDOWS\\SYSTEM: .EXE .DLL .DRV .CPL .MOD .TSK .SCR .3GR .2GR .386 are
tried; non-NE files are skipped) and lists, for one target module (GDI by
default), every ordinal imported, ranked by the number of importing
modules, with names from docs/exports/<MODULE>.txt.

Only static imports are seen: entry points found with GetProcAddress at
run time (as GDI finds the display driver's) are not in these tables.

usage: modimports.py [-t GDI] [-m FILE]... [--per-module] [--lnk FILE] WINDOWS_DIR
       e.g. tools/modimports.py .tools/ref/win31/cdrive/WINDOWS
--lnk: a wlink directive file whose "export NAME.ORD" lines are w16's
       exports; each ordinal is then marked ok or MISSING.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kimports import NE, lnk_exports  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTS = ('.exe', '.dll', '.drv', '.cpl', '.mod', '.tsk', '.scr', '.3gr', '.2gr', '.386')


def names_for(module):
    """ordinal -> name and NAME -> ordinal from docs/exports/MODULE.txt."""
    byord, byname = {}, {}
    p = os.path.join(ROOT, 'docs', 'exports', module.upper() + '.txt')
    if not os.path.exists(p):
        return byord, byname
    for line in open(p):
        if line.startswith('#') or not line.strip():
            continue
        f = line.split()
        o = int(f[0])
        name = f[-1]
        if not name.isdigit() and name not in ('yes', 'no', '-') and not name.islower():
            byord[o] = name
            byname[name.upper()] = o
    return byord, byname


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('windir')
    ap.add_argument('-t', '--target', default='GDI')
    ap.add_argument('-m', action='append', default=[], help='extra module file')
    ap.add_argument('--per-module', action='store_true')
    ap.add_argument('--lnk')
    a = ap.parse_args()
    target = a.target.upper()
    byord, byname = names_for(target)
    have = lnk_exports(a.lnk) if a.lnk else None

    files = []
    for d in (a.windir, os.path.join(a.windir, 'SYSTEM')):
        if os.path.isdir(d):
            files += sorted(os.path.join(d, f) for f in os.listdir(d)
                            if f.lower().endswith(EXTS))
    files += a.m
    union = {}
    for p in files:
        try:
            ne = NE(p)
            imps = ne.imports()
        except (ValueError, IndexError, Exception):
            continue
        ords = set()
        for mod, what, seg, off in imps:
            if mod.upper() != target:
                continue
            if isinstance(what, str):
                o = byname.get(what.upper())
                if o is None:
                    print(f'# {ne.name}: imports {target}.{what} by name (unknown ordinal)')
                    continue
                what = o
            ords.add(what)
        if not ords:
            continue
        label = f'{os.path.basename(p)}'
        if a.per_module:
            print(f'{label} ({ne.name}): {len(ords)}')
            print('  ' + ' '.join(str(o) for o in sorted(ords)))
        for o in ords:
            union.setdefault(o, set()).add(os.path.basename(p).upper())
    if a.per_module:
        print()
    print(f'{"ordinal":>7}  {"name":28} {"n":>3}  ' + ('w16      ' if have is not None else '') + 'imported by')
    nmiss = 0
    for o in sorted(union, key=lambda o: (-len(union[o]), o)):
        mark = ''
        if have is not None:
            ok = o in have
            nmiss += not ok
            mark = f'{"ok" if ok else "MISSING":7}  '
        print(f'{o:7}  {byord.get(o, "?"):28} {len(union[o]):3}  {mark}{" ".join(sorted(union[o]))}')
    tail = f', {len(union) - nmiss} exported by w16, {nmiss} missing' if have is not None else ''
    print(f'\n{len(union)} {target} entry points imported{tail}')


if __name__ == '__main__':
    main()
