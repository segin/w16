#!/usr/bin/env python3
"""fonhdr - print the header fields of a .FON (NE resource-only DLL) or
.FNT file: the NE header items a font file uses, the module name and
description, the resource table, the font directory and, for each font
resource, the .FNT header (dfVersion ... dfBitsOffset, the version 3.0
fields when present, the device and face names).

Clean room (ARCHITECTURE.md 3, 6.9): header fields are file-format facts
and metrics and may be read from the reference fonts. This tool stops at
the header: it never reads the character table's glyph offsets or the
glyph bitmaps, and it must not be extended to do so for Microsoft's
files. Our own fonts are checked by tools/fontc's --check and by
tools/fontspec.py, which render glyphs from our sources.

Format source: the published Windows 3.x font file format (.FNT, SDK
"Font Files" reference; dfVersion 0x200/0x300) and the NE format
(Microsoft's published "Executable-File Header Format"); layout as in
include/w16/ne.h.

usage: fonhdr.py FILE...        (.FON or .FNT)
"""
import struct
import sys

# .FNT header, published layout (offsets from the start of the font)
FNT_FIELDS = [
    ('dfVersion', 0, 'H'), ('dfSize', 2, 'I'),
    ('dfType', 66, 'H'), ('dfPoints', 68, 'H'), ('dfVertRes', 70, 'H'),
    ('dfHorizRes', 72, 'H'), ('dfAscent', 74, 'H'),
    ('dfInternalLeading', 76, 'H'), ('dfExternalLeading', 78, 'H'),
    ('dfItalic', 80, 'B'), ('dfUnderline', 81, 'B'), ('dfStrikeOut', 82, 'B'),
    ('dfWeight', 83, 'H'), ('dfCharSet', 85, 'B'), ('dfPixWidth', 86, 'H'),
    ('dfPixHeight', 88, 'H'), ('dfPitchAndFamily', 90, 'B'),
    ('dfAvgWidth', 91, 'H'), ('dfMaxWidth', 93, 'H'), ('dfFirstChar', 95, 'B'),
    ('dfLastChar', 96, 'B'), ('dfDefaultChar', 97, 'B'), ('dfBreakChar', 98, 'B'),
    ('dfWidthBytes', 99, 'H'), ('dfDevice', 101, 'I'), ('dfFace', 105, 'I'),
    ('dfBitsPointer', 109, 'I'), ('dfBitsOffset', 113, 'I'),
    ('dfReserved', 117, 'B'),
]
FNT3_FIELDS = [
    ('dfFlags', 118, 'I'), ('dfAspace', 122, 'H'), ('dfBspace', 124, 'H'),
    ('dfCspace', 126, 'H'), ('dfColorPointer', 128, 'I'),
]


def cstr(b, off):
    end = b.index(b'\0', off)
    return b[off:end].decode('latin-1')


def fnt_header(b, indent='    '):
    out = []
    for name, off, f in FNT_FIELDS:
        v = struct.unpack_from('<' + f, b, off)[0]
        out.append('%s%-18s %d (0x%X)' % (indent, name, v, v))
    ver = struct.unpack_from('<H', b, 0)[0]
    if ver >= 0x300:
        for name, off, f in FNT3_FIELDS:
            v = struct.unpack_from('<' + f, b, off)[0]
            out.append('%s%-18s %d (0x%X)' % (indent, name, v, v))
    # copyright: only its length is shown (Microsoft's text is not ours)
    cr = b[6:66].split(b'\0')[0]
    out.append('%s%-18s %d bytes of text' % (indent, 'dfCopyright', len(cr)))
    dev, face = struct.unpack_from('<II', b, 101)
    if dev:
        out.append('%s%-18s "%s"' % (indent, 'device name', cstr(b, dev)))
    if face:
        out.append('%s%-18s "%s"' % (indent, 'face name', cstr(b, face)))
    return out


def ne(path, b):
    lfa = struct.unpack_from('<I', b, 0x3C)[0]
    assert b[lfa:lfa + 2] == b'NE', 'not an NE file'
    h = lambda off, f='H': struct.unpack_from('<' + f, b, lfa + off)[0]
    print('%s: NE at 0x%X, linker %d.%d, flags 0x%04X, exever 0x%04X, '
          'target OS %d, segments %d, stub %d bytes'
          % (path, lfa, b[lfa + 2], b[lfa + 3], h(0x0C), h(0x3E), b[lfa + 0x36],
             h(0x1C), lfa))
    restab, resnam = lfa + h(0x24), lfa + h(0x26)
    nonres, nonsz = h(0x2C, 'I'), h(0x20)
    n = b[resnam]
    print('  module name "%s"' % b[resnam + 1:resnam + 1 + n].decode('latin-1'))
    n = b[nonres]
    print('  description "%s"' % b[nonres + 1:nonres + 1 + n].decode('latin-1'))
    print('  non-resident table %d bytes' % nonsz)
    shift = struct.unpack_from('<H', b, restab)[0]
    p = restab + 2
    while True:
        typ = struct.unpack_from('<H', b, p)[0]
        if typ == 0:
            break
        cnt = struct.unpack_from('<H', b, p + 2)[0]
        p += 8
        for _ in range(cnt):
            off, ln, fl, rid = struct.unpack_from('<HHHH', b, p)
            p += 12
            off <<= shift
            ln <<= shift
            if rid & 0x8000:
                rname = '%d' % (rid & 0x7FFF)
            else:
                q = restab + rid
                rname = '"%s"' % b[q + 1:q + 1 + b[q]].decode('latin-1')
            tname = {0x8007: 'FONTDIR', 0x8008: 'FONT', 0x8010: 'VERSION'}.get(typ, hex(typ))
            print('  resource %s %s at 0x%X len %d flags 0x%04X' % (tname, rname, off, ln, fl))
            if typ == 0x8007:
                fontdir(b[off:off + ln])
            elif typ == 0x8008:
                for line in fnt_header(b[off:off + ln]):
                    print(line)


def fontdir(d):
    n = struct.unpack_from('<H', d, 0)[0]
    print('    fonts %d' % n)
    p = 2
    for _ in range(n):
        ordinal = struct.unpack_from('<H', d, p)[0]
        e = d[p + 2:]
        # the directory entry is the .FNT header up to dfBitsOffset (113
        # bytes, ends before dfBitsOffset) followed by dfReserved? -- shown
        # raw so the layout can be checked: device and face names follow.
        print('    entry for font %d: dfVersion 0x%X dfPoints %d dfPixHeight %d'
              % (ordinal, struct.unpack_from('<H', e, 0)[0],
                 struct.unpack_from('<H', e, 68)[0], struct.unpack_from('<H', e, 88)[0]))
        q = 113
        dev = cstr(e, q)
        q += len(dev) + 1
        face = cstr(e, q)
        q += len(face) + 1
        print('      after 113 header bytes: device "%s" face "%s"; entry %d bytes'
              % (dev, face, q))
        p += 2 + q
    print('    directory %d bytes used of %d' % (p, len(d)))


def main():
    for path in sys.argv[1:]:
        b = open(path, 'rb').read()
        if b[:2] == b'MZ':
            ne(path, b)
        else:
            print(path)
            for line in fnt_header(b):
                print(line)


if __name__ == '__main__':
    main()
