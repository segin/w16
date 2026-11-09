#!/usr/bin/env python3
"""gdtrace - compare the display-driver output calls of two GD logs.

tests/gdiboot.sh -l (or -l -L) records every call GDI makes into the
display driver (GD1, tests/gdi). Two runs, Microsoft's GDI and w16's, draw
the same pixels when they make the same output calls with the same
coordinates, ROPs, strings, clip rectangles and drawing modes. This tool
extracts the drawing calls (BITBLT, EXTTEXTOUT with a positive count,
OUTPUT, FASTBORDER, PIXEL with a DRAWMODE, DIBSCREENBLT, STRBLT,
DEVICEBITMAPBITS, SCANLR), drops what differs between runs without
meaning (selectors, offsets of GDI's own data, the caller's address) and
prints a unified diff of the two sequences.

usage: gdtrace.py MS.LOG W16.LOG [--context N] [--all]
       --all: every driver call, not only drawing calls
"""
import difflib
import re
import sys

DRAW = {'BITBLT', 'EXTTEXTOUT', 'OUTPUT', 'FASTBORDER', 'PIXEL', 'DIBSCREENBLT', 'STRBLT',
        'DEVICEBITMAPBITS', 'SCANLR', 'SAVESCREENBITMAP'}
PTR = re.compile(r'\b[0-9A-F]{4}:[0-9A-F]{4}\b')


def calls(path, everything):
    out, cur = [], None
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        m = re.match(r'\s*D>[0-9A-F]+ (\w+)(.*?)(  @.*)?$', line)
        if m:
            name, args = m.group(1), m.group(2)
            cur = None
            if not everything and name not in DRAW:
                continue
            if name == 'EXTTEXTOUT':
                f = args.split()
                if len(f) > 5 and int(f[5], 16) >= 0x8000:
                    continue                    # an extent query
            if name == 'PIXEL' and args.split()[-1] == '0000:0000':
                continue                        # a read
            # keep plain values, mark pointers as null or not
            args = PTR.sub(lambda p: 'null' if p.group(0) == '0000:0000' else 'ptr', args)
            cur = [f'{name}{args}']
            out.append(cur)
            continue
        m = re.match(r'\s*\.(\w+): (.*)$', line)
        if m and cur is not None and m.group(1) in ('clip', 'str', 'pts', 'rect', 'opaque', 'dm', 'dx',
                                                     'brush', 'pen', 'bmi'):
            val = m.group(2)
            if m.group(1) == 'dm':
                val = val[:23 * 3]              # Rop2, bkMode, colours; not the break fields
            cur.append(f'  {m.group(1)}: {val}')
            continue
        if line.startswith('    ') and cur is not None and cur[-1].startswith('  pts'):
            cur[-1] += ' ' + line.strip()
        if re.match(r'\s*D<', line):
            cur = None
    return ['\n'.join(c) for c in out]


def main():
    a = sys.argv[1:]
    ctx = 3
    everything = '--all' in a
    if everything:
        a.remove('--all')
    if '--context' in a:
        i = a.index('--context')
        ctx = int(a[i + 1])
        del a[i:i + 2]
    ms, w16 = calls(a[0], everything), calls(a[1], everything)
    diff = list(difflib.unified_diff(ms, w16, 'ms', 'w16', n=ctx, lineterm=''))
    for d in diff:
        print(d)
    print(f'# {len(ms)} calls (ms), {len(w16)} calls (w16), '
          f'{sum(1 for d in diff if d.startswith(("-", "+")) and not d.startswith(("---", "+++")))} lines differ')


if __name__ == '__main__':
    main()
