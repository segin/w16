#!/usr/bin/env python3
"""mkfot - writes the .FOT file of a TrueType font as Windows 3.1's
CreateScalableFontResource does (ARCHITECTURE.md 6.9.3).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).

    mkfot.py TTFDIR [OUTDIR]          X.FOT for every X.TTF (portable form:
                                      the file name only, as Control Panel
                                      and Setup make them)
    mkfot.py --path PATH X.TTF OUT    with PATH (e.g. C:\\FONTS\\X.TTF)
                                      recorded instead of the file name
    mkfot.py --compare DIR TTFDIR     compares with the files 3.1 wrote for
                                      the same fonts (tests/ttfonts.sh: X.FOT
                                      portable, X.FOB full path, X.FOC
                                      portable again); exit 1 on a difference

Source: the published NE (New Executable) and .FNT layouts, and the bytes
3.1 writes for w16's own fonts (FN4's FOT section, tests/ttfonts.sh): an
NE font library with no segments and two resources:

  - RT_FONTDIR (FONTDIR, at file offset 0x480): a count of 1, font
    number 0, then a version 2.00 .FNT header filled from the TrueType
    file: dfType 3 plus OS/2 fsSelection's low byte times 256, dfPoints
    the em, resolution 72 x 72, dfAscent usWinAscent, dfPixHeight
    usWinAscent + usWinDescent, dfInternalLeading that less the em,
    dfExternalLeading hhea's lineGap less what the win metrics exceed
    hhea's ascent and descent by (at least 0), dfItalic 255 for an italic
    style, dfWeight usWeightClass, dfPitchAndFamily as GetTextMetrics shows
    it, dfAvgWidth xAvgCharWidth, dfMaxWidth head's xMax - xMin, first 30,
    last 255, default 1 and break 2 (relative), dfFace 118, a reserved
    dword 11, an empty device name, then the family, full and style names
    (name IDs 1, 4, 2) and 16 zero bytes; the copyright field holds 3.1's
    own text "Windows! Windows! Windows!";
  - type 204 (0xCC, id 1, at 0x400, 128 bytes): the font file as passed.

The DOS stub (3.1's "This is a TrueType font, not a program." and its
"Kiesa" mark) is 3.1's output, the same in every file; the module name is
the font file's base name, the non-resident name "FONTRES:" and the full
name. Nothing in the file depends on when or where it was made (3.1 wrote
the same bytes into another file name; the full-path form differs only in
the recorded path), so w16's files are byte for byte 3.1's.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w16ttf  # noqa: E402

STUB = (bytes.fromhex('4d5a01000200000004000f00ffff0000b8000000000000004000000000000000')
        + bytes(28) + bytes.fromhex('80000000')
        + bytes.fromhex('0e1fba0e00b409cd21b8014ccd21')
        + b'This is a TrueType font, not a program.\r\r\n$\0Kiesa\0')
# the copyright field: 3.1's text, then four bytes 3.1 leaves there in every
# file (10h 03h 01h 01h, not set from the font)
COPYRIGHT = b'Windows! Windows! Windows!\0\x10\x03\x01\x01'


def pitch_family(f):
    """dfPitchAndFamily: TMPF_TRUETYPE | TMPF_VECTOR | variable pitch, and
    the family from PANOSE (monospaced: modern, sans serif: swiss, other
    text faces: roman)"""
    pan = f['os2']['panose']
    fixed = f['post']['fixed'] or pan[3] == 9
    low = 0x06 | (0 if fixed else 0x01)
    if fixed:
        fam = 0x30
    elif pan[0] == 2 and pan[1] in (11, 12, 13):
        fam = 0x20
    elif pan[0] == 2:
        fam = 0x10
    elif pan[0] == 3:
        fam = 0x40
    else:
        fam = 0x50
    return fam | low


def fot(ttf_bytes, ttf_name, recorded=None):
    """the .FOT bytes for a TrueType file; ttf_name its file name (the
    module name is its base name), recorded the string kept in the file
    (default: ttf_name)"""
    f = w16ttf.parse(ttf_bytes)
    names = {k[3]: v for k, v in f['name'].items() if k[:3] == (3, 1, 0x409)}
    family, style, full = names[1], names[2], names[4]
    o = f['os2']
    h = f['head']
    hh = f['hhea']
    upem = h['upem']
    win = o['wasc'] + o['wdesc']
    el = max(0, hh['gap'] - (win - (hh['asc'] - hh['desc'])))
    base = os.path.splitext(os.path.basename(ttf_name))[0].upper()
    recorded = recorded if recorded is not None else ttf_name
    # the FONTDIR resource
    fnt = struct.pack('<HI60sHHHHHHHBBBHBHHBHHBBBBHII',
                      0x200, 149, COPYRIGHT, 3 | (o['sel'] & 0xFF) << 8, upem, 72, 72,
                      o['wasc'], win - upem, el, 255 if o['sel'] & 1 else 0, 0, 0,
                      o['weight'], 0, 0, win, pitch_family(f), o['avg'],
                      h['xmax'] - h['xmin'], 30, 255, 1, 2, 0, 0, 118)
    fnt += struct.pack('<I', 11) + b'\0'
    for s in (family, full, style):
        fnt += s.encode('latin-1') + b'\0'
    fontdir = struct.pack('<HH', 1, 0) + fnt
    dir_len = (len(fontdir) + 15) // 16
    # NE tables, offsets relative to the NE header
    restab = struct.pack('<H', 4)
    restab += struct.pack('<HHI', 0x8007, 1, 0) + struct.pack('<HHHHI', 0x48, dir_len, 0x0C50, 0x2C, 0)
    restab += struct.pack('<HHI', 0x80CC, 1, 0) + struct.pack('<HHHHI', 0x40, 8, 0x0C50, 0x8001, 0)
    restab += struct.pack('<H', 0) + b'\x07FONTDIR'
    mod = base.encode('ascii')
    resname = bytes([len(mod)]) + mod + b'\0\0' + b'\0'
    fname = os.path.basename(ttf_name).upper().encode('ascii')
    imported = bytes([len(fname) + 1]) + fname + b'\0'
    res_off = 0x40
    resn_off = res_off + len(restab)
    modref_off = resn_off + len(resname) + 2
    entry_off = modref_off + len(imported)
    nonres_name = b'FONTRES:' + full.encode('latin-1')
    # (3.1's length byte counts the ordinal and the terminator too)
    nonres = bytes([len(nonres_name) + 4]) + nonres_name + b'\0\0' + b'\0'
    nonres_file = 0x80 + entry_off + 2 + 2
    ne = struct.pack('<2sBBHHIHHHHIIHHHHHHHHIHHHBBHHHH',
                     b'NE', 5, 16, entry_off, 2, 0, 0x8000, 0, 0, 0, 0, 0, 0, 0,
                     len(nonres), 0x40, res_off, resn_off, modref_off, modref_off,
                     nonres_file, 0, 4, 2, 2, 0, 0, 0, 0, 0x300)
    body = bytearray(0x520 + 0x100)
    body[0:len(STUB)] = STUB
    body[0x80:0x80 + len(ne)] = ne
    body[0x200] = 0xC3          # a byte 3.1 writes at 200h in every file
    body[0x80 + res_off:0x80 + res_off + len(restab)] = restab
    body[0x80 + resn_off:0x80 + resn_off + len(resname)] = resname
    body[0x80 + modref_off:0x80 + modref_off + len(imported)] = imported
    body[nonres_file:nonres_file + len(nonres)] = nonres
    rec = recorded.encode('latin-1')
    body[0x400:0x400 + len(rec)] = rec
    end = 0x480 + len(fontdir)
    body[0x480:end] = fontdir
    return bytes(body[:end + 16])


def compare(ref_dir, ttf_dir):
    bad = 0
    n = 0
    for fn in sorted(os.listdir(ttf_dir)):
        if not fn.upper().endswith('.TTF'):
            continue
        base = fn[:-4].upper()
        data = open(os.path.join(ttf_dir, fn), 'rb').read()
        for ext, rec in (('FOT', None), ('FOC', None), ('FOB', 'C:\\WINDOWS\\SYSTEM\\' + base + '.TTF')):
            p = os.path.join(ref_dir, base + '.' + ext)
            if not os.path.exists(p):
                continue
            n += 1
            ours = fot(data, base + '.TTF', rec)
            theirs = open(p, 'rb').read()
            if ours != theirs:
                bad += 1
                diff = [i for i in range(min(len(ours), len(theirs))) if ours[i] != theirs[i]]
                print('%s.%s: differs (%d vs %d bytes; first differences at %s)' % (
                    base, ext, len(ours), len(theirs), ', '.join('%#x' % d for d in diff[:8])))
    print('mkfot: %d of %d files byte for byte as 3.1 wrote them' % (n - bad, n))
    return 1 if bad or not n else 0


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == '--compare':
        return compare(a[1], a[2])
    if len(a) == 4 and a[0] == '--path':
        data = open(a[2], 'rb').read()
        open(a[3], 'wb').write(fot(data, os.path.basename(a[2]), a[1]))
        return 0
    if len(a) in (1, 2) and not a[0].startswith('-'):
        src = a[0]
        dst = a[1] if len(a) == 2 else src
        os.makedirs(dst, exist_ok=True)
        for fn in sorted(os.listdir(src)):
            if fn.upper().endswith('.TTF'):
                base = fn[:-4].upper()
                data = open(os.path.join(src, fn), 'rb').read()
                open(os.path.join(dst, base + '.FOT'), 'wb').write(fot(data, base + '.TTF'))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
