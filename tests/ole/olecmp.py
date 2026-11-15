#!/usr/bin/env python3
"""olecmp - compare two OLE observation runs (tests/ole/ocrun.sh output):
Microsoft's OLECLI against w16's, or any two.

  - oc.log: the client calls, callbacks, stream traffic and DDE messages,
    after masking what differs from run to run by nature: handle values
    (client documents, the hMF word of a saved METAFILEPICT, dead windows),
    checksums over GlobalSize-rounded blocks whose tail is uninitialised
    memory, and the TASK lines (a server rewrites its command tail);
  - the files the run left: OLE streams (*.BIN that start 01 05 00 00) are
    compared field by field with the hMF word and a metafile's bytes past
    its own size masked; metafile files (01 00 09 00) up to their size;
    .BMP files by pixels.

usage: olecmp.py DIR_A DIR_B        exit status 1 when they differ
"""
import os
import re
import struct
import sys


def norm_log(path):
    out = []
    if not os.path.exists(path):
        return ['(no log)']
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('TASK '):
            continue
        line = re.sub(r'lh=[0-9A-F]{8}', 'lh=*', line)
        line = re.sub(r'dead:[0-9A-F]{4}', 'dead:*', line)
        line = re.sub(r'(OCSRV .*psptail=[0-9A-F]{4}:)"(.*)', lambda m: m.group(1) + '*', line)
        # the hMF word of a METAFILEPICT put to or got from a stream
        line = re.sub(r'^((?:PUT|GET) 8 @\d+ [0-9A-F]{12})[0-9A-F]{4}', r'\1****', line)
        line = re.sub(r'^(OleSaveToStream = .*) sum=[0-9A-F]{8}', r'\1 sum=*', line)
        line = re.sub(r'mf:size=(\d+) sum=[0-9A-F]{8}', r'mf:size=\1 sum=*', line)
        # an OwnerLink/ObjectLink block: its three strings (the rest of the
        # GlobalAlloc rounding is uninitialised in Microsoft's)
        m = re.match(r'(.*"(?:OwnerLink|ObjectLink)"(?: = OLE_OK)?:? size=\d+) sum=[0-9A-F]{8} '
                     r'([0-9A-F]*)', line)
        if m:
            b = bytes.fromhex(m.group(2)[:len(m.group(2)) // 2 * 2])
            line = m.group(1) + ' ' + repr(b.split(b'\0')[:3])
        # sums over blocks with a rounded, uninitialised tail: Native data
        # from GetData or the clipboard, OC1's pixel buffers (row padding)
        line = re.sub(r'("Native"(?: = OLE_OK)?:? size=\d+) sum=[0-9A-F]{8}', r'\1 sum=*', line)
        line = re.sub(r'pixels sum=[0-9A-F]{8}', 'pixels sum=*', line)
        line = re.sub(r'^(DDE P POKE .* size=\d+) sum=[0-9A-F]{8}', r'\1 sum=*', line)
        line = re.sub(r'^(OleSaveToStream = .*bytes=\d+ puts=\d+) sum=\S+', r'\1', line)
        # poke blocks of StdDocDimensions etc. carry the last block's bytes
        m = re.match(r'(DDE P POKE .*item="Std\w+" flags=0000 fmt="Binary") size=\d+ sum=\S+ (\S+)', line)
        if m:
            line = m.group(1)
        out.append(line)
    return out


def mf_size(d, off=0):
    if len(d) - off < 18:
        return len(d) - off
    return struct.unpack_from('<I', d, off + 6)[0] * 2


def payload(b):
    """the length of Native data inside its GlobalSize-rounded block, for
    the formats whose length is known (a .BMP file, a RIFF file)"""
    if b[:2] == b'BM' and len(b) >= 6:
        return struct.unpack_from('<I', b, 2)[0]
    if b[:4] == b'RIFF' and len(b) >= 8:
        return struct.unpack_from('<I', b, 4)[0] + 8
    return None


class Stream:
    """walk an OLE 1.0 stream, recording the byte ranges to mask"""

    def __init__(self, d):
        self.d, self.p, self.mask = d, 0, []

    def dw(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]
        self.p += 4
        return v

    def s(self):
        n = self.dw()
        v = self.d[self.p:self.p + n]
        self.p += n
        return v.rstrip(b'\0')

    def body(self, cls):
        if cls == b'METAFILEPICT':
            self.dw(), self.dw()
            n = self.dw()
            self.mask.append((self.p + 6, self.p + 8))        # hMF
            mf = self.p + 8
            size = mf_size(self.d, mf)
            self.mask.append((mf + size, self.p + n))           # rounded tail
            self.p += n
        elif cls in (b'DIB', b'BITMAP'):
            self.dw(), self.dw()
            self.p += self.dw()
        else:
            f = self.dw()
            if f == 0:
                self.s()
            self.native(self.dw())

    def native(self, n):
        k = payload(self.d[self.p:self.p + n])
        if k is not None and k < n:
            self.mask.append((self.p + k, self.p + n))
        self.p += n

    def walk(self):
        self.dw()
        fmt = self.dw()
        cls = self.s()
        if fmt == 3:
            self.body(cls)
            return
        if fmt == 1:
            self.s(), self.s(), self.s(), self.dw(), self.dw()
        else:
            self.s(), self.s()
            self.native(self.dw())
        self.dw()
        if self.dw() == 5:
            self.body(self.s())


def masked(d):
    d = bytearray(d)
    if d[:4] == b'\x01\x05\x00\x00':
        st = Stream(bytes(d))
        try:
            st.walk()
        except struct.error:
            return bytes(d)
        for a, b in st.mask:
            for i in range(a, min(b, len(d))):
                d[i] = 0
    elif d[:4] == b'\x01\x00\x09\x00':
        d = d[:mf_size(bytes(d))]
    return bytes(d)


def bmp_pixels(path):
    try:
        from PIL import Image
        return Image.open(path).convert('RGB').tobytes()
    except Exception:
        return open(path, 'rb').read()


def main():
    a, b = sys.argv[1], sys.argv[2]
    bad = 0
    la, lb = norm_log(os.path.join(a, 'oc.log')), norm_log(os.path.join(b, 'oc.log'))
    if la != lb:
        import difflib
        bad += 1
        print('oc.log differs:')
        for line in list(difflib.unified_diff(la, lb, a, b, n=1, lineterm=''))[:60]:
            print('  ' + line[:220])
    names = sorted(set(os.listdir(a)) | set(os.listdir(b)))
    for n in names:
        if not re.match(r'.*\.(BIN|BMP|WRI|CRD)$', n):
            continue
        pa, pb = os.path.join(a, n), os.path.join(b, n)
        if not os.path.exists(pa) or not os.path.exists(pb):
            print('%s: only in %s' % (n, a if os.path.exists(pa) else b))
            bad += 1
            continue
        if n.endswith('.BMP'):
            same = bmp_pixels(pa) == bmp_pixels(pb)
        else:
            same = masked(open(pa, 'rb').read()) == masked(open(pb, 'rb').read())
        if not same:
            print('%s differs' % n)
            bad += 1
    sys.exit(1 if bad else 0)


main()
