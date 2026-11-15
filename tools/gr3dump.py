#!/usr/bin/env python3
"""gr3dump - decode C:\\GR3.LOG, the 386 grabber recorder's log
(tests/apps/gr3.asm; tests/grabber3.sh).

usage: gr3dump.py GR3.LOG [--struct] [--no-vdd-bytes]
Prints one line per record:
    seq tick  > NAME(args) ...           a call (stack words, struct pointer)
    seq tick  < NAME = DX:AX fl=.. pop=n a return
    seq tick    S+/S- off: bytes          EXTPAINTSTRUC changes (- before the
                                          call: by WINOLDAP; + after: by the
                                          grabber)
    seq tick    VDD> regs / VDD< regs    the VDD API entry, in and out
--struct prints the full structure dumps (otherwise only their size).
"""
import struct
import sys

NAMES = [None, 'PaintScreen', 'ScreenFree', 'BeginSelection', 'KeySelection',
         'MakeSelctRect', 'InvertSelection', 'RenderSelection', 'ConsSelecRec',
         'EndSelection', 'GetFontList', 'SetPaintFnt', 'UpdateScreen', 'CursorOn',
         'CursorPosit', 'CursorOff', 'CheckGRBVersion', 'AdjustInitEndPt',
         'GetDisplayUpd', 'GrabEvent', 'GrabComplete', 'GrbGetTextColor',
         'GrbUnLockApp', 'TranslateWinToFScreen', 'WEP']
ARGB = [0, 4, 4, 8, 8, 8, 4, 10, 4, 4, 4, 12, 10, 4, 4, 4, 4, 8, 10, 10, 10, 8, 4, 4, 2]


def hexs(b, n=None):
    if n is not None and len(b) > n:
        return b[:n].hex(' ') + ' ...'
    return b.hex(' ')


def dump16(b, base=0, indent='        '):
    out = []
    for i in range(0, len(b), 16):
        out.append('%s%04X: %s' % (indent, base + i, b[i:i + 16].hex(' ')))
    return '\n'.join(out)


def records(d):
    p = 0
    while p + 8 <= len(d):
        typ, ordn, seq, ln, tick = struct.unpack_from('<BBHHH', d, p)
        if ln < 8 or p + ln > len(d):
            print('?? bad record at %X' % p)
            return
        yield typ, ordn, seq, tick, d[p + 8:p + ln]
        p += ln


def main():
    a = sys.argv[1:]
    full = '--struct' in a
    novb = '--no-vdd-bytes' in a
    path = [x for x in a if not x.startswith('--')][0]
    d = open(path, 'rb').read()
    for typ, ordn, seq, tick, b in records(d):
        h = '%5d %04X ' % (seq, tick)
        if typ == 1:
            r = struct.unpack_from('<10H', b, 0)
            w = struct.unpack_from('<8H', b, 20)
            ret = struct.unpack_from('<2H', b, 36)
            n = ARGB[ordn] if ordn < len(ARGB) else None
            if n is not None:
                args = list(w[:n // 2])
            else:
                args = list(w)
            print(h + '> %s(%s)  from %04X:%04X ax=%04X bx=%04X cx=%04X dx=%04X si=%04X di=%04X ds=%04X es=%04X'
                  % (NAMES[ordn] if ordn < len(NAMES) else ordn,
                     ' '.join('%04X' % x for x in reversed(args)) + ('' if n is not None else ' ?'),
                     ret[1], ret[0], r[0], r[1], r[2], r[3], r[4], r[5], r[7], r[8]))
        elif typ == 2:
            ax, dx, fl, n = struct.unpack_from('<4H', b, 0)
            print(h + '< %s = %04X:%04X fl=%04X pop=%d' % (NAMES[ordn] if ordn < len(NAMES) else ordn, dx, ax, fl, n))
        elif typ == 3:
            when, off, sel, size = struct.unpack_from('<4H', b, 0)
            print(h + '  S%s full %04X:%04X size %X' % ('-+'[when], sel, off, size))
            if full:
                print(dump16(b[8:8 + size]))
        elif typ == 4:
            when, off, sel = struct.unpack_from('<3H', b, 0)
            q = 6
            parts = []
            while True:
                o, = struct.unpack_from('<H', b, q)
                q += 2
                if o == 0xFFFF:
                    break
                n, = struct.unpack_from('<H', b, q)
                q += 2
                parts.append('%03X: %s' % (o, b[q:q + n].hex(' ')))
                q += n
            print(h + '  S%s ' % '-+'[when] + ' | '.join(parts))
        elif typ == 5:
            off, sel, n = struct.unpack_from('<3H', b, 0)
            print(h + '  M %04X:%04X [%d] %s' % (sel, off, n, hexs(b[6:6 + n])))
        elif typ in (6, 7):
            r = struct.unpack_from('<7I5H', b, 0)
            q = 38
            if typ == 6:
                rip, rcs = struct.unpack_from('<2H', b, q)
                q += 4
            n, = struct.unpack_from('<H', b, q)
            q += 2
            regs = 'eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X ds=%04X es=%04X fs=%04X gs=%04X fl=%04X' % r
            if typ == 6:
                print(h + '  VDD> ' + regs + ' from %04X:%04X' % (rcs, rip))
            else:
                print(h + '  VDD< ' + regs)
            if n and not novb:
                print('        [%d bytes at es:di]' % n)
                print(dump16(b[q:q + n], indent='          '))
        elif typ == 8:
            off, sel = struct.unpack_from('<2H', b, 0)
            print(h + '  INT2F 1684h/000Ah -> %04X:%04X' % (sel, off))
        elif typ == 9:
            print(h + '  LOST %d records' % struct.unpack_from('<H', b, 0))
        else:
            print(h + '  type %d ord %d: %s' % (typ, ordn, hexs(b, 32)))


if __name__ == '__main__':
    main()
