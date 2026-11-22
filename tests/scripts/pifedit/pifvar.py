#!/usr/bin/env python3
"""pifvar - make a variant of a PIF file for the PIF Editor's observation
scenarios (tests/pifobs.sh): copy IN to OUT with some bytes changed and the
checksum (byte 1: the sum of bytes 2-170h) made right again. The input is
normally a file of the reference install, read at test time; the result is
written under build/ and never committed.
  usage: pifvar.py IN OUT [OFFSET=HEXBYTES ...] [-nock]
  e.g.   pifvar.py DOSPRMPT.PIF build/x.pif 1B9=8002 E5=00
"""
import sys

args = [a for a in sys.argv[1:] if not a.startswith('-')]
src, dst, edits = args[0], args[1], args[2:]
b = bytearray(open(src, 'rb').read())
for e in edits:
    off, val = e.split('=')
    off = int(off, 16)
    data = bytes.fromhex(val)
    if off + len(data) > len(b):
        b.extend(b'\0' * (off + len(data) - len(b)))
    b[off:off + len(data)] = data
if '-nock' not in sys.argv:
    b[1] = sum(b[2:0x171]) & 0xFF
open(dst, 'wb').write(b)
