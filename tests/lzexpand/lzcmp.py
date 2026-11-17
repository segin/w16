#!/usr/bin/env python3
"""lzcmp - compare two LZEXPAND probe logs (tests/lzexpand/lzp.c) section
by section ("== NAME" lines start a section; the lines before the first
one are "start", "LZP end" is "end"). Nothing is masked: two runs of
Microsoft's LZEXPAND give identical logs (ARCHITECTURE.md, LZEXPAND).

usage: lzcmp.py REF.log OTHER.log OUTDIR [SECTION ...]
Prints "SECTION: ok" or "SECTION: FAIL (OUTDIR/SECTION.diff)" for every
section (or the ones named), exit status 1 if any differs or is missing.
"""
import difflib
import os
import re
import sys


def sections(path):
    secs = {}
    order = []
    cur = 'start'
    secs[cur] = []
    order.append(cur)
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        m = re.match(r'== (.*)$', line)
        if m:
            cur = m.group(1)
            secs[cur] = []
            order.append(cur)
            continue
        if line.startswith('LZP end'):
            cur = 'end'
            secs[cur] = []
            order.append(cur)
        secs[cur].append(line)
    return secs, order


def main():
    ref, other, out = sys.argv[1:4]
    want = sys.argv[4:]
    a, order = sections(ref)
    b, _ = sections(other)
    os.makedirs(out, exist_ok=True)
    status = 0
    for name in order:
        if want and name not in want:
            continue
        fn = re.sub(r'[^A-Za-z0-9]+', '_', name)
        if name not in b:
            print('%s: FAIL (missing)' % name)
            status = 1
            continue
        if a[name] == b[name]:
            print('%s: ok' % name)
            continue
        d = os.path.join(out, fn + '.diff')
        with open(d, 'w', encoding='latin-1') as f:
            f.writelines(l + '\n' for l in difflib.unified_diff(a[name], b[name], 'ms', 'w16',
                                                                 lineterm='', n=1))
        print('%s: FAIL (%s)' % (name, d))
        status = 1
    for name in want:
        if name not in a:
            print('%s: FAIL (not in the reference log)' % name)
            status = 1
    return status


if __name__ == '__main__':
    sys.exit(main())
