#!/usr/bin/env python3
"""exportdb - every export of every NE module in the reference installs.

Reads the NE tables (entry table, resident and non-resident names, module
references, relocation records: a published file format, ARCHITECTURE.md
3; no code is read) of every NE file in each reference install's Windows
and SYSTEM directories, and writes:

  docs/exportdb/exports.csv     one row per (install, module, ordinal):
      install, module, file, ordinal, name, residency, kind, importers,
      importer_files, w16
  docs/exportdb/modules.csv     one row per (install, module): file, type
      (DLL/driver/program/...), description, exports, imported ordinals,
      importers, w16 file, w16 exports
  docs/exportdb/README.md       a summary table per install
  build/exportdb.sqlite         (with --sqlite) the same two tables in
                                SQLite, for ad-hoc queries

Names: the module's own resident/non-resident names first, else
docs/exports/<MODULE>.txt (OpenWatcom import libraries, then X11-licensed
wine-20020228 specs), else blank.
importers: how many other NE files in the same install import the ordinal
(by ordinal or by name), and which.
w16: "yes" when w16's build of a module with the same module name
(build/dos, build/w16, build/win386, build/fonts) exports that ordinal.
That is "exported", not "implemented": w16's USER and GDI export every
ordinal and log the unimplemented ones as stubs (ARCHITECTURE.md 10, 11).

usage: exportdb.py [--sqlite]
"""
import csv
import os
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import neimports as ni  # noqa: E402

ROOT = ni.ROOT
INSTALLS = {'win31': '.tools/ref/win31/cdrive/WINDOWS',
            'wfw311a': '.tools/ref/wfw311a/cdrive/WINDOWS'}
W16_DIRS = ['build/dos', 'build/w16', 'build/w16/SYSTEM', 'build/win386', 'build/fonts']
OUT = os.path.join(ROOT, 'docs', 'exportdb')


def mtype(ne):
    ext = os.path.splitext(ne.path)[1].lower()
    if ext in ('.fon', '.fot'):
        return 'font'
    if ext == '.drv':
        return 'driver'
    if ext == '.cpl':
        return 'control panel'
    if ne.flags & 0x8000:
        return 'library'
    return 'program'


def names_of(ne):
    out = {}
    for s, o in ne.res_names[1:]:
        out.setdefault(o, (s, 'resident'))
    for s, o in ne.nonres_names[1:]:
        out.setdefault(o, (s, 'nonresident'))
    return out


def w16_modules():
    mods = {}
    for d in W16_DIRS:
        p = os.path.join(ROOT, d)
        if not os.path.isdir(p):
            continue
        for f in sorted(os.listdir(p)):
            if os.path.splitext(f)[1].lower() not in ni.NE_EXT:
                continue
            try:
                ne = ni.NE(os.path.join(p, f))
            except (ValueError, ni.struct.error, IndexError):
                continue
            mods.setdefault(ne.name.upper(), (os.path.relpath(ne.path, ROOT), set(ne.entries())))
    return mods


def main():
    want_sqlite = '--sqlite' in sys.argv[1:]
    w16 = w16_modules()
    os.makedirs(OUT, exist_ok=True)
    rows, mrows = [], []
    for inst, rel in INSTALLS.items():
        windir = os.path.join(ROOT, rel)
        if not os.path.isdir(windir):
            print(f'skipping {inst}: no {rel}', file=sys.stderr)
            continue
        files = ni.ne_files(windir)
        # importers: (MODULE, ordinal or NAME) -> set of importing files
        imp = {}
        for ne in files:
            fname = os.path.basename(ne.path)
            for mod, what in set(ne.imports()):
                imp.setdefault((mod, what if isinstance(what, int) else what.upper()), set()).add(fname)
        for ne in files:
            mod = ne.name.upper()
            fname = os.path.basename(ne.path)
            ents = ne.entries()
            nm = names_of(ne)
            docn = ni.ord_names(mod)
            wfile, wents = w16.get(mod, ('', set()))
            importers_all = set()
            imported = 0
            for o in sorted(ents):
                seg, _fl = ents[o]
                name, res = nm.get(o, (docn.get(o, ''), 'ordinal only'))
                who = set(imp.get((mod, o), set())) | set(imp.get((mod, name.upper()), set()) if name else set())
                who.discard(fname)
                importers_all |= who
                imported += bool(who)
                kind = 'const' if seg == 'const' else ('data' if ne.segs[seg - 1][2] & 1 else 'code')
                rows.append([inst, mod, fname, o, name, res, kind, len(who), ' '.join(sorted(who)),
                             ('yes' if o in wents else 'no') if wfile else ''])
            desc = ne.nonres_names[0][0] if ne.nonres_names else ''
            mrows.append([inst, mod, fname, mtype(ne), desc, len(ents), imported, len(importers_all),
                          wfile, len(set(ents) & wents) if wfile else ''])
    hdr = ['install', 'module', 'file', 'ordinal', 'name', 'residency', 'kind', 'importers',
           'importer_files', 'w16']
    mhdr = ['install', 'module', 'file', 'type', 'description', 'exports', 'imported_ordinals',
            'importers', 'w16_file', 'w16_exports']
    with open(os.path.join(OUT, 'exports.csv'), 'w', newline='') as f:
        csv.writer(f).writerows([hdr] + rows)
    with open(os.path.join(OUT, 'modules.csv'), 'w', newline='') as f:
        csv.writer(f).writerows([mhdr] + mrows)
    with open(os.path.join(OUT, 'README.md'), 'w') as f:
        f.write('# Export database\n\n'
                'Every export of every NE module in the reference installs, from the NE tables '
                '(`tools/exportdb.py`; regenerate after building w16). `exports.csv` has one row per '
                'ordinal (name, residency, code/data/constant, how many files in the same install import '
                'it and which, whether w16 exports it); `modules.csv` one row per module. '
                '`tools/exportdb.py --sqlite` also writes `build/exportdb.sqlite`. "w16 exports" counts '
                'exported ordinals, not implemented ones: w16\'s USER and GDI export stubs for the rest.\n')
        for inst in INSTALLS:
            ms = [m for m in mrows if m[0] == inst]
            if not ms:
                continue
            f.write(f'\n## {inst}\n\n| Module | File | Type | Exports | Imported | Importers | w16 exports |\n'
                    '|---|---|---|---|---|---|---|\n')
            for m in sorted(ms, key=lambda m: (-m[7], m[1])):
                w = f'{m[9]} ({m[8]})' if m[8] else ''
                f.write(f'| {m[1]} | {m[2]} | {m[3]} | {m[5]} | {m[6]} | {m[7]} | {w} |\n')
            f.write(f'\n{len(ms)} modules, {sum(m[5] for m in ms)} exports.\n')
    if want_sqlite:
        db = os.path.join(ROOT, 'build', 'exportdb.sqlite')
        os.makedirs(os.path.dirname(db), exist_ok=True)
        if os.path.exists(db):
            os.remove(db)
        c = sqlite3.connect(db)
        c.execute(f'create table exports ({", ".join(hdr)})')
        c.execute(f'create table modules ({", ".join(mhdr)})')
        c.executemany(f'insert into exports values ({",".join("?" * len(hdr))})', rows)
        c.executemany(f'insert into modules values ({",".join("?" * len(mhdr))})', mrows)
        c.commit()
        c.close()
    print(f'{len(mrows)} modules, {len(rows)} exports -> docs/exportdb/')


if __name__ == '__main__':
    main()
