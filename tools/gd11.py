#!/usr/bin/env python3
"""gd11 - read GD11's log (tests/apps/gd11.c: the font mapper and raster
font integer scaling, ARCHITECTURE.md 11.10).

usage: tools/gd11.py scale LOG          every request with the real size
                                        and the factors of its result
       tools/gd11.py cmp REF LOG        the M and X lines of LOG must equal
                                        REF's (exit 1 and the differences
                                        if not)

The factors come from the E lines (EnumFonts: the real sizes): a result
of face F is real size r times (sx, sy) when its height, ascent and
internal leading are sy times r's and its average and maximum widths sx
times r's (one more each when emboldened).
"""
import re
import sys

M_RE = re.compile(r'^M "([^"]*)" (.*) -> (?:"([^"]*)" (.*))?$')


def parse(path):
    enum, maps, xs = [], [], []
    with open(path, encoding='latin-1') as f:
        for line in f:
            line = line.rstrip('\r\n')
            if line.startswith('E "'):
                face, rest = line[3:].split('"', 1)
                a, b = rest.split('->')
                a = [int(v) for v in a.split()]
                b = [int(v) for v in b.split()]
                enum.append(dict(face=face, type=a[0], H=b[0], A=b[1], IL=b[2], EL=b[3],
                                 AW=b[4], MW=b[5], WT=b[6], PF=b[7], CS=b[8]))
            elif line.startswith('M "'):
                maps.append(line)
            elif line.startswith('X "'):
                xs.append(line)
    return enum, maps, xs


def fields(line):
    m = M_RE.match(line)
    if not m:
        return None
    req = [int(v) for v in m.group(2).split()]
    if m.group(3) is None:
        return m.group(1), req, None, None
    res = m.group(4).split('|')
    t = [int(v) for v in res[0].split()]
    names = 'H A D IL EL AW MW WT IT UL SO PF CS OH AX AY'.split()
    r = dict(zip(names, t))
    r['face'] = m.group(3)
    ex = [int(v) for v in res[1].split()]
    r['cwW'], r['cwi'], r['ext'], r['exth'] = ex
    return m.group(1), req, r, line


def factor(enum, r):
    """(real font, sx, sy) for result r, or None"""
    out = []
    for e in enum:
        if e['face'] != r['face'] or e['type'] & 4 or e['CS'] != r['CS']:
            continue
        if r['H'] % e['H']:
            continue
        sy = r['H'] // e['H']
        if r['A'] != sy * e['A'] or r['IL'] != sy * e['IL']:
            continue
        emb = 1 if r['WT'] >= 600 and e['WT'] < 600 and r['OH'] >= 1 else 0
        mw, aw = r['MW'] - emb, r['AW'] - emb
        if mw % e['MW'] or aw % e['AW'] if e['AW'] else True:
            continue
        sx = mw // e['MW']
        if aw != sx * e['AW']:
            continue
        out.append((e, sx, sy))
    return out[0] if out else None


def cmd_scale(path):
    enum, maps, _ = parse(path)
    for line in maps:
        f = fields(line)
        if not f or not f[2]:
            print(line)
            continue
        face, req, r, _ = f
        fx = factor(enum, r)
        if fx:
            e, sx, sy = fx
            s = 'real %d x%d y%d' % (e['H'], sx, sy)
        else:
            s = 'real ?'
        print('%-28s %5d %4d wt%-3d i%d u%d s%d cs%-3d pf%-3d -> %-14s H%-4d A%-4d IL%-3d AW%-4d MW%-4d WT%d OH%d | %s'
              % ('"' + face + '"', req[0], req[1], req[2], req[3], req[4], req[5], req[6], req[7],
                 r['face'], r['H'], r['A'], r['IL'], r['AW'], r['MW'], r['WT'], r['OH'], s))


def skipped(line):
    """a reference result w16 cannot give yet: a TrueType or vector font
    (tmPitchAndFamily 4 or 2)"""
    f = fields(line)
    return f is not None and f[2] is not None and f[2]['PF'] & 6


def cmd_cmp(ref, log):
    _, rm, rx = parse(ref)
    _, lm, lx = parse(log)
    bad = skip = 0
    for name, a, b in (('M', rm, lm), ('X', rx, lx)):
        if len(a) != len(b):
            print('%s lines: %d in %s, %d in %s' % (name, len(a), ref, len(b), log))
            bad += 1
        for x, y in zip(a, b):
            if name == 'M' and skipped(x):
                skip += 1
                continue
            if x != y:
                bad += 1
                if bad <= 40:
                    print('- ' + x)
                    print('+ ' + y)
    print('gd11: %d difference(s) (%d TrueType or vector results not compared)' % (bad, skip))
    return 1 if bad else 0


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'scale':
        cmd_scale(sys.argv[2])
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == 'cmp':
        return cmd_cmp(sys.argv[2], sys.argv[3])
    sys.stderr.write(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
