"""pifreads.py DOSBOX.LOG NAME: the reads of file NAME in a DOSBox-X log
with fileio=debug and files=debug (tests/worun.sh -l), one line per open:
"open: end 1E0h=... 171h+22 187h+22 ..." (each read as offset+length,
hexadecimal offsets; "end" a seek to the end, "@N" where a read started
without a seek before it). Used by tests/pifread.sh."""
import re
import sys

log, name = sys.argv[1], sys.argv[2].upper()
cur = None
pos = 0
for line in open(log, errors='replace'):
    if 'FILES:' not in line:
        continue
    up = line.upper()
    if 'OPEN COMMAND' in up and up.rstrip().endswith('\\' + name):
        cur = []
        pos = 0
        continue
    if cur is None:
        continue
    if not up.rstrip().endswith(name) and name not in up:
        continue
    m = re.search(r'Seeking to (-?\d+) bytes from position type \((\d)\)', line)
    if m:
        off, how = int(m.group(1)), int(m.group(2))
        if how == 2:
            cur.append('end')
            pos = None
        elif how == 0:
            pos = off
        else:
            pos = None if pos is None else pos + off
        continue
    m = re.search(r'Reading (\d+) bytes', line)
    if m:
        n = int(m.group(1))
        cur.append('%s+%d' % ('?' if pos is None else '%Xh' % pos, n))
        if pos is not None:
            pos += n
        continue
    if 'Closing file' in line:
        print('open:', ' '.join(cur) if cur else '(nothing read)')
        cur = None
if cur is not None:
    print('open (not closed):', ' '.join(cur))
