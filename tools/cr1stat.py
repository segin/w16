#!/usr/bin/env python3
"""cr1stat - what CR1 (tests/apps/cr1.c, tests/currec.sh) recorded.

usage: cr1stat.py OUTDIR        (its cr1.txt, and CR1.BIN when there)
       cr1stat.py --check REFDIR TESTDIR   (exit 1 when they differ; see
                                           check())

Prints, for SetCursor (102), MoveCursor (103) and CheckCursor (104): the
calls, their rate over the time they span, the callers (module CS:IP)
with interrupts on or off, and for MoveCursor the spacing of the calls
in ms (from the BIOS tick and the PIT count: 1193182 Hz, counting down
from 65536 a tick) and how many CheckCursor calls fell between moves.
With frames: for each kept frame (one a timer message, when the screen
changed) the arrow pointers in it, counted by their tips (an arrow's
first two rows: two dark pixels, then dark, light, dark, on a lighter
background above and to the left), and over the stretch after the last
move, how the count fell.
"""
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vg9png  # noqa: E402


def recs(path):
    out = []
    for line in open(path):
        f = line.split()
        if f and f[0] == 'rec':
            tick = int(f[2], 16)
            pit = int(f[3])
            t = (tick + (65536 - pit) / 65536.0) * 54.925
            out.append(dict(ord=int(f[1]), t=t, caller='%s %s' % (f[5], f[4]),
                            a0=int(f[6]), a1=int(f[7]), ifl=int(f[8])))
    return out


def tips(px, h):
    """arrow tips in a frame (indices of colours: 0 black, 15 white)"""
    n = 0
    for y in range(1, h - 2):
        row = y * 640
        for x in range(1, 637):
            if px[row + x] != 0 or px[row + x + 1] != 0:
                continue
            if px[row + 640 + x] != 0 or px[row + 640 + x + 1] != 15 or px[row + 640 + x + 2] != 0:
                continue
            if px[row - 640 + x] == 0 or px[row + x - 1] == 0:
                continue
            n += 1
    return n


def tip_xs(px, h):
    """the x of each arrow tip in a frame, sorted (see tips)"""
    out = []
    for y in range(1, h - 2):
        row = y * 640
        for x in range(1, 637):
            if px[row + x] != 0 or px[row + x + 1] != 0:
                continue
            if px[row + 640 + x] != 0 or px[row + 640 + x + 1] != 15 or px[row + 640 + x + 2] != 0:
                continue
            if px[row - 640 + x] == 0 or px[row + x - 1] == 0:
                continue
            out.append(x)
    return sorted(out)


def summary(d):
    """what --check compares: call rates, the trail's length in motion,
    the spacing of its images, and how it runs down when the pointer
    stops (runs of frames whose count falls one by one to 1)"""
    R = recs(os.path.join(d, 'cr1.txt'))
    out = {}
    for o in (103, 104):
        rs = [r for r in R if r['ord'] == o]
        out[o] = len(rs)
    counts, times, gaps = [], [], Counter()
    for f in vg9png.frames(os.path.join(d, 'CR1.BIN')):
        xs = tip_xs(vg9png.pixels(f), f['h'])
        counts.append(len(xs))
        times.append(f['ms'])
        for a, b in zip(xs, xs[1:]):
            gaps[b - a] += 1
    # run-downs: from the last frame of 6 or more images (the pointer
    # moving, or just stopped) to the first of one or none after it, in ms
    # (frames are taken at timer messages, which a loaded host skips now
    # and then: counted in frames they varied)
    runs, top = [], None
    for c, t in zip(counts, times):
        if c >= 6:
            top = t
        elif c <= 1 and top is not None:
            runs.append(t - top)
            top = None
    moving = sorted(c for c in counts if c >= 2)
    out['median'] = moving[len(moving) // 2] if moving else 0
    out['gaps'] = gaps
    out['runs'] = runs
    out['counts'] = counts
    return out


# a still trail's run-down from six images (or seven, a tick after the
# stop) to one: 10 ticks of 54.9 ms at an image every two ticks, give or
# take two ticks for the frames' phase
RUNDOWN = (440, 660)


def check(ref, test):
    """0 when the test run's trail is the reference's but for the phase of
    the ticks against the frames: MoveCursor and CheckCursor calls within
    10 percent, the trail's usual length in motion within one image (the
    frames are taken at timer messages, a move or none after the tick),
    gaps between neighbouring images of twice the usual gap (a position
    missing) at most twice the reference's share and 3 points, and as
    many run-downs, each 440 to 660 ms long (RUNDOWN): w16's trail runs
    down an image every two ticks, seven in about 600 ms, as 3.1's looked
    to the user booted interactively (drivers/vga/README.md); in the
    harness 3.1's driver drops one a tick, so its run-downs are printed
    but not the measure"""
    A, B = summary(ref), summary(test)
    bad = []
    # one move a PS/2 packet of the same xdotool stream, one tick a timer
    # tick: on a loaded host the tree once got a sixth fewer moves (w16's
    # MOUSE.DRV dropped the motion of packets that came while USER was
    # still busy with the last; drivers/mouse/README.md), so the moves are
    # counted too
    for o, n in ((103, 'MoveCursor'), (104, 'CheckCursor')):
        if abs(B[o] - A[o]) > A[o] / 10:
            bad.append('%s calls %d against %d' % (n, B[o], A[o]))
    if abs(B['median'] - A['median']) > 1:
        bad.append('trail length in motion %d against %d' % (B['median'], A['median']))
    # gaps: the share of gaps of twice the usual one or more (a position
    # missing from the trail) at most 3 points above the reference's
    g = A['gaps'].most_common(1)[0][0] if A['gaps'] else 0

    def big(G):
        tot = sum(G.values())
        return sum(n for k, n in G.items() if k >= 2 * g - 1) / tot if tot else 1.0
    # (a loaded host makes some on 3.1 too: DOSBox adds up the motion of
    # host events it is late with)
    if big(B['gaps']) > 2 * big(A['gaps']) + 0.03:
        bad.append('gaps of %d or more: %.1f%% against %.1f%%'
                   % (2 * g - 1, 100 * big(B['gaps']), 100 * big(A['gaps'])))
    if len(A['runs']) != len(B['runs']) or any(not RUNDOWN[0] <= b <= RUNDOWN[1] for b in B['runs']):
        bad.append('run-downs %s (ms; %d of them, each %d-%d ms expected)'
                   % (B['runs'], len(A['runs']), RUNDOWN[0], RUNDOWN[1]))
    print('reference: moves %d, ticks %d, length %d, gaps %s, run-downs %s'
          % (A[103], A[104], A['median'], sorted(A['gaps'].items()), A['runs']))
    print('test:      moves %d, ticks %d, length %d, gaps %s, run-downs %s'
          % (B[103], B[104], B['median'], sorted(B['gaps'].items()), B['runs']))
    for b in bad:
        print('DIFF ' + b)
    return 1 if bad else 0


def main():
    if sys.argv[1] == '--check':
        sys.exit(check(sys.argv[2], sys.argv[3]))
    d = sys.argv[1]
    R = recs(os.path.join(d, 'cr1.txt'))
    for o, name in ((102, 'SetCursor'), (103, 'MoveCursor'), (104, 'CheckCursor')):
        rs = [r for r in R if r['ord'] == o]
        if not rs:
            print('%-11s 0 calls' % name)
            continue
        span = (rs[-1]['t'] - rs[0]['t']) / 1000.0
        print('%-11s %d calls over %.1f s (%.1f/s)' % (name, len(rs), span, len(rs) / span if span else 0))
        for c, n in Counter((r['caller'], r['ifl']) for r in rs).most_common():
            print('    %5d from %s, interrupts %s' % (n, c[0], 'on' if c[1] else 'off'))
    mv = [r for r in R if r['ord'] == 103]
    if len(mv) > 1:
        gaps = sorted(b['t'] - a['t'] for a, b in zip(mv, mv[1:]))
        q = lambda p: gaps[min(len(gaps) - 1, int(p * len(gaps)))]
        print('MoveCursor spacing ms: min %.1f, 10%% %.1f, median %.1f, 90%% %.1f, max %.1f'
              % (gaps[0], q(.1), q(.5), q(.9), gaps[-1]))
        between = Counter()
        k = 0
        for r in R:
            if r['ord'] == 103:
                between[k] += 1
                k = 0
            elif r['ord'] == 104:
                k += 1
        print('CheckCursor calls between moves (during motion, gaps < 200 ms):',
              dict(sorted(Counter(min(b['t'] - a['t'], 999) // 55 for a, b in zip(mv, mv[1:])
                                  if b['t'] - a['t'] < 200).items())))
        per = defaultdict(int)
        for r in mv:
            per[int((r['t'] - mv[0]['t']) // 1000)] += 1
        print('MoveCursor per second:', [per[i] for i in range(max(per) + 1)])
    binp = os.path.join(d, 'CR1.BIN')
    if os.path.exists(binp):
        fr = list(vg9png.frames(binp))
        counts = [(f['ms'], tips(vg9png.pixels(f), f['h'])) for f in fr]
        print('frames %d; arrows per frame: %s' % (len(fr), Counter(c for _, c in counts).most_common()))
        print('frames (ms, arrows):', ' '.join('%d:%d' % c for c in counts))


if __name__ == '__main__':
    main()
