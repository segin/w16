#!/usr/bin/env python3
"""Print the trace DOSXT.EXE (w16's DOS extender built with -dTRACE)
writes to C:\\DOSX.TRC: gate calls, results, exceptions and interrupts
through vectors 00h-1Fh, in order (32-byte records).

usage: tools/dosxtrc.py DOSX.TRC [-n LAST]
"""
import struct
import sys

OPS = {0: 'reflect', 1: 'int31', 2: 'int21', 3: 'excdef'}


def main():
    args = sys.argv[1:]
    last = None
    if '-n' in args:
        i = args.index('-n')
        last = int(args[i + 1])
        del args[i:i + 2]
    data = open(args[0], 'rb').read()
    recs = [struct.unpack_from('<14HI', data, off)
            for off in range(0, len(data) - 31, 32)]
    total = len(recs)
    if last:
        recs = recs[-last:]
    print(f'{total} records written')
    for w in recs:
        kind = w[0]
        regs = 'ax=%04X bx=%04X cx=%04X dx=%04X si=%04X di=%04X bp=%04X ds=%04X es=%04X' % w[1:10]
        x = w[10:14]
        if kind & 0xFF00 in (0, 0x8000):
            op = kind & 0xFF
            name = OPS.get(op, f'op{op}')
            if op in (0, 3):
                name += '(stub %04X)' % x[3]
            tag = '->' if kind & 0x8000 else '  '
            print(f'{w[14]:7d} {tag} {name:20s} {regs}  at {x[1]:04X}:{x[0]:04X} fl={x[2]:04X}')
        elif kind & 0xFF00 == 0x100:
            err = 'soft' if x[2] == 0xFFFF else f'err={x[2]:04X}'
            print(f'{w[14]:7d} !! vector {kind & 0xFF:02X} {err:9s} {regs}  at {x[1]:04X}:{x[0]:04X} fl={x[3]:04X}')
        else:
            print(f'{w[14]:7d} ?? kind {kind:04X} {regs} {x}')


if __name__ == '__main__':
    main()
