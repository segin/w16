#!/usr/bin/env python3
"""tonecmp - compare two recordings' tones (tools/wavtones.py output),
the sound driver's against Windows 3.1's (tests/soundprobe.sh).

SN1 (tests/apps/sn1.c) puts markers on the recording: tones of divisor 400
(2983 Hz) lasting 110 ms, in runs of 1-4. The recordings are cut at the
markers into segments, and segment k of one is compared with segment k of
the other: the same number of tones, and for each tone
  - its frequency within FREQ percent (default 0.5),
  - its length within LEN ms (default 6: about a 5 ms tick and the
    measurement's half-period),
  - the time from the segment's first tone to its start within ONSET ms
    (default 6), and the time from the tone before within GAP ms
    (default 6).
Tones shorter than --min ms (default 1.5) are clicks, not compared, and
the marker runs themselves must match.

usage: tonecmp.py [--freq P] [--len MS] [--onset MS] [--min MS] A.txt B.txt
Exit status 0 when everything is within the limits; the worst deviation
of each kind is printed either way.
"""
import argparse
import sys


def load(path, minlen):
    tones = []
    for line in open(path):
        f = line.split()
        if not f or f[0] != 'tone':
            continue
        st, ln, fr, div = float(f[1]), float(f[3]), float(f[4]), int(f[6])
        if ln < minlen:
            continue
        tones.append((st, ln, fr, div))
    return tones


def is_marker(t):
    return abs(t[3] - 400) <= 2 and 100 <= t[1] <= 120


def segments(tones):
    """[(marker run length before it, [tones])]"""
    segs, cur, run = [], [], 0
    for t in tones:
        if is_marker(t):
            if cur or not segs:
                segs.append((run, cur))
                cur, run = [], 0
            run += 1
        else:
            cur.append(t)
    segs.append((run, cur))
    return segs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--freq', type=float, default=0.5)
    ap.add_argument('--len', type=float, default=6)
    ap.add_argument('--onset', type=float, default=6)
    ap.add_argument('--gap', type=float, default=6)
    ap.add_argument('--min', type=float, default=1.5)
    ap.add_argument('a')
    ap.add_argument('b')
    a = ap.parse_args()
    sa, sb = segments(load(a.a, a.min)), segments(load(a.b, a.min))
    bad = 0
    worst = {'freq': 0.0, 'len': 0.0, 'onset': 0.0, 'gap': 0.0}
    if len(sa) != len(sb):
        print('segments: %d against %d' % (len(sa), len(sb)))
        bad += 1
    for k, ((ra, ta), (rb, tb)) in enumerate(zip(sa, sb)):
        if ra != rb:
            print('segment %d: marker run %d against %d' % (k, ra, rb))
            bad += 1
        if len(ta) != len(tb):
            print('segment %d (after %d markers): %d tones against %d' % (k, ra, len(ta), len(tb)))
            for x in range(max(len(ta), len(tb))):
                p = ta[x] if x < len(ta) else None
                q = tb[x] if x < len(tb) else None
                fmt = lambda t, s0: ('%8.1f %7.1f %8.2f' % (t[0] - s0, t[1], t[2])) if t else ' ' * 26
                print('   %s | %s' % (fmt(p, ta[0][0] if ta else 0), fmt(q, tb[0][0] if tb else 0)))
            bad += 1
            continue
        if not ta:
            continue
        a0, b0 = ta[0][0], tb[0][0]
        for x, (p, q) in enumerate(zip(ta, tb)):
            d = {'freq': abs(p[2] - q[2]) / p[2] * 100 if p[2] else (0 if not q[2] else 100),
                 'len': abs(p[1] - q[1]),
                 'onset': abs((p[0] - a0) - (q[0] - b0)),
                 'gap': abs((p[0] - ta[x - 1][0]) - (q[0] - tb[x - 1][0])) if x else 0}
            lim = {'freq': a.freq, 'len': a.len, 'onset': a.onset, 'gap': a.gap}
            for key in d:
                worst[key] = max(worst[key], d[key])
                if d[key] > lim[key]:
                    print('segment %d tone %d: %s %.2f over %.2f: %.1f+%.1f ms %.2f Hz against %.1f+%.1f ms %.2f Hz'
                          % (k, x, key, d[key], lim[key], p[0] - a0, p[1], p[2], q[0] - b0, q[1], q[2]))
                    bad += 1
    print('tonecmp: %d segments, worst: frequency %.2f%%, length %.1f ms, onset %.1f ms, gap %.1f ms; %d differences'
          % (len(sa), worst['freq'], worst['len'], worst['onset'], worst['gap'], bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
