#!/usr/bin/env python3
"""Make the files of boot-test case runassoc (tests/boottest.sh) in DIR.

usage: tests/scripts/runassocfiles.py DIR

Each document is a tiny .COM program (w16's own bytes) that creates
C:\\RANCOM.TXT and exits, so that a run that hands it to DOS as a program
leaves the file behind instead of crashing: U.ABC (an extension without an
association), N (no extension), M.W1Q (an extension WIN.INI maps to a
missing program), D (put in a directory D.ABC). A.TXT is text (Notepad
opens it).
"""
import os
import sys

COM = bytes([
    0xB4, 0x3C,             # mov ah, 3Ch
    0x31, 0xC9,             # xor cx, cx
    0xBA, 0x13, 0x01,       # mov dx, 113h (the name)
    0xCD, 0x21,             # int 21h
    0x93,                   # xchg bx, ax
    0xB4, 0x3E,             # mov ah, 3Eh
    0xCD, 0x21,             # int 21h
    0xB8, 0x00, 0x4C,       # mov ax, 4C00h
    0xCD, 0x21,             # int 21h
]) + b"C:\\RANCOM.TXT\0"


def put(path, data):
    """write path whole (the case's parts make the same files at once)"""
    tmp = "%s.%d" % (path, os.getpid())
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


d = sys.argv[1]
os.makedirs(d, exist_ok=True)
for n in ("U.ABC", "N", "M.W1Q", "D"):
    put(os.path.join(d, n), COM)
put(os.path.join(d, "A.TXT"), b"w16 runassoc\r\n")
