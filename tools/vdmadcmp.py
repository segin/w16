#!/usr/bin/env python3
"""vdmadcmp - compare two runs of tests/vdmadprobe.sh (the Virtual DMA
Device, ARCHITECTURE.md 12.8 VDMAD): Windows 3.1's WIN386 (REF) and
w16's (W16), for the boot-test case vdmad.

SBD.COM's log (sbd.log): the same lines in the same order, with what
depends on where the box's memory is masked (the 16-bit address read
back, the physical addresses VDS gives, the place 'b' found) and the
counts read back at each interrupt allowed to differ by up to 400h bytes
(they race the card); the status read at each interrupt, the interrupt
count, VDS's registers and error codes must be equal.

The recording (tones.txt, tools/wavtones.py --split): the tones of 150 ms
and more, neighbours of one frequency merged, must be the same sequence
on both sides: each within 1% in frequency and 15% (at least 60 ms) in
length.

VXM.386's lines (com1.txt): which ports someone traps (Install_IO_
Handler's carry), VDMAD_Virtualize_Channel's carry, Get_EISA_Adr_Mode,
the translation counts, the buffer's and Lock_DMA_Region's error codes
and Scatter_Lock's flags and counts: equal; the addresses are not
compared. Ports and results that differ only because Windows 3.1 has
devices w16 lacks are listed in KNOWN.

usage: vdmadcmp.py REFDIR W16DIR
Exit status 0 when the two agree.
"""
import re
import sys

# results that differ because of devices or memory w16 does not have:
# Windows 3.1's VFD owns channel 2; its VDD maps video memory, so the page
# at A0000h is not contiguous with 9F000h in its System VM
KNOWN = [
    re.compile(r'Virtualize_Channel 02:'),
    re.compile(r'Lock_DMA_Region 0009F000'),
]


def sbd(d):
    out = []
    for line in open(d + '/sbd.log', errors='replace'):
        line = line.strip()
        line = re.sub(r'ticks=[0-9A-F]+', 'ticks=*', line)
        line = re.sub(r' t=[0-9A-F]+', ' t=*', line)
        line = re.sub(r'^rb16 adr=[0-9A-F]+', 'rb16 adr=*', line)
        line = re.sub(r'^SBD b at [0-9A-F]+', 'SBD b at *', line)
        m = re.match(r'(vds \w+: .* dds=)(\S+) (\S+) (\S+) (\S+) (\S+) (\S+) (\S+) (\S+)$', line)
        if m:   # the DDS: size, offset kept; segment, the physical address not
            w = m.group(2, 3, 4, 5, 6, 7, 8, 9)
            line = m.group(1) + ' '.join([w[0], w[1], w[2], w[3], '*', w[5], '*', '*'])
        if line.startswith('table='):
            line = 'table=' + ' '.join('*' if i % 4 == 0 or i % 4 == 1 else x
                                       for i, x in enumerate(line[6:].split()))
        out.append(line)
    return out


def counts_close(a, b):
    ma = re.match(r'(irq \S+ cnt=)([0-9A-F]+)( .*)', a)
    mb = re.match(r'(irq \S+ cnt=)([0-9A-F]+)( .*)', b)
    if not ma or not mb:
        return a == b
    if ma.group(1) != mb.group(1) or ma.group(3) != mb.group(3):
        return False
    return abs(int(ma.group(2), 16) - int(mb.group(2), 16)) <= 0x400


def tones(d):
    t = []
    for line in open(d + '/tones.txt', errors='replace'):
        f = line.split()
        if len(f) < 5 or f[0] != 'tone':
            continue
        dur, fr = float(f[3]), float(f[4])
        if dur < 150 or fr <= 0:
            continue
        if t and abs(t[-1][0] - fr) < 0.01 * fr:
            t[-1][1] += dur
        else:
            t.append([fr, dur])
    return t


def vxm(d):
    keep = []
    for line in open(d + '/com1.txt', errors='replace'):
        line = line.strip()
        if not line.startswith('VXM '):
            continue
        line = line[4:]
        if any(k.search(line) for k in KNOWN):
            continue
        m = re.match(r'Install_IO_Handler (\S+): cf (\S+)', line)
        if m:
            keep.append(line)
            continue
        m = re.match(r'(Virtualize_Channel \S+: cf \S+)', line)
        if m:
            keep.append(m.group(1))
            continue
        if line.startswith('Get_EISA_Adr_Mode') or 'Translation' in line:
            keep.append(line)
            continue
        m = re.match(r'(\s*(Lock_DMA_Region|Request_Buffer|Copy_\w+|Release_Buffer)[^:]*): cf (\S+)( al (\S+))?', line)
        if m and 'Lock_DMA_Region 0009' in line or m and 'Lock_DMA_Region 0008' in line:
            keep.append(line)
            continue
        if m:
            name = re.sub(r'[0-9A-F]{8}', '*', m.group(1))
            keep.append('%s: cf %s al %s' % (name, m.group(3), m.group(5) or '-'))
            continue
        m = re.match(r'(\s*Scatter_\w+[^:]*): cf (\S+)( zf (\S+) edx (\S+) esi (\S+) size (\S+) used (\S+))?', line)
        if m:
            keep.append(' '.join(x for x in m.group(1, 2, 4, 5, 6, 7, 8) if x))
            continue
        if line.startswith('Get_Version'):
            keep.append(re.sub(r'Get_Version \S+', 'Get_Version', line))
    return keep


def main():
    ref, w16 = sys.argv[1], sys.argv[2]
    bad = []
    a, b = sbd(ref), sbd(w16)
    if len(a) != len(b):
        bad.append('SBD: %d lines against %d' % (len(b), len(a)))
    for x, y in zip(a, b):
        if not counts_close(x, y):
            bad.append('SBD: %r for %r' % (y, x))
            break
    ta, tb = tones(ref), tones(w16)
    if len(ta) != len(tb):
        bad.append('tones: %d against %d (%s / %s)' % (len(tb), len(ta),
                   ' '.join('%.0f' % f for f, _ in tb), ' '.join('%.0f' % f for f, _ in ta)))
    else:
        for (fa, da), (fb, db) in zip(ta, tb):
            if abs(fa - fb) > 0.01 * fa or abs(da - db) > max(60, 0.15 * da):
                bad.append('tone %.1f Hz %.0f ms against %.1f Hz %.0f ms' % (fb, db, fa, da))
                break
    if not ta:
        bad.append('no tones in the reference')
    va, vb = vxm(ref), vxm(w16)
    if not va:
        bad.append('no VXM lines in the reference')
    for x, y in zip(va, vb):
        if x != y:
            bad.append('VXM: %r for %r' % (y, x))
            break
    if len(va) != len(vb):
        bad.append('VXM: %d lines against %d' % (len(vb), len(va)))
    if bad:
        print('; '.join(bad))
        return 1
    print('%d SBD lines, %d tones, %d VXM results as Windows 3.1\'s' % (len(a), len(ta), len(va)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
