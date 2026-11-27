#!/usr/bin/env python3
"""ttmap - find which Unicode character Windows 3.1's TrueType engine looks
up for each ANSI code (ARCHITECTURE.md 6.9.3).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).

  ttmap.py font OUT.TTF [A|B]  writes the test font "w16 Map Test": a
                               (3,1) cmap of every BMP character in the
                               ranges below, each to its own empty glyph
                               whose advance width names it (20 + its
                               index in the list; .notdef is 7 wide);
                               A, B: "w16 Map A"/"B" with an hdmx table
                               (see font())
  ttmap.py decode FN4LOG       reads probe FN4's MAP section (GetCharWidth
                               0-255 at 2048 pixels per em) and prints,
                               per code, the character 3.1 showed

A width says which glyph GDI used, so the table comes from the API alone.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w16ttf  # noqa: E402

RANGES = [(0x0000, 0x02FF), (0x0370, 0x03FF), (0x2000, 0x206F), (0x20A0, 0x20BF),
          (0x2100, 0x215F), (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2300, 0x23FF),
          (0x2500, 0x25FF), (0x2600, 0x266F), (0xF000, 0xF0FF)]


def codes():
    out = []
    for a, b in RANGES:
        out.extend(range(a, b + 1))
    return out


def width_of(i):
    return 20 + i


def font(path, variant=''):
    """variant 'A'/'B': with an hdmx table for 12 and 20 pixels per em whose
    widths are 1 + glyph index mod 13 (not the scaled ones), head flags
    bit 4 clear (A) or set (B)"""
    f = w16ttf.Font()
    box = [(100, 0, True), (100, 700, True), (500, 700, True), (500, 0, True)]
    f.glyphs.append(w16ttf.Glyph('.notdef', 7, [box]))
    for i, c in enumerate(codes()):
        f.glyphs.append(w16ttf.Glyph('uni%04X' % c, width_of(i)))
        f.cmap[c] = i + 1
    name = 'w16 Map ' + variant if variant else 'w16 Map Test'
    f.names = {0: 'w16 test font, public domain (CC0 1.0)', 1: name,
               2: 'Regular', 3: name + ' 1.0', 4: name,
               5: 'Version 1.00', 6: name.replace(' ', '')}
    if variant:
        f.hdmx = {p: [1 + g % 13 for g in range(len(f.glyphs))] for p in (12, 20)}
        f.head_flags = 0x000B if variant == 'A' else 0x001B
    f.hhea = (1854, -434, 67)
    f.typo = (1491, -431, 307)
    f.win = (1854, 434)
    f.avg_width = 904
    f.unicode_ranges = (3, 0, 0, 0)
    open(path, 'wb').write(f.build())


def decode(log):
    by_width = {width_of(i): c for i, c in enumerate(codes())}
    on = False
    ws = []
    for line in open(log, encoding='latin-1'):
        line = line.strip()
        if line.startswith('MAP '):
            on = True
            print('#', line)
            continue
        if on and line.startswith('w') and '=' in line:
            ws.extend(int(v) for v in line.split('=', 1)[1].split(','))
        elif on and ws and not line.startswith('w'):
            if len(ws) >= 256:
                break
    for code, w in enumerate(ws[:256]):
        u = by_width.get(w)
        print('%02X %s' % (code, 'U+%04X' % u if u is not None else
                              ('notdef' if w == 7 else 'width %d' % w)))


def main():
    if len(sys.argv) in (3, 4) and sys.argv[1] == 'font':
        font(sys.argv[2], sys.argv[3] if len(sys.argv) == 4 else '')
    elif len(sys.argv) == 3 and sys.argv[1] == 'decode':
        decode(sys.argv[2])
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == '__main__':
    main()
