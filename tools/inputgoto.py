#!/usr/bin/env python3
"""inputgoto.py - expand "goto X Y" lines of a tests/inputrun.sh script.

    usage: tools/inputgoto.py SCRIPT.src > SCRIPT.txt

"goto X Y" puts the pointer at screen pixel X,Y: "home" twice (the pointer
stops at 0,0), then relative moves of at most 20 units each, 0.05 s apart.
That lands exactly only without mouse acceleration: the scripts that use
it run with WIN.INI MouseSpeed=0 (one pixel a unit whatever the host's
timing; with acceleration, how DOSBox splits a move decides). Every other
line is copied as it is.
"""
import sys


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.stdout
    with open(sys.argv[1]) as f:
        for line in f:
            p = line.split()
            if p and p[0] == 'goto':
                x, y = int(p[1]), int(p[2])
                out.write('home\nhome\nsleep 0.3\n')
                while x or y:
                    dx, dy = min(x, 20), min(y, 20)
                    out.write('move %d %d\nsleep 0.05\n' % (dx, dy))
                    x -= dx
                    y -= dy
                out.write('sleep 0.3\n')
            else:
                out.write(line)


main()
