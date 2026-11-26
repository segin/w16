#!/usr/bin/env python3
"""tk74nt.py OUT - TK74's name table (resource type 15, RT_NAMETABLE).

The entries as Windows 3.0's resource compiler wrote them (seen in the
Entertainment Pack's setup program's file, read as data): a word with the
entry's length, the resource type (an integer), the resource's id (with
8000h for an integer id), then a NUL-terminated type name and the
NUL-terminated resource name; a zero length word ends the table. TK74
(tests/apps/tk74.c) asks FindResource for these names.
"""
import struct
import sys

ENTRIES = [
    (10, 0x8001, b'', b'FIRST'),      # RCDATA 1
    (10, 0x8002, b'', b'second'),     # lower case in the table
    (10, 0x0003, b'', b'NOBIT'),      # id without 8000h
    (4, 0x8001, b'', b'MENUONLY'),    # another type
    (10, 0x8001, b'XYZ', b'TNAMED'),  # a type name with an integer type
    (10, 0x8002, b'', b'REAL'),       # also a named RCDATA in the file
    (10, 0x8009, b'', b'MISSING'),    # no RCDATA 9
    (5, 0x8001, b'', b'DLGNAME'),     # DIALOG 1
]

out = b''
for typ, rid, tname, name in ENTRIES:
    body = struct.pack('<HH', typ, rid) + tname + b'\0' + name + b'\0'
    out += struct.pack('<H', 2 + len(body)) + body
out += b'\0\0'
with open(sys.argv[1], 'wb') as f:
    f.write(out)
