#!/usr/bin/env python3
"""dxlog - condense a DOSBox-X debug log (tests/worun.sh -l) to the
file-level events: opens, creates, deletes, renames, searches, EXECs and
errors; reads, writes and seeks are left out. Consecutive repeats are
counted.

usage: dxlog.py dosbox.log [FROM-REGEX]
"""
import re
import sys

skip = re.compile(r'Reading \d+ bytes|Writing \d+ bytes|Seeking to|AUTOEXEC\.BAT|'
                  r'XRandR|X11 main|CRTC|Goes to output|pixratio|Aspect ratio|surface consider|'
                  r'menuScale|Our window')
keep = re.compile(r'FILES:|EXEC:|INT21|DOSMISC|ERROR')
start = re.compile(sys.argv[2]) if len(sys.argv) > 2 else None
prev, n, on = None, 0, start is None
for line in open(sys.argv[1], errors='replace'):
    line = line.rstrip('\n')
    if not on:
        if start.search(line):
            on = True
        else:
            continue
    if skip.search(line) or not keep.search(line):
        continue
    body = re.sub(r'^\s*\d+\s+', '', line)
    if body == prev:
        n += 1
        continue
    if prev is not None:
        print(prev + (f'   (x{n})' if n > 1 else ''))
    prev, n = body, 1
if prev is not None:
    print(prev + (f'   (x{n})' if n > 1 else ''))
