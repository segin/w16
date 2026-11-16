# szdd.py SRC DST: expand an SZDD-compressed file (COMPRESS.EXE, the
# Windows 3.1 install disks: LZSS, 4 KiB window, filled with spaces,
# starting at 4096-16). Used by tests/md2bprobe.sh.
import sys
d = open(sys.argv[1], 'rb').read()
assert d[:8] == b'SZDD\x88\xf0\x27\x33', d[:8]
n = int.from_bytes(d[10:14], 'little')
i = 14
win = bytearray(b' ' * 4096)
pos = 4096 - 16
out = bytearray()
while i < len(d) and len(out) < n:
    flags = d[i]
    i += 1
    for b in range(8):
        if i >= len(d) or len(out) >= n:
            break
        if flags & (1 << b):
            c = d[i]
            i += 1
            out.append(c)
            win[pos] = c
            pos = (pos + 1) & 4095
        else:
            lo, hi = d[i], d[i + 1]
            i += 2
            off = lo | ((hi & 0xF0) << 4)
            ln = (hi & 15) + 3
            for k in range(ln):
                c = win[(off + k) & 4095]
                out.append(c)
                win[pos] = c
                pos = (pos + 1) & 4095
open(sys.argv[2], 'wb').write(out[:n])
