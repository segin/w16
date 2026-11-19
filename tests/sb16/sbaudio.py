#!/usr/bin/env python3
"""sbaudio - the sound w16's SB16.DRV makes, against what it was given
(tests/sb16/sbaudio.sh; ARCHITECTURE.md 24.4).

  sbaudio.py gen DIR      writes the test material into DIR: T*.WAV (each
                          format of FORMATS: a rising tone sweep on the
                          left channel, a falling one on the right, no
                          silence inside), SCALE.MID (a format 0 MIDI file
                          of four notes for MCISEQ), SBP.TXT (the script of
                          tests/sb16/sbp.c) and manifest.txt (what the
                          recording must hold, in order)
  sbaudio.py cmp CAP.wav DIR [MANIFEST]
                          reads DOSBox-X's recording (DX-CAPTURE /A: 16-bit
                          stereo), cuts it where it is silent, and checks
                          each piece against the manifest (below); exit
                          status 0 when every check passes

The checks, per manifest line:
  skip N                  N pieces not checked (MMSYSTEM's start-up sound)
  rest                    whatever comes after is not checked (Microsoft's
                          MMSYSTEM plays SystemExit when Windows ends)
  wav FILE LOOPS [GAIN_DB]  the piece lasts the file's length times LOOPS
                          (within 15 ms); the file resampled to the
                          recording's rate (linear interpolation), aligned
                          to the sample (cross-correlation), correlates with
                          it at 0.995 or more on each channel (0.99 for
                          8-bit files), the left channel with the left;
                          the level within 3 dB of GAIN_DB (default: that
                          of the first file) relative to the first file.
                          At the recording's own rate the residual after
                          the gain is printed too (0 is sample-accurate)
  wavr FILE               a sound of Windows 3.1's (CHIMES.WAV, DING.WAV):
                          the same but for the length, at least 75% of the
                          file's (its quiet end counts as silence)
  total MS                the pieces left last at least MS milliseconds
                          in all (QuickTime's sample movie's sound track:
                          nothing to compare it with)
  notes K1 K2 ...         one piece per MIDI note, its pitch (zero
                          crossings over its middle) within 1% of the
                          note's (the sine SoundFont of tools/mksf2.py
                          plays every note at its true pitch)
Recorded input (rec) makes no sound and is checked from SBP's log only.
Sources: the RIFF WAVE and Standard MIDI File layouts (Multimedia
Programmer's Guide; the MIDI 1.0 specification); nothing else.
"""
import math
import os
import struct
import sys

# name, channels, rate, bits, seconds
FORMATS = [
    ('T1.WAV', 1, 11025, 8, 1.0),
    ('T2.WAV', 2, 22050, 8, 1.0),
    ('T3.WAV', 1, 22050, 16, 1.0),
    ('T4.WAV', 2, 44100, 16, 1.0),
    ('T5.WAV', 1, 8000, 16, 0.8),
    ('T6.WAV', 1, 44100, 8, 0.6),
    ('T7.WAV', 2, 32000, 16, 0.8),
    ('T8.WAV', 1, 22050, 16, 0.25),
]
NOTES = [60, 64, 67, 72, 69, 65, 62]           # sbp.c's "notes"
MIDNOTES = [72, 71, 69, 67]                    # SCALE.MID
LOOPS = 3


def sweep(t, f0, f1, dur):
    """a logarithmic sweep's value at time t"""
    k = math.log(f1 / f0) / dur
    return math.sin(2 * math.pi * f0 * (math.exp(k * t) - 1) / k)


def signal(ch, rate, dur):
    n = int(rate * dur)
    out = []
    for i in range(n):
        t = i / rate
        l = 0.45 * sweep(t, 200, 1500, dur) + 0.2 * math.sin(2 * math.pi * 330 * t)
        r = 0.45 * sweep(t, 1500, 200, dur) + 0.2 * math.sin(2 * math.pi * 523 * t)
        out.append((l, r) if ch == 2 else (l,))
    return out


def writewav(path, ch, rate, bits, frames):
    data = bytearray()
    for fr in frames:
        for v in fr:
            if bits == 8:
                data.append(max(0, min(255, int(round(128 + v * 127)))))
            else:
                data += struct.pack('<h', max(-32768, min(32767, int(round(v * 32767)))))
    align = ch * bits // 8
    fmt = struct.pack('<HHIIHH', 1, ch, rate, rate * align, align, bits)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + bytes(data)
    if len(data) & 1:
        body += b'\0'
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)


def readwav(path):
    """(channels, rate, bits, [[samples per channel as floats -1..1]])"""
    d = open(path, 'rb').read()
    assert d[:4] == b'RIFF' and d[8:12] == b'WAVE', path
    p, fmt, data = 12, None, None
    while p + 8 <= len(d):
        cid, size = d[p:p + 4], struct.unpack_from('<I', d, p + 4)[0]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', d, p + 8)
        elif cid == b'data':
            data = d[p + 8:p + 8 + size]
        p += 8 + size + (size & 1)
    tag, ch, rate, _, align, bits = fmt
    n = len(data) // align
    chans = [[] for _ in range(ch)]
    if bits == 8:
        for i in range(n):
            for c in range(ch):
                chans[c].append((data[i * align + c] - 128) / 128.0)
    else:
        vals = struct.unpack('<%dh' % (n * ch), data[:n * align])
        for c in range(ch):
            chans[c] = [v / 32768.0 for v in vals[c::ch]]
    return ch, rate, bits, chans


def writemid(path, keys):
    """format 0, 96 ticks a quarter, 120 bpm: each key 250 ms on, 125 off"""
    ev = bytearray(b'\x00\xff\x51\x03\x07\xa1\x20')     # tempo 500000
    ev += b'\x00\xc0\x00'                               # program 0, channel 1
    for i, k in enumerate(keys):
        ev += bytes([0x18 if i else 0, 0x90, k, 100, 0x30, 0x80, k, 0])
    ev += b'\x18\xff\x2f\x00'
    hdr = b'MThd' + struct.pack('>IHHH', 6, 0, 1, 96)
    open(path, 'wb').write(hdr + b'MTrk' + struct.pack('>I', len(ev)) + bytes(ev))


def gen(d):
    os.makedirs(d, exist_ok=True)
    script, man = [], ['skip 1']             # MMSYSTEM's start-up sound
    for name, ch, rate, bits, dur in FORMATS:
        writewav(os.path.join(d, name), ch, rate, bits, signal(ch, rate, dur))
    writemid(os.path.join(d, 'SCALE.MID'), MIDNOTES)
    # every format, cut into 4 headers
    for name, ch, rate, bits, dur in FORMATS[:7]:
        script += ['play C:\\SB16T\\%s 4' % name, 'pause 600']
        man.append('wav %s 1' % name)
    # one header looped
    script += ['loop C:\\SB16T\\T8.WAV %d' % LOOPS, 'pause 600']
    man.append('wav T8.WAV %d' % LOOPS)
    # the wave volume through the mixer: C000 is level 24 of 31, 2 dB a
    # step on the SB16's mixer (Creative's guide): 14 dB down
    script += ['vol C000 C000', 'play C:\\SB16T\\T3.WAV 2', 'vol FFFF FFFF', 'pause 600']
    man.append('wav T3.WAV 1 -14')
    # Windows 3.1's own sounds (the reference install's, copied into DIR
    # at test time by sbaudio.sh): CHIMES.WAV through MCI (MCIWAVE, auto-
    # opened), DING.WAV through sndPlaySound
    script += ['mci play C:\\WINDOWS\\CHIMES.WAV wait', 'pause 600',
               'snd C:\\WINDOWS\\DING.WAV', 'pause 600']
    man += ['wavr CHIMES.WAV', 'wavr DING.WAV']
    # recording: no sound
    script += ['rec 1000 11025 8 1', 'rec 500 22050 16 2', 'pause 300']
    # MIDI through the MPU-401
    script += ['notes', 'pause 600']
    man.append('notes ' + ' '.join(map(str, NOTES)))
    script += ['mci open C:\\SB16T\\SCALE.MID type sequencer alias s', 'mci set s port 0',
               'mci play s wait', 'mci close s', 'pause 600']
    man.append('notes ' + ' '.join(map(str, MIDNOTES)))
    # Windows' end: Microsoft's MMSYSTEM plays SystemExit (w16's does not)
    man.append('rest')
    open(os.path.join(d, 'SBP.TXT'), 'w', newline='\r\n').write('\n'.join(script) + '\n')
    open(os.path.join(d, 'manifest.txt'), 'w').write('\n'.join(man) + '\n')
    # QuickTime for Windows' sample movie through its MCI driver (MCIQTW,
    # on a drive QuickTime's installer set up: sbaudio.sh's qt sides): its
    # sound track through SB16.DRV: about 4.5 s of sound in all (a phrase
    # of 1.4 s, short beats a second apart, a closing phrase of 1.9 s, the
    # same under either MMSYSTEM)
    qt = ['mci open C:\\WINDOWS\\SAMPLE.MOV alias q', 'mci play q wait', 'mci close q', 'pause 600']
    open(os.path.join(d, 'SBPQT.TXT'), 'w', newline='\r\n').write('\n'.join(qt) + '\n')
    open(os.path.join(d, 'qtmanifest.txt'), 'w').write('skip 1\ntotal 4000\n')


# ---- the recording ----
def pieces(chans, rate, thresh=0.01, gap_ms=60):
    """[(start, end)] in samples where it is not silent"""
    n = rate // 200                                    # 5 ms frames
    act = []
    for i in range(0, len(chans[0]) - n, n):
        e = 0.0
        for c in chans:
            f = c[i:i + n]
            m = sum(f) / n
            e = max(e, math.sqrt(sum((v - m) ** 2 for v in f) / n))
        act.append(e > thresh)
    out, cur, last = [], None, None
    gap = gap_ms * rate // 1000 // n
    for k, a in enumerate(act):
        if not a:
            continue
        if cur is not None and k - last <= gap:
            last = k
            continue
        if cur is not None:
            out.append((cur * n, (last + 1) * n))
        cur = last = k
    if cur is not None:
        out.append((cur * n, (last + 1) * n))
    return out


def resample(x, r0, r1, n):
    out = []
    step = r0 / r1
    for i in range(n):
        p = i * step
        k = int(p)
        if k + 1 >= len(x):
            out.append(x[-1] if k < len(x) else 0.0)
        else:
            f = p - k
            out.append(x[k] * (1 - f) + x[k + 1] * f)
    return out


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def align(cap, ref, start, search, rate):
    """the offset into cap where ref[0] lies: the piece's start less where
    the reference itself starts to sound (the same rule as pieces()),
    refined within SEARCH samples by the normalised correlation of 100 ms
    around that start, 20 ms of the silence before it included: the
    attack tells one period of a steady tone from the next (DING.WAV's),
    and the silence before it a shifted fit (an unnormalised product over
    the loudest stretch, or from sample 0 over CHIMES.WAV's quiet start,
    picked the wrong one)"""
    on = pieces([ref], rate)
    o0 = on[0][0] if on else 0
    guess = start - o0
    p = max(0, o0 - rate // 50)
    w = min(len(ref) - p, rate // 10)
    r = ref[p:p + w]
    best, bo = None, guess
    for st, lo, hi in ((4, guess - search, guess + search), (1, None, None)):
        if lo is None:
            lo, hi = bo - 4, bo + 4
        for o in range(max(0, lo), hi + 1, st):
            seg = cap[o + p:o + p + w]
            if len(seg) < w:
                break
            v = corr(seg, r)[0]
            if best is None or v > best:
                best, bo = v, o
    return bo


def corr(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    sab = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    saa = sum((x - ma) ** 2 for x in a)
    sbb = sum((y - mb) ** 2 for y in b)
    return sab / math.sqrt(saa * sbb) if saa and sbb else 0.0, math.sqrt(saa / n), math.sqrt(sbb / n)


def pitch(x, rate):
    n = len(x)
    seg = x[n // 10: n - n // 10]
    m = sum(seg) / len(seg) if seg else 0.0
    t = [i - 1 + (m - seg[i - 1]) / (seg[i] - seg[i - 1])
         for i in range(1, len(seg)) if seg[i - 1] < m <= seg[i]]
    # rising crossings, placed between the samples: cycles over the time
    return (len(t) - 1) * rate / (t[-1] - t[0]) if len(t) > 2 else 0.0


def cmp(cap, d, manifest='manifest.txt'):
    ch, rate, bits, chans = readwav(cap)
    if ch == 1:
        chans = [chans[0], chans[0]]
    ps = pieces(chans, rate)
    man = [l.split() for l in open(os.path.join(d, manifest)) if l.strip()]
    need = sum(int(m[1]) if m[0] == 'skip' else len(m) - 1 if m[0] == 'notes' else
               0 if m[0] in ('rest', 'total') else 1 for m in man)
    rest = man and man[-1][0] in ('rest', 'total')
    print('recording: %d Hz, %.1f s, %d pieces (%d expected)' % (rate, len(chans[0]) / rate, len(ps), need))
    fails = 0
    if len(ps) != need and not (rest and len(ps) > need):
        for s, e in ps:
            print('  piece at %.3f s, %.0f ms' % (s / rate, (e - s) * 1000 / rate))
        print('FAIL the pieces: %d, expected %d' % (len(ps), need))
        return 1
    k = 0
    ref_level = None
    for m in man:
        if m[0] == 'skip':
            k += int(m[1])
        elif m[0] == 'total':
            got = sum(e - s for s, e in ps[k:]) * 1000 / rate
            ok = got >= int(m[1])
            print('%s sound: %.0f ms in %d pieces (at least %s)' % ('ok  ' if ok else 'FAIL', got,
                                                                  len(ps) - k, m[1]))
            k = len(ps)
            fails += not ok
        elif m[0] in ('wav', 'wavr'):
            name, loops = m[1], int(m[2]) if len(m) > 2 else 1
            want_db = float(m[3]) if len(m) > 3 else 0.0
            sch, srate, sbits, src = readwav(os.path.join(d, name))
            s, e = ps[k]
            k += 1
            src = [c * loops for c in src]
            if sch == 1:
                src = [src[0], src[0]]
            n = int(len(src[0]) * rate / srate)
            ref = [resample(c, srate, rate, n) for c in src]
            o = align(chans[0], ref[0], s, rate // 30, rate)
            dur = len(src[0]) / srate * 1000
            got = (e - s) * 1000 / rate
            res = []
            if m[0] == 'wavr':
                # a recorded sound's quiet end is cut by the silence
                # detection (CHIMES.WAV's last 40 ms, DING.WAV's 100)
                ok = 0.75 * dur <= got <= dur + 15
            else:
                ok = abs(got - dur) <= 15
            lim = 0.99 if sbits == 8 else 0.995
            lv = []
            for c in range(2):
                cc, rc, rr = corr(chans[c][o:o + n], ref[c])
                lv.append(20 * math.log10(rc / rr) if rc and rr else -99)
                res.append('%s %.4f' % ('LR'[c], cc))
                ok = ok and cc >= lim
            if sch == 2:
                xc, _, _ = corr(chans[0][o:o + n], ref[1])
                res.append('L~R %.3f' % xc)
                ok = ok and xc < 0.5
            gain = sum(lv) / 2
            if ref_level is None:
                ref_level = gain
            rel = gain - ref_level
            ok = ok and abs(rel - want_db) <= 3
            extra = ''
            if srate == rate:
                g = dot(chans[0][o:o + n], ref[0]) / dot(ref[0], ref[0])
                err = max(abs(chans[0][o + i] - g * ref[0][i]) for i in range(n))
                extra = ', residual %.5f' % err
            print('%s wav %s x%d: %.0f ms (%.0f), %s, level %+.1f dB (%+.0f)%s' % (
                'ok  ' if ok else 'FAIL', name, loops, got, dur, ' '.join(res), rel, want_db, extra))
            fails += not ok
        elif m[0] == 'notes':
            for key in map(int, m[1:]):
                s, e = ps[k]
                k += 1
                f = pitch(chans[0][s:e], rate)
                want = 440.0 * 2 ** ((key - 69) / 12)
                # (MCISEQ's first note comes out 50-100 ms short with
                # either MMSYSTEM: its timing, not the driver's)
                ok = abs(f / want - 1) <= 0.01 and (e - s) * 1000 / rate >= 100
                print('%s note %d: %.1f Hz (%.1f), %.0f ms' % ('ok  ' if ok else 'FAIL', key, f, want,
                                                             (e - s) * 1000 / rate))
                fails += not ok
    print('sbaudio: %d failure(s)' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    if sys.argv[1] == 'gen':
        gen(sys.argv[2])
    elif sys.argv[1] == 'cmp':
        sys.exit(cmp(*sys.argv[2:5]))
