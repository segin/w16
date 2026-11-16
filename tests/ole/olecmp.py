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
        if line.startswith('TASK ') or line.startswith('OC1: ExitWindows refused'):
            continue
        line = re.sub(r'lh=[0-9A-F]{8}', 'lh=*', line)
        line = re.sub(r'dead:[0-9A-F]{4}', 'dead:*', line)
        # instance handles (memory layout) and Write's object names (its
        # own, from the time: "WINWRITE%(Untitled)%6af987fe#001")
        line = re.sub(r'^WinExec = \d+', 'WinExec = *', line)
        line = re.sub(r'(%)[0-9a-f]{8}(#\d{3})', r'\1*\2', line)
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
        line = re.sub(r'^((?:OleSaveToStream = )?\S+ bytes=\d+ puts=\d+) sum=\S+', r'\1', line)
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


def mf_mask(d, off=0):
    """zero the row padding of the DIBs in a metafile's records (DIB
    StretchBlt 0B41, StretchDIBits 0F43): Microsoft's OLECLI's metafile of
    a Paintbrush picture has other padding bytes than its Native DIB
    (deterministic, but not reproduced: ARCHITECTURE.md 16)"""
    end = off + mf_size(bytes(d), off)
    p = off + 18
    while p + 6 <= end:
        n, fn = struct.unpack_from('<IH', d, p)
        if n < 3:
            break
        if fn in (0x0B41, 0x0F43):
            bi = p + 6 + (2 * 11 if fn == 0x0F43 else 2 * 10)
            if bi + 40 <= len(d) and struct.unpack_from('<I', d, bi)[0] == 40:
                w, h, _, bpp = struct.unpack_from('<iiHH', d, bi + 4)
                used = struct.unpack_from('<I', d, bi + 32)[0]
                ncol = used if used else (1 << bpp if bpp <= 8 else 0)
                bits = bi + 40 + 4 * ncol
                row = (w * bpp + 31) // 32 * 4
                data = (w * bpp + 7) // 8
                for y in range(abs(h)):
                    for i in range(bits + y * row + data, bits + (y + 1) * row):
                        if i < len(d):
                            d[i] = 0
        p += 2 * n
    return d


def wri_part(d):
    """a Write document's text and pictures (header fcMac), with the OLE
    objects' number (Write's own, from the clock) and the word after it
    masked. Left out: the word 8 bytes on and the character and paragraph
    property pages. Microsoft's OLECLI gives the same bytes there in every
    run, w16's others: by their values (0x1257 against 0x1227, local
    offsets 0x30 apart) they look like Write's own handles, which depend on
    the memory layout. UNVERIFIED that they are handles."""
    d = bytearray(d)
    if len(d) < 18 or d[:2] not in (b'\x31\xbe', b'\x32\xbe'):
        return bytes(d)
    fcmac = struct.unpack_from('<I', d, 14)[0]
    p = 128
    while True:
        p = d.find(b'\xe4\x00', p, fcmac)
        if p < 0:
            break
        # the OLE picture header: mm 0xE4 ... the object number at +24,
        # cbHeader 40 at +30; the stream follows the 40 bytes
        if p + 40 <= len(d) and struct.unpack_from('<H', d, p + 30)[0] == 40:
            for i in list(range(p + 24, p + 28)) + list(range(p + 32, p + 36)):
                d[i] = 0
            p += 40
        else:
            p += 2
    return masked(bytes(d[:fcmac]))


def masked(d):
    """the file with the bytes that differ by nature zeroed: in an OLE
    stream, or in the OLE streams inside a document (Write, Cardfile)"""
    d = bytearray(d)
    if d[:4] == b'\x01\x00\x09\x00':
        return bytes(mf_mask(d)[:mf_size(bytes(d))])
    if d[:2] == b'BM' or d[:4] == b'RIFF':
        k = payload(bytes(d))
        return bytes(d[:k] if k else d)
    raw = bytes(d)
    p = raw.find(b'\x01\x05\x00\x00')
    while p >= 0:
        if p + 12 <= len(raw) and struct.unpack_from('<I', raw, p + 4)[0] in (1, 2, 3) \
                and 0 < struct.unpack_from('<I', raw, p + 8)[0] < 256:
            st = Stream(raw)
            st.p = p
            try:
                st.walk()
                for a, b in st.mask:
                    for i in range(a, min(b, len(d))):
                        d[i] = 0
                p = raw.find(b'\x01\x05\x00\x00', max(st.p, p + 1))
                continue
            except (struct.error, IndexError):
                pass
        p = raw.find(b'\x01\x05\x00\x00', p + 1)
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
        elif n.endswith('.WRI'):
            same = wri_part(open(pa, 'rb').read()) == wri_part(open(pb, 'rb').read())
        else:
            same = masked(open(pa, 'rb').read()) == masked(open(pb, 'rb').read())
        if not same:
            print('%s differs' % n)
            bad += 1
    sys.exit(1 if bad else 0)


main()
