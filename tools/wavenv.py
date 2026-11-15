#!/usr/bin/env python3
"""wavenv - where a recording is not silent, whatever the source (the PC
speaker or a sound card's wave output: tests/soundprobe.sh sb16).

Splits the recording (DOSBox-X's DX-CAPTURE /A) into 10 ms frames, takes
each frame's RMS (around its own mean: the speaker's output drifts back
to zero after a tone) and prints the stretches whose frames are above
THRESH, joined across gaps of up to GAP ms:
  sound START_MS LEN_MS PEAK_RMS

  usage: wavenv.py [--thresh N] [--gap MS] FILE.wav
         wavenv.py --cmp [--tol MS] A.txt B.txt
With --cmp, two such lists are compared: the same number of stretches,
each starting (from the first one) and lasting within TOL ms (default 20)
of the other's, the peak within a factor of 2. Exit status 0 when they
agree.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wavtones import samples  # noqa: E402


def stretches(s, rate, thresh, gap_ms):
    n = rate // 100
    act = []
    for i in range(0, len(s) - n, n):
        f = s[i:i + n]
        m = sum(f) / n
        rms = math.sqrt(sum((v - m) ** 2 for v in f) / n)
        act.append(rms)
    out, cur = [], None
    gap = gap_ms / 10
    for k, r in enumerate(act):
        if r > thresh:
            if cur and k - cur[1] <= gap:
                cur[1] = k
                cur[2] = max(cur[2], r)
            else:
                if cur:
                    out.append(cur)
                cur = [k, k, r]
    if cur:
        out.append(cur)
    return [(a * 10, (b - a + 1) * 10, p) for a, b, p in out]


def load(path):
    r = []
    for line in open(path):
        f = line.split()
        if f and f[0] == 'sound':
            r.append((float(f[1]), float(f[2]), float(f[3])))
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--thresh', type=float, default=300)
    ap.add_argument('--gap', type=float, default=30)
    ap.add_argument('--cmp', action='store_true')
    ap.add_argument('--tol', type=float, default=20)
    ap.add_argument('files', nargs='+')
    a = ap.parse_args()
    if not a.cmp:
        s, rate = samples(a.files[0])
        for st, ln, pk in stretches(s, rate, a.thresh, a.gap):
            print('sound %9.0f %7.0f %7.0f' % (st, ln, pk))
        return 0
    x, y = load(a.files[0]), load(a.files[1])
    bad = 0
    if len(x) != len(y):
        print('wavenv: %d stretches against %d' % (len(x), len(y)))
        bad += 1
    x0 = x[0][0] if x else 0
    y0 = y[0][0] if y else 0
    for k, (p, q) in enumerate(zip(x, y)):
        on = abs((p[0] - x0) - (q[0] - y0))
        ln = abs(p[1] - q[1])
        pk = max(p[2], q[2]) / max(1, min(p[2], q[2]))
        if on > a.tol or ln > a.tol or pk > 2:
            print('stretch %d: %.0f+%.0f ms rms %.0f against %.0f+%.0f ms rms %.0f'
                  % (k, p[0] - x0, p[1], p[2], q[0] - y0, q[1], q[2]))
            bad += 1
    print('wavenv: %d stretches; %d differences' % (len(x), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
