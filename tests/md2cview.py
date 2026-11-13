#!/usr/bin/env python3
"""md2cview.py - MD2's TK.LOG (tests/md2cprobe.sh) cycle by cycle: what MS3
gave the driver's C207h handler and what reached the event procedure.

For each cycle: every packet MS3 passed to the handler (kind 5: status, X,
Y, table index, the BIOS callback it rode on, tick) and, after it, each
event procedure call (E) whose MS3 sequence number (q) falls after that
packet's record, i.e. the events that came out after it was given.
Prints a one-line summary per cycle: table indices given, X/Y of the
events, and which given packets have no event.
    usage: python3 -I tests/md2cview.py LOG [-v]
"""
import sys


def main():
    path = sys.argv[1]
    verbose = '-v' in sys.argv
    cycles, steps, events, recs = [], [], [], []
    for l in open(path, encoding='latin-1'):
        l = l.rstrip('\n')
        p = l.split()
        if not p:
            continue
        if p[0] == 'C':
            seq = int(l.rsplit('seq=', 1)[1])
            cycles.append((int(p[1]), ' '.join(p[2:-1]), seq))
        elif p[0] == 'S':
            steps.append(l)
        elif p[0] == 'E':
            ev = dict(cyc=int(p[1]), ax=int(p[2], 16), x=int(p[3]), y=int(p[4]),
                      t=int(p[6][2:]), q=int(p[7][2:]))
            events.append(ev)
        elif p[0] == 'R':
            recs.append(dict(kind=int(p[1]), a=int(p[2], 16), b=int(p[3], 16),
                             c=int(p[4], 16), d=int(p[5], 16), t=int(p[6][2:]),
                             seq=int(p[8][1:])))
    bounds = [c[2] for c in cycles] + [10 ** 9]
    for i, (n, what, s0) in enumerate(cycles):
        s1 = bounds[i + 1]
        rs = [r for r in recs if s0 <= r['seq'] < s1]
        evs = [e for e in events if e['cyc'] == n]
        print(f'== cycle {n}: {what}')
        items = []
        for r in rs:
            if r['kind'] == 5:
                sx = r['b'] - 256 if r['a'] & 0x10 else r['b']
                sy = r['c'] - 256 if r['a'] & 0x20 else r['c']
                items.append((r['seq'], 'P', r, sx, sy))
            elif r['kind'] == 1 and verbose:
                items.append((r['seq'], 'I', r, 0, 0))
            elif r['kind'] == 3:
                items.append((r['seq'], 'Q', r, 0, 0))
        for e in evs:
            items.append((e['q'] - 0.5, 'E', e, 0, 0))
        items.sort(key=lambda x: x[0])
        given, got = [], []
        pend = []
        for seq, k, r, sx, sy in items:
            if k == 'P':
                idx = 'real' if r['d'] == 0xFFFF else str(r['d'])
                given.append((idx, r['a'], sx, sy))
                pend.append(idx)
                if verbose:
                    print(f'   #{int(seq)} give [{idx}] st={r["a"]:02X} dx={sx} dy={sy} t={r["t"]}')
            elif k == 'Q':
                if verbose:
                    print(f'   #{int(seq)} IRQ 12 t={r["t"]}')
            elif k == 'I':
                print(f'   #{int(seq)} INT 15h {r["a"]:04X} BX={r["b"]:04X}')
            else:
                got.append((r['ax'], r['x'], r['y']))
                print(f'   event AX={r["ax"]:04X} x={r["x"]} y={r["y"]} q={r["q"]} t={r["t"]}'
                      f'  (given since last event: {",".join(pend) or "-"})')
                pend = []
        print(f'   given: {" ".join(f"[{g[0]}]{g[1]:02X}/{g[2]},{g[3]}" for g in given)}')
        print(f'   events: {" ".join(f"{a:04X}/{x},{y}" for a, x, y in got)}')
        if pend:
            print(f'   given after the last event: {",".join(pend)}')


if __name__ == '__main__':
    main()
