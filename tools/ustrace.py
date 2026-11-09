#!/usr/bin/env python3
"""ustrace - decode probe US1's records (tests/user/ustrace.c).

usage: ustrace.py DIR        (DIR holds USTRACE.BIN and USCOUNT.BIN)
       ustrace.py DIR --counts-only
Prints every record in call order, entries as
    seq task  MODULE.NAME(args)  from CS:IP  [registers]
and returns (recorded for the forwarders built with --ret) as
    seq task  <- MODULE.NAME = DX:AX  [registers]
then the call counts per module and ordinal.
Argument types come from Wine's X11-licensed spec files (ARCHITECTURE.md 3)
and, for DISPLAY's cursor entries, the DDK documentation.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODS = ['USER', 'GDI', 'DISPLAY', 'KEYBOARD', 'MOUSE', 'SYSTEM', 'SOUND', 'COMM', 'KERNEL']
SPEC = {'USER': 'user', 'GDI': 'gdi', 'DISPLAY': 'display', 'KEYBOARD': 'keyboard',
        'MOUSE': 'mouse', 'SYSTEM': 'system', 'SOUND': 'sound', 'COMM': 'comm',
        'KERNEL': 'kernel'}
EXTRA = {('DISPLAY', 101): ('Inquire', ['ptr']), ('DISPLAY', 102): ('SetCursor', ['ptr']),
         ('DISPLAY', 103): ('MoveCursor', ['word', 'word']), ('DISPLAY', 104): ('CheckCursor', [])}
REC = 168
NORD = 1024


def load_specs():
    out = {}
    for mod, f in SPEC.items():
        p = os.path.join(ROOT, '.tools', 'wine', 'specs', f + '.spec')
        if not os.path.exists(p):
            continue
        for line in open(p, encoding='latin-1'):
            m = re.match(r'^\s*(\d+)\s+(\S+)\s+(\w+)(?:\(([^)]*)\))?', line)
            if m:
                out[(mod, int(m.group(1)))] = (m.group(3), m.group(4).split() if m.group(4) is not None
                                               and m.group(2).startswith('pascal') else None)
    for k, v in EXTRA.items():
        out[k] = v
    return out


def docs_names():
    out = {}
    for mod in MODS:
        p = os.path.join(ROOT, 'docs', 'exports', mod + '.txt')
        if os.path.exists(p):
            for line in open(p):
                f = line.split()
                if f and f[0].isdigit() and f[-1] != '?':
                    out[(mod, int(f[0]))] = f[-1]
    return out


def show_ptr(p, typ):
    argw, sel, off, ln = p[0], p[1], p[2], p[3]
    data = p[5][:ln]
    s = f'{sel:04X}:{off:04X}'
    if not ln:
        return s + (' (null)' if not sel and not off else ' (unreadable)')
    if typ in ('str', 'segstr'):
        t = data.split(b'\0')[0]
        if all(32 <= c < 127 for c in t) and (len(t) < ln or ln == 24):
            return s + ' "' + t.decode('latin-1') + '"'
    return s + ' [' + data.hex(' ') + ']'


def main():
    d = sys.argv[1]
    specs, names = load_specs(), docs_names()
    regn = 'ax bx cx dx si di bp ds es ss sp fl'.split()
    segs = {}
    sp = os.path.join(d, 'USSEGS.TXT')
    if os.path.exists(sp):
        for line in open(sp, encoding='latin-1'):
            f = line.split()
            if len(f) >= 2:
                segs[int(f[0], 16)] = f[1] + (f':{int(f[2], 16)}' if len(f) > 2 else '')
    binp = os.path.join(d, 'USTRACE.BIN')
    recs = open(binp, 'rb').read() if os.path.exists(binp) else b''
    if '--counts-only' not in sys.argv:
        for i in range(len(recs) // REC):
            r = recs[i * REC:(i + 1) * REC]
            kind, modid, ordn, task, seq = struct.unpack_from('<HHHHI', r, 0)
            regs = struct.unpack_from('<12H', r, 12)
            rip, rcs = struct.unpack_from('<HH', r, 36)
            args = struct.unpack_from('<16H', r, 40)
            ptrs = []
            for k in range(3):
                argw, sel, off, ln, _ = struct.unpack_from('<HHHBB', r, 72 + 32 * k)
                ptrs.append((argw, sel, off, ln, 0, r[72 + 32 * k + 8:72 + 32 * k + 32]))
            mod = MODS[modid] if modid < len(MODS) else f'M{modid}'
            sname, types = specs.get((mod, ordn), (None, None))
            nm = names.get((mod, ordn)) or sname or '?'
            rs = ' '.join(f'{n}={v:04X}' for n, v in zip(regn, regs))
            if kind == 2:
                print(f'{seq:7} {task:04X}  <- {mod}.{nm} = {regs[3]:04X}:{regs[0]:04X}  [{rs}]')
                continue
            if types is not None:
                w, parts = 0, []
                pbyw = {p[0]: p for p in ptrs if p[0] != 0xFFFF}
                tot = sum(2 if t in ('word', 's_word') else 4 for t in types)
                for t in types:                 # left to right = from the top
                    sz = 2 if t in ('word', 's_word') else 4
                    w0 = (tot - sz) // 2 - w // 2
                    w += sz
                    if sz == 2:
                        parts.append(f'{args[w0]:04X}')
                    elif t == 'long':
                        parts.append(f'{args[w0 + 1]:04X}{args[w0]:04X}')
                    elif w0 in pbyw:
                        parts.append(show_ptr(pbyw[w0], t))
                    else:
                        parts.append(f'{args[w0 + 1]:04X}:{args[w0]:04X}')
                a = ', '.join(parts)
            else:
                a = 'stack ' + ' '.join(f'{x:04X}' for x in args[:8])
            who = segs.get(rcs, '')
            print(f'{seq:7} {task:04X}  {mod}.{nm}({a})  from {rcs:04X}:{rip:04X}{" " + who if who else ""}  [{rs}]')
    cp = os.path.join(d, 'USCOUNT.BIN')
    if os.path.exists(cp):
        c = open(cp, 'rb').read()
        seq, lost = struct.unpack_from('<II', c, 0)
        print(f'\n# counts: {seq} calls in all, {lost} records lost (buffer full)')
        for m in range(16):
            row = struct.unpack_from(f'<{NORD}H', c, 8 + m * NORD * 2)
            tot = [(o, n) for o, n in enumerate(row) if n]
            if not tot:
                continue
            mod = MODS[m] if m < len(MODS) else f'M{m}'
            print(f'{mod}: ' + ' '.join(f'{names.get((mod, o)) or specs.get((mod, o), ("?",))[0]}.{o}={n}'
                                        for o, n in sorted(tot, key=lambda x: -x[1])))


if __name__ == '__main__':
    main()
