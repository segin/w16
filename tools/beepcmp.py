#!/usr/bin/env python3
"""beepcmp - compare the sound of two recordings of SN1's beep parts
(tests/soundprobe.sh beep; SN1 parts M N I R L Y Z, tests/apps/sn1.c),
whatever made it: the PC speaker or a sound card's wave output.

SN1 puts marker tones on the recording (2983 Hz, divisor 400, 110 ms,
in runs of 1-4) before each step. This finds them (tools/wavtones.py),
cuts the recording into segments after each run of markers and lists the
sound in each segment (tools/wavenv.py's stretches: 10 ms frames above
an RMS threshold, joined across gaps of up to 60 ms, the markers left
out), each stretch's start from the
segment's start (the end of its last marker) and its length. Two
recordings agree when they have the same number of segments, the same
number of stretches in each, and each stretch starts and lasts within TOL
ms (default 60: a wave's start follows the sound card's DMA and
interrupts) of the other's, its peak within a factor of 3.

usage: beepcmp.py [--tol MS] [--thresh N] A.wav B.wav
       beepcmp.py --list FILE.wav
Exit status 0 when they agree.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wavtones import samples, tones  # noqa: E402
from wavenv import stretches  # noqa: E402


def markers(s, rate):
    out = []
    for st, ln, fr, _ in tones(s, rate, 9, 2000, False):
        if 90 <= ln <= 130 and 2900 <= fr <= 3070:
            out.append((st, st + ln))
    return out


def segments(path, thresh):
    s, rate = samples(path)
    mk = markers(s, rate)
    # runs of markers: no more than 400 ms between one's end and the next
    runs = []
    for a, b in mk:
        if runs and a - runs[-1][1] <= 400:
            runs[-1][1] = b
            runs[-1][2] += 1
        else:
            runs.append([a, b, 1])
    segs = [{'at': r[1], 'n': r[2], 'snd': []} for r in runs]
    for st, ln, pk in stretches(s, rate, thresh, 60):
        en = st + ln
        # a stretch over a marker is the marker (10 ms frames: a little
        # either side)
        if any(st < b + 20 and en > a - 20 for a, b in mk):
            continue
        k = None
        for i, g in enumerate(segs):
            if g['at'] <= st:
                k = i
        if k is None:
            continue
        segs[k]['snd'].append((st - segs[k]['at'], ln, pk))
    return segs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tol', type=float, default=60)
    ap.add_argument('--thresh', type=float, default=300)
    ap.add_argument('--list', action='store_true')
    ap.add_argument('files', nargs='+')
    a = ap.parse_args()
    if a.list:
        for i, g in enumerate(segments(a.files[0], a.thresh)):
            print('segment %d at %.0f ms (%d markers): %s' % (
                i, g['at'], g['n'],
                ', '.join('%.0f+%.0f' % (st, ln) for st, ln, _ in g['snd']) or 'quiet'))
        return 0
    if len(a.files) != 2:
        ap.error('two recordings')
    x, y = segments(a.files[0], a.thresh), segments(a.files[1], a.thresh)
    bad = 0
    if len(x) != len(y):
        print('beepcmp: %d segments against %d' % (len(x), len(y)))
        bad += 1
    nsnd = 0
    for i, (p, q) in enumerate(zip(x, y)):
        sp, sq = p['snd'], q['snd']
        nsnd += len(sp)
        desc = lambda l: ', '.join('%.0f+%.0f' % (st, ln) for st, ln, _ in l) or 'quiet'
        if p['n'] != q['n'] or len(sp) != len(sq):
            print('segment %d: %d markers, %s against %d markers, %s' % (i, p['n'], desc(sp), q['n'], desc(sq)))
            bad += 1
            continue
        for (s1, l1, k1), (s2, l2, k2) in zip(sp, sq):
            if abs(s1 - s2) > a.tol or abs(l1 - l2) > a.tol or max(k1, k2) / max(1, min(k1, k2)) > 3:
                print('segment %d: %.0f+%.0f ms rms %.0f against %.0f+%.0f ms rms %.0f'
                      % (i, s1, l1, k1, s2, l2, k2))
                bad += 1
    print('beepcmp: %d segments, %d sounds; %d differences' % (len(x), nsnd, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
