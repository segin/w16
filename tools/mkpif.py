#!/usr/bin/env python3
"""mkpif - make a PIF for tests from an existing one (e.g. the reference
install's _DEFAULT.PIF), setting the fields of the published PIF format
that Windows 3.1's WINOLDAP.MOD reads (ARCHITECTURE.md 9.8):

  02h title (30 bytes)     20h max/min memory (KB)   24h program (63)
  63h flags                65h start-up directory    A5h parameters (64)

usage: mkpif.py BASE.PIF OUT.PIF [title=T] [program=P] [dir=D] [params=S]
                [maxmem=K] [minmem=K] [windowed=0|1] [background=0|1]

windowed: the 386 Enhanced section's ("WINDOWS 386 3.0", found through the
published extension headers from 171h: a 16-byte name, the next header's
offset, the data's offset and size) flags dword at +10h, bit 3 full
screen: windowed=1 clears it (the program starts in a window), 0 sets it.
Confirmed by observation (tests/scripts/input/dosbox386win.txt: Windows
3.1's MS-DOS Prompt from such a PIF opens in a window).
background: the same dword's bit 1 (02h), Execution: Background (the bit
WINOA386 hands SHELL as its 02h: tests/pifread.sh, ARCHITECTURE.md 12.11).
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
    elif k in ('windowed', 'background'):
        bit, on = (8, v != '1') if k == 'windowed' else (2, v == '1')
        off = 0x171
        while 0 < off < len(d) - 22:
            name = bytes(d[off:off + 16]).split(b'\0')[0]
            nxt, data, size = struct.unpack_from('<HHH', d, off + 16)
            if name == b'WINDOWS 386 3.0':
                f = struct.unpack_from('<I', d, data + 0x10)[0]
                f = (f | bit) if on else (f & ~bit)
                struct.pack_into('<I', d, data + 0x10, f)
                break
            if nxt in (0, 0xFFFF):
                sys.exit('no 386 Enhanced section')
            off = nxt
        else:
            sys.exit('no 386 Enhanced section')
    else:
        sys.exit(f'unknown field {k}')
open(sys.argv[2], 'wb').write(d)
