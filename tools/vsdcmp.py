#!/usr/bin/env python3
"""vsdcmp - compare two runs of tests/vsdprobe.sh's fg scenario (the
Virtual Sound Device, ARCHITECTURE.md 12.8): Windows 3.1's WIN386 (REF)
and w16's (W16), for the boot-test case vsd.

From VXV.386's COM1 lines (com1.txt): VSD_Get_Version and every VSD_Bell
(registers and flags back) and Install_IO_Handler's carry for ports 42h,
61h and 0E0h (nobody's: 40h and 43h are VTD's) must be identical, and
port 61h's bits 0 and 1 set after each bell.

From the recording (tones.txt: tools/wavtones.py --split), each side's
tones as a sequence of kinds: B the bell (887-898 Hz), and SPK's 1000,
1200 and 1500 Hz. From the last bell before the 1000 Hz tone (VXV's at
Sys_VM_Init: a bell alone) on, both sides must give
  B (0.4-0.8 s), 1000 (2.5-5 s), 1200 (2.5-5 s), B (0.4-0.8 s: the
  bell cuts the box's tone and ends it), 1500 (1-3.5 s: the box stops
  sounding when Alt+Tab stops it), 1500 (the rest, when it is back:
  after 3 s of silence at least)
and nothing else. (The lengths are loose and the rest's is not
compared: a box's clock under w16's VTD runs slower than Windows 3.1's,
which also catches it up when the box runs again.)
Exit status 0 when both agree with this and with each other.

With -b, two runs of the hung scenario instead: the tones 10 s and more
after the first (the boot's bells left out) must be bells of 0.4-0.75 s,
three on each side (x, Space and F1 on Reboot's "not responding" screen).

usage: vsdcmp.py [-b] REFDIR W16DIR
"""
import re
import sys


def com1(d):
    keep = []
    for line in open(d + '/com1.txt', errors='replace'):
        line = line.strip()
        if re.match(r'VXP (VSD_Get_Version|VSD_Bell)', line):
            keep.append(line)
        m = re.match(r'VXP Install_IO_Handler cf 40h 42h 43h 61h 0E0h: (\S+) (\S+) (\S+) (\S+) (\S+)', line)
        if m:       # 40h and 43h are VTD's (Windows 3.1's traps them, w16's not yet)
            keep.append('Install_IO_Handler cf 42h 61h 0E0h: %s %s %s' % (m.group(2), m.group(4), m.group(5)))
        m = re.match(r'VXP 61h after bell: ([0-9A-F]+)', line)
        if m:
            keep.append('61h after bell: bits 0-1 %d' % (int(m.group(1), 16) & 3))
    return keep


def kind(f):
    if 887 <= f <= 898:
        return 'B'
    for k in (1000, 1200, 1500):
        if abs(f - k) < 0.01 * k:
            return str(k)
    return '?%d' % f


def tones(d):
    t = []
    for line in open(d + '/tones.txt'):
        p = line.split()
        if len(p) >= 5 and p[0] == 'tone' and float(p[4]) > 0:
            t.append((float(p[1]), float(p[3]), kind(float(p[4]))))
    # from the last bell before the 1000 Hz tone
    first = next((i for i, x in enumerate(t) if x[2] == '1000'), None)
    if first is None:
        return None, 'no 1000 Hz tone'
    b = [i for i in range(first) if t[i][2] == 'B']
    if not b:
        return None, 'no bell before the 1000 Hz tone'
    return t[b[-1]:], None


SHAPE = [('B', 400, 800), ('1000', 2500, 5000), ('1200', 2500, 5000),
         ('B', 400, 800), ('1500', 1000, 3500), ('1500', 0, 1e9)]


def check(name, d):
    t, err = tones(d)
    if err:
        return ['%s: %s' % (name, err)]
    errs = []
    kinds = [x[2] for x in t]
    if kinds != [s[0] for s in SHAPE]:
        return ['%s: tones %s, expected %s' % (name, ' '.join('%s/%d' % (k, l) for _, l, k in t),
                                               ' '.join(s[0] for s in SHAPE))]
    for (start, length, k), (_, lo, hi) in zip(t, SHAPE):
        if not lo <= length <= hi:
            errs.append('%s: %s tone of %d ms (expected %d-%d)' % (name, k, length, lo, hi))
    gap = t[5][0] - (t[4][0] + t[4][1])
    if gap < 3000:
        errs.append('%s: the box sounded again after %d ms (Alt+Tab: 6 s)' % (name, gap))
    return errs


def bells(name, d):
    t = []
    for line in open(d + '/tones.txt'):
        p = line.split()
        if len(p) >= 5 and p[0] == 'tone' and float(p[4]) > 0:
            t.append((float(p[1]), float(p[3]), kind(float(p[4]))))
    if not t:
        return ['%s: no tones' % name]
    t = [x for x in t if x[0] >= t[0][0] + 10000]
    desc = ' '.join('%s/%d' % (k, l) for _, l, k in t)
    if len(t) != 3 or any(k != 'B' or not 400 <= l <= 750 for _, l, k in t):
        return ['%s: tones on the screen %s, expected 3 bells of 400-750 ms' % (name, desc or 'none')]
    return []


def main():
    if sys.argv[1] == '-b':
        errs = bells('ref', sys.argv[2]) + bells('w16', sys.argv[3])
        for e in errs:
            print(e)
        if not errs:
            print('Reboot\'s screen: 3 bells as Windows 3.1\'s')
        return 1 if errs else 0
    ref, w16 = sys.argv[1], sys.argv[2]
    errs = []
    a, b = com1(ref), com1(w16)
    if not a:
        errs.append('ref: no VXV lines')
    elif a != b:
        errs.append('VXV lines differ:\n  ref: %s\n  w16: %s' % ('\n       '.join(a), '\n       '.join(b)))
    errs += check('ref', ref) + check('w16', w16)
    for e in errs:
        print(e)
    if not errs:
        print('the bells, the ports and the tones as Windows 3.1\'s')
    return 1 if errs else 0


sys.exit(main())
