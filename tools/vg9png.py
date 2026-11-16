#!/usr/bin/env python3
"""vg9png - the frames VG9 (tests/apps/vg9.c) recorded in C:\\VG9.BIN.

Each frame: a 16-byte header ("FR", sequence, phase, first row, rows, the
time in ms since the probe started, padding; little-endian words and a
dword), then the band's four planes (80 bytes a row), plane 0 first.

usage: vg9png.py VG9.BIN OUTDIR      every frame as OUTDIR/PHASE-SEQ.png
       vg9png.py --cmp A.BIN B.BIN [DIFFDIR]
                                     the frames of each phase compared in
                                     order; the timings are not compared
                                     (the frames are the trail's states);
                                     exit status 1 when they differ
"""
import os
import struct
import sys

# the colours as the four plane bits give them (plane 0 blue, 1 green,
# 2 red, 3 intensity: VGA's default palette); for looking only
PAL = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0),
       (170, 0, 170), (170, 85, 0), (170, 170, 170), (85, 85, 85),
       (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85),
       (255, 85, 255), (255, 255, 85), (255, 255, 255)]


def frames(path):
    data = open(path, 'rb').read()
    i = 0
    while i + 16 <= len(data):
        tag, seq, phase, y0, h, ms, _ = struct.unpack_from('<2sHHHHIH', data, i)
        if tag != b'FR':
            raise SystemExit('%s: bad frame at %d' % (path, i))
        i += 16
        n = 80 * h * 4
        yield dict(seq=seq, phase=phase, y0=y0, h=h, ms=ms, bits=data[i:i + n])
        i += n


def pixels(f):
    h, b = f['h'], f['bits']
    pl = [b[p * 80 * h:(p + 1) * 80 * h] for p in range(4)]
    out = bytearray(640 * h)
    for y in range(h):
        for c in range(80):
            o = y * 80 + c
            v = [pl[p][o] for p in range(4)]
            for k in range(8):
                m = 0x80 >> k
                out[y * 640 + c * 8 + k] = sum(1 << p for p in range(4) if v[p] & m)
    return out


def image(f):
    from PIL import Image
    img = Image.new('RGB', (640, f['h']))
    img.putdata([PAL[i] for i in pixels(f)])
    return img


def main():
    a = sys.argv[1:]
    if a and a[0] == '--cmp':
        A = list(frames(a[1]))
        B = list(frames(a[2]))
        diffdir = a[3] if len(a) > 3 else None
        bad = 0
        for ph in sorted(set(f['phase'] for f in A) | set(f['phase'] for f in B)):
            fa = [f for f in A if f['phase'] == ph]
            fb = [f for f in B if f['phase'] == ph]
            sa = [f['bits'] for f in fa]
            sb = [f['bits'] for f in fb]
            if sa == sb:
                print('ok   phase %d: %d frames' % (ph, len(fa)))
                continue
            bad += 1
            k = next((j for j in range(min(len(sa), len(sb))) if sa[j] != sb[j]), min(len(sa), len(sb)))
            print('DIFF phase %d: %d frames against %d; first difference at frame %d of the phase'
                  % (ph, len(fa), len(fb), k))
            if diffdir and k < min(len(sa), len(sb)):
                os.makedirs(diffdir, exist_ok=True)
                image(fa[k]).save(os.path.join(diffdir, 'p%02d-%03d-a.png' % (ph, k)))
                image(fb[k]).save(os.path.join(diffdir, 'p%02d-%03d-b.png' % (ph, k)))
        sys.exit(1 if bad else 0)
    if a and a[0] == '--strip':
        # --strip BIN PHASES Y H OUT: rows Y..Y+H of every frame of the
        # phases (comma-separated), one below the other, a red line
        # between phases
        from PIL import Image
        phs = [int(p) for p in a[2].split(',')]
        y, h = int(a[3]), int(a[4])
        rows = []
        for ph in phs:
            fs = [f for f in frames(a[1]) if f['phase'] == ph]
            rows += [image(f).crop((0, y - f['y0'], 640, y - f['y0'] + h)) for f in fs]
            rows.append(Image.new('RGB', (640, 2), (255, 0, 0)))
        out = Image.new('RGB', (640, sum(r.height for r in rows)))
        k = 0
        for r in rows:
            out.paste(r, (0, k))
            k += r.height
        out.save(a[5])
        return
    if len(a) != 2:
        print(__doc__)
        sys.exit(2)
    os.makedirs(a[1], exist_ok=True)
    for f in frames(a[0]):
        image(f).save(os.path.join(a[1], 'p%02d-%04d.png' % (f['phase'], f['seq'])))


if __name__ == '__main__':
    main()
