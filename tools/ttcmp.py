#!/usr/bin/env python3
"""ttcmp - compares probe FN4's dump of w16's TrueType fonts with its dump
of Windows 3.1's (ARCHITECTURE.md 6.9.3, tests/ttfonts.sh).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).

    ttcmp.py REF.txt W16.txt [-v]

Per face pair (Arial / w16 Sans, Times New Roman / w16 Serif, Courier New /
w16 Mono, four styles each) it compares what must be equal for text to set
the same:

  - GetCharWidth at the em (font units) and at every size 6-255 pixels
    per em, for each ANSI code whose character w16's fonts have (the rest
    of 0x80-0x9F, and the 8 Latin-1 characters ISO 8859-15 replaced, are
    counted apart: 3.1 shows the default character there);
  - TEXTMETRIC at the em and at every size (all fields);
  - OUTLINETEXTMETRIC: everything but the font box (otmrcFontBox: the
    designs' own extents) and the names;
  - the kerning pairs between characters w16's fonts have.

Prints one line per face and the differences (-v: all of them); exit
status 1 if any face differs.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ttgen  # noqa: E402

PAIRS = {'Arial': 'w16 Sans', 'Times New Roman': 'w16 Serif', 'Courier New': 'w16 Mono'}
TM_SKIP = set()
OTM_SKIP = {'box.l', 'box.t', 'box.r', 'box.b', 'size', 'ret'}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    verbose = '-v' in sys.argv
    if len(args) != 2:
        print(__doc__)
        return 2
    ref = ttgen.parse_ref(args[0])
    ours = ttgen.parse_ref(args[1])
    ansi = ttgen.ansi_to_unicode()
    have = set(ord(c) for c in ttgen.repertoire())
    codes = [c for c, u in ansi.items() if u in have]
    others = [c for c in range(256) if c not in codes]
    bad = 0
    for (name, wt, it), r in sorted(ref.items()):
        key = (PAIRS[name], wt, it)
        o = ours.get(key)
        label = '%s %d %s' % (PAIRS[name], wt, 'italic' if it else 'upright')
        if not o:
            print('%s: not in the w16 log' % label)
            bad += 1
            continue
        diffs = []
        for c in codes:
            if r['w'][c] != o['w'][c]:
                diffs.append('width %02X: %d, 3.1 %d' % (c, o['w'][c], r['w'][c]))
        nsizes = 0
        for p, ws in sorted(r['sizes'].items()):
            if p not in o['sizes']:
                diffs.append('size %d missing' % p)
                continue
            nsizes += 1
            for c in codes:
                if ws[c] != o['sizes'][p][c]:
                    diffs.append('%d ppem width %02X: %d, 3.1 %d' % (p, c, o['sizes'][p][c], ws[c]))
            for k, v in r['tmsize'][p].items():
                if k not in TM_SKIP and o['tmsize'][p].get(k) != v:
                    diffs.append('%d ppem tm %s: %s, 3.1 %d' % (p, k, o['tmsize'][p].get(k), v))
        for k, v in r['tm'].items():
            if o['tm'].get(k) != v:
                diffs.append('tm %s: %s, 3.1 %d' % (k, o['tm'].get(k), v))
        for k, v in r['otm'].items():
            if k not in OTM_SKIP and o['otm'].get(k) != v:
                diffs.append('otm %s: %s, 3.1 %d' % (k, o['otm'].get(k), v))
        rk = {(a, b): v for a, b, v in r['kern'] if a in codes and b in codes}
        ok_ = {(a, b): v for a, b, v in o['kern']}
        for k, v in rk.items():
            if ok_.get(k) != v:
                diffs.append('kern %02X %02X: %s, 3.1 %d' % (k[0], k[1], ok_.get(k), v))
        for k in ok_:
            if k not in rk:
                diffs.append('kern %02X %02X: extra' % k)
        other = sum(1 for c in others if r['w'][c] != o['w'][c])
        print('%s: %s (%d codes, %d sizes, %d kerning pairs compared; %d other codes differ at the em)' % (
            label, 'same' if not diffs else '%d differences' % len(diffs), len(codes), nsizes, len(rk), other))
        for d in diffs[:None if verbose else 12]:
            print('   ', d)
        bad += bool(diffs)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
