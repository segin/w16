#!/usr/bin/env python3
"""termcmp - compare two runs of the termination dialog scenario
(tests/ddeml/dmshot.sh xterm with tests/ddeml/scripts/xtermr.in): one
with Microsoft's DDEML, one with w16's (ARCHITECTURE.md, DDEML).

The probe's ticker (B) logs the gaps the system-modal dialog makes in
every other task: when the first dialog came after DdeUninitialize began,
how long after Retry the second one came; DdeUninitialize must return 1
after Close. Those times are GetTickCount differences, taken on DOSBox's
emulated clock: equal within TOL ms (the ticker's 100 ms and one timer
tick). The dialog's screenshot (r0.png) must equal Microsoft's but for
the three places w16's text reads "w16" for "Windows" (ARCHITECTURE.md 3).

usage: termcmp.py MSDIR W16DIR   (exit 0: equal)
"""
import re
import subprocess
import sys

TOL = 300
# the rebranded words: the caption's "Windows DDE" and the two
# "return to Windows." lines (left, top, right, bottom; inclusive)
MASK = [(276, 38, 372, 51), (345, 120, 402, 132), (401, 162, 458, 174)]


def events(path):
    t0 = took = ret = None
    gaps = []
    for line in open(path, encoding='latin-1'):
        m = re.search(r'uninit at (\d+)', line)
        if m:
            t0 = int(m.group(1))
        m = re.search(r'gap (\d+) to (\d+)', line)
        if m:
            gaps.append((int(m.group(1)), int(m.group(2))))
        m = re.search(r'DdeUninitialize\(\S+\) -> (\d+)', line)
        if m:
            ret = m.group(1)
        m = re.search(r'took (\d+) ms', line)
        if m:
            took = int(m.group(1))
    return t0, gaps, ret, took


def times(d):
    t0, gaps, ret, took = events(d + '/os.log')
    if t0 is None or len(gaps) != 2 or ret != '1':
        return None, 'expected the start, two dialogs and a return of 1: start %s, gaps %s, ' \
            'returned %s' % (t0, gaps, ret)
    return (gaps[0][0] - t0, gaps[1][0] - gaps[0][1]), None


def masked(png):
    args = ['magick', png, '-fill', 'black']
    for l, t, r, b in MASK:
        args += ['-draw', 'rectangle %d,%d %d,%d' % (l, t, r, b)]
    return args + ['-depth', '8', 'rgb:-']


def main():
    ms, w16 = sys.argv[1], sys.argv[2]
    ok = True
    tm, err = times(ms)
    if err:
        print('ms: ' + err)
        return 1
    tw, err = times(w16)
    if err:
        print('w16: ' + err)
        return 1
    for what, a, b in (('first dialog after', tm[0], tw[0]), ('Retry to the next dialog', tm[1], tw[1])):
        same = abs(a - b) <= TOL
        print('%s: Microsoft %d ms, w16 %d ms%s' % (what, a, b, '' if same else '  DIFFERENT'))
        ok = ok and same
    a = subprocess.run(masked(ms + '/r0.png'), capture_output=True).stdout
    b = subprocess.run(masked(w16 + '/r0.png'), capture_output=True).stdout
    if not a or a != b:
        print('the dialog (r0.png) differs from Microsoft\'s beyond the rebranded words')
        ok = False
    else:
        print('the dialog (r0.png) equals Microsoft\'s but for the rebranded words')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
