#!/usr/bin/env python3
"""mkpif - make a PIF for tests from an existing one (e.g. the reference
install's _DEFAULT.PIF), setting the fields of the published PIF format
that Windows 3.1's WINOLDAP.MOD reads (ARCHITECTURE.md 9.8):

  02h title (30 bytes)     20h max/min memory (KB)   24h program (63)
  63h flags                65h start-up directory    A5h parameters (64)

usage: mkpif.py BASE.PIF OUT.PIF [title=T] [program=P] [dir=D] [params=S]
                [maxmem=K] [minmem=K] [windowed=0|1] [background=0|1]
                [exclusive=0|1] [idle=0|1] [emslock=0|1] [xmslock=0|1]
                [hma=0|1] [fg=N] [bg=N] [emslim=K] [emsreq=K] [xmslim=K]
                [xmsreq=K]

windowed: the 386 Enhanced section's ("WINDOWS 386 3.0", found through the
published extension headers from 171h: a 16-byte name, the next header's
offset, the data's offset and size) flags dword at +10h, bit 3 full
screen: windowed=1 clears it (the program starts in a window), 0 sets it.
Confirmed by observation (tests/scripts/input/dosbox386win.txt: Windows
3.1's MS-DOS Prompt from such a PIF opens in a window).
params is written to the 386 section's parameters (+28h, 64 bytes) as
well: WINOA386 reads those, not the base's (ARCHITECTURE.md 12.11).
The other 386 settings as w16's PIF Editor writes them (shell/pifedit/
dlg.c, pif.c): in the same dword Background 2, Exclusive 4, Detect Idle
Time 1000h, EMS Memory Locked 8000h, Uses High Memory Area 2000h clear,
XMS Memory Locked 10000h; the words +04h/+06h the foreground and
background priorities, +08h/+0Ah the EMS KB limit and required, +0Ch/+0Eh
the XMS ones.
"""
import struct
import sys

FIELDS = {'title': (0x02, 30), 'program': (0x24, 63), 'dir': (0x65, 64), 'params': (0xA5, 64)}
# the flags dword's bit and whether the setting is the bit cleared
BITS = {'windowed': (8, True), 'background': (2, False), 'exclusive': (4, False),
        'idle': (0x1000, False), 'emslock': (0x8000, False), 'hma': (0x2000, True),
        'xmslock': (0x10000, False)}
WORDS = {'fg': 4, 'bg': 6, 'emslim': 8, 'emsreq': 0x0A, 'xmslim': 0x0C, 'xmsreq': 0x0E}


def sect386(d):
    off = 0x171
    while 0 < off < len(d) - 22:
        name = bytes(d[off:off + 16]).split(b'\0')[0]
        nxt, data, size = struct.unpack_from('<HHH', d, off + 16)
        if name == b'WINDOWS 386 3.0':
            return data
        if nxt in (0, 0xFFFF):
            break
        off = nxt
    sys.exit('no 386 Enhanced section')


d = bytearray(open(sys.argv[1], 'rb').read())
for arg in sys.argv[3:]:
    k, v = arg.split('=', 1)
    if k in FIELDS:
        off, n = FIELDS[k]
        b = v.encode('latin-1')[:n - 1 if k != 'title' else n]
        d[off:off + n] = b + bytes(n - len(b))
        if k == 'params':           # 386 Enhanced mode reads its own copy (+28h)
            d386 = sect386(d)
            d[d386 + 0x28:d386 + 0x68] = b + bytes(64 - len(b))
    elif k == 'maxmem':
        struct.pack_into('<H', d, 0x20, int(v))
    elif k == 'minmem':
        struct.pack_into('<H', d, 0x22, int(v))
    elif k in WORDS:
        struct.pack_into('<H', d, sect386(d) + WORDS[k], int(v))
    elif k in BITS:
        data = sect386(d)
        bit, inv = BITS[k]
        f = struct.unpack_from('<I', d, data + 0x10)[0]
        f = (f | bit) if (v == '1') != inv else (f & ~bit)
        struct.pack_into('<I', d, data + 0x10, f)
    else:
        sys.exit(f'unknown field {k}')
open(sys.argv[2], 'wb').write(d)
