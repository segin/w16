#!/usr/bin/env python3
"""gdtfont - test raster fonts for GD11's test-font mode (tests/apps/gd11.c,
ARCHITECTURE.md 11.10): GDT.FON, one font per line of a spec file, every
glyph a bar at the top of the character height and one at the bottom
(original, public domain; built by tools/fontc.py).

    usage: tools/gdtfont.py OUTDIR SPEC      -> OUTDIR/GDT.FON

spec lines (after '#' a comment):
    FACE HEIGHT ASCENT INTERNAL-LEADING AVGWIDTH MAXWIDTH POINTS [PITCH-FAMILY [CHARSET]]
FACE must be T1, T2, ... (the probe asks T1 to T59); every character's
advance is AVGWIDTH but 'W' and 'M' (MAXWIDTH). A character set of a face's
own (e.g. 101, 102, ...) keeps the faces from competing with each other.
Run the probe with the file as C:\\GDT.FON:
    REFRUN_FILES=OUTDIR/GDT.FON tests/gdiheap.sh -r -p build/dos/GD11.EXE
"""
import os
import subprocess
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    out, spec = sys.argv[1], sys.argv[2]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(out, exist_ok=True)
    ids = []
    n = 0
    with open(spec) as sf:
        for line in sf:
            line = line.split('#')[0].split()
            if not line:
                continue
            face = line[0]
            H, A, IL, AW, MW, PTS = [int(v) for v in line[1:7]]
            PF = int(line[7], 0) if len(line) > 7 else 0x21
            CS = int(line[8], 0) if len(line) > 8 else 0
            n += 1
            src = os.path.join(out, 'f%d.txt' % n)
            with open(src, 'w') as f:
                f.write('face %s\nversion 2\ncopyright "w16 test font"\npoints %d\nheight %d\n'
                        'ascent %d\ninternal-leading %d\nweight 400\ncharset %d\n'
                        'pitch-family 0x%x\navgwidth %d\nfirst 32\nlast 126\ndefault 63\n'
                        'break 32\nsentinel-width %d\n' % (face, PTS, H, A, IL, CS, PF, AW, AW))
                for c in range(32, 127):
                    w = MW if chr(c) in 'WM' else AW
                    f.write('char %d\n' % c)
                    for r in range(H):
                        bar = c != 32 and r in (IL, H - 1)
                        f.write(('#' if bar else '.') * w + '\n')
            ids.append('%d=%s' % (n, src))
    subprocess.check_call([sys.executable, os.path.join(root, 'tools', 'fontc.py'), 'fon',
                           os.path.join(out, 'GDT.FON'), '--module', 'GDT',
                           '--desc', 'w16 mapper test fonts'] + ids)
    return 0


if __name__ == '__main__':
    sys.exit(main())
