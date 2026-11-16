#!/usr/bin/env python3
"""Make the files of boot-test case runassoc (tests/boottest.sh) in DIR.

usage: tests/scripts/runassocfiles.py DIR

Each document is a tiny .COM program (w16's own bytes) that creates
C:\\RANCOM.TXT and exits, so that a run that hands it to DOS as a program
leaves the file behind instead of crashing: U.ABC (an extension without an
association), N (no extension), M.W1Q (an extension WIN.INI maps to a
missing program), D (put in a directory D.ABC). A.TXT is text (Notepad
opens it). PE.EXE is a 32-bit Windows program (a PE image, w16's own
bytes) whose MS-DOS stub does the same as the documents when DOS runs it.
"""
import os
import struct
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


def pe():
    """a minimal PE32 image ("PE32 executable for MS Windows 4.00 (GUI),
    Intel i386"): an MS-DOS header and stub (the .COM's code: it creates
    C:\\RANCOM.TXT), "PE\\0\\0" at e_lfanew 80h, the file header, the
    optional header, one section (.text: RET) at 200h; 400h bytes. The
    layout is the published PE/COFF format's."""
    f = bytearray(0x400)
    # MZ: 400h bytes = 2 pages; header 4 paragraphs; SS:SP 40h:100h past
    # the image; IP 0, CS 0; relocations at 40h (a new-format header);
    # e_lfanew 80h
    struct.pack_into("<2s13H", f, 0, b"MZ", 0, 2, 0, 4, 0x10, 0xFFFF,
                     0x40, 0x100, 0, 0, 0, 0x40, 0)
    struct.pack_into("<I", f, 0x3C, 0x80)
    stub = bytes([0x0E, 0x1F]) + COM[:4] + bytes([0xBA, 0x15, 0x00]) + COM[7:]
    f[0x40:0x40 + len(stub)] = stub
    o = 0x80
    f[o:o + 4] = b"PE\0\0"
    # file header: i386, 1 section, optional header E0h bytes; executable,
    # 32-bit, no relocations, line numbers or symbols
    struct.pack_into("<HHIIIHH", f, o + 4, 0x14C, 1, 0, 0, 0, 0xE0, 0x010F)
    o += 24
    # optional header (PE32): entry and code at 1000h, image base 400000h,
    # sections aligned 1000h in memory and 200h in the file, OS and
    # subsystem 4.0, image 2000h, headers 200h, the GUI subsystem (2),
    # stack and heap 1 MB reserved, 4 KB committed, 16 empty directories
    struct.pack_into("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII", f, o,
                     0x10B, 2, 50, 0x200, 0, 0, 0x1000, 0x1000, 0x2000,
                     0x400000, 0x1000, 0x200, 4, 0, 0, 0, 4, 0, 0,
                     0x2000, 0x200, 0, 2, 0, 0x100000, 0x1000, 0x100000,
                     0x1000, 0, 16)
    o += 0xE0
    struct.pack_into("<8sIIIIIIHHI", f, o, b".text", 1, 0x1000, 0x200,
                     0x200, 0, 0, 0, 0, 0x60000020)
    f[0x200] = 0xC3
    return bytes(f)


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
put(os.path.join(d, "PE.EXE"), pe())
