#!/usr/bin/env python3
"""thcmp - compare two logs of the TOOLHELP.DLL probe THP.EXE
(tests/toolhelp/thp.c; ARCHITECTURE.md 22) section by section.

usage: thcmp.py [--raw] [--mask FILE] A.log B.log [SECTION...]

Lines starting with "#" are notes and never compared. Lines starting
with "%" hold raw numbers (handles, addresses, sizes): compared only with
--raw, for two runs on the same system (Microsoft's TOOLHELP against
w16's on one KERNEL). --mask FILE: a list of regular expressions, one a
line ("#" comments); a line matching one is left out of both logs, for
comparing runs on different systems (w16's against Windows 3.1's): each
mask there says why the systems differ. A mask line "s/REGEX/TEXT/" is a
substitution made in every line of both logs before they are compared
(a field left out, rather than the whole line). Prints one line a section (ok, or
FAIL with the first differences) and a summary line; exits 1 on any FAIL.
"""
import re
import sys


def sections(path, raw, masks, subs=()):
    out, cur = {}, None
    order = []
    with open(path, errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('== '):
                cur = line[3:].split()[0]
                out.setdefault(cur, [])
                order.append(cur)
                continue
            if cur is None or line.startswith('#'):
                continue
            if line.startswith('%') and not raw:
                continue
            for r, t in subs:
                line = r.sub(t, line)
            if any(m.search(line) for m in masks):
                continue
            out[cur].append(line)
    return out, order


def main():
    args = sys.argv[1:]
    raw = False
    masks = []
    subs = []
    while args and args[0].startswith('--'):
        o = args.pop(0)
        if o == '--raw':
            raw = True
        elif o == '--mask':
            for m in open(args.pop(0)):
                m = m.rstrip('\n')
                if not m or m.startswith('#'):
                    continue
                if m.startswith('s/') and m.endswith('/') and m.count('/') == 3:
                    _, r, t, _ = m.split('/')
                    subs.append((re.compile(r), t))
                else:
                    masks.append(re.compile(m))
    a, b = args[0], args[1]
    want = args[2:]
    sa, oa = sections(a, raw, masks, subs)
    sb, ob = sections(b, raw, masks, subs)
    bad = 0
    names = [s for s in oa if not want or s in want]
    for s in ob:
        if s not in names and (not want or s in want):
            names.append(s)
    for s in names:
        la, lb = sa.get(s), sb.get(s)
        if la is None or lb is None:
            print('FAIL %s: only in %s' % (s, 'A' if lb is None else 'B'))
            bad += 1
            continue
        if la == lb:
            print('ok   %s (%d lines)' % (s, len(la)))
            continue
        bad += 1
        print('FAIL %s:' % s)
        n = 0
        import difflib
        for d in difflib.unified_diff(la, lb, 'A', 'B', n=0, lineterm=''):
            if d.startswith(('---', '+++', '@@')):
                continue
            print('    ' + d)
            n += 1
            if n >= int(__import__("os").environ.get("THCMP_LINES", "12")):
                print('    ...')
                break
    print('%s: %d section(s) differ' % ('FAIL' if bad else 'ok', bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
