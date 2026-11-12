import struct, sys
# e10dump.py E10.LOG: the ring in order, oldest first
d = open(sys.argv[1], 'rb').read()
pos, count = struct.unpack_from('<HH', d, 0)
N = 256
ring = d[4:4 + N * 14]
text = d[4 + N * 14:4 + N * 14 + N * 32]
tails = d[4 + N * 14 + N * 32:4 + N * 14 + N * 64]
n = min(count, N)
start = (pos - n) % N
for k in range(n):
    i = (start + k) % N
    tag, ax, bx, cx, dx, cs, ip = struct.unpack_from('<BHHHHHH', ring, i * 14)
    t = ''
    if tag == 0x21 and (ax >> 8) in (0x09, 0x40, 0x4B):
        t = ' ' + repr(text[i * 32:i * 32 + 32])
        if (ax >> 8) == 0x4B:
            t += ' tail ' + repr(tails[i * 32:i * 32 + 32])
    if tag == 0x21 and (ax >> 8) in (0x02, 0x06):
        t = ' ' + repr(chr(dx & 0xFF))
    if tag == 0x10 and (ax >> 8) in (0x0E, 0x09, 0x0A):
        t = ' ' + repr(chr(ax & 0xFF))
    print('%02X AX=%04X BX=%04X CX=%04X DX=%04X from %04X:%04X%s' % (tag, ax, bx, cx, dx, cs, ip, t))
