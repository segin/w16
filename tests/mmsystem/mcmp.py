#!/usr/bin/env python3
"""mcmp - compare two logs of the MMSYSTEM probe MMP.EXE
(tests/mmsystem/mmp.c): Microsoft's MMSYSTEM.DLL (the reference) against
w16's, on the same system (ARCHITECTURE.md 23).

Every line must be identical after the masks below. MASKS hide what
differs between two runs of Microsoft's own MMSYSTEM (each one seen in
such a pair of runs); KNOWN lists what differs between Microsoft's and
w16's for a reason given there: those lines are shown, not failed.

usage: mcmp.py MS.log W16.log
Exit status 0 when they agree.
"""
import re
import sys

MASKS = [
    # StackEnter's flags after a switch: AF differs between two of 3.1's
    # runs (3202h, 3212h)
    (re.compile(r'^(  StackEnter: .* flags )([0-9A-F]{4})'),
     lambda m: m.group(1) + '%04X' % (int(m.group(2), 16) & ~0x10)),
    # a timer callback's SI (left over from the timer driver's code)
    (re.compile(r'^(  cb \d+: ax .* si )[0-9A-F]{4}( di [0-9A-F]{4} ds TIMER)'),
     lambda m: m.group(1) + '(si)' + m.group(2)),
    # the word above MMTASK's stack (not the task's: whatever was there)
    (re.compile(r'^(  task: ret to .* beyond: \S+ \S+ \S+ )[0-9A-F]{4}'),
     lambda m: m.group(1) + '(any)'),
]

KNOWN = [
    # The task function's BX, CX and DI: the instance handle of MMTASK's
    # data (DI) and what is left from the start-up calls (BX, CX); handle
    # values follow the global heap's state, which w16's MMSYSTEM's own
    # allocations change.
    (re.compile(r'^(  task: dwInst .* regs ax \S+ )bx \S+ cx \S+( dx \S+ si \S+ )di \S+'),
     r'\1bx (h) cx (r)\2di (h)'),
    # the function's code selector in MMTASK's data: MMC.DLL's, loaded
    # where the heap puts it
    (re.compile(r'^(  task: data from 10h: \S+ )[0-9A-F]{4}'), r'\1(sel)'),
]


def norm(lines, rules):
    out = []
    for ln in lines:
        for rx, rep in rules:
            ln = rx.sub(rep, ln)
        out.append(ln)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    a = norm(open(sys.argv[1], errors='replace').read().splitlines(), MASKS)
    b = norm(open(sys.argv[2], errors='replace').read().splitlines(), MASKS)
    ka = norm(a, KNOWN)
    kb = norm(b, KNOWN)
    if len(a) != len(b):
        print('FAIL: %d lines against %d' % (len(a), len(b)))
    bad = known = 0
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else '(none)'
        y = b[i] if i < len(b) else '(none)'
        if x == y:
            continue
        if i < len(ka) and i < len(kb) and ka[i] == kb[i]:
            known += 1
            print('known %d:\n  - %s\n  + %s' % (i + 1, x, y))
            continue
        bad += 1
        print('FAIL %d:\n  - %s\n  + %s' % (i + 1, x, y))
    print('%s: %d lines, %d differ, %d known differences'
          % ('FAIL' if bad or len(a) != len(b) else 'ok', len(a), bad, known))
    sys.exit(1 if bad or len(a) != len(b) else 0)


main()
