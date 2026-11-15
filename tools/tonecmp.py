#!/usr/bin/env python3
"""tonecmp - compare two recordings' tones (tools/wavtones.py output),
the sound driver's against Windows 3.1's (tests/soundprobe.sh).

SN1 (tests/apps/sn1.c) puts markers on the recording: tones of divisor 400
(2983 Hz) lasting 110 ms, in runs of 1-4. The recordings are cut at the
markers into segments, and segment k of one is compared with segment k of
the other: the same number of tones, and for each tone
  - its frequency within FREQ percent (default 2, 3 above 2 kHz;
    FREQSHORT, default 20, for tones under 40 ms: a note's first half-period is longer or
    shorter by where the 8254's count was when the driver wrote it, and
    on a tone of a few periods that is a few percent),
  - its length within LEN ms (default 7: about a 5 ms tick and the
    measurement's half-period),
  - the time from the segment's first tone to its start within ONSET ms
    (default 7), and the time from the tone before within GAP ms
    (default 7); 8 ms more for tones above 3 kHz, 12 above 6 kHz (their
    edges are a few samples apart in a 48 kHz recording).
Tones shorter than --min ms (default 8: a tone of one 5 ms tick, which
the recording breaks up differently on each run) are not compared;
fragments of one tone (the same frequency within 3%, less than 10 ms
apart, 20 above 3 kHz: the recording breaks up some high notes) are
joined first, the short ones too; the marker
runs themselves must match.

usage: tonecmp.py [--freq P] [--freqshort P] [--len MS] [--onset MS] [--gap MS] [--min MS] A.txt B.txt
Exit status 0 when everything is within the limits; the worst deviation
of each kind is printed either way.
"""
import argparse
import sys


def near(t, st, fr):
    """t and a tone of frequency fr starting at st: fragments of one tone"""
    return fr and t[2] and abs(t[2] - fr) / fr < 0.03 and st - (t[0] + t[1]) < (10 if fr < 3000 else 20)


def load(path, minlen):
    tones = []
    for line in open(path):
        f = line.split()
        if not f or f[0] != 'tone':
            continue
        st, ln, fr, div = float(f[1]), float(f[3]), float(f[4]), int(f[6])
        if not fr and ln < minlen:
            continue                    # a click (under 3 edges)
        # fragments of one tone (a high note the recording breaks up):
        # the same frequency within 3%, less than 10 ms apart (20 above
        # 3 kHz). Joined before short tones are dropped: a note whose
        # first few ms the recording broke into pieces shorter than
        # --min keeps its start (dropped first, the tone started where
        # the first long piece did: tens of ms late, by where the
        # recording's samples fell on its edges)
        # (short pieces of another frequency between two such fragments,
        # a few edges the recording put elsewhere, are passed over)
        j = len(tones) - 1
        while j >= 0 and tones[j][1] < minlen and not near(tones[j], st, fr):
            j -= 1
        if j >= 0 and near(tones[j], st, fr):
            p = tones[j]
            del tones[j:]
            ln2 = st + ln - p[0]
            fr2 = (p[2] * p[1] + fr * ln) / (p[1] + ln)
            tones.append((p[0], ln2, fr2, round(1193182 / fr2)))
            continue
        tones.append((st, ln, fr, div))
    return [t for t in tones if t[1] >= minlen]


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
    ap.add_argument('--freq', type=float, default=2)
    ap.add_argument('--freqshort', type=float, default=20)
    ap.add_argument('--len', type=float, default=7)
    ap.add_argument('--onset', type=float, default=7)
    ap.add_argument('--gap', type=float, default=7)
    ap.add_argument('--min', type=float, default=8)
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
            # (a length is measured to half a period: more for low notes)
            half = 500 / p[2] if p[2] else 0
            # (above 2 kHz a half-period is a few samples of the 48 kHz
            # recording: the edges' times are coarser, and above 3 kHz the
            # recording loses or adds the odd edge at a tone's ends: its
            # start and length are known to about 2 ticks)
            fmax = max(p[2], q[2])
            hi = 0 if fmax < 3000 else 8 if fmax < 6000 else 12
            lim = {'freq': (a.freq if p[2] < 2000 else 1.5 * a.freq) if p[1] >= 40 else a.freqshort,
                   'len': a.len + half + hi, 'onset': a.onset + hi, 'gap': a.gap + hi}
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
