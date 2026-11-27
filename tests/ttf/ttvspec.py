#!/usr/bin/env python3
"""ttvspec.py - TTVIEW's specimen (tests/apps/ttvpage.c ttv_sizes: a line
at each size from 8 to 24 pixels to the em, then the alphabets at 11 and
13) as w16 draws it (a .PG page: build/host/ttvhost -2, or TTVIEW /T /2)
against Windows' own rendering of the same lines (TTVIEW /T /G: its
TTVIEW.SCR, the client area read back), line by line: whether each line
is identical, the pixels that differ, and the characters whose pixels
(the columns from the character's pen position to the next's, in both)
are identical.

  usage: ttvspec.py [-q] [-m PPEM] [-p PNG] FONT.TTF PAGE.PG TTVIEW.SCR [MIN%]

MIN%: fail (exit 1) if fewer of the lines' characters match. -p: a
picture of both, Windows' above w16's, differences in red. -q: only the
totals. -m: leave out the lines below PPEM (default 10: below that
3.1's mapper gives a raster font instead, ARCHITECTURE.md 11.12).
"""
import struct
import sys

SAMPLE = "Hamburgefonts: The quick brown fox jumps 0123456789"
ALPHA = ["ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz &?!",
         "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123"]


def tables(data):
    n = struct.unpack('>H', data[4:6])[0]
    t = {}
    for i in range(n):
        tag, _, off, ln = struct.unpack('>4sIII', data[12 + 16 * i:28 + 16 * i])
        t[tag.decode('latin1')] = (off, ln)
    return t


def mdr(v, num, den):
    """ttfx_MulDivRound: v * num / den to nearest, a half away from zero"""
    q, r = divmod(v * num, den)
    if 2 * r > den or (2 * r == den and q >= 0):
        q += 1
    return q


def lines(data):
    """ttv_line's lines: (em, baseline y, text)"""
    t = tables(data)
    upem = struct.unpack('>H', data[t['head'][0] + 18:t['head'][0] + 20])[0]
    h = t['hhea'][0]
    asc, dsc, gap = struct.unpack('>hhh', data[h + 4:h + 10])
    out, y, e = [], 0, 16
    for k in range(20):
        if k:
            y += mdr(gap - dsc, e, upem)
        e = 16 if not k else 7 + k if k <= 17 else 11 if k == 18 else 13
        y += mdr(asc, e, upem)
        if k == 0:
            text = None
        elif k <= 17:
            text = "%d %s" % (e, SAMPLE)
        else:
            text = ALPHA[k - 18]
        out.append((e, y, text, mdr(asc, e, upem), mdr(-dsc, e, upem)))
    return out


def load_pg(path):
    b = open(path, 'rb').read()
    return [[(b[y * 80 + (x >> 3)] >> (7 - (x & 7))) & 1 for x in range(640)] for y in range(480)]


def load_scr(path):
    b = open(path, 'rb').read()
    w, h, stride = struct.unpack('<HHH', b[:6])
    rows = []
    for y in range(480):
        if y < h:
            r = b[6 + y * stride:6 + (y + 1) * stride]
            rows.append([0 if x >= w else 1 - ((r[x >> 3] >> (7 - (x & 7))) & 1) for x in range(640)])
        else:
            rows.append([0] * 640)
    return rows, h


def main():
    args = sys.argv[1:]
    quiet = png = None
    minppem = 10
    while args and args[0].startswith('-'):
        if args[0] == '-q':
            quiet = True
            args = args[1:]
        elif args[0] == '-m':
            minppem = int(args[1])
            args = args[2:]
        elif args[0] == '-p':
            png = args[1]
            args = args[2:]
        else:
            break
    if len(args) < 3:
        sys.exit(__doc__)
    data = open(args[0], 'rb').read()
    want = float(args[3]) if len(args) > 3 else None
    a, sh = load_scr(args[2])
    b = load_pg(args[1])
    tot_same = tot_lines = tot_diff = tot_ch = tot_chok = 0
    prev_bottom = 0
    for i, (em, y, text, asc, dsc) in enumerate(lines(data)):
        top, bot = max(prev_bottom, y - asc), min(y + dsc, 479, sh - 1)
        prev_bottom = y + dsc + 1
        if text is None or top > bot or em < minppem:
            continue
        diff = sum(1 for r in range(top, bot + 1) for x in range(640) if a[r][x] != b[r][x])
        ink = sum(a[r][x] for r in range(top, bot + 1) for x in range(640))
        # the characters: runs of columns between gaps in both (ink in
        # either) -- a character's columns are the same in both only if
        # the advances before it agree
        cols = [any(a[r][x] or b[r][x] for r in range(top, bot + 1)) for x in range(640)]
        same_col = [all(a[r][x] == b[r][x] for r in range(top, bot + 1)) for x in range(640)]
        runs, x = [], 0
        while x < 640:
            if cols[x]:
                s = x
                while x < 640 and cols[x]:
                    x += 1
                runs.append((s, x))
            x += 1
        ok = sum(1 for s, e in runs if all(same_col[s:e]))
        tot_lines += 1
        tot_same += diff == 0
        tot_diff += diff
        tot_ch += len(runs)
        tot_chok += ok
        if not quiet:
            print("%2d ppem: %s, %5d pixels differ of %5d (Windows' black), glyph runs %d/%d identical"
                  % (em, "identical" if not diff else "differs  ", diff, ink, ok, len(runs)))
    rate = 100.0 * tot_chok / tot_ch if tot_ch else 0
    print("specimen: %d of %d lines identical, %d pixels differ; %d of %d glyph runs identical (%.1f%%)"
          % (tot_same, tot_lines, tot_diff, tot_chok, tot_ch, rate))
    if png:
        import zlib
        hh = 480 * 2 + 4
        raw = bytearray()
        for r in range(hh):
            raw.append(0)
            for x in range(640):
                if r < 480:
                    p = (0, 0, 0) if a[r][x] else (255, 255, 255)
                elif r < 484:
                    p = (128, 128, 128)
                else:
                    rr = r - 484
                    p = (0, 0, 0) if b[rr][x] else (255, 255, 255)
                    if a[rr][x] != b[rr][x]:
                        p = (220, 0, 0) if b[rr][x] else (255, 170, 170)
                raw.extend(p)

        def chunk(t, d):
            c = struct.pack('>I', len(d)) + t + d
            return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
        with open(png, 'wb') as f:
            f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 640, hh, 8, 2, 0, 0, 0))
                    + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))
    if want is not None and rate < want:
        print("FAIL: below %.1f%%" % want)
        sys.exit(1)


if __name__ == '__main__':
    main()
