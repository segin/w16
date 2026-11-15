#!/usr/bin/env python3
"""calccmp - compare two CA1 logs (tests/apps/ca1.c) line by line.

A log line is the test line followed by 8 fields: the standard display,
the scientific display, the clipboard after Copy, the indicators 400-403,
and the display after Copy. The test line itself may contain '|', so the
fields are taken from the right.

usage: calccmp.py REF.txt OTHER.txt [-v] [-n MAX]
Prints the differing lines (up to MAX, default 40) and a summary:
"compared N results, M differ".
"""
import sys

NAMES = ['std', 'sci', 'clip', 'stat', 'msci', 'mstd', 'paren', 'after']


def load(fn):
    out = []
    for line in open(fn, encoding='latin-1'):
        line = line.rstrip('\r\n')
        f = line.split('|')
        if len(f) < 9:
            continue
        out.append(('|'.join(f[:-8]), f[-8:]))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    verbose = '-v' in sys.argv
    mx = 40
    if '-n' in sys.argv:
        mx = int(sys.argv[sys.argv.index('-n') + 1])
        args.remove(str(mx))
    a, b = load(args[0]), load(args[1])
    n = min(len(a), len(b))
    bad = 0
    shown = 0
    for i in range(n):
        la, fa = a[i]
        lb, fb = b[i]
        if la != lb:
            print('line %d: test lines differ: %r / %r' % (i, la, lb))
            bad += 1
            continue
        # the hidden display keeps old text; compare only the visible one
        diffs = [NAMES[k] for k in range(8) if fa[k] != fb[k]]
        if diffs:
            bad += 1
            if shown < mx:
                shown += 1
                print('%-34s %s' % (la, ' '.join('%s: %r vs %r' % (d, fa[NAMES.index(d)], fb[NAMES.index(d)]) for d in diffs)))
    if len(a) != len(b):
        print('logs have %d and %d results' % (len(a), len(b)))
    print('compared %d results, %d differ' % (n, bad))
    return 1 if bad or len(a) != len(b) else 0


if __name__ == '__main__':
    sys.exit(main())
