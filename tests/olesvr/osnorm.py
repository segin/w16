#!/usr/bin/env python3
"""osnorm - what tests/olesvrscen.sh compares of one OLESVR probe run.

usage: osnorm.py RUNDIR [SAVEDFILE...]

Writes RUNDIR/os.cmp: the DDE log (os.log) without the "wait" lines of
OS1 and OS2 (OS1's script runs from a timer and a server OS2 starts logs
while OS2 waits, so where they fall depends on timing) and with the object identifiers clients put in document names
("WINWRITE%(Untitled)%6af17234#001": Write uses the time) masked. Each
saved file (e.g. OLE1.WRI) becomes FILE.norm, masked as the OLECLI tests
mask theirs (tests/ole/olecmp.py: a Write document's text and pictures
only, OLE streams without the hMF word and metafile tails).
"""
import os
import re
import sys

# olecmp.py's helpers, without running its main()
_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ole',
                         'olecmp.py')).read()
_olecmp = {}
exec(compile(_src[:_src.rindex('main()')], 'olecmp.py', 'exec'), _olecmp)

run = sys.argv[1]
try:
    log = open(run + '/os.log', encoding='latin-1').read()
except OSError:
    sys.exit(0)
ids = sorted(set(re.findall(r'%([0-9a-f]{8})#', log)))
out = []
item = False
for line in log.splitlines():
    # the properties of object windows (WIN 2 ... ItemWndClass) hold a
    # handle of the library's own heap: not compared
    if line.startswith('WIN '):
        item = 'ItemWndClass' in line
    elif item and line.startswith('    prop '):
        line = re.sub(r' = [0-9A-F]{4}$', ' = *', line)
    else:
        item = False
    if line.startswith('OS1 step wait ') or line.startswith('OS2 wait '):
        continue
    line = re.sub(r'%[0-9a-f]{8}#', '%XXXXXXXX#', line)
    # blocks the client filled (POKE) and the copies servers get of them
    # ("data" lines) end in uninitialised memory up to the rounding of
    # GlobalSize: their checksum and bytes past the first 16 are not
    # compared (OLESVR's own DATA blocks are zero-filled, and compared)
    if ' POKE item=' in line or line.startswith('  data fmt='):
        line = re.sub(r'sum=[0-9A-F]{4} \[((?:\\[0-9A-F]{2}|[^\\\]]){0,16}).*', r'sum=* [\1...', line)
    out.append(line)
open(run + '/os.cmp', 'w', encoding='latin-1').write('\n'.join(out) + '\n')
for f in sys.argv[2:]:
    try:
        data = open(run + '/' + f, 'rb').read()
    except OSError:
        continue
    if f.upper().endswith('.WRI'):
        data = _olecmp['wri_part'](data)
    else:
        data = _olecmp['masked'](data)
    open(run + '/' + f + '.norm', 'wb').write(data)
