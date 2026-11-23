import sys
# scrdesc.py IN OUT TEXT: a copy of one of w16's own screen savers (an NE
# file) whose module description (the non-resident name table's first
# entry) reads "SCRNSAVE : TEXT", padded with blanks or cut to the
# original's length so nothing after it moves (tests/cplscenarios.sh)
src, dst, text = sys.argv[1], sys.argv[2], sys.argv[3]
b = bytearray(open(src, 'rb').read())
ne = int.from_bytes(b[0x3C:0x40], 'little')
assert b[ne:ne + 2] == b'NE'
nonres = int.from_bytes(b[ne + 0x2C:ne + 0x30], 'little')
n = b[nonres]
new = ('SCRNSAVE : ' + text).encode('latin-1')[:n].ljust(n, b' ')
b[nonres + 1:nonres + 1 + n] = new
open(dst, 'wb').write(b)
