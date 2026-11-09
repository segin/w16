#!/usr/bin/env python3
"""fondump - list the fonts in .FON files (Windows .FNT resources in NE).

Prints, per RT_FONT resource, the .FNT header fields that GDI's font
mapper and GetTextMetrics work from. Formats: NE (published) and the .FNT
font file format as documented in the Windows 3.1 DDK (Font File Format).
For the user's own font files; nothing is copied anywhere.

usage: fondump.py FILE.FON...
"""
import struct
import sys

FIELDS = [('dfVersion', 'H'), ('dfSize', 'I'), ('dfCopyright', '60s'), ('dfType', 'H'),
          ('dfPoints', 'H'), ('dfVertRes', 'H'), ('dfHorizRes', 'H'), ('dfAscent', 'H'),
          ('dfInternalLeading', 'H'), ('dfExternalLeading', 'H'), ('dfItalic', 'B'),
          ('dfUnderline', 'B'), ('dfStrikeOut', 'B'), ('dfWeight', 'H'), ('dfCharSet', 'B'),
          ('dfPixWidth', 'H'), ('dfPixHeight', 'H'), ('dfPitchAndFamily', 'B'),
          ('dfAvgWidth', 'H'), ('dfMaxWidth', 'H'), ('dfFirstChar', 'B'), ('dfLastChar', 'B'),
          ('dfDefaultChar', 'B'), ('dfBreakChar', 'B'), ('dfWidthBytes', 'H'), ('dfDevice', 'I'),
          ('dfFace', 'I'), ('dfBitsPointer', 'I'), ('dfBitsOffset', 'I'), ('dfReserved', 'B')]


def fnt_header(b):
    fmt = '<' + ''.join(t for _, t in FIELDS)
    vals = struct.unpack_from(fmt, b, 0)
    h = dict(zip([n for n, _ in FIELDS], vals))
    face = b[h['dfFace']:b.index(b'\0', h['dfFace'])].decode('latin-1') if h['dfFace'] else ''
    h['face'] = face
    return h


def fonts(path):
    d = open(path, 'rb').read()
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    rt = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
    shift = struct.unpack_from('<H', d, rt)[0]
    p = rt + 2
    while True:
        tid, cnt = struct.unpack_from('<HH', d, p)
        if tid == 0:
            break
        p += 8
        for _ in range(cnt):
            off, ln, flags, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            if tid == 0x8008:
                yield rid & 0x7FFF, d[off << shift:(off << shift) + (ln << shift)]


def main():
    for path in sys.argv[1:]:
        for rid, b in fonts(path):
            h = fnt_header(b)
            print(f'{path.split("/")[-1]:12} id {rid:3} "{h["face"]}" v{h["dfVersion"]:04X} '
                  f'pts {h["dfPoints"]} res {h["dfHorizRes"]}x{h["dfVertRes"]} '
                  f'h {h["dfPixHeight"]} asc {h["dfAscent"]} il {h["dfInternalLeading"]} '
                  f'el {h["dfExternalLeading"]} w {h["dfPixWidth"]} avg {h["dfAvgWidth"]} '
                  f'max {h["dfMaxWidth"]} wt {h["dfWeight"]} it {h["dfItalic"]} cs {h["dfCharSet"]} '
                  f'pf {h["dfPitchAndFamily"]:02X} chars {h["dfFirstChar"]}-{h["dfLastChar"]} '
                  f'def {h["dfDefaultChar"]} brk {h["dfBreakChar"]} type {h["dfType"]:X}')


if __name__ == '__main__':
    main()
