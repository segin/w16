import re
import sys
# cmplog.py A B: two C:\CMP.LOG files of tests/cplscenarios.sh (SNAP's
# append, caretblink, blinkrate, kbrlog) the same, line for line, but for
# the counts of "blinkrate ...: N changes in MS ms" lines, which depend on
# the host's timing: each pair within a quarter of the larger plus one.
# Exit 1 (and the first difference) when they are not.
rx = re.compile(r'^(blinkrate .*: )(\d+)( changes in \d+ ms)$')


def lines(p):
    try:
        return open(p, 'rb').read().decode('latin-1').replace('\r', '').rstrip('\n').split('\n')
    except FileNotFoundError:
        return ['(no file)']


a, b = lines(sys.argv[1]), lines(sys.argv[2])
if len(a) != len(b):
    print(f'    {len(a)} lines against {len(b)}')
    sys.exit(1)
for x, y in zip(a, b):
    mx, my = rx.match(x), rx.match(y)
    if mx and my and mx.group(1) == my.group(1) and mx.group(3) == my.group(3):
        p, q = int(mx.group(2)), int(my.group(2))
        if abs(p - q) <= 1 + max(p, q) // 4:
            continue
    elif x == y:
        continue
    print(f'    3.1: {x}\n    w16: {y}')
    sys.exit(1)
