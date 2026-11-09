#!/usr/bin/env python3
"""mkw3.py - build w16's WIN386.EXE: an MZ program followed by a W3 table of
LE modules, the container layout of Windows 3.1's WIN386.EXE (docs/
win386-vxds.md, from readexe's reading of the W3 format):

  MZ header (e_lfarlc >= 40h, e_lfanew at 3Ch -> the W3 header)
  the real-mode program (what DOS loads)
  W3 header: "W3", VMM version (030Ah), module count, 10 bytes of zeros
  the module table: 16 bytes each - name (8, blank padded), file offset of
      the module's LE header, size of its header, loader and fixup sections
  each module: its LE header and loader/fixup sections (no MZ stub), then
      its data pages; the LE data-pages offset is rewritten to be from the
      start of WIN386.EXE. The non-resident name table is not carried (as
      in Windows 3.1's file).

usage: mkw3.py OUT.EXE LOADER.EXE NAME=MODULE.386 [NAME=MODULE.386 ...]
MODULE.386 is an LE file as wlink writes it (MZ stub + LE).
"""
import struct
import sys

ALIGN = 0x200


def pad(b, a=ALIGN):
    return b + b'\0' * (-len(b) % a)


def rebuild_mz(exe):
    """Return the MZ program with a 40h-byte header, relocations at 40h."""
    (magic, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs,
     lfarlc, ovno) = struct.unpack_from('<2s13H', exe, 0)
    assert magic == b'MZ'
    size = (cp - 1) * 512 + (cblp or 512) if cp else len(exe)
    image = exe[cparhdr * 16:size]
    relocs = exe[lfarlc:lfarlc + 4 * crlc]
    hdr = bytearray(0x40) + relocs
    hdr = pad(bytes(hdr), 16)
    total = len(hdr) + len(image)
    out = bytearray(hdr + image)
    struct.pack_into('<2s13H', out, 0, b'MZ', total % 512, (total + 511) // 512, crlc,
                     len(hdr) // 16, minalloc, maxalloc, ss, sp, 0, ip, cs, 0x40, 0)
    return out


def le_parts(path):
    d = open(path, 'rb').read()
    lfanew = struct.unpack_from('<I', d, 0x3C)[0] if d[:2] == b'MZ' else 0
    le = d[lfanew:]
    assert le[:2] == b'LE', path
    npages, = struct.unpack_from('<I', le, 0x14)
    pagesize, lastpage = struct.unpack_from('<II', le, 0x28)
    fpt, = struct.unpack_from('<I', le, 0x68)
    fixsize, = struct.unpack_from('<I', le, 0x30)
    datapages, = struct.unpack_from('<I', le, 0x80)
    hlen = fpt + fixsize
    data = d[datapages:datapages + (npages - 1) * pagesize + (lastpage if npages else 0)]
    return bytearray(le[:hlen]), data


def main():
    out, loader = sys.argv[1], sys.argv[2]
    mods = [a.split('=', 1) for a in sys.argv[3:]]
    mz = pad(bytes(rebuild_mz(open(loader, 'rb').read())))
    w3off = len(mz)
    table_len = 16 + 16 * len(mods)
    pos = w3off + len(pad(b'\0' * table_len))
    entries, blobs = [], []
    for name, path in mods:
        hdr, data = le_parts(path)
        hoff = pos
        dataoff = hoff + len(pad(bytes(hdr)))
        struct.pack_into('<I', hdr, 0x80, dataoff)
        struct.pack_into('<III', hdr, 0x88, 0, 0, 0)    # no non-resident names
        entries.append((name.upper().ljust(8)[:8].encode(), hoff, len(hdr)))
        blob = pad(bytes(hdr)) + pad(data)
        blobs.append(blob)
        pos += len(blob)
    w3 = struct.pack('<2sHH10s', b'W3', 0x030A, len(mods), b'\0' * 10)
    for n, o, s in entries:
        w3 += struct.pack('<8sII', n, o, s)
    res = bytearray(mz) + pad(w3)
    for b in blobs:
        res += b
    struct.pack_into('<I', res, 0x3C, w3off)
    open(out, 'wb').write(res)


if __name__ == '__main__':
    main()
