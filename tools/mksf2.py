#!/usr/bin/env python3
"""mksf2 - write a SoundFont 2 file whose every instrument is a plain sine
wave (tests/sb16/sbaudio.sh: DOSBox-X's FluidSynth plays the MIDI that
w16's SB16.DRV sends to the MPU-401 with it, and the recording's pitch is
then easy to read back). w16's own; no sample or preset from anywhere.

One sample: 440 Hz (A4, key 69) at 44000 Hz, exactly 100 samples a cycle,
looped over ten cycles; one instrument that loops it with a short release;
a preset for each of the 128 programs of bank 0 and for program 0 of bank
128 (the drums), all that instrument. The layout is the SoundFont 2.01
specification's (RIFF 'sfbk': INFO, sdta smpl, pdta phdr pbag pmod pgen
inst ibag imod igen shdr; each list ends with a terminal record).

  usage: mksf2.py OUT.sf2
"""
import math
import struct
import sys


def chunk(cid, data):
    if len(data) & 1:
        data += b'\0'
    return cid + struct.pack('<I', len(data)) + data


def lst(kind, body):
    return chunk(b'LIST', kind + body)


def zstr(s, n):
    b = s.encode('ascii')[:n - 1]
    return b + b'\0' * (n - len(b))


def main():
    rate, period, cycles = 44000, 100, 10
    lead = period                       # one cycle before the loop
    n = lead + period * cycles + period
    smp = [int(round(16000 * math.sin(2 * math.pi * i / period))) for i in range(n)]
    smpl = struct.pack('<%dh' % n, *smp) + b'\0\0' * 46
    info = lst(b'INFO', chunk(b'ifil', struct.pack('<HH', 2, 1)) +
               chunk(b'isng', b'EMU8000\0') + chunk(b'INAM', b'w16 sine\0'))
    sdta = lst(b'sdta', chunk(b'smpl', smpl))
    presets = [(p, 0) for p in range(128)] + [(0, 128)]
    phdr = b''.join(zstr('sine %d' % i, 20) + struct.pack('<HHHIII', p, b, i, 0, 0, 0)
                    for i, (p, b) in enumerate(presets))
    phdr += zstr('EOP', 20) + struct.pack('<HHHIII', 0, 0, len(presets), 0, 0, 0)
    pbag = b''.join(struct.pack('<HH', i, 0) for i in range(len(presets) + 1))
    pmod = b'\0' * 10
    pgen = b''.join(struct.pack('<HH', 41, 0) for _ in presets) + b'\0' * 4   # instrument 0
    inst = zstr('sine', 20) + struct.pack('<H', 0) + zstr('EOI', 20) + struct.pack('<H', 1)
    ibag = struct.pack('<HH', 0, 0) + struct.pack('<HH', 3, 0)
    imod = b'\0' * 10
    igen = (struct.pack('<Hh', 38, -8000) +        # releaseVolEnv: about 10 ms
            struct.pack('<HH', 54, 1) +            # sampleModes: loop
            struct.pack('<HH', 53, 0) +            # sampleID 0 (last)
            b'\0' * 4)
    shdr = (zstr('sine440', 20) +
            struct.pack('<IIIIIBbHH', 0, n, lead, lead + period * cycles, rate, 69, 0, 0, 1) +
            zstr('EOS', 20) + struct.pack('<IIIIIBbHH', 0, 0, 0, 0, 0, 0, 0, 0, 0))
    pdta = lst(b'pdta', chunk(b'phdr', phdr) + chunk(b'pbag', pbag) + chunk(b'pmod', pmod) +
               chunk(b'pgen', pgen) + chunk(b'inst', inst) + chunk(b'ibag', ibag) +
               chunk(b'imod', imod) + chunk(b'igen', igen) + chunk(b'shdr', shdr))
    body = b'sfbk' + info + sdta + pdta
    open(sys.argv[1], 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)


if __name__ == '__main__':
    main()
