#!/usr/bin/env python3
"""mkinputs - the files the LZEXPAND probe (tests/lzexpand/lzp.c) reads,
made at test time with w16's own compressor (tools/szddenc.py): nothing
of Microsoft's. They go to OUTDIR, which the runs put at C:\\LZ.

usage: mkinputs.py OUTDIR
"""
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import szddenc  # noqa: E402


def text(n):
    return b''.join(b'Line %04d: the quick brown fox jumps over the lazy dog.\r\n' % i
                    for i in range(n))


def big():
    # 70000 bytes: every byte value, runs and repeats (matches up to the
    # whole window back), past 64K so that LONG results and seeks are seen
    r = random.Random(16)
    out = bytearray()
    while len(out) < 70000:
        k = r.randrange(4)
        if k == 0:
            out += bytes(r.randrange(256) for _ in range(r.randrange(1, 40)))
        elif k == 1:
            out += bytes([r.randrange(256)]) * r.randrange(3, 60)
        elif k == 2 and len(out) > 4000:
            p = len(out) - r.randrange(1, 4000)
            out += out[p:p + r.randrange(3, 30)]
        else:
            out += b'%05d ' % len(out)
    return bytes(out[:70000])


def main():
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    t = text(120)                       # 6840 bytes
    b = big()
    small = bytes((i * 7 + 3) & 0xFF for i in range(300))
    sp = b' ' * 40 + b'after forty spaces' + b' ' * 20 + b'end'
    rr = random.Random(8)
    rnd8k = bytes(rr.randrange(256) for _ in range(8000))
    files = {
        'TEXT.TX_': szddenc.szdd(t, 'T'),
        'TEXT.TXT': t,                                  # the same, plain
        'ONLYC.TX_': szddenc.szdd(t[:500], 'T'),        # no ONLYC.TXT
        'NOCHAR.DA_': szddenc.szdd(small, 0),           # no /r character
        'BIG.DA_': szddenc.szdd(b, 'T'),
        'BIG.DAT': b,
        'EMPTY.TX_': szddenc.szdd(b'', 'T'),
        'BADALG.TX_': szddenc.szdd(t[:500], 'T', alg=b'B'),
        'ALG0.TX_': szddenc.szdd(t[:500], 'T', alg=b'\0'),
        'TRUNC.TX_': szddenc.szdd(t, 'T')[:600],         # data cut short
        'HDR.TX_': szddenc.szdd(t, 'T')[:10],            # header cut short
        'HDR13.TX_': szddenc.szdd(t, 'T')[:13],
        'MAGIC.TX_': szddenc.szdd(t, 'T')[:8],           # the magic alone
        'NODATA.TX_': szddenc.szdd(t, 'T')[:14],         # header, no data
        'SHORTSZ.TX_': szddenc.szdd(t, 'T', size=100),   # size < data
        'LONGSZ.TX_': szddenc.szdd(t[:500], 'T', size=1000),  # size > data
        'QB.TX_': szddenc.szdd_qb(t[:500]),               # MS-DOS 5 variant
        'SPACES.TX_': szddenc.szdd(sp, 'T'),              # a match into the primed window
        'LIT.TX_': szddenc.szdd(t[:500], 'T', literal_only=True),
        'LOWER.TX_': szddenc.szdd(t[:100], 't'),          # a lower-case /r character
        # LZRead's request sizes: literals only, one long run, far matches
        'LITBIG.TX_': szddenc.szdd(rnd8k, 'T', literal_only=True),
        'RUNS.TX_': szddenc.szdd(b'b' * 8000, 'T'),
        'FAR.TX_': szddenc.szdd(rnd8k[:2000] * 4, 'T'),
        # matches of up to 18 bytes, which the format allows and COMPRESS
        # never writes (tools/szddenc.py): 3.1's LZRead fails on some reads
        'TEXT18.TX_': szddenc.szdd(t, 'T', maxlen=18),
        'RUNS18.TX_': szddenc.szdd(b'b' * 8000, 'T', maxlen=18),
        'FAR18.TX_': szddenc.szdd(rnd8k[:2000] * 4, 'T', maxlen=18),
        'BIG18.DA_': szddenc.szdd(b, 'T', maxlen=18),
        'LITBIG.DAT': rnd8k,
        'RUNS.DAT': b'b' * 8000,
        'FAR.DAT': rnd8k[:2000] * 4,
        # names for GetExpandedName's rule
        'NOEXT': szddenc.szdd(t[:50], 'X'),
        'ONE.E_': szddenc.szdd(t[:50], 'X'),
        'NONE._': szddenc.szdd(t[:50], 'X'),
        'NONE0._': szddenc.szdd(t[:50], 0),
        'NOUND.ABC': szddenc.szdd(t[:50], 'X'),
        'NOUND0.ABC': szddenc.szdd(t[:50], 0),
        'TWO.AB': szddenc.szdd(t[:50], 'X'),
        'UNDER_': szddenc.szdd(t[:50], 'X'),
        'DIGIT.DA_': szddenc.szdd(t[:50], '7'),
        'HIGH.DA_': szddenc.szdd(t[:50], '\xe9'),
        'PLAIN.EX_': t[:50],                             # not compressed
        'CREAT.TX_': szddenc.szdd(t[:500], 'T'),          # OF_CREATE truncates it
        'TINY.TX_': b'SZ',                                # 2 bytes
        'ZERO.TX_': b'',                                  # empty file
    }
    # BIG.DA_ cut short at several points (LZCopy's output chunks)
    bc = szddenc.szdd(b, 'T')
    for cut in (3000, 5000, 9000, 17000, 30000):
        files['CUT%d.DA_' % cut] = bc[:cut]
    # all of BIG.DA_'s data under a header that says 100 bytes: how much
    # LZCopy reads of it
    files['SHORTBIG.DA_'] = szddenc.szdd(b, 'T', size=100)
    for i in range(18):                                  # the handle limit
        files['MANY%02d.TX_' % i] = szddenc.szdd(t[:200 + i], 'T')
    for name, data in files.items():
        with open(os.path.join(d, name), 'wb') as f:
            f.write(data)
    sub = os.path.join(d, 'A.B')                          # a dot in a directory
    os.makedirs(sub, exist_ok=True)
    with open(os.path.join(sub, 'NAME'), 'wb') as f:
        f.write(szddenc.szdd(t[:50], 'X'))
    with open(os.path.join(sub, 'UNDER_'), 'wb') as f:
        f.write(szddenc.szdd(t[:50], 'X'))
    # a file only the search path finds (lzrun.sh puts SYS\* in WINDOWS\SYSTEM)
    sysd = os.path.join(d, '..', 'SYS')
    os.makedirs(sysd, exist_ok=True)
    with open(os.path.join(sysd, 'SYSFILE.TX_'), 'wb') as f:
        f.write(szddenc.szdd(t[:50], 'T'))


if __name__ == '__main__':
    main()
