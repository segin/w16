#!/usr/bin/env python3
"""gd13 - compare two logs of the TrueType probe GD13 (tests/apps/gd13.c;
tests/gd13.sh): 3.1's (REF) and w16's (LOG). ARCHITECTURE.md 11.12.

usage: tools/gd13.py [-v] REF LOG

Prints, for each kind of line: R (GetRasterizerCaps), C/FD/TN
(CreateScalableFontResource and the .FOT's resources), A, K, D, RM: same
or not; M (metrics): per face, how many requests differ in the face and
in each field; W (GetCharWidth 32-255): how many widths differ; B
(GetCharABCWidths): how many values differ; P (pictures): per picture
the black pixels each and how many pixels differ. -v lists differences.
Exit status 0 always: the differences are what is measured (hinting is
not done yet), not a pass or fail.
"""
import sys

FIELDS = 'H A D IL EL AW MW WT IT UL SO FC LC DC BC PF CS OH AX AY ext exth'.split()


def parse(path):
    lines = [l.rstrip('\r\n') for l in open(path, errors='replace')]
    out = {'M': {}, 'W': {}, 'B': {}, 'P': {}, 'other': {}}
    pic = None
    for l in lines:
        if l.startswith('ROW ') and pic is not None:
            out['P'][pic][1].append(l.split()[2] if len(l.split()) > 2 else '')
            continue
        pic = None
        k = l.split(' ', 1)[0]
        if k in ('M', 'W', 'B'):
            key, val = l.split(' -> ', 1) if ' -> ' in l else (l, '')
            out[k][key] = val
        elif k == 'P':
            pic = l.rsplit(' ', 3)[0]
            out['P'][pic] = (l, [])
        elif k in ('R', 'C', 'FD', 'TN', 'A', 'K', 'D', 'RM'):
            out['other'].setdefault(k, []).append(l)
    return out


def bits(hexrow):
    return bin(int(hexrow, 16))[2:].zfill(len(hexrow) * 4) if hexrow else ''


def main():
    args = [a for a in sys.argv[1:] if a != '-v']
    verbose = '-v' in sys.argv
    if len(args) != 2:
        sys.stderr.write(__doc__)
        return 2
    a, b = parse(args[0]), parse(args[1])
    for k in sorted(set(a['other']) | set(b['other'])):
        x, y = a['other'].get(k, []), b['other'].get(k, [])
        print('%-3s %s' % (k, 'same' if x == y else 'DIFFERENT'))
        if x != y and verbose:
            for l in x:
                print('   3.1 ' + l[:160])
            for l in y:
                print('   w16 ' + l[:160])
    faces = {}
    for key, ra in a['M'].items():
        face = key.split('"')[1]
        st = faces.setdefault(face, {'n': 0, 'face': 0, 'same': 0})
        st['n'] += 1
        rb = b['M'].get(key)
        if rb is None:
            continue
        fa, fb = ra.split('"'), rb.split('"')
        if fa[1] != fb[1]:
            st['face'] += 1
            continue
        va = fa[2].replace('|', '').split()
        vb = fb[2].replace('|', '').split()
        diff = [FIELDS[i] for i in range(min(len(va), len(vb))) if va[i] != vb[i]]
        if not diff:
            st['same'] += 1
        for d in diff:
            st[d] = st.get(d, 0) + 1
        if diff and verbose:
            print('M %s\n   3.1 %s\n   w16 %s %s' % (key, ra, rb, diff))
    for face, st in faces.items():
        print('M "%s": %d requests, %d another face, %d identical; fields: %s' % (
            face, st['n'], st['face'], st['same'],
            ' '.join('%s %d' % (k, v) for k, v in sorted(st.items()) if k not in ('n', 'face', 'same'))))
    for kind in ('W', 'B'):
        tot = bad = lines = 0
        for key, ra in a[kind].items():
            rb = b[kind].get(key, '')
            va, vb = ra.split(), rb.split()
            lines += 1
            for i in range(len(va)):
                tot += 1
                bad += i >= len(vb) or va[i] != vb[i]
        print('%s: %d values in %d lines, %d differ' % (kind, tot, lines, bad))
    for key, (la, ra) in a['P'].items():
        lb, rb = b['P'].get(key, ('', []))
        diff = 0
        for i in range(max(len(ra), len(rb))):
            x = bits(ra[i]) if i < len(ra) else ''
            y = bits(rb[i]) if i < len(rb) else ''
            n = max(len(x), len(y))
            x, y = x.ljust(n, '0'), y.ljust(n, '0')
            diff += sum(1 for p, q in zip(x, y) if p != q)
        print('P %-28s 3.1 black %s, w16 black %s, %d pixels differ' % (
            key[2:], la.split()[-1], lb.split()[-1] if lb else '-', diff))
    return 0


if __name__ == '__main__':
    sys.exit(main())
