#!/usr/bin/env python3
"""grpref - check tools/mkgrp.py against the group files Windows 3.1 Setup
wrote in the reference install (.tools/ref/win31/cdrive/WINDOWS): for
each of them a definition is made from what the file says (name, show
state, rectangles, each item's title, command, icon file and index, and
position), mkgrp.py builds the group again with the icons taken from the
reference's own programs, and the result must equal Setup's file byte
for byte (header, checksum, layout, the icons' cached bits).

A format check only: nothing of Microsoft's is written into w16 (the
definitions and the rebuilt files live in a temporary directory and are
deleted). Skipped (exit 0) when the reference install is not there.

usage: tests/grpref.py [WINDOWS_DIR]
"""
import os
import struct
import subprocess
import sys
import tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cstr(d, p):
    return d[p:d.index(b'\0', p)].decode('latin-1')


def definition(d):
    show, l, t, r, b, mx, my, pname = struct.unpack_from('<H4h2hH', d, 8)
    n = struct.unpack_from('<H', d, 0x20)[0]
    offs = struct.unpack_from('<%dH' % n, d, 0x22)
    if any(o == 0 for o in offs) and any(offs):
        return None                     # gaps: not a layout mkgrp makes
    if sorted(o for o in offs if o) != [o for o in offs if o]:
        return None                     # z-order differs from data order
    if struct.unpack_from('<H', d, 6)[0] != len(d):
        return None                     # tags: mkgrp writes none
    out = ['name ' + cstr(d, pname), 'show %d' % show, 'rect %d %d %d %d' % (l, t, r, b),
           'min %d %d' % (mx, my)]
    for o in offs:
        if not o:
            continue
        it = struct.unpack_from('<2h10H', d, o)
        out.append('item %s | %s | %s %d @ %d %d' % (cstr(d, it[9]), cstr(d, it[10]),
                                                    cstr(d, it[11]), it[2], it[0], it[1]))
    return '\n'.join(out) + '\n'


def main():
    win = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, '.tools/ref/win31/cdrive/WINDOWS')
    if not os.path.isdir(win):
        print('grpref: no reference install (%s): skipped' % win)
        return 0
    grps = sorted(f for f in os.listdir(win) if f.upper().endswith('.GRP'))
    fails = done = 0
    with tempfile.TemporaryDirectory() as tmp:
        for g in grps:
            d = open(os.path.join(win, g), 'rb').read()
            text = definition(d)
            if text is None:
                print('grpref: %s: skipped (a layout mkgrp does not make)' % g)
                continue
            src, out = os.path.join(tmp, 'g.txt'), os.path.join(tmp, 'g.grp')
            open(src, 'w', encoding='latin-1').write(text)
            r = subprocess.run([sys.executable, os.path.join(root, 'tools/mkgrp.py'), '-d', win,
                                '-d', os.path.join(win, 'SYSTEM'), src, out],
                               capture_output=True, text=True)
            done += 1
            if r.returncode or open(out, 'rb').read() != d:
                fails += 1
                print('grpref: %s: FAIL %s' % (g, r.stderr.strip()))
    print('grpref: %d of %d group files of the reference rebuilt byte for byte' % (done - fails, done))
    return 1 if fails or not done else 0


if __name__ == '__main__':
    sys.exit(main())
