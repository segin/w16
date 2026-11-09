#!/usr/bin/env python3
"""ustracesum - a readable summary of a US1 trace (tools/ustrace.py output).

Each call on one line with its result (from the matching return record),
without the register dump; runs of calls to the same function from the
same place are folded into one line with a count. Callers' code segments
are named as module:segment.

usage: ustracesum.py TRACE.TXT [LAST-SEQ] > summary.txt
"""
import re
import sys


def main():
    path = sys.argv[1]
    last = int(sys.argv[2]) if len(sys.argv) > 2 else 1 << 30
    entries, results = [], {}
    for line in open(path, encoding='latin-1'):
        if line.startswith('#'):
            break
        m = re.match(r'\s*(\d+) (\w{4})  (<- )?(\S+?)(\(.*\))?(?:  from (\S+)(?: (\S+))?)?(?: = (\S+))?  \[', line)
        if not m:
            continue
        seq = int(m.group(1))
        if seq > last:
            continue
        if m.group(3):
            results[seq] = m.group(8)
        else:
            entries.append((seq, m.group(2), m.group(4), m.group(5) or '()', m.group(7) or m.group(6) or ''))
    prev, count = None, 0
    # no byte dumps: they can hold other modules' code or artwork; strings stay
    entries = [(s, t, f, re.sub(r' \[[0-9a-f ]*\]', '', a), c) for s, t, f, a, c in entries]
    out = []
    for seq, task, fn, args, caller in entries:
        key = (fn, caller)
        r = results.get(seq)
        text = f'{seq:6} {task} {fn}{args}' + (f' = {r}' if r else '') + f'  <{caller}>'
        if key == prev:
            count += 1
            continue
        if count:
            out.append(f'           ... {count} more {prev[0]} from <{prev[1]}>')
        out.append(text)
        prev, count = key, 0
    if count:
        out.append(f'           ... {count} more {prev[0]} from <{prev[1]}>')
    print('\n'.join(out))


if __name__ == '__main__':
    main()
