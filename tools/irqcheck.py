#!/usr/bin/env python3
"""irqcheck - USER's interrupt-time code touches only fixed data.

The keyboard and mouse drivers call USER's event procedures, and SYSTEM.DRV
its timer tick, at interrupt time. USER's data segment is moveable (its
local heap grows) and in protected mode a moveable segment can be in the
middle of being moved when the interrupt comes: a write through its
selector then lands in the old copy and is lost. So what those paths
touch must be far data in a fixed segment (ARCHITECTURE.md 10.3):

1. every segment of class FAR_DATA in USER.EXE (user/user.lnk) is fixed in
   the NE segment table of the built file (the map gives the segment
   numbers);
2. no instruction of the interrupt-time code - the roots below and every
   function of USER's they call, followed through the objects - reaches
   memory through DS: each memory operand has a segment override (ES, CS,
   SS) or is BP-based (the stack). (The __loadds prologue's load of DS is
   allowed; using it is not.) A line can be exempted with "irqcheck: ok"
   in a comment of the C source line (not done anywhere yet).

usage: irqcheck.py USER.EXE MAPFILE OBJ...   exit status 1 when anything is
flagged

The drivers in assembly (drivers/: SYSTEM.DRV's INT 8 handler and the
timer procedures it calls, KEYBOARD's INT 9, MOUSE's IRQ 12, COMM's
UARTs) load their own data selector at interrupt time and keep
everything they touch there in their own segments, so for them the rule
is that every segment of the file is fixed (neither moveable nor
discardable) in its NE segment table:

usage: irqcheck.py --fixed DRIVER...
"""
import re
import struct
import subprocess
import sys
import os

ROOTS = ['KBD_EVENT', 'MOUSE_EVENT_C', 'TIMER_TICK']


def ne_segments(path):
    d = open(path, 'rb').read()
    ne = struct.unpack_from('<H', d, 0x3C)[0]
    assert d[ne:ne + 2] == b'NE', 'not an NE file'
    nseg = struct.unpack_from('<H', d, ne + 0x1C)[0]
    segtab = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
    return [struct.unpack_from('<HHHH', d, segtab + 8 * i) for i in range(nseg)]


def check_segments(exe, mapfile):
    bad = 0
    segs = ne_segments(exe)
    found = 0
    for line in open(mapfile, encoding='latin-1'):
        m = re.match(r'(\S+)\s+FAR_DATA\s+\S+\s+([0-9a-f]{4}):', line)
        if not m:
            continue
        found += 1
        n = int(m.group(2), 16)
        flags = segs[n - 1][2]       # offset, length, flags, minimum allocation
        if flags & 0x10:
            print('%s: segment %d (%s, FAR_DATA) is moveable (flags %04Xh)' % (exe, n, m.group(1), flags))
            bad += 1
    if not found:
        print('%s: no FAR_DATA segment in the map' % mapfile)
        bad += 1
    return bad


def disassemble(objs):
    """{function: [instruction lines]} over all objects"""
    wdis = os.path.join(os.environ.get('WATCOM', '/opt/watcom'), 'binl64', 'wdis')
    funcs = {}
    for o in objs:
        out = subprocess.run([wdis, o], capture_output=True, text=True, errors='replace').stdout
        cur = None
        for line in out.splitlines():
            m = re.match(r'^[0-9A-F]{4}\t+(\w+):\s*$', line)
            if m:
                cur = m.group(1)
                funcs[cur] = []
                continue
            if line.startswith('Routine Size') or line.startswith('Segment:'):
                cur = None
                continue
            if cur and re.match(r'^[0-9A-F]{4}  ', line):
                funcs[cur].append(line)
    return funcs


SYM = r'(?:_\w+|\w+_)'


def memory_without_override(ins):
    """the instruction's operand text if it reaches memory through DS"""
    parts = ins.split('\t')
    parts = [p for p in parts if p.strip()]
    if len(parts) < 3:
        return None
    mnem, ops = parts[-2].strip(), parts[-1].strip()
    if mnem.startswith(('call', 'j', 'lea', 'push', 'ret', 'int', 'loop')) and mnem != 'push':
        return None
    for op in ops.split(','):
        op = op.strip()
        if re.match(r'^(seg|offset)\b', op) or op.startswith('DGROUP:'):
            continue
        if re.match(r'^(byte|word|dword) ptr ', op):
            op = op.split(' ptr ', 1)[1]
        if re.match(r'^(es|cs|ss):', op):
            continue
        if '[' in op:
            if 'bp' in op:
                continue                # the stack (SS)
            return ops
        if re.match(r'^' + SYM + r'(\+0x[0-9a-f]+)?$', op):
            return ops                  # a symbol as memory, DS-relative
    return None


def check_code(objs):
    funcs = disassemble(objs)
    bad = 0
    seen = set()
    todo = list(ROOTS)
    for r in ROOTS:
        if r not in funcs:
            print('irqcheck: %s not found in the objects' % r)
            bad += 1
    while todo:
        f = todo.pop()
        if f in seen or f not in funcs:
            continue
        seen.add(f)
        for ins in funcs[f]:
            m = re.search(r'\tcall\t+(\w+)', ins)
            if m:
                todo.append(m.group(1))
                continue
            ops = memory_without_override(ins)
            if ops:
                print('irqcheck: %s (interrupt time) reaches memory through DS: %s' % (f, ins.strip()))
                bad += 1
    return bad, sorted(seen)


def check_fixed(files):
    """every segment of each file fixed and not discardable"""
    bad = 0
    for f in files:
        for i, (_, _, flags, _) in enumerate(ne_segments(f)):
            if flags & 0x1010:
                print('irqcheck: %s: segment %d is %s (flags %04Xh)' %
                      (f, i + 1, 'moveable' if flags & 0x10 else 'discardable', flags))
                bad += 1
    if not bad:
        print('irqcheck: every segment fixed in %s' % ', '.join(os.path.basename(f) for f in files))
    return 1 if bad else 0


def main(args):
    if args and args[0] == '--fixed':
        return check_fixed(args[1:])
    exe, mapfile, objs = args[0], args[1], args[2:]
    bad = check_segments(exe, mapfile)
    b, seen = check_code(objs)
    bad += b
    if not bad:
        print('irqcheck: FAR_DATA fixed; interrupt-time code (%s) uses no DS data' % ', '.join(seen))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
