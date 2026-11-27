#!/usr/bin/env python3
"""mkinputs - the files the VER.DLL probe (tests/ver/vrp.c) works on, made
at test time with w16's own tools: version resources by tools/mkver.py
(well formed, and broken in chosen ways), modules by tools/mkne.py,
compressed copies by tools/szddenc.py. Nothing of Microsoft's.

OUTDIR/c holds the files for C:\\ (paths below it as on C:), OUTDIR/d
those for D:\\ (the cross-drive cases). OUTDIR/c/VT/LIST.TXT lists them.

usage: mkinputs.py OUTDIR
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import mkne  # noqa: E402
import mkpe  # noqa: E402
import mkver  # noqa: E402
import szddenc  # noqa: E402
from mkver import node  # noqa: E402

STRINGS = [('CompanyName', 'w16 project'), ('FileDescription', 'VER.DLL test file'),
           ('FileVersion', '3.10'), ('InternalName', 'VTEST'),
           ('LegalCopyright', 'Public domain'), ('OriginalFilename', 'VTEST.DLL'),
           ('ProductName', 'w16'), ('ProductVersion', '3.10')]


def vi(file='3.10.0.103', product=None, lang=0x409, cp=1252, type=2, subtype=0,
       os_=0x10001, strings=STRINGS, trans=None, tables=None):
    key = '%04X%04X' % (lang, cp)
    return mkver.versioninfo(mkver.fixed(file, product, os_, type, subtype),
                             tables or [(key, strings)], trans or [(lang, cp)])


def ow_style(strings=STRINGS):
    """the 16-bit VERSIONINFO OpenWatcom's wrc writes for strings given in
    the .rc as "text\\0" (observed in w16's own DDEML.DLL as built before
    this change, read as data): a string's wValueLength counts one byte
    more than the bytes written and its wLength is padded to 4 as if they
    were there; the parents' lengths add the children's wLengths"""
    def onode(key, value=b'', children=(), vlen=None, padlen=False):
        head = struct.pack('<HH', 0, len(value) if vlen is None else vlen)
        head += key.encode('latin-1') + b'\0'
        head += b'\0' * (-len(head) % 4)
        body = head + value
        claimed = len(head) + (len(value) if vlen is None else vlen)
        if padlen:
            claimed += -claimed % 4
        for c, clen in children:
            body += b'\0' * (-len(body) % 4)
            claimed += -claimed % 4
            body += c
            claimed += clen
        return struct.pack('<H', claimed) + body[2:], claimed
    strs = [onode(k, v.encode('latin-1') + b'\0', vlen=len(v) + 2, padlen=True)
            for k, v in strings]
    table = onode('040904E4', children=strs)
    sfi = onode('StringFileInfo', children=[table])
    vfi = onode('VarFileInfo', children=[onode('Translation', struct.pack('<HH', 0x409, 1252))])
    root = onode('VS_VERSION_INFO', mkver.fixed('3.10.0.0', type=2), [sfi, vfi])
    return root[0]


def setlen(b, n):
    return struct.pack('<H', n) + b[2:]


class Tree:
    def __init__(self, out):
        self.out = out
        self.names = []

    def put(self, path, data):
        p = os.path.join(self.out, *path.split('\\'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(data)
        self.names.append(path)

    def mod(self, path, res=(), module=None, shift=4):
        base = path.split('\\')[-1].split('.')[0]
        self.put(path, mkne.build(module or base.upper(), 'w16 VER test file', list(res), shift))


def ver(data, rid=1, ovr=None):
    return (16, rid, data, ovr)


def resoff(b):
    """the file offset of the first resource of an mkne file made with
    shift 0 and one resource"""
    ne = struct.unpack_from('<I', b, 0x3C)[0]
    rt = ne + struct.unpack_from('<H', b, ne + 0x24)[0]
    return struct.unpack_from('<H', b, rt + 2 + 8)[0]


def audit(t, good):
    """the VER audit's cases (probe section a and w, vrp.c): malformed
    blocks, NE headers and install pool files, in C:\\VT\\AUD and
    C:\\VT\\POOL (neither is in LIST.TXT). In the good block the
    Translation node is at 16Ch, its value at 17Ch, the block's end 180h."""
    def patch(blk, off, val):
        b = bytearray(blk)
        struct.pack_into('<H', b, off, val)
        return bytes(b)
    # VerQueryValue: value lengths past the node and the block
    for name, blk in (('TRV100', patch(good, 0x16E, 0x100)),
                      ('TRVFFFF', patch(good, 0x16E, 0xFFFF)),
                      ('TRV2', patch(good, 0x16E, 2)),
                      ('RTVFFFF', patch(good, 2, 0xFFFF))):
        t.put('VT\\AUD\\%s.BIN' % name, blk)
    # Translation's key not ended inside the block: the node is its header
    # and "Translation", the block ends there; the file has "Q" after it
    keyend = good[:0x16C] + struct.pack('<HH', 15, 0) + b'Translation'
    keyend = setlen(keyend, len(keyend))
    keyend = keyend[:0x15C] + struct.pack('<H', len(keyend) - 0x15C) + keyend[0x15E:]
    t.put('VT\\AUD\\KEYEND.BIN', keyend + b'Q')
    t.put('VT\\AUD\\KEYEND0.BIN', keyend)
    # the same with the parent and the root one byte longer: the byte
    # after "Translation" (NUL, or "Q" and the NUL past the parent's end)
    # inside them
    k1 = setlen(keyend + b'\0', len(keyend) + 1)
    k1 = k1[:0x15C] + struct.pack('<H', len(k1) - 0x15C) + k1[0x15E:]
    t.put('VT\\AUD\\KEYEND1.BIN', k1)
    t.put('VT\\AUD\\KEYENDQ.BIN', k1[:-1] + b'Q')
    # four more bytes in the root after VarFileInfo (a German entry), so
    # that a Translation value of 8 bytes reads them inside the block
    tail = setlen(good + struct.pack('<HH', 0x407, 1252) + b'\0' * 4, len(good) + 8)
    trv8 = patch(tail, 0x16E, 8)
    t.put('VT\\AUD\\TRV8.BIN', trv8)
    # VerInstallFile: destinations and sources with such blocks
    for name, blk in (('AVLEN0', patch(good, 2, 0)), ('AVLEN40', patch(good, 2, 40)),
                      ('AVLEN8', patch(good, 2, 8)), ('ATRV2', patch(good, 0x16E, 2)),
                      ('ATRV8', trv8), ('ATAIL', tail),
                      ('ALEN20', setlen(good, 20)), ('ATRVFFFF', patch(good, 0x16E, 0xFFFF)),
                      ('ATRV100', patch(good, 0x16E, 0x100))):
        t.mod('VT\\POOL\\%s.DLL' % name, [ver(blk)], module='VTEST', shift=0)
        t.mod('VT\\SRC\\%s.DLL' % name, [ver(blk)], module='VTEST', shift=0)
    # roots of 60 and 72 bytes (one of 20 with a table length of 20 too
    # is left out: 3.1 then compares whatever its buffer held before)
    for name, blk in (('ALEN60', setlen(good, 60)), ('ALEN72', setlen(good, 72))):
        t.mod('VT\\POOL\\%s.DLL' % name, [ver(blk)], module='VTEST', shift=0)
        t.mod('VT\\SRC\\%s.DLL' % name, [ver(blk)], module='VTEST', shift=0)
    # NE files: the resource table's alignment shift and offset
    base = mkne.build('GOOD', 'w16 VER test file', [ver(good)], 0)
    ne = struct.unpack_from('<I', base, 0x3C)[0]
    rt = ne + struct.unpack_from('<H', base, ne + 0x24)[0]
    for sh in (1, 2, 4, 8, 12, 13, 14, 15, 16, 17, 31, 32, 33, 40, 0xFFFF):
        b = bytearray(base)
        struct.pack_into('<H', b, rt, sh)
        t.put('VT\\AUD\\SH%d.DLL' % sh, bytes(b))
    # the same shifts with the resource at offset 0, one unit long
    for sh in (1, 8, 12, 13, 14, 15, 16, 17, 31, 32):
        b = bytearray(base)
        struct.pack_into('<H', b, rt, sh)
        struct.pack_into('<HH', b, rt + 2 + 8, 0, 1)
        t.put('VT\\AUD\\SZ%d.DLL' % sh, bytes(b))
    b = bytearray(base)
    struct.pack_into('<H', b, ne + 0x24, 0)                # the table at the header
    t.put('VT\\AUD\\RT0.DLL', bytes(b))
    # GetFileVersionInfoSize: a resource of 20 bytes (the table's length),
    # the signature after it in the file, or the file ending with it
    l20 = setlen(good, 20)
    b = mkne.build('TBL20', 'w16 VER test file', [ver(l20, ovr=20)], 0)
    t.put('VT\\AUD\\TBL20.DLL', b)
    t.put('VT\\AUD\\CUT20.DLL', b[:resoff(b) + 20])
    t.put('VT\\AUD\\CUT22.DLL', b[:resoff(b) + 22])
    l8 = setlen(good, 8)
    b = mkne.build('TBL8', 'w16 VER test file', [ver(l8, ovr=8)], 0)
    t.put('VT\\AUD\\TBL8.DLL', b)


def main():
    out = sys.argv[1]
    t = Tree(os.path.join(out, 'c'))
    d = Tree(os.path.join(out, 'd'))

    good = vi()
    # ---- C:\VT: files read by GetFileVersionInfo*, GetFileResource*
    t.mod('VT\\GOOD.DLL', [ver(good)])
    t.mod('VT\\GOOD0.DLL', [ver(good)], shift=0)           # exact byte lengths
    t.mod('VT\\NONE.DLL', [])                              # NE, no resources
    t.mod('VT\\NOVER.DLL', [(10, 1, b'just some data', None)])
    t.put('VT\\PLAIN.TXT', b'This is a plain text file, not an executable.\r\n' * 4)
    t.put('VT\\EMPTY.DLL', b'')
    t.put('VT\\STUB.EXE', mkne.stub())
    t.put('VT\\SHORT.EXE', mkne.stub()[:0x3C] + struct.pack('<I', 0x4000))   # e_lfanew past the end
    owb = ow_style()
    n = len(good)
    sfi_vfi = good[0x48:]

    def patch(off, fmt, val):
        b = bytearray(good)
        if fmt == 's':
            b[off:off + len(val)] = val
        else:
            struct.pack_into(fmt, b, off, val)
        return bytes(b)
    broken = {
        'OW': owb,                                   # OpenWatcom's lengths
        'LEN0': setlen(good, 0), 'LEN20': setlen(good, 20), 'LEN72': setlen(good, 72),
        'LEN71': setlen(good, 71), 'LEN4': setlen(good, 4), 'LEN5': setlen(good, 5),
        'LEN8': setlen(good, 8), 'LEN19': setlen(good, 19),
        # CompanyName's own length cut to its header and key (16) and less
        'CH16': patch(0x6C, '<H', 16), 'CH15': patch(0x6C, '<H', 15),
        'CH12': patch(0x6C, '<H', 12),
        'CH17': patch(0x6C, '<H', 17),
        'KEY': patch(4, 's', b'VS_VERSION_INFX'), 'KEYCASE': patch(4, 's', b'vs_version_info'),
        'SIG': patch(20, '<I', 0x12345678), 'STRUC': patch(24, '<I', 0x00020000),
        'VLEN0': patch(2, '<H', 0), 'VLEN40': patch(2, '<H', 40),
        # no fixed part at all: the children follow the key
        'NOFIXED': struct.pack('<HH', 0x14 + len(sfi_vfi), 0) + b'VS_VERSION_INFO\0' + sfi_vfi,
        # a short key: the fixed part right after it (signature at 8),
        # and the same with the signature also at 20
        'KEYSHORT': struct.pack('<HH', 8 + n - 20, 52) + b'VS\0\0' + good[20:],
        'KEYSH20': (lambda b: b[:20] + struct.pack('<I', 0xFEEF04BD) + b[24:])(
            struct.pack('<HH', 8 + n - 20, 52) + b'VS\0\0' + good[20:]),
    }
    for k, name in ((1, 'LONG1'), (2, 'LONG2'), (4, 'LONG4'), (12, 'LONG12'), (-1, 'SHORT1'),
                    (-4, 'SHORT4'), (-100, 'SHORT100')):
        broken[name] = setlen(good, n + k)
    for name, blk in broken.items():
        t.mod('VT\\%s.DLL' % name, [ver(blk)], shift=0)    # exact byte lengths
        t.put('VT\\BIN\\%s.BIN' % name, blk)             # for VerQueryValue alone
    t.put('VT\\BIN\\GOOD.BIN', good)
    gd = open(os.path.join(t.out, 'VT', 'GOOD.DLL'), 'rb').read()
    t.put('VT\\NOMZ.DLL', b'XZ' + gd[2:])                  # no MZ signature
    t.mod('VT\\OW4.DLL', [ver(owb)])                      # rounded to 16
    t.mod('VT\\OWFIT.DLL', [ver(owb, ovr=struct.unpack_from('<H', owb)[0])], shift=0)
    t.mod('VT\\TBLBIG.DLL', [ver(good, ovr=n + 40)], shift=0)    # table says 40 more
    t.mod('VT\\TBLSMALL.DLL', [ver(good, ovr=n - 40)], shift=0)  # table says 40 less
    t.mod('VT\\ID2.DLL', [ver(good, 2)])
    t.mod('VT\\NAMED.DLL', [ver(good, 'VS_VERSION_INFO')])
    t.mod('VT\\TWO.DLL', [ver(vi('2.0.0.0'), 2), ver(vi('1.0.0.0'), 1)])
    t.mod('VT\\TWO2.DLL', [ver(vi('2.0.0.0'), 1), ver(vi('1.0.0.0'), 2)])
    t.mod('VT\\STRTYPE.DLL', [('VERSION', 1, good, None)])
    t.mod('VT\\MIXED.DLL', [(10, 5, b'rcdata five', None), (7, 'FONTDIR', b'\x01\x00dir', None),
                            ver(good), (6, 1, b'\x00' * 32, None)])
    t.put('VT\\GOOD.DL_', szddenc.szdd(open(os.path.join(t.out, 'VT', 'GOOD.DLL'), 'rb').read(),
                                        ord('L')))
    # many strings, two tables, a value with no string, odd lengths
    t.mod('VT\\RICH.DLL', [ver(vi(tables=[
        ('040904E4', STRINGS + [('Comments', 'odd'), ('Empty', ''), ('X', 'y' * 300)]),
        ('040704E4', [('CompanyName', 'w16 Projekt'), ('FileDescription', 'Testdatei')])],
        trans=[(0x409, 1252), (0x407, 1252)]))])
    # lower-case hex in the table's key
    t.mod('VT\\LOWER.DLL', [ver(vi(tables=[('040904e4', STRINGS)]))])
    # a node with a value and children, keys with odd lengths
    blk = node('VS_VERSION_INFO', mkver.fixed('1.2.3.4'), [
        node('StringFileInfo', b'', [node('040904E4', b'', [node('A', b'1\0'), node('AB', b'12\0'),
                                                          node('ABC', b'123\0'),
                                                          node('ABCD', b'1234\0')])]),
        node('VarFileInfo', b'', [node('Translation', struct.pack('<HH', 0x409, 1252)),
                                  node('Other', b'\x01\x02\x03')]),
        node('Extra', b'value', [node('Child', b'cv\0')])])
    t.mod('VT\\ODD.DLL', [ver(blk)])
    t.put('VT\\ODD.BIN', blk)

    # ---- Win32 (PE) files: what VER.DLL makes of a 32-bit file's version
    # (Win32s's setup and W32SYS.DLL read those of OLE32.DLL and the like)
    fx = mkver.fixed('2.0.1.4', '3.10.0.103', 0x40004, 2)
    v32 = mkpe.versioninfo32(fx, [('040904B0', STRINGS)], [(0x409, 1200)])
    t.put('VT\\PE32.DLL', mkpe.pe([(16, 1, v32, 0x409)]))
    t.put('VT\\PE32EXE.EXE', mkpe.pe([(16, 1, v32, 0x409)], dll=False))
    t.put('VT\\PE32L0.DLL', mkpe.pe([(16, 1, v32, 0)]))
    t.put('VT\\PE32ID2.DLL', mkpe.pe([(16, 2, v32, 0x409)]))
    t.put('VT\\PE32TWO.DLL', mkpe.pe([(10, 5, b'rcdata five', 0x409), (16, 1, v32, 0x407),
                                       (16, 1, mkpe.versioninfo32(mkver.fixed('9.0.0.0')), 0x409)]))
    t.put('VT\\PE16V.DLL', mkpe.pe([(16, 1, good, 0x409)]))     # a 16-bit block in a PE file
    t.put('VT\\PENOVER.DLL', mkpe.pe([(10, 1, b'just some data', 0x409)]))
    t.put('VT\\PENAMED.DLL', mkpe.pe([('VERSION', 1, v32, 0x409)]))
    t.put('VT\\BIN\\PE32.BIN', v32)

    # ---- install pool (copied into place by the probe before each case)
    pool = [('V300', vi('3.0.0.0')), ('V310', vi()), ('V310B', vi('3.10.0.104')),
            ('V400', vi('4.0.0.0')), ('VPROD', vi('3.10.0.103', '4.0.0.0')),
            ('VFILE', vi('4.0.0.0', '3.0.0.0')), ('VFLO', vi('3.0.0.0', '4.0.0.0')),
            ('VGER', vi(lang=0x407)), ('VCP', vi(cp=850)), ('VCP1250', vi(cp=1250)),
            ('VNEU', vi(lang=0, cp=0)), ('VLANG0', vi(lang=0)), ('VCP0', vi(cp=0)),
            ('VGER437', vi(lang=0x407, cp=437)),
            ('VMULTI', vi(tables=[('040704E4', STRINGS), ('040904E4', STRINGS)],
                          trans=[(0x407, 1252), (0x409, 1252)])),
            ('VMULTI2', vi(tables=[('040904E4', STRINGS), ('040704E4', STRINGS)],
                           trans=[(0x409, 1252), (0x407, 1252)])),
            ('VTYPE', vi(type=1)), ('VSUB', vi(type=3, subtype=4)), ('VOS', vi(os_=0x10000)),
            ('V200GER', vi('2.0.0.0', lang=0x407)), ('V400GER', vi('4.0.0.0', lang=0x407)),
            ('V400TYP', vi('4.0.0.0', type=1)),
            ('VCP932', vi(lang=0x411, cp=932)), ('VCP1200', vi(cp=1200)),
            ('VCP437', vi(cp=437)), ('VCPFFFF', vi(cp=0xFFFF)),
            ('VUS932', vi(cp=932)),
            ('VNOTR', node('VS_VERSION_INFO', mkver.fixed('3.10.0.103', type=2)))]
    for name, blk in pool:
        t.mod('VT\\POOL\\%s.DLL' % name, [ver(blk)], module='VTEST')
    t.mod('VT\\POOL\\VNONE.DLL', [], module='VTEST')
    t.put('VT\\POOL\\TEXT.TXT', b'A text file to install.\r\n' * 20)
    t.put('VT\\POOL\\TEXT2.TXT', b'Another text file.\r\n' * 30)
    t.mod('VT\\POOL\\VBAD.DLL', [ver(ow_style())], module='VTEST', shift=0)
    # sources: plain and compressed (COMPRESS-style names and /r)
    for name, blk in pool:
        t.mod('VT\\SRC\\%s.DLL' % name, [ver(blk)], module='VTEST')
    t.mod('VT\\SRC\\VNONE.DLL', [], module='VTEST')
    t.mod('VT\\SRC\\VBAD.DLL', [ver(ow_style())], module='VTEST', shift=0)
    t.put('VT\\SRC\\TEXT.TXT', b'A text file to install.\r\n' * 20)
    v4 = mkne.build('VTEST', 'w16 VER test file', [ver(vi('4.0.0.0'))])
    v3 = mkne.build('VTEST', 'w16 VER test file', [ver(vi('3.0.0.0'))])
    t.put('VT\\SRC\\C400.DL_', szddenc.szdd(v4, ord('L')))
    t.put('VT\\SRC\\C300.DL_', szddenc.szdd(v3, ord('L')))
    t.put('VT\\SRC\\R400.DLL', szddenc.szdd(v4, ord('L')))      # compressed, not renamed
    t.put('VT\\SRC\\CTRUNC.DL_', szddenc.szdd(v4, ord('L'))[:200])
    t.put('VT\\SRC\\CBADALG.DL_', szddenc.szdd(v4, ord('L'), alg=b'B'))
    t.put('VT\\SRC\\TEXT2.TX_', szddenc.szdd(b'Another text file.\r\n' * 30, ord('T')))
    big = bytes((i * 7 + (i >> 8)) & 0xFF for i in range(100000))
    t.put('VT\\SRC\\BIG.DAT', big)
    t.put('VT\\SRC\\BIG.DA_', szddenc.szdd(big, ord('T')))
    t.put('VT\\SRC\\lower.dll', v4)
    # modules loaded while installing over them (module name = file name)
    t.mod('VT\\POOL\\VUSE.DLL', [ver(vi('3.0.0.0'))], module='VUSE')
    t.mod('VT\\POOL\\VUSEX.DLL', [ver(vi('3.0.0.0'))], module='XUSE')

    # ---- VerFindFile: copies in the places OpenFile searches
    for p in ('WINDOWS\\VFWIN.DLL', 'WINDOWS\\SYSTEM\\VFSYS.DLL', 'VT\\APP\\VFAPP.DLL',
              'VT\\PATH\\VFPATH.DLL', 'VT\\CWD\\VFCWD.DLL', 'VFEXE.DLL',
              'WINDOWS\\VFBOTH.DLL', 'WINDOWS\\SYSTEM\\VFBOTH.DLL',
              'WINDOWS\\SYSTEM\\VUSE.DLL', 'WINDOWS\\SYSTEM\\VUSEX.DLL',
              'WINDOWS\\SYSTEM\\VUSE2.DLL', 'VT\\OTHER\\VUSE2.DLL', 'VT\\APP\\VFAPPW.DLL',
              'WINDOWS\\VFAPPW.DLL', 'VT\\APP\\VFAPPP.DLL', 'VT\\PATH\\VFAPPP.DLL',
              'VT\\APP\\VFAPPS.DLL', 'WINDOWS\\SYSTEM\\VFAPPS.DLL', 'VT\\APP\\VFAPPC.DLL',
              'VT\\CWD\\VFAPPC.DLL', 'VT\\CWD\\VFCWDS.DLL', 'WINDOWS\\SYSTEM\\VFCWDS.DLL',
              'VT\\CWD\\VFCWDW.DLL', 'WINDOWS\\VFCWDW.DLL', 'VT\\PATH\\VFSYSP.DLL',
              'WINDOWS\\SYSTEM\\VFSYSP.DLL', 'VFEXEW.DLL', 'WINDOWS\\VFEXEW.DLL'):
        base = p.split('\\')[-1].split('.')[0]
        mod = {'VUSEX': 'XUSE'}.get(base, base)
        t.mod(p, [ver(vi())], module=mod)
    t.put('VT\\DST\\KEEP', b'')
    t.put('VT\\CUR\\KEEP', b'')
    t.put('VT\\CUR2\\KEEP', b'')

    audit(t, good)

    # ---- D:
    for name, blk in pool[:4]:
        d.mod('VT\\SRC\\%s.DLL' % name, [ver(blk)], module='VTEST')
    d.put('VT\\SRC\\C400.DL_', szddenc.szdd(v4, ord('L')))
    d.put('VT\\DST\\KEEP', b'')
    d.put('VT\\CUR\\KEEP', b'')

    lst = '\r\n'.join(n for n in t.names if n.startswith('VT\\') and '\\' not in n[3:]) + '\r\n'
    t.put('VT\\LIST.TXT', lst.encode())


if __name__ == '__main__':
    main()
