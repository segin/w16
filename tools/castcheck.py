#!/usr/bin/env python3
"""castcheck - local-heap handles turned into far pointers through segment 0.

Without STRICT, windows.h makes HLOCAL (and every HANDLE) a UINT. In the
large model an ordinary pointer is far, and an integer cast to a far
pointer gets segment 0: (T *)LocalAlloc(...) or (T *)hlocal then reaches
the block through selector 0 - writes that land wherever that offset is
in low memory on an emulator that does not check (DOSBox), a fault on one
that does. USER had four of these (savebits, clipboard, edit tab stops,
MDI's window list); the symptom moved with the layout of USER's heap.

Flagged: a cast to a pointer type that is not __near/NEAR (an explicit
"*", or a far pointer typedef such as LPSTR/LPVOID) applied directly to
LocalAlloc, LocalReAlloc, LocalHandle, or to an expression cast to
HLOCAL/HANDLE first. (LocalLock returns a near pointer: widening that is
right.) A line can be exempted with a comment containing "castcheck: ok".

usage: castcheck.py FILE...    exit status 1 when anything is flagged
"""
import re
import sys

FAR_TYPEDEFS = r'(?:LP[A-Z]+|FARPROC|PWORD)'
CAST = r'\(\s*((?:const\s+)?[A-Za-z_][\w\s]*?(?:\*\s*)+|' + FAR_TYPEDEFS + r')\s*\)'
SRC = r'\s*\(?\s*(?:Local(?:Re)?Alloc|LocalHandle|\(\s*(?:HLOCAL|HANDLE|UINT|WORD)\s*\))'
PAT = re.compile(CAST + SRC)

def check(path):
    bad = 0
    for n, line in enumerate(open(path, encoding='latin-1'), 1):
        if 'castcheck: ok' in line:
            continue
        for m in PAT.finditer(line):
            t = m.group(1)
            if re.search(r'\b(__near|NEAR|_near|near)\b', t):
                continue
            print('%s:%d: %s cast to a far pointer: %s' % (path, n, m.group(0).strip(), line.strip()))
            bad += 1
    return bad

def main(files):
    bad = sum(check(f) for f in files)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
