#!/usr/bin/env python3
"""w32mcmp.py - W32M.EXE's log (tests/win32s/w32m.c) made comparable
between runs: Win32s picks its addresses itself (under Windows 3.1 with
Microsoft's W32S.386 the first reservation came at 88300000h in one run
and 88700000h in another), so every address in Win32s's arena (80000000h
up to C0000000h) is written relative to the first reservation's ("R+20000"),
and a free region's size (VirtualQuery, MEM_FREE) as the address it ends
at, which is the next reservation's (Win32s's system DLLs at B0000000h).

  usage: w32mcmp.py LOG    the normalised log on standard output
"""
import sys

MEM_FREE = 0x10000
# lines whose values are memory contents, not addresses (A5A5A5A5h ...)
DATA = {'fresh', 'readback', 'afterwrite', 'kept', 'zero', 'bigsum', 'heapalloc',
        'heap2rw', 'global', 'shared'}


def norm(path):
    lines = open(path, 'rb').read().decode('latin-1').replace('\r', '').split('\n')
    base = None
    for ln in lines:
        t = ln.split()
        if len(t) >= 2 and t[0] == 'reserve':
            base = int(t[1], 16)
            break
    out = []
    for ln in lines:
        t = ln.split()
        if not t:
            continue
        name, vals = t[0], t[1:]
        if name == 'W32M' and vals and vals[0] in ('start', 'end'):
            out.append(' '.join(t))
            continue
        try:
            nums = [int(v, 16) for v in vals]
        except ValueError:
            out.append(' '.join(t))
            continue
        res = []
        for i, v in enumerate(nums):
            # VirtualQuery's 7 dwords after its result: base, allocation
            # base, its protection, size, state, protection, type
            if name.startswith('q.') and len(nums) == 8 and i == 4 and nums[5] == MEM_FREE:
                res.append('to %08X' % ((nums[1] + v) & 0xFFFFFFFF))
            elif base is not None and 0x80000000 <= v < 0xC0000000 and name not in DATA:
                d = v - base
                res.append('R%s%X' % ('+' if d >= 0 else '-', abs(d)))
            else:
                res.append('%08X' % v)
        out.append(' '.join([name] + res))
    return out


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    for ln in norm(sys.argv[1]):
        print(ln)
