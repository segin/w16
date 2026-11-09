#!/usr/bin/env python3
"""osnorm - what tests/olesvrscen.sh compares of one OLESVR probe run.

usage: osnorm.py RUNDIR [SAVEDFILE...]

Writes RUNDIR/os.cmp: the DDE log (os.log) without OS1's "step wait"
lines (OS1's script runs from a timer, so where they fall depends on
timing) and with the object identifiers clients put in document names
("WINWRITE%(Untitled)%6af17234#001": Write uses the time) masked. Each
saved file (e.g. OLE1.WRI) becomes FILE.norm with those identifiers,
stored as little-endian double words, zeroed.
"""
import re
import sys

run = sys.argv[1]
try:
    log = open(run + '/os.log', encoding='latin-1').read()
except OSError:
    sys.exit(0)
ids = sorted(set(re.findall(r'%([0-9a-f]{8})#', log)))
out = []
for line in log.splitlines():
    if line.startswith('OS1 step wait '):
        continue
    out.append(re.sub(r'%[0-9a-f]{8}#', '%XXXXXXXX#', line))
open(run + '/os.cmp', 'w', encoding='latin-1').write('\n'.join(out) + '\n')
for f in sys.argv[2:]:
    try:
        data = open(run + '/' + f, 'rb').read()
    except OSError:
        continue
    for i in ids:
        data = data.replace(int(i, 16).to_bytes(4, 'little'), b'\0\0\0\0')
    open(run + '/' + f + '.norm', 'wb').write(data)
