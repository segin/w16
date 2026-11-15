#!/usr/bin/env python3
"""calcgen - random CA1 test scripts for Calculator (tests/calcscenarios.sh).

Writes NAME.txt files of CA1 test lines (tests/apps/ca1.c's syntax): random
key sequences of several kinds, each line starting from C (or a mode
change), so that the original Calculator and w16's can be compared
result by result. The same seed gives the same scripts.

usage: calcgen.py OUTDIR [SEED] [LINES]
"""
import os
import random
import sys


def num(r, big=False):
    k = r.random()
    if k < 0.35:
        return str(r.randint(0, 99))
    if k < 0.6:
        return '%d.%s' % (r.randint(0, 999), ''.join(r.choice('0123456789') for _ in range(r.randint(1, 6))))
    if k < 0.8:
        return ''.join(r.choice('0123456789') for _ in range(r.randint(1, 15 if big else 9)))
    if k < 0.9:
        return '0.' + '0' * r.randint(0, 9) + ''.join(r.choice('123456789') for _ in range(r.randint(1, 5)))
    return '%dx%s%d' % (r.randint(1, 9), 'N' if r.random() < 0.5 else '', r.randint(0, 40))


def std_line(r):
    s = num(r)
    for _ in range(r.randint(1, 4)):
        k = r.random()
        if k < 0.6:
            s += r.choice('+-*/') + num(r)
        elif k < 0.7:
            s += r.choice(['=', '==', 'N', 'b', 'e'])
        elif k < 0.8:
            s += r.choice(['[235,86]', '[235,122]', '[235,158]'])     # sqrt % 1/x
        else:
            s += r.choice(['S', 'P', 'R', 'Q'])
    if r.random() < 0.8:
        s += '='
    return 'c ' + s


def sci_line(r):
    ops = list('+-*/y') + ['%', '&', '|', '^', 'L']
    fns = list('sotnlr@#!m;~p') + ['is', 'io', 'it', 'in', 'il', 'i@', 'i#', 'im', 'i;', 'hs', 'ho', 'ht', 'ihs', 'iho', 'iht']
    s = num(r)
    for _ in range(r.randint(1, 4)):
        k = r.random()
        if k < 0.45:
            s += r.choice(ops) + num(r)
        elif k < 0.75:
            s += r.choice(fns)
        elif k < 0.85:
            s += '(' + num(r) + r.choice(ops) + num(r) + ')'
        else:
            s += r.choice(['=', 'N', 'b', 'v', 'iy' + num(r)])
    if r.random() < 0.85:
        s += '='
    return r.choice('wuz') + ' c ' + s


def base_line(r):
    radix = r.choice('HKG')
    size = r.choice('wuz')
    digits = {'H': '0123456789ABCDEF', 'K': '01234567', 'G': '01'}[radix]
    def n():
        return ''.join(r.choice(digits) for _ in range(r.randint(1, 6 if radix != 'G' else 12)))
    s = n()
    for _ in range(r.randint(1, 3)):
        s += r.choice(['+', '-', '*', '/', '&', '|', '^', 'L', '%']) + n()
        if r.random() < 0.2:
            s += r.choice(['~', 'N', 'b', 'i@', '@'])
    s += '='
    if r.random() < 0.5:
        s += 'J'
    return 'J w c %s %s c %s' % (radix, size, s)


def mem_line(r):
    s = num(r) + 'S'
    for _ in range(r.randint(1, 3)):
        s += num(r) + r.choice(['P', 'R', 'S', 'Q', '+R=', '*R='])
    return 'c ' + s


def entry_line(r):
    s = num(r, True)
    for _ in range(r.randint(1, 3)):
        s += r.choice(['b', 'bb', 'N', '.', '0', '00', 'e', '5', 'x3', 'xN2'])
    return 'c ' + s


def main():
    out = sys.argv[1]
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    lines = int(sys.argv[3]) if len(sys.argv) > 3 else 400
    r = random.Random(seed)
    os.makedirs(out, exist_ok=True)
    kinds = [('std', std_line, '{306}'), ('sci', sci_line, '{305}'), ('base', base_line, '{305}'),
             ('mem', mem_line, '{306}'), ('entry', entry_line, '{306}')]
    for name, fn, view in kinds:
        n = lines if name in ('std', 'sci') else lines // 2
        with open(os.path.join(out, 'r%d%s.txt' % (seed, name)), 'w') as f:
            f.write('run CALC.EXE\n%s\n# random %s, seed %d\n' % (view, name, seed))
            for _ in range(n):
                f.write(fn(r) + '\n')
            f.write('J w c\n')


if __name__ == '__main__':
    main()
