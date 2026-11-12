#!/usr/bin/env python3
"""swapdirs.py WC.LOG - the switcher block's two swap directories (D7h,
11Bh) from WC4's dump (tests/dos/wc4.asm), on one line."""
import re
import sys

data = bytearray()
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'\s+([0-9A-F]{4}): ((?:[0-9A-F]{16} ?){1,4})', line)
    if m:
        off = int(m.group(1), 16)
        if off != len(data):
            break
        data += bytes.fromhex(m.group(2).replace(' ', ''))


def asciiz(o):
    return data[o:data.index(0, o)].decode('latin-1') if len(data) > o else '(none)'


print(asciiz(0xD7), asciiz(0x11B))
