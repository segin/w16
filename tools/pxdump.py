#!/usr/bin/env python3
"""pxdump - print a region of a screenshot as characters, one per pixel.

For measuring Windows 3.1's drawing (black-box: screenshots of the running
system) and comparing w16's with it. Colours of the 16-colour VGA palette
get a letter (the nearest; DOSBox's palette is not exact): K black, W white, G light gray (C0C0C0), D dark gray
(808080), B navy (000080), b blue, R maroon, r red, N green (008000),
n lime, Y olive, y yellow, T teal, t cyan, P purple, p magenta; others '?'.

usage: pxdump.py PNG X Y W H
"""
import sys
from PIL import Image

NAMES = {(0, 0, 0): 'K', (255, 255, 255): 'W', (192, 192, 192): 'G', (128, 128, 128): 'D',
         (0, 0, 128): 'B', (0, 0, 255): 'b', (128, 0, 0): 'R', (255, 0, 0): 'r',
         (0, 128, 0): 'N', (0, 255, 0): 'n', (128, 128, 0): 'Y', (255, 255, 0): 'y',
         (0, 128, 128): 'T', (0, 255, 255): 't', (128, 0, 128): 'P', (255, 0, 255): 'p'}


def main():
    im = Image.open(sys.argv[1]).convert('RGB')
    x, y, w, h = map(int, sys.argv[2:6])
    print('     ' + ''.join(str((x + i) // 100 % 10) for i in range(w)))
    print('     ' + ''.join(str((x + i) // 10 % 10) for i in range(w)))
    print('     ' + ''.join(str((x + i) % 10) for i in range(w)))
    cache = {}

    def name(c):
        if c not in cache:
            best = min(NAMES, key=lambda k: sum((a - b) ** 2 for a, b in zip(k, c)))
            cache[c] = NAMES[best]
        return cache[c]
    for j in range(y, y + h):
        print(f'{j:4} ' + ''.join(name(im.getpixel((i, j))[:3]) for i in range(x, x + w)))


if __name__ == '__main__':
    main()
