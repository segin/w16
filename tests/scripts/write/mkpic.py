"""mkpic.py OUT.BMP: a 40 x 20, 16-colour test picture for Write's picture
probes (our own: colour bars and a frame), as a Windows 3.x .BMP"""
import struct
import sys

W, H = 40, 20
PAL = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
       (0, 128, 128), (192, 192, 192), (128, 128, 128), (255, 0, 0), (0, 255, 0),
       (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]


def px(x, y):
    if x in (0, W - 1) or y in (0, H - 1):
        return 0
    return 9 + (x * 6 // W)


rows = b''
for y in range(H - 1, -1, -1):
    r = bytearray()
    for x in range(0, W, 2):
        r.append((px(x, y) << 4) | px(x + 1, y))
    while len(r) % 4:
        r.append(0)
    rows += bytes(r)
bih = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 4, 0, len(rows), 0, 0, 16, 0)
pal = b''.join(struct.pack('<BBBB', b, g, r, 0) for r, g, b in PAL)
off = 14 + len(bih) + len(pal)
bfh = struct.pack('<2sIHHI', b'BM', off + len(rows), 0, 0, off)
open(sys.argv[1], 'wb').write(bfh + bih + pal + rows)
