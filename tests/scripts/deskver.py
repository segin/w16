#!/usr/bin/env python3
"""deskver.py - boot-test case deskver's check of the desktop version stamp.

usage: deskver.py FONT TEXT OFFDIR ONDIR EXACT,... PARTIAL,... BOX,...

OFFDIR and ONDIR are two runs of tests/scripts/input/deskver.txt
(tests/inputrun.sh), with [w16] DesktopVersion=0 and =1, everything else
the same. For each shot NAME in EXACT (the desktop's corner uncovered) the
DesktopVersion=1 screen must equal the DesktopVersion=0 one with the stamp
as tools/deskstamp.py draws it from FONT and TEXT, pixel for pixel. For
each in PARTIAL (a window over part of the stamp) every pixel where the
two screens differ must be one of the stamp's, in the stamp's colour, and
some must differ. For each in BOX (a drag's frame, drawn by XOR, over
the stamp) the screens may differ only inside the stamp's box. Prints
one line per shot; exit status 1 if any fails.
"""
import os
import sys

from PIL import Image, ImageChops

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import deskstamp  # noqa: E402


def diffs(a, b):
    d = ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v else 0)
    return d


def main():
    if len(sys.argv) != 8:
        sys.exit(__doc__)
    font, text, offd, ond, exact, partial, inbox = sys.argv[1:]
    f = deskstamp.load_font(font)
    bad = 0
    for kind, names in (('exact', exact), ('partial', partial), ('box', inbox)):
        for n in filter(None, names.split(',')):
            try:
                off = Image.open(os.path.join(offd, n + '.png')).convert('RGB')
                on = Image.open(os.path.join(ond, n + '.png')).convert('RGB')
            except OSError as e:
                print('FAIL %s: %s' % (n, e))
                bad += 1
                continue
            want = deskstamp.stamp(f, text, off)
            if kind == 'exact':
                d = diffs(on, want)
                k = sum(1 for v in d.tobytes() if v)
                if k:
                    print('FAIL %s: %d pixels differ from the stamp drawn over DesktopVersion=0 (%s)'
                          % (n, k, d.getbbox()))
                    bad += 1
                else:
                    print('ok   %s: the stamp, exactly' % n)
            elif kind == 'box':
                bx, by, bw, bh = deskstamp.box(f, text, off.width, off.height)
                bb = diffs(on, off).getbbox()
                if bb and (bb[0] < bx or bb[1] < by or bb[2] > bx + bw or bb[3] > by + bh):
                    print('FAIL %s: the screens differ outside the stamp\'s box (%s)' % (n, bb))
                    bad += 1
                else:
                    print('ok   %s: differences only in the stamp\'s box' % n)
            else:
                d = diffs(on, off)
                m = d.tobytes()
                shown = sum(1 for v in m if v)
                # where the screens differ, the stamp's pixel must be there
                wrong = diffs(on, want)
                w = sum(1 for a, b in zip(m, wrong.tobytes()) if a and b)
                if not shown:
                    print('FAIL %s: no stamp visible' % n)
                    bad += 1
                elif w:
                    print('FAIL %s: %d differing pixels are not the stamp\'s' % (n, w))
                    bad += 1
                else:
                    print('ok   %s: %d stamp pixels shown, nothing else differs' % (n, shown))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
