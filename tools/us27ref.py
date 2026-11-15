#!/usr/bin/env python3
"""us27ref - the logs of probe US27 (tests/apps/us27.c) narrowed to what is
checked, for tools/obsref.py.

usage: tools/us27ref.py LOG... (writes LOG.chk for each)
       then tools/obsref.py LOG.chk... > tests/apps/us27ref.h

US27 passes each pointer-taking USER function NULL (N), a freed (F) and a
not-present (P) selector, an offset past a segment's limit (L), the edge
of a segment (E) and a code selector to write to (R). F and P fault when
the selector is loaded, on any processor and in any emulator. The others
fault on the access, and the emulators the tests run in check that only
partly: DOSBox-X's normal core checks a segment's limit for the string
instructions, not for an ordinary MOV, and neither a null selector nor a
code segment written to through a data access; DOSBox checks none. So a
Windows 3.1 run that took no fault for an N, L, E or R case may have
read or written past the end (what it shows then is the emulator's, not
3.1's), and only the N, L, E and R cases in which every log took a fault
(a FLT or KILLED line) are kept; the rest of their lines are left out, so
obsref.py does not check them. The F and P cases are kept whole, as are
the lines that are no case's (the set-up), but for the return values in
UNDEFINED: what 3.1 returns there is no value it sets (the same in every
run, but unlike anything the function returns otherwise: what a register
held), so it is not checked.
"""
import re
import sys

# IsRectEmpty 0971h, PtInRect 38D6h for a freed or not-present RECT
UNDEFINED = {'OBS IsRectEmpty.F=', 'OBS IsRectEmpty.P=', 'OBS PtInRect.F=', 'OBS PtInRect.P='}

CASE = re.compile(r'^(?:OBS |FLT |KILLED |RUN |SKIP )([A-Za-z0-9]+)\.([NFPLER])\b')


def faulted(path):
    out = set()
    for line in open(path, encoding='latin-1'):
        if line.startswith(('FLT ', 'KILLED ')):
            m = CASE.match(line)
            if m:
                out.add(m.group(1, 2))
    return out


def main():
    logs = sys.argv[1:]
    keep = None
    for p in logs:
        f = faulted(p)
        keep = f if keep is None else keep & f
    for p in logs:
        with open(p + '.chk', 'w', encoding='latin-1') as o:
            for line in open(p, encoding='latin-1'):
                m = CASE.match(line)
                if m and m.group(2) in 'NLER' and m.group(1, 2) not in keep:
                    continue
                if any(line.startswith(u) for u in UNDEFINED):
                    continue
                o.write(line)


if __name__ == '__main__':
    main()
