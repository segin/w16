#!/usr/bin/env python3
"""Read a Windows 3.1 registration database (REG.DAT).

usage: tools/regdat.py [-r] REG.DAT       the keys, as REGEDIT /V shows them
       tools/regdat.py -d REG.DAT         the raw tables (header, entries,
                                          text blocks), for diffing

The format, as observed black-box on Windows 3.1 (probe SH1,
tests/apps/sh1.c; ARCHITECTURE.md 13): never from SHELL.DLL's code.

  header (32 bytes)
    00  8 bytes  "SHCC3.10"
    08  DWORD    offset of the entry table (20h)
    0C  DWORD    20h (UNVERIFIED: meaning; never seen otherwise)
    10  DWORD    number of 8-byte entries
    14  DWORD    offset of the text area (= 20h + 8 * entries)
    18  DWORD    size of the text area
    1C  WORD     number of hash buckets (25h = 37)
    1E  WORD     first free entry (0: none)
  entries (8 bytes each, 4 WORDs), by index:
    0              the top node: { 0, first child, 0, 0 }; its one child
                   is the node of ".classes" = HKEY_CLASSES_ROOT
    1 .. buckets   hash buckets: { first text entry, 0, 0, 0 }; a chain
                   is circular, its last entry points back at the bucket
    key node       { next sibling, first child, name text, value text }
                   (0 = none); children newest first
    text entry     { next in hash chain, reference count, length, offset
                   in the text area }; names and values share the strings
    free entry     { next free entry, 0, 0, 0 }
  text area: blocks, each a WORD then its bytes: an odd WORD (2 * entry +
  1) starts a used string of that text entry (no NUL); an even WORD is a
  free block of that many bytes, the WORD included (UNVERIFIED: see -d).
  A string's bucket is 1 + (sum of its upper-cased bytes) % buckets.
"""
import struct
import sys


def load(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'SHCC3.10', 'not a REG.DAT'
    tab, h2, cnt, toff, tsize = struct.unpack_from('<5I', d, 8)
    nb, free = struct.unpack_from('<2H', d, 0x1c)
    ents = [struct.unpack_from('<4H', d, tab + 8 * i) for i in range(cnt)]
    return d, (tab, h2, cnt, toff, tsize, nb, free), ents


def text(d, hdr, ents, i):
    if not i:
        return None
    e = ents[i]
    return d[hdr[3] + e[3]:hdr[3] + e[3] + e[2]].decode('latin-1')


def tree(d, hdr, ents, node, path, out):
    c = ents[node][1]
    while c:
        e = ents[c]
        p = path + '\\' + text(d, hdr, ents, e[2]) if path else text(d, hdr, ents, e[2])
        v = text(d, hdr, ents, e[3])
        out.append(p + (' = ' + v if v is not None else ''))
        tree(d, hdr, ents, c, p, out)
        c = e[0]


def raw(d, hdr, ents):
    tab, h2, cnt, toff, tsize, nb, free = hdr
    print('header: table %#x, h2 %#x, entries %d, text %#x size %#x, buckets %d, free %d'
          % (tab, h2, cnt, toff, tsize, nb, free))
    for i, e in enumerate(ents):
        print('%4x  %04x %04x %04x %04x' % ((i,) + e))
    o = 0
    while o < tsize:
        w, = struct.unpack_from('<H', d, toff + o)
        if w & 1:
            e = ents[w >> 1]
            print('text %4x: entry %x %r' % (o, w >> 1, d[toff + o + 2:toff + o + 2 + e[2]]))
            o += 2 + e[2]
        else:
            print('text %4x: free %d bytes' % (o, w))
            if not w:
                break
            o += w


def main():
    args = sys.argv[1:]
    mode = 'tree'
    if args and args[0] == '-d':
        mode = 'raw'
        args = args[1:]
    d, hdr, ents = load(args[0])
    if mode == 'raw':
        raw(d, hdr, ents)
        return
    out = []
    tree(d, hdr, ents, 0, '', out)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
