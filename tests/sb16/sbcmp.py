#!/usr/bin/env python3
"""sbcmp - MMP's device-section log over Windows 3.1's SNDBLST2.DRV against
the same sections over w16's SB16.DRV (tests/sb16/sbscen.sh;
ARCHITECTURE.md 24.4): what MMSYSTEM sees of the two drivers, with the
driver-private and expected differences masked, so that what is left is a
difference in behaviour toward MMSYSTEM.

Masked (each the driver's own business, or a value of the run):
  - the driver's names (SNDBLST2/SB16, the shims MDSW1/2 and MDSX1/2/3);
  - the instance the driver gives MMSYSTEM at an open ("user" of every
    later call, and the "[user A -> B]" of the open) and the value on
    MMSYSTEM's stack before it;
  - a header's lpNext and reserved fields ("next", "res": the driver's,
    Programmer's Guide ch. 5) and bit 31 of its flags (MMSYSTEM lets a
    driver use it: ARCHITECTURE.md 23.2);
  - dwBytesRecorded and the other fields of MMSYSTEM's own start-up
    header (uninitialised memory);
  - the timestamps of MIM_LONGDATA (ms since midiInStart).
Expected differences (the SB16 does more; each listed in KNOWN and counted
as such): the capabilities (names, formats, volume support), the formats
accepted (16-bit, stereo, rates to 44100), the volume answers, the aux
devices and the boot's open of the aux key, TIME_MS kept by GETPOS.

  usage: sbcmp.py SB2LOG SB16LOG    exit status 0 when nothing else differs
"""
import re
import sys
import difflib


def norm(line):
    l = line.rstrip('\n')
    l = l.replace('SNDBLST2', 'DRV').replace('SB16', 'DRV')
    l = re.sub(r'MDS[WX]([123])', r'MDS\1', l)
    l = re.sub(r'\[user [0-9A-F]{8}( -> [0-9A-F]{8})?\]', '[user]', l)
    l = re.sub(r'(MD MDS\d \w+ \w{4} dev \w{4}) user [0-9A-F]{8}', r'\1 user U', l)
    l = re.sub(r'next (\w+)', 'next N', l)
    l = re.sub(r'res (\[DRV\]|\w+)', 'res R', l)
    l = re.sub(r'flags 8000([0-9A-F]{4})\b', r'flags \1', l)
    # MMSYSTEM's own blocks (sndPlaySound's image, its start-up sound):
    # their selectors follow the heap, their header's other fields are
    # uninitialised
    l = re.sub(r'\b0C[0-9A-F]{2}(00[0-9A-F]{2})\b', r'SEL\1', l)
    if '[MMSYSTEM] len' in l or 'data [MMP]' in l or 'data SEL' in l:
        l = re.sub(r'rec -?\d+', 'rec X', l)
    # MIDIMAP's calls: a pointer to its stack
    l = re.sub(r'(FWD MMSYSTEM.202 from=MIDI args \w{4} \w{4}) \w{4}', r'\1 SSSS', l)
    if l.startswith('~'):
        l = '~'
    if 'msg 03C4' in l:
        l = re.sub(r'p2 [0-9A-F]{8}', 'p2 T', l)
        l = re.sub(r'dw2 [0-9A-F]{8}', 'dw2 T', l)
    return l


# (a pattern on a line of either side, why): expected differences
KNOWN = [
    (r'GetDevCaps\((0000|0), size \d+\) = 0', 'capabilities: names, formats, support'),
    (r' (wod 0004|wid 0033|mod 0002|mid 0036|aux 0004) ', 'capabilities'),
    (r'auxGetDevCaps|aux(Get|Set)Volume|auxOutMessage|auxGetNumDevs', 'aux devices (SNDBLST2 has none)'),
    (r'MDS3 |aux 0003|aux 0064|aux 0005|aux 0006', 'aux devices'),
    (r'^devices: |^again: |^open drivers: ', 'aux devices'),
    (r'waveOut(Get|Set)Volume|wod 0010|wod 0011|waveOutMessage\(h, 0010', 'wave volume (SNDBLST2: 8)'),
    (r'query 1 2 22050 8|query 1 1 44100 8|query 1 2 44100 16|query 1 1 11025 16|44100 16 stereo',
     'formats: 16-bit, stereo, 44100'),
    (r'fmt 010002002256|fmt 0100010044AC|fmt 0100020044AC|fmt 01000100112B000022560000020010', 'formats'),
    (r'GetPosition\(type 1\)|0100EEEEEEEEEEEE', 'GETPOS keeps TIME_MS'),
]


def known(l):
    for p, why in KNOWN:
        if re.search(p, l):
            return why
    return None


def main():
    a = [norm(l) for l in open(sys.argv[1], errors='replace')]
    b = [norm(l) for l in open(sys.argv[2], errors='replace')]
    bad = kn = 0
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == 'equal':
            continue
        hunk = [('-', l) for l in a[i1:i2]] + [('+', l) for l in b[j1:j2]]
        # a hunk that starts with a known difference is that difference
        # (the calls that follow from it: the close of an open that
        # SNDBLST2 refused)
        if known(hunk[0][1]):
            kn += len(hunk)
            continue
        for s, l in hunk:
            if known(l):
                kn += 1
            else:
                bad += 1
                print('%s %s' % (s, l[:300]))
    print('sbcmp: %d lines differ unexplained, %d known differences' % (bad, kn))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
