#!/usr/bin/env python3
"""md2adrop - condense MD2 mode 8's log (tests/apps/md2ax.c, drop()).

Per phase: the PS/2 BIOS calls [AX BX], each packet the BIOS gave the
C207h handler pkt(status x y) (MS2 kind 5), each event the driver reported
EV(status x y), and which packets with motion or a button change never
became an event.   usage: tests/md2adrop.py md2.log
"""
import re
import sys


def main():
    pk, evs = [], []

    def flush():
        if not pk and not evs:
            return
        ex = [(int(x, 16) if x else 0) for _, x, _ in pk]
        # x as a signed byte-ish value (DOSBox gives the 9-bit value's low
        # byte in the word); events give signed decimal
        got = [int(e[1]) for e in evs if int(e[0], 16) & 1]
        print('  packets x:', [x if x < 0x80 else x - 0x100 for x in ex])
        print('  events  x:', got)

    for line in open(sys.argv[1], errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('PHASE'):
            flush()
            pk, evs = [], []
            print(line)
        elif ' MS2 1 ' in line:
            f = line.split()
            print('  BIOS', f[2], f[3])
        elif ' MS2 5 ' in line:
            f = line.split()
            pk.append((f[2][3:], f[3][3:], f[4]))
        elif line.startswith('E '):
            f = line.split()
            evs.append((f[1], f[2], f[3]))
        elif line.startswith('P ') or line.startswith('== '):
            print(' ', line)
    flush()


main()
