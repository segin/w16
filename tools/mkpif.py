#!/usr/bin/env python3
"""mkpif - make a PIF for tests from an existing one (e.g. the reference
install's _DEFAULT.PIF), setting the fields of the published PIF format
that Windows 3.1's WINOLDAP.MOD reads (ARCHITECTURE.md 9.6):

  02h title (30 bytes)     20h max/min memory (KB)   24h program (63)
  63h flags                65h start-up directory    A5h parameters (64)

usage: mkpif.py BASE.PIF OUT.PIF [title=T] [program=P] [dir=D] [params=S]
                [maxmem=K] [minmem=K]
"""
import struct
import sys

FIELDS = {'title': (0x02, 30), 'program': (0x24, 63), 'dir': (0x65, 64), 'params': (0xA5, 64)}

d = bytearray(open(sys.argv[1], 'rb').read())
for arg in sys.argv[3:]:
    k, v = arg.split('=', 1)
    if k in FIELDS:
        off, n = FIELDS[k]
        b = v.encode('latin-1')[:n - 1 if k != 'title' else n]
        d[off:off + n] = b + bytes(n - len(b))
    elif k == 'maxmem':
        struct.pack_into('<H', d, 0x20, int(v))
    elif k == 'minmem':
        struct.pack_into('<H', d, 0x22, int(v))
    else:
        sys.exit(f'unknown field {k}')
open(sys.argv[2], 'wb').write(d)
