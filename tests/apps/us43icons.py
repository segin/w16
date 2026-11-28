#!/usr/bin/env python3
"""us43icons - the US43 probe's icons (tests/apps/us43.c): a black frame,
a transparent ring inside it and a square of one colour per instance (A
red, B green, C blue, D yellow, E fuchsia) with as many black dots as the
instance's number, so a screen shows which icon is drawn where and that the
transparent pixels show what is behind.
usage: us43icons.py OUTDIR"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from mkicons import Icon, BLACK, RED, LIME, BLUE, YELLOW, FUCHSIA  # noqa: E402

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for n, c in enumerate((RED, LIME, BLUE, YELLOW, FUCHSIA), 1):
    i = Icon()
    i.frame(0, 0, 31, 31, BLACK)
    i.rect(4, 4, 27, 27, c)
    for k in range(n):
        i.rect(6 + 4 * k, 14, 7 + 4 * k, 17, BLACK)
    open(os.path.join(out, 'us43-%d.ico' % n), 'wb').write(i.ico())
