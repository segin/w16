#!/usr/bin/env python3
"""nechain - turn floating-point OS fixups of TK10 into the less common
record forms, to see how a loader treats them (tests/apps/tk10.c).

wlink writes one additive OSFIXUP record per site. TK10's code segment
holds the marker "CHN!" followed by four 8-byte sites A, B, C, D, each
"9B D9 C0 ...", A-C with an additive FIDRQQ record (type 5), D with
none. This rewrites:
  A: record not additive; A's first word = offset of B (a chain link)
  B: first word = FFFFh (chain end); its record moved to D (additive)
  C: record not additive; C keeps 9B D9, so its "link" is D99Bh
  D: gets B's record (additive, as wlink wrote it)

usage: nechain.py FILE.EXE
"""
import struct
import sys

NERF_ADDITIVE = 4
NERF_OSFIXUP = 3

path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
ne = struct.unpack_from('<I', data, 0x3C)[0]
assert data[ne:ne + 2] == b'NE'
seg_count = struct.unpack_from('<H', data, ne + 0x1C)[0]
seg_off = struct.unpack_from('<H', data, ne + 0x22)[0]
shift = struct.unpack_from('<H', data, ne + 0x32)[0] or 9
done = False
for i in range(seg_count):
    sector, flen, flags, _ = struct.unpack_from('<HHHH', data, ne + seg_off + 8 * i)
    if not sector or not (flags & 0x100):
        continue
    start = sector << shift
    flen = flen or 0x10000
    seg = data[start:start + flen]
    m = seg.find(b'CHN!')
    if m < 0:
        continue
    a, b, c, d = m + 4, m + 12, m + 20, m + 28
    rel = start + flen
    n = struct.unpack_from('<H', data, rel)[0]
    recs = {}
    for k in range(n):
        o = rel + 2 + 8 * k
        src, fl, off, t1, t2 = struct.unpack_from('<BBHHH', data, o)
        if (fl & 3) == NERF_OSFIXUP:
            recs[off] = o
    assert all(x in recs for x in (a, b, c)) and d not in recs, 'unexpected records'
    # A: chain head, not additive, link to B
    data[recs[a] + 1] &= ~NERF_ADDITIVE & 0xFF
    struct.pack_into('<H', data, start + a, b)
    # B: chain end; its own record goes to D
    struct.pack_into('<H', data, start + b, 0xFFFF)
    struct.pack_into('<H', data, recs[b] + 2, d)
    # C: not additive, bytes unchanged
    data[recs[c] + 1] &= ~NERF_ADDITIVE & 0xFF
    done = True
if not done:
    sys.exit('%s: no CHN! sites' % path)
open(path, 'wb').write(data)
