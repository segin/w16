#!/usr/bin/env python3
"""ttshowcmp - compare FN4 show pages (tests/ttshow.sh): the lines in
w16's faces as Windows 3.1 drew them and as w16's GDI drew them.

    ttshowcmp.py MS.BMP W16.BMP MS.png W16.png LIMIT LABEL

The text lines are found on 3.1's page (rows with ink, below the
caption and the label line); they come in pairs, Microsoft's face then
w16's, and the second of each pair is compared: the pixels that differ,
per cent of the black pixels 3.1 drew there. Writes both screens as PNG;
exit status 1 when a line is over LIMIT per cent.

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).
"""
import sys
from PIL import Image


def bands(im, top):
    w, h = im.size
    px = im.load()
    rows = [any(px[x, y] < 128 for x in range(w)) for y in range(h)]
    out, y = [], top
    while y < h:
        if rows[y]:
            y0 = y
            while y < h and rows[y]:
                y += 1
            out.append((y0, y))
        y += 1
    return out


def main():
    ms_p, w_p, ms_o, w_o, limit, label = sys.argv[1:7]
    ms = Image.open(ms_p).convert('L')
    w = Image.open(w_p).convert('L')
    ms.save(ms_o)
    w.save(w_o)
    # below the caption (white from 20) and the label line
    lines = bands(ms, 20)[1:]
    worst = (0.0, None)
    a, b = ms.load(), w.load()
    W = min(ms.size[0], w.size[0])
    for k in range(1, len(lines), 2):
        y0, y1 = lines[k]
        black = diff = 0
        for y in range(y0, y1):
            for x in range(W):
                ka, kb = a[x, y] < 128, b[x, y] < 128
                black += ka
                diff += ka != kb
        pct = 100.0 * diff / max(1, black)
        if pct >= worst[0]:
            worst = (pct, (k // 2 + 1, diff, black))
    if worst[1] is None:
        print('%s: no lines found' % label)
        return 1
    n, diff, black = worst[1]
    bad = worst[0] > float(limit)
    print('%s: %s, worst line %d of %d: %d of %d pixels differ (%.1f%%)' % (
        label, 'OVER' if bad else 'ok', n, len(lines) // 2, diff, black, worst[0]))
    return 1 if bad else 0


sys.exit(main())
