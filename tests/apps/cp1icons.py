#!/usr/bin/env python3
"""cp1icons - the CP1 probe's icons (tests/apps/cp1.c): 1 a solid black
square, 2 a one-pixel black frame, 3 a white square; their edges show
exactly where Control Panel draws an applet's icon.
usage: cp1icons.py OUTDIR"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from mkicons import Icon, BLACK, WHITE  # noqa: E402

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
a, b, c = Icon(), Icon(), Icon()
a.rect(0, 0, 31, 31, BLACK)
b.frame(0, 0, 31, 31, BLACK)
c.rect(0, 0, 31, 31, WHITE)
for n, i in ((1, a), (2, b), (3, c)):
    open(os.path.join(out, 'cp1-%d.ico' % n), 'wb').write(i.ico())
