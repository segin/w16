#!/usr/bin/env python3
"""wavtones - the PC speaker's tones in a WAV recording (DOSBox-X's
DX-CAPTURE /A, tests/soundprobe.sh).

The speaker gives a square wave: each change of its level is a steep
edge in the recording (DOSBox-X spreads it over 2-3 samples), and between
edges the level only drifts back towards zero (the output's DC filter),
so silence after a tone is a slow slope, not a tone. This finds the
edges (a change of more than THRESH between samples two apart), groups
edges no more than GAP ms apart into tones and prints, for each, its
start (ms from the start of the recording, and from the first tone), its
length in ms (first edge to last edge plus one half-period) and its
frequency (the mean period over the whole tone, from the edge times
interpolated between samples). With --split a tone is also cut where the
half-period changes by more than 25% (a new note with no silence before
it).

usage: wavtones.py [--gap MS] [--thresh N] [--split] [--from MS] FILE.wav
Output, one line per tone:
  tone START_MS REL_MS LEN_MS FREQ_HZ divisor N edges E
where divisor is 1193182 / FREQ rounded: the 8254 count for that tone,
and E the number of edges (a tone of fewer than 3 is a click: its
frequency is 0).
"""
import argparse
import struct
import sys
import wave

PIT = 1193182


def raw_samples(path):
    """A capture whose writer was ended before it closed the file: the
    sizes in its header are not filled in; the data runs to the end."""
    b = open(path, 'rb').read()
    i = b.find(b'fmt ')
    if b[:4] != b'RIFF' or i < 0:
        sys.exit('%s: not a WAV file' % path)
    _, ch, rate, _, _, bits = struct.unpack('<HHIIHH', b[i + 8:i + 24])
    j = b.find(b'data', i)
    d = b[j + 8:]
    d = d[:len(d) - len(d) % (2 * ch)]
    if bits != 16:
        sys.exit('16-bit samples only')
    return list(struct.unpack('<%dh' % (len(d) // 2), d)[::ch]), rate


def samples(path):
    try:
        w = wave.open(path)
    except (wave.Error, EOFError):
        return raw_samples(path)
    n, ch, sw, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    if sw != 2:
        sys.exit('16-bit samples only')
    out = []
    while True:
        d = w.readframes(1 << 20)
        if not d:
            break
        a = struct.unpack('<%dh' % (len(d) // 2), d)
        out.extend(a[::ch])
    return out, rate


def edges(s, thresh):
    """Edge times (fractional sample index: where the signal crosses the
    middle of the step)."""
    out = []
    i, n = 0, len(s) - 3
    while i < n:
        if abs(s[i + 2] - s[i]) > thresh:
            # the step: from s[i] to where it stops moving
            a = i
            b = i + 2
            while b + 1 < len(s) and abs(s[b + 1] - s[b]) > thresh // 8 and \
                    (s[b + 1] - s[b]) * (s[i + 2] - s[i]) > 0:
                b += 1
            lo, hi = s[a], s[b]
            mid = (lo + hi) / 2
            t = a
            for k in range(a, b):
                p, q = s[k] - mid, s[k + 1] - mid
                if (p <= 0 < q) or (p >= 0 > q):
                    t = k + (p / (p - q) if p != q else 0)
                    break
            out.append(t)
            i = b + 1
        else:
            i += 1
    return out


def tones(s, rate, gap_ms, thresh, split):
    gap = rate * gap_ms / 1000
    ed = edges(s, thresh)
    groups, cur = [], []
    for t in ed:
        if cur and t - cur[-1] > gap:
            groups.append(cur)
            cur = []
        cur.append(t)
    if cur:
        groups.append(cur)
    if split:
        out = []
        for g in groups:
            start = 0
            for k in range(2, len(g)):
                h0, h1 = g[k - 1] - g[k - 2], g[k] - g[k - 1]
                if h0 > 0 and abs(h1 - h0) / h0 > 0.25 and k - 1 - start > 2:
                    out.append(g[start:k - 1 + 1])
                    start = k
            out.append(g[start:])
        groups = out
    res = []
    for g in groups:
        f = 0.0
        half = 0.0
        if len(g) >= 3:
            half = (g[-1] - g[0]) / (len(g) - 1)
            f = rate / (2 * half)
        res.append((g[0] / rate * 1000, (g[-1] - g[0] + half) / rate * 1000, f, len(g)))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gap', type=float, default=30)
    ap.add_argument('--thresh', type=int, default=2000)
    ap.add_argument('--split', action='store_true')
    ap.add_argument('--from', dest='frm', type=float, default=0)
    ap.add_argument('file')
    a = ap.parse_args()
    s, rate = samples(a.file)
    t = [x for x in tones(s, rate, a.gap, a.thresh, a.split) if x[0] >= a.frm]
    t0 = t[0][0] if t else 0
    for st, ln, f, ne in t:
        div = round(PIT / f) if f else 0
        print('tone %10.1f %9.1f %8.1f %8.2f divisor %5d edges %d' % (st, st - t0, ln, f, div, ne))


if __name__ == '__main__':
    main()
