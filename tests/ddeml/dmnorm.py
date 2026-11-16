#!/usr/bin/env python3
"""dmnorm - normalise a DDEML probe log (tests/ddeml/dm.c, OSSPY.DLL) for
comparison (tests/ddemlscen.sh): what differs between Microsoft's own
runs, or by nature between two libraries, is masked here, each rule with
its reason.

usage: dmnorm.py os.log > os.cmp
"""
import re
import sys


SPYDATA = re.compile(r'size=(\d+) len=\d+ sum=[0-9A-F]+ \[(.*)\]$')
PROBEDATA = re.compile(r'size=(\d+) "(.*)"$')


def norm(line):
    # data blocks: what follows the first NUL is whatever the block's
    # memory held before (3.1's DDEML does not clear the blocks it
    # allocates: "mmsystem.dll" turns up there), so a block is compared
    # up to its first NUL, and by size (the spy's checksum covers it all)
    m = SPYDATA.search(line)
    if m:
        line = line[:m.start()] + 'size=%s [%s]' % (m.group(1), m.group(2).split('\\00')[0])
    m = PROBEDATA.search(line)
    if m:
        line = line[:m.start()] + 'size=%s "%s"' % (m.group(1), m.group(2).split('\\x00')[0])
    # the spy shows an EXECUTE's commands up to their NUL already
    return line


def main():
    for line in open(sys.argv[1], encoding='latin-1'):
        line = norm(line.rstrip('\n'))
        if line is not None:
            print(line)


if __name__ == '__main__':
    main()
