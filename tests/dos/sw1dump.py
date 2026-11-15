#!/usr/bin/env python3
"""Print C:\\SW1.LOG, the record of tests/dos/sw1.asm (SW1.COM).

usage: sw1dump.py SW1.LOG [-b] [-v] [-m] [-q]
  -b  print every switcher-block snapshot as a diff from the one before
  -v  print the interrupt vectors that changed at each snapshot
  -m  print the MCB chain at each snapshot
  -q  calls only (no snapshots)
"""
import struct
import sys

INTS = {0: '21', 1: '2F', 2: '15', 3: '10', 0xFF: 'XMS'}
VECS = [0x08, 0x09, 0x10, 0x13, 0x15, 0x16, 0x1C, 0x21, 0x22, 0x23, 0x24,
        0x28, 0x2F, 0x33, 0x67, 0x70, 0x74]


def main():
    args = sys.argv[1:]
    opts = {a for a in args if a.startswith('-')}
    path = [a for a in args if not a.startswith('-')][0]
    d = open(path, 'rb').read()
    if d[:3] != b'SW1':
        sys.exit('not an SW1 log')
    n, lost, boff, bseg = struct.unpack_from('<IHHH', d, 4)
    print(f'records {n}, lost {lost}, block {bseg:04X}:{boff:04X}')
    pos = 32
    prevblk = None
    prevvec = None
    while pos + 32 <= len(d):
        r = d[pos:pos + 32]
        pos += 32
        t = chr(r[0])
        nd = r[1]
        data = d[pos:pos + 32 * nd]
        pos += 32 * nd
        slot = r[2]
        indos = r[3]
        ax, bx, cx, dx, si, di, ds, es, cs, ip, psp, fl, tick, bp = \
            struct.unpack_from('<14H', r, 4)
        intn = INTS.get(slot >> 4 if slot != 0xFF else 0xFF, '?')
        sl = slot & 15 if slot != 0xFF else 0
        regs = (f'AX={ax:04X} BX={bx:04X} CX={cx:04X} DX={dx:04X} SI={si:04X} '
                f'DI={di:04X} DS={ds:04X} ES={es:04X} BP={bp:04X}')
        where = f'{cs:04X}:{ip:04X} psp={psp:04X} t={tick:04X}'
        if t in 'iox':
            if t == 'x':
                intn = 'XMS'
            arrow = '->' if t == 'i' else '<-'
            extra = ''
            if t == 'o':
                extra = f' CF={fl & 1} ZF={(fl >> 6) & 1}'
            print(f'{intn}{"." + str(sl) if sl else ""} {arrow} {regs}{extra}  {where}{" indos" if indos else ""}')
            if data:
                if intn == '21':
                    s = data.split(b'\0')[0].decode('latin-1')
                    print(f'      name "{s}"')
                elif intn == '15':
                    src = data[0x12] | data[0x13] << 8 | data[0x14] << 16 | data[0x17] << 24
                    dst = data[0x1A] | data[0x1B] << 8 | data[0x1C] << 16 | data[0x1F] << 24
                    print(f'      move {cx * 2:X} bytes {src:08X} -> {dst:08X}')
                else:
                    print('      ' + data[:32].hex(' '))
        elif t == 'X':
            print(f'XMS -> {regs}  {where}')
            if data:
                ln, sh, so, dh, do = struct.unpack_from('<IHIHI', data, 0)
                print(f'      move {ln:X}: {sh:04X}:{so:08X} -> {dh:04X}:{do:08X}')
        elif t == 'E':
            path = data[:64].split(b'\0')[0].decode('latin-1')
            pb = data[64:96]
            tl = data[96:224]
            tail = tl[1:1 + tl[0]].decode('latin-1')
            env, toff, tseg, f1o, f1s, f2o, f2s = struct.unpack_from('<7H', pb, 0)
            print(f'=== EXEC AL={ax & 0xFF:02X} "{path}" tail "{tail}" env={env:04X} '
                  f'tail@{tseg:04X}:{toff:04X} fcb {f1s:04X}:{f1o:04X} {f2s:04X}:{f2o:04X}  {where}')
        elif t == 'T':
            print(f'=== TERMINATE AH={ax >> 8:02X} AL={ax & 0xFF:02X} DX={dx:04X}  {where}')
        elif t == 'S':
            print(f'=== INT 21h AX=0B80h (switch)  {regs}  {where}')
        elif t == 'P':
            print(f'=== INT 2Fh AX={ax:04X} SI={si:04X}  {regs}  {where}')
        elif t == 'N':
            print(f'=== NOTIFY AX={ax:04X} BX={bx:04X} CX={cx:04X} DX={dx:04X} ES:DI={es:04X}:{di:04X}  {where}')
        elif t == 'I':
            print('=== installed')
        elif t == 'H':
            print(f'    (vector {INTS.get(slot >> 4)}h hooked by {ax:04X}:{dx:04X}: slot {slot & 15})')
        elif t == 'M':
            if '-m' in opts and '-q' not in opts:
                for i in range(32):
                    seg, sig, owner, size = struct.unpack_from('<HBxHH', data, i * 16)
                    if not seg:
                        break
                    name = data[i * 16 + 8:i * 16 + 16].split(b'\0')[0].decode('latin-1')
                    print(f'      MCB {seg:04X} {chr(sig)} owner {owner:04X} size {size:04X} {name!r}')
        elif t == 'V':
            vec = [struct.unpack_from('<HH', data, 4 * v) for v in range(256)]
            if '-v' in opts and '-q' not in opts:
                for v in VECS if prevvec is None else range(256):
                    if prevvec is None or vec[v] != prevvec[v]:
                        o, s = vec[v]
                        print(f'      INT {v:02X}h = {s:04X}:{o:04X}')
                bda = data[1024:1280]
                print(f'      video mode {bda[0x49]:02X} cols {bda[0x4A]} rows {bda[0x84] + 1} '
                      f'cursor {bda[0x50]},{bda[0x51]} shape {bda[0x61]:02X}{bda[0x60]:02X}')
            prevvec = vec
        elif t == 'B':
            if '-b' in opts and '-q' not in opts:
                if prevblk is None:
                    print(f'      block {bx:04X}:{ax:04X}:')
                    for o in range(0, len(data), 16):
                        print(f'      {o:03X}: {data[o:o + 16].hex(" ")}')
                else:
                    diffs = [o for o in range(len(data)) if data[o] != prevblk[o]]
                    if diffs:
                        print('      block: ' + ' '.join(
                            f'{o:03X}:{prevblk[o]:02X}>{data[o]:02X}' for o in diffs[:200]))
            prevblk = data
        else:
            print(f'?? {t!r} nd={nd}')


if __name__ == '__main__':
    main()
