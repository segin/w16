#!/usr/bin/env python3
"""gd4sum - GD4's logs side by side (tests/apps/gd4.c, tests/gdiheap.sh).

usage: gd4sum.py NAME=LOG [NAME=LOG ...] [--curve KIND]

Prints, for each log: the start of GDI's heaps (GFSR(1), GetHeapSpaces,
DGROUP bytes in use), what each kind of object adds to DGROUP and to the
other GDI heaps, and the limits; with --curve KIND (e.g. pen), the GDI
percentage after every 32 objects of that kind.
"""
import re
import sys


def parse(path):
    d = {'cost': {}, 'other': {}, 'limit': {}, 'curve': {}, 'start': None, 'block': {}}
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\r\n')
        m = re.match(r'OBS ~start: GFSR\(1\)=(\w+) ghs=(\w+)/(\w+)(.*)', line)
        if m:
            heaps = dict((h[0], h[1:]) for h in re.findall(r' (\w)=(\w+)/(\w+)/(\w+)/(\w+)', m.group(4)))
            d['start'] = (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16),
                          int(heaps.get('D', ('0', '0', '0', '0'))[2], 16))
            continue
        m = re.match(r'OBS ~(.*)D bytes=(\w+)', line)
        if m:
            d['cost'][m.group(1)] = int(m.group(2), 16)
            continue
        m = re.match(r'OBS ~(.*)([1-9]) bytes=(\w+)', line)
        if m:
            v = int(m.group(3), 16)
            if v >= 0x8000:
                v -= 0x10000
            d['other'][m.group(1)] = d['other'].get(m.group(1), 0) + v
            continue
        m = re.match(r'OBS ~(.*) block size=(\w+)', line)
        if m:
            d['block'][m.group(1)] = int(m.group(2), 16)
            continue
        m = re.match(r'OBS ~limit (.*)=(\w+)', line)
        if m:
            d['limit'][m.group(1)] = int(m.group(2), 16)
            continue
        m = re.match(r'OBS ~curve (.*) (\w+): GFSR\(1\)=(\w+)', line)
        if m:
            d['curve'].setdefault(m.group(1), []).append((int(m.group(2), 16), int(m.group(3), 16)))
    return d


def main(argv):
    curve = None
    if '--curve' in argv:
        i = argv.index('--curve')
        curve = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    runs = []
    for a in argv:
        name, path = a.split('=', 1)
        runs.append((name, parse(path)))
    w = max(12, max(len(n) for n, _ in runs))
    hdr = '%-34s' % '' + ''.join('%*s' % (w + 1, n) for n, _ in runs)
    print(hdr)
    for i, label in enumerate(['start GFSR(1) %', 'start GetHeapSpaces free', 'start GetHeapSpaces total',
                               'start DGROUP bytes in use']):
        print('%-34s' % label + ''.join('%*s' % (w + 1, d['start'][i] if d['start'] else '-')
                                        for _, d in runs))
    keys = []
    for _, d in runs:
        for k in d['cost']:
            if k not in keys:
                keys.append(k)
    print('-- DGROUP bytes added (other GDI heaps in brackets)')
    for k in keys:
        cells = []
        for _, d in runs:
            v = d['cost'].get(k)
            if v is not None and v >= 0x8000:
                v -= 0x10000
            o = d['other'].get(k)
            cells.append('-' if v is None else ('%d' % v) + (' [%d]' % o if o else ''))
        print('%-34s' % k[:34] + ''.join('%*s' % (w + 1, c) for c in cells))
    print('-- limits')
    keys = []
    for _, d in runs:
        for k in d['limit']:
            if k not in keys:
                keys.append(k)
    for k in keys:
        print('%-34s' % k[:34] + ''.join('%*s' % (w + 1, d['limit'].get(k, '-')) for _, d in runs))
    if curve:
        print('-- GFSR(1) after n objects:', curve)
        ns = sorted(set(n for _, d in runs for n, _ in d['curve'].get(curve, [])))
        for n in ns:
            print('%-34s' % n + ''.join('%*s' % (w + 1, dict(d['curve'].get(curve, [])).get(n, ''))
                                        for _, d in runs))


if __name__ == '__main__':
    main(sys.argv[1:])
