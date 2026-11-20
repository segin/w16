#!/usr/bin/env python3
"""mktree.py OUTDIR: File Manager's test tree (tests/wfobs.sh), laid over
C:\\ as WFTEST with fixed file and directory times so that the details
views read the same in every run. The tree has nested directories (some
expandable, some not), files of several extensions (programs, documents,
others) and sizes, and names that sort differently by name, type, size and
date."""
import os
import sys
import time

out = sys.argv[1]
base = os.path.join(out, 'WFTEST')

# (path under WFTEST, size, 'YYYY-MM-DD HH:MM:SS'); a trailing / is a directory
ENTRIES = [
    ('ALPHA/', 0, '1992-03-01 09:00:00'),
    ('ALPHA/ONE/', 0, '1992-03-02 10:00:00'),
    ('ALPHA/ONE/DEEP/', 0, '1992-03-02 10:30:00'),
    ('ALPHA/ONE/DEEP/LEAF.TXT', 10, '1992-03-02 10:31:00'),
    ('ALPHA/TWO/', 0, '1992-03-03 11:00:00'),
    ('ALPHA/README.TXT', 1234, '1992-03-04 12:00:00'),
    ('BETA/', 0, '1992-04-01 08:15:00'),
    ('BETA/GAMMA.DOC', 2048, '1992-04-02 13:45:00'),
    ('EMPTY/', 0, '1992-05-05 05:05:00'),
    ('ZULU/', 0, '1991-12-31 23:59:58'),
    ('ZULU/Z.BAT', 33, '1991-12-31 23:00:00'),
    ('APP.EXE', 4096, '1992-06-10 14:20:00'),
    ('BATCH.BAT', 50, '1992-01-15 07:30:00'),
    ('CMD.COM', 777, '1992-02-29 16:00:00'),
    ('DATA.DAT', 65536, '1990-08-08 08:08:08'),
    ('NOTES.TXT', 300, '1992-07-04 17:00:00'),
    ('LETTER.WRI', 1500, '1992-03-15 09:45:00'),
    ('PICTURE.BMP', 630, '1992-03-16 18:00:00'),
    ('ZZZ', 1, '1992-01-01 00:00:00'),
    ('A.PIF', 545, '1992-03-10 10:10:00'),
    ('CARDS.CRD', 120, '1992-09-09 09:09:00'),
    ('BIG.LOG', 123456, '1992-10-10 22:22:00'),
]


def stamp(t):
    return time.mktime(time.strptime(t, '%Y-%m-%d %H:%M:%S'))


dirs = []
for path, size, t in ENTRIES:
    p = os.path.join(base, path)
    if path.endswith('/'):
        os.makedirs(p, exist_ok=True)
        dirs.append((p, t))
    else:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(bytes((i * 7 + 13) & 0xFF for i in range(size)))
        os.utime(p, (stamp(t), stamp(t)))
# directories last, deepest first, so that making their files does not
# move their times
for p, t in sorted(dirs, key=lambda d: -d[0].count(os.sep)):
    os.utime(p, (stamp(t), stamp(t)))
os.utime(base, (stamp('1992-03-01 08:00:00'),) * 2)
