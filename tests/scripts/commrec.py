#!/usr/bin/env python3
"""commrec - COMMREC's log (tests/apps/commrec.c) made comparable between
runs (tests/commusr.sh).

Each line of the log is a call USER made to the communications driver,
"ord(args)" when it was made and " =result" when it came back, all in
hexadecimal, pointers as seg:off. What depends on layout is reduced:
a pointer into the probe's data segment (its "OBS ~ds=" line) becomes
app:off, or app:stack on the probe's stack (its "OBS ~stk=" line: a
local of its main function); another pointer argument (USER's own
memory) usr; a pointer the
driver returns drv:off (its offset in the driver's data); the probe's
window handle (its "OBS ~hwnd=" line) HWND. Without those lines in the
probe's log every pointer is ptr.

usage: commrec.py PROBELOG COMMRECLOG
"""
import re
import sys


def main():
    probe, rec = sys.argv[1:3]
    ds = hwnd = stk = None
    for line in open(probe, encoding='latin-1'):
        m = re.match(r'OBS ~ds=([0-9A-F]{4})', line)
        if m:
            ds = m.group(1)
        m = re.match(r'OBS ~hwnd=([0-9A-F]{4})', line)
        if m:
            hwnd = m.group(1)
        m = re.match(r'OBS ~stk=([0-9A-F]{4})', line)
        if m:
            stk = int(m.group(1), 16)

    def arg_ptr(m):
        seg, off = m.group(1), m.group(2)
        if seg == '0000' and off == '0000':
            return '0000:0000'
        if ds is None:
            return 'ptr'
        if seg != ds:
            return 'usr'
        # the probe's stack (USER's locals: how deep they are is USER's)
        if stk is not None and int(off, 16) > stk - 0x800:
            return 'app:stack'
        return 'app:' + off

    for line in open(rec, encoding='latin-1'):
        line = line.rstrip('\r\n')
        if line.startswith(' ='):
            v = line[2:]
            m = re.match(r'^([0-9A-F]{4}):([0-9A-F]{4})$', v)
            if m and v != '0000:0000':
                v = 'drv:' + m.group(2)
            print(' =' + v)
            continue
        m = re.match(r'^([0-9A-F]{4})\((.*)\)$', line)
        if not m:
            print(line)
            continue
        ordn, args = m.group(1), m.group(2)
        args = re.sub(r'([0-9A-F]{4}):([0-9A-F]{4})', arg_ptr, args)
        if ordn == '0064' and hwnd:
            parts = args.split(',')
            if len(parts) > 1 and parts[1] == hwnd:
                parts[1] = 'HWND'
            args = ','.join(parts)
        print('%s(%s)' % (ordn, args))


if __name__ == '__main__':
    main()
