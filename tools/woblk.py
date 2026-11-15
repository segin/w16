#!/usr/bin/env python3
"""woblk - the WIN.COM switcher-block snapshots in a WO2.LOG (tests/apps/wo2.c):
print each snapshot's non-zero ranges, and the differences between
consecutive snapshots (as the switch starts / when it returned).

usage: woblk.py WO2.LOG [-a] [-m OFF:LEN]...
  -a          full hex of every snapshot
  -m OFF:LEN  zero those bytes (hex offset) of every block snapshot first
              (and leave out the dumps of WINOLDAP's buffer)
"""
import re
import sys


def snapshots(path):
    out, cur, title = [], None, None
    for line in open(path, errors='replace'):
        line = line.rstrip('\r\n')
        if line.startswith('== block') or 'bytes at its 185h' in line:
            if cur is not None:
                out.append((title, bytes(cur)))
            title, cur = line, bytearray()
        elif cur is not None and re.match(r' [0-9A-F]{4}:', line):
            cur += bytes.fromhex(line[6:].replace(' ', ''))
        elif cur is not None:
            out.append((title, bytes(cur)))
            cur = None
    if cur is not None:
        out.append((title, bytes(cur)))
    return out


def text(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)


def ranges(diff):
    rs, start = [], None
    for i, d in enumerate(diff + [False]):
        if d and start is None:
            start = i
        elif not d and start is not None:
            rs.append((start, i))
            start = None
    # merge close ranges
    m = []
    for a, b in rs:
        if m and a - m[-1][1] < 4:
            m[-1] = (m[-1][0], b)
        else:
            m.append((a, b))
    return m


def main():
    snaps = snapshots(sys.argv[1])
    masks = [a.split(':') for a in sys.argv[2:] if ':' in a]
    if masks:
        out = []
        for t, b in snaps:
            if '185h' not in t:
                b = bytearray(b)
                for off, ln in masks:
                    o = int(off, 16)
                    b[o:o + int(ln)] = bytes(int(ln))
                b = bytes(b)
            out.append((t, b))
        snaps = out
    prev, prevlin = None, None
    for title, b in snaps:
        print(title)
        if '185h' in title and masks:
            continue
        if '185h' in title:     # WINOLDAP's buffer: non-zero ranges, 32 a line
            for a, e in ranges([c != 0 for c in b]):
                for k in range(a, e, 32):
                    print(f'  {k:04X}: {b[k:min(e, k + 32)].hex(" ")}  |{text(b[k:min(e, k + 32)])}|')
            continue
        if prev is None or '-a' in sys.argv:
            for a, e in ranges([c != 0 for c in b]):
                print(f'  {a:04X}-{e - 1:04X}: {b[a:e].hex(" ")}  |{text(b[a:e])}|')
        else:
            for a, e in ranges([x != y for x, y in zip(prev, b)]):
                print(f'  {a:04X}-{e - 1:04X}: {prev[a:e].hex(" ")}\n           -> {b[a:e].hex(" ")}  |{text(b[a:e])}|')
        prev = b


if __name__ == '__main__':
    main()
