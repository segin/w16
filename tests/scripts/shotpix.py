#!/usr/bin/env python3
"""shotpix.py - pixels of a screenshot, for tests/inputrun.sh's waits.

  shotpix.py pixel IMG X Y                  the colour at X,Y (rrggbb)
  shotpix.py count IMG HEX X0 Y0 X1 Y1      how many pixels of colour HEX
                                            lie outside the rectangle
  shotpix.py bbox IMG HEX                   "N X0 Y0 X1 Y1": how many pixels
                                            of colour HEX, and their box
"""
import subprocess
import sys


def load(path):
    size = subprocess.run(['magick', 'identify', '-format', '%w %h', path],
                          capture_output=True, text=True).stdout.split()
    if len(size) != 2:
        return 0, 0, b''
    w, h = int(size[0]), int(size[1])
    data = subprocess.run(['magick', path, '-depth', '8', 'rgb:-'], capture_output=True).stdout
    return w, h, data


def main():
    mode, path = sys.argv[1], sys.argv[2]
    w, h, data = load(path)
    if mode == 'pixel':
        x, y = int(sys.argv[3]), int(sys.argv[4])
        if not w or x >= w or y >= h:
            print('none')
            return
        i = 3 * (y * w + x)
        print(data[i:i + 3].hex())
    elif mode == 'count':
        want = bytes.fromhex(sys.argv[3])
        x0, y0, x1, y1 = (int(v) for v in sys.argv[4:8])
        n = 0
        for y in range(h):
            row = data[3 * y * w:3 * (y + 1) * w]
            inside_y = y0 <= y <= y1
            for x in range(w):
                if row[3 * x:3 * x + 3] == want and not (inside_y and x0 <= x <= x1):
                    n += 1
        print(n)
    elif mode == 'bbox':
        want = bytes.fromhex(sys.argv[3])
        n, x0, y0, x1, y1 = 0, w, h, -1, -1
        for y in range(h):
            row = data[3 * y * w:3 * (y + 1) * w]
            for x in range(w):
                if row[3 * x:3 * x + 3] == want:
                    n += 1
                    x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
        print(n, x0, y0, x1, y1)


main()
