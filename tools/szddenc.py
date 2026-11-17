#!/usr/bin/env python3
"""szddenc - w16's own compressor for the "SZDD" format that Microsoft's
COMPRESS.EXE writes and LZEXPAND.DLL and EXPAND.EXE read (tools/szdd.py
expands it). The format is publicly documented; as a statement of facts:

  header (14 bytes): "SZDD" 88 F0 27 33, the algorithm byte ('A'), the
  last character of the original name for COMPRESS's /r (or 0), the
  expanded size (32 bits, little-endian)
  data: LZSS. A flag byte, then 8 items, low bit first: a set bit is a
  literal byte, a clear one a 2-byte match: position low 8 bits, then
  (position high 4 bits << 4) | (length - 3), length 3-18. Positions
  are absolute in a 4096-byte ring that starts filled with spaces, the
  first byte written at 4096 - 16.

Also the older variant of MS-DOS 5's tools (QBasic's disks): magic
"SZ" 20 88 F0 27 33 D1, the size at 8 (12-byte header), first write at
4096 - 18 (szdd_qb; for probes only).

Greedy matching with hash chains. Matches are at most 16 bytes long by
default, as COMPRESS.EXE writes them (observed: its output of the probe's
files has no longer ones; the ring's first position, 4096 - 16, reflects
that look-ahead); 3.1's LZEXPAND does not always read 17- and 18-byte
matches (ARCHITECTURE.md, LZEXPAND), which maxlen=18 makes for tests. Used by tests
(tests/lzexpand/mkinputs.py) to make compressed inputs at test time.

usage: szddenc.py [-c LASTCHAR] IN OUT
"""
import struct
import sys

MAGIC = b'SZDD\x88\xf0\x27\x33'
QBMAGIC = b'SZ \x88\xf0\x27\x33\xd1'


def lzss(data, start=4096 - 16, literal_only=False, maxdist=4096 - 18, maxlen=16):
    """The LZSS stream for data (no header). The stream is seen as 4096
    spaces followed by the data: data[j] goes to ring position
    (start + j) & 4095, and the prefix's byte i to (start + i - 4096)."""
    s = b' ' * 4096 + data
    n = len(s)
    chains = {}
    out = bytearray()
    items = []                  # (True, byte) or (False, ringpos, length)

    def add(i):
        if i + 2 < n:
            chains.setdefault(s[i:i + 3], []).append(i)

    # the prefix's 3-byte keys are all "   ": one entry close to the
    # data is enough (any of them reaches the same bytes)
    chains[b'   '] = [4096 - 18]
    c = 4096
    while c < n:
        best, bpos = 0, 0
        if not literal_only and c + 2 < n:
            for k in reversed(chains.get(s[c:c + 3], [])[-64:]):
                if c - k > maxdist:
                    break
                ln = 0
                while ln < maxlen and c + ln < n and s[k + ln] == s[c + ln]:
                    ln += 1
                if ln > best:
                    best, bpos = ln, k
                    if ln == maxlen:
                        break
        if best >= 3:
            items.append((False, (start + bpos - 4096) & 4095, best))
            for i in range(c, c + best):
                add(i)
            c += best
        else:
            items.append((True, s[c]))
            add(c)
            c += 1
    for g in range(0, len(items), 8):
        grp = items[g:g + 8]
        flags = 0
        body = bytearray()
        for b, it in enumerate(grp):
            if it[0]:
                flags |= 1 << b
                body.append(it[1])
            else:
                pos, ln = it[1], it[2]
                body.append(pos & 0xFF)
                body.append(((pos >> 4) & 0xF0) | (ln - 3))
        out.append(flags)
        out += body
    return bytes(out)


def szdd(data, lastchar=0, alg=b'A', size=None, literal_only=False, maxlen=16):
    if isinstance(lastchar, str):
        lastchar = ord(lastchar) if lastchar else 0
    if size is None:
        size = len(data)
    return (MAGIC + alg + bytes([lastchar]) + struct.pack('<I', size)
            + lzss(data, literal_only=literal_only, maxlen=maxlen))


def szdd_qb(data):
    return QBMAGIC + struct.pack('<I', len(data)) + lzss(data, start=4096 - 18)


if __name__ == '__main__':
    args = sys.argv[1:]
    lc = 0
    if args and args[0] == '-c':
        lc = args[1]
        args = args[2:]
    open(args[1], 'wb').write(szdd(open(args[0], 'rb').read(), lc))
