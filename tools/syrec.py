#!/usr/bin/env python3
"""syrec - decode C:\\SYREC.BIN, the records of SYREC.DRV (tests/apps/syrec.asm),
the recording system driver in front of the driver under test
(drivers/system/README.md).

usage: syrec.py SYREC.BIN [TK.LOG]
TK.LOG: SY1's log, whose "CSMAP sel module" lines name the callers' code
segments. Prints the counts of calls per ordinal, then every record kept:
calls with their arguments (the 8 stack words above the return address,
last pushed first), results and the bytes taken off; timer procedure
calls with their registers and context.
"""
import struct
import sys

NAMES = {1: 'InquireSystem', 2: 'CreateSystemTimer', 3: 'KillSystemTimer',
         4: 'EnableSystemTimers', 5: 'DisableSystemTimers', 6: 'GetSystemMsecCount',
         7: 'Get80x87SaveSize', 8: 'Save80x87State', 9: 'Restore80x87State',
         10: 'WEP', 20: 'A20_Proc'}
ARGS = {1: 2, 2: 3, 3: 1, 8: 2, 9: 2, 10: 1, 20: 1}   # words (measured, SY1)


def main():
    pad = open(sys.argv[1], 'rb').read()
    # the file starts with SYREC's 4096-byte pad (anything written there is
    # not SYREC's: listed), then the header
    stray = [(i, w) for i, w in enumerate(struct.unpack_from('<2048H', pad, 0)) if w]
    d = pad[4096:]
    csmap = {}
    if len(sys.argv) > 2:
        for line in open(sys.argv[2], encoding='latin-1'):
            if line.startswith('CSMAP '):
                p = line.split(None, 2)
                csmap[int(p[1], 16)] = p[2].strip().split('\\')[-1] if len(p) > 2 else '?'
    count, mx, rs, total, depth, maxdep, nslot, nkind = struct.unpack_from('<8H', d, 0)
    counts = struct.unpack_from('<%dH' % nkind, d, 16)
    print('# %d records kept of %d; deepest timer procedure nesting %d' % (count, total, maxdep))
    if stray:
        print('# written into the pad: ' + ' '.join('+%02X=%04X' % (2 * i, w) for i, w in stray))
    for k in range(nkind):
        if counts[k]:
            name = NAMES.get(k, 'ord %d' % k) if k < 32 else 'timer slot %d' % (k - 32)
            print('# calls: %-22s %d' % (name, counts[k]))
    slots = [struct.unpack_from('<4H', d, 272 + 8 * i) for i in range(nslot)]
    outx = 400
    recs = outx + mx * 6
    for i in range(count):
        r = struct.unpack_from('<32H', d, recs + i * rs)
        kind, seq = r[0], r[1]
        ax, bx, cx, dx, si, di, bp, fl, ds, es, ss, sp, rip, rcs = r[2:16]
        who = csmap.get(rcs, '%04X' % rcs)
        if kind >= 0x100:
            s = kind - 0x100
            tick, tickh, isrimr, dep, rax, rfl = r[16:22]
            print('%5d timer slot %d (handle %04X): AX=%04X IF=%d DS=%04X ES=%04X SS:SP=%04X:%04X '
                  'tick=%04X isr=%02X imr=%02X depth=%d from %s:%04X' %
                  (seq, s, slots[s][1], ax, (fl >> 9) & 1, ds, es, ss, sp, tick, isrimr & 0xFF,
                   isrimr >> 8, dep, who, rip))
            continue
        w = r[16:24]
        n = ARGS.get(kind, 0)
        args = ', '.join('%04X' % w[n - 1 - j] for j in range(n))
        oax, obx, ocx, odx, osi, odi, obp, popped = r[24:32]
        ofl, ods, oes = struct.unpack_from('<3H', d, outx + i * 6)
        kept = []
        for nm, a, b in (('BX', bx, obx), ('CX', cx, ocx), ('SI', si, osi), ('DI', di, odi),
                         ('BP', bp, obp), ('DS', ds, ods), ('ES', es, oes)):
            if a != b:
                kept.append(nm)
        print('%5d %s.%s(%s) = %04X:%04X popped=%d IF in %d out %d changed %s  <%s:%04X SS=%04X>' %
              (seq, 'SYSTEM', NAMES.get(kind, 'ord %d' % kind), args, odx, oax, popped,
               (fl >> 9) & 1, (ofl >> 9) & 1, ' '.join(kept) or '-', who, rip, ss))


main()
