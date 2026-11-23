#!/usr/bin/env python3
"""sbvxdcmp - compare two runs of tests/sbvxdprobe.sh (the Sound Blaster's
VxD in 386 Enhanced mode, ARCHITECTURE.md 24.6): Windows 3.1 with
SNDBLST2.DRV and its VSBD.386 (REF) and w16's WIN386 with SB16.DRV and
w16's VSBD.386 (W16), the same script.

- SBD.COM's log (sbd.log): the same lines, the ticks masked and the counts
  read back within 40h (sbbox: the box plays; sbwin: the box finds no
  card while Windows plays);
- VXB.386's lines (com1.txt): the card's ports someone traps
  (Install_IO_Handler's carry) and whether the IRQ is virtualised
  (VPICD_Get_IRQ_Complete_Status's carry) at Init_Complete: equal;
- the tones of 150 ms and more (tones.txt): the same sequence, within 1%
  in frequency and 15% in length (sbbox);
- with -s SHOT: that screenshot (run/SHOT.png, sbwin's 03: the contention
  message box) equal but for the bottom-left corner, where the box's icon
  differs (w16's WINOA386 draws its own).
usage: sbvxdcmp.py [-s SHOT] REFDIR W16DIR
"""
import re
import sys


def sbd(d):
    out = []
    for line in open(d + '/sbd.log', errors='replace'):
        line = re.sub(r'(ticks|t)=[0-9A-F]+', r'\1=*', line.strip())
        out.append(line)
    return out


def close(a, b):
    ma, mb = re.match(r'(.*cnt=)([0-9A-F]+)(.*)', a), re.match(r'(.*cnt=)([0-9A-F]+)(.*)', b)
    if ma and mb and ma.group(1) == mb.group(1) and ma.group(3) == mb.group(3):
        return abs(int(ma.group(2), 16) - int(mb.group(2), 16)) <= 0x40
    return a == b


def vxb(d):
    keep = []
    for line in open(d + '/com1.txt', errors='replace'):
        m = re.match(r'VXM (Install_IO_Handler \S+: cf \S+)', line)
        if m:
            keep.append(m.group(1))
        m = re.match(r'VXM IRQ (\S+) status 20206369: ecx \S+ cf (\S+)', line)
        if m:
            keep.append('IRQ %s virtualised %s' % m.group(1, 2))
    return keep


def tones(d):
    t = []
    for line in open(d + '/tones.txt', errors='replace'):
        f = line.split()
        if len(f) < 5 or f[0] != 'tone' or float(f[3]) < 150:
            continue
        fr, dur = float(f[4]), float(f[3])
        if t and abs(t[-1][0] - fr) < 0.01 * fr:
            t[-1][1] += dur
        else:
            t.append([fr, dur])
    return t


def shot(a, b):
    from PIL import Image
    ia, ib = Image.open(a).convert('RGB'), Image.open(b).convert('RGB')
    if ia.size != ib.size:
        return 'sizes %s against %s' % (ib.size, ia.size)
    w, h = ia.size
    pa, pb = ia.load(), ib.load()
    n = sum(1 for y in range(h) for x in range(w)
            if not (x < 130 and y > h - 80) and pa[x, y] != pb[x, y])
    return '%d pixels differ' % n if n else None


def main():
    args = sys.argv[1:]
    sh = None
    if args[:1] == ['-s']:
        sh, args = args[1], args[2:]
    ref, w16 = args
    bad = []
    a, b = sbd(ref), sbd(w16)
    if len(a) != len(b) or not all(close(x, y) for x, y in zip(a, b)):
        bad.append('SBD: %s against %s' % (' | '.join(b), ' | '.join(a)))
    va, vb = vxb(ref), vxb(w16)
    if not va:
        bad.append('no VXB lines in the reference')
    elif va != vb:
        d = [y for x, y in zip(va, vb) if x != y][:3]
        bad.append('VXB: %s (%d against %d lines)' % ('; '.join(d), len(vb), len(va)))
    ta, tb = tones(ref), tones(w16)
    if len(ta) != len(tb) or any(abs(x[0] - y[0]) > 0.01 * x[0] or abs(x[1] - y[1]) > max(60, 0.15 * x[1])
                                 for x, y in zip(ta, tb)):
        bad.append('tones: %s against %s' % (' '.join('%.0f/%.0f' % tuple(x) for x in tb),
                                             ' '.join('%.0f/%.0f' % tuple(x) for x in ta)))
    if sh:
        r = shot('%s/run/%s.png' % (ref, sh), '%s/run/%s.png' % (w16, sh))
        if r:
            bad.append('shot %s: %s' % (sh, r))
    if bad:
        print('; '.join(bad))
        return 1
    print('%d SBD lines, %d VXB results, %d tones as Windows 3.1\'s%s' % (
        len(a), len(va), len(ta), ', shot %s as 3.1\'s' % sh if sh else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
