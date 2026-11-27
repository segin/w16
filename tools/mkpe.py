#!/usr/bin/env python3
"""mkpe - a small Win32 PE image (an i386 DLL or EXE with no code) holding
only a resource section, for the tests of what 16-bit Windows reads from
32-bit files (VER.DLL's version functions: tests/ver/mkinputs.py).

The layout is the Microsoft Portable Executable and Common Object File
Format Specification's: an MS-DOS header whose e_lfanew (3Ch) points at
"PE\\0\\0", the COFF file header (machine 14Ch, one section, a 224-byte
PE32 optional header with 16 data directories, the resource table the
third), one section ".rsrc" whose data is the resource tree: a type
directory, a name directory per type, a language directory per name and
a data entry per language (RVA, size, code page), then the resources'
bytes. Directory entries with the high bit set point at a subdirectory;
names are counted UTF-16 strings, offsets from the section's start.

The Win32 version resource (VS_VERSIONINFO) differs from the 16-bit one
(tools/mkver.py) by a WORD wType after wValueLength (1 text, 0 binary)
and UTF-16 keys and strings; a string's wValueLength counts characters
(Win32 SDK, VS_VERSIONINFO, String, StringTable, Var).

usage (as a module): pe(resources, dll=True) -> bytes; resources is
[(type, name, data, lang)], type and name an int or a str.
"""
import struct


def pad(b, n):
    return b + b'\0' * (-len(b) % n)


def _stub():
    """an MS-DOS header and a stub that prints a line and ends"""
    msg = b'This program requires Microsoft Windows.\r\n$'
    code = bytes([0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21,
                  0xB8, 0x01, 0x4C, 0xCD, 0x21]) + msg
    hdr = bytearray(64)
    size = 64 + len(code)
    struct.pack_into('<2sHHHHHHHHHHHHH', hdr, 0, b'MZ', size % 512, (size + 511) // 512, 0,
                     4, 0x10, 0xFFFF, 0, 0xB8, 0, 0, 0, 0x40, 0)
    return pad(bytes(hdr) + code, 16)


def _rsrc(resources, rva):
    """the .rsrc section's data for resources [(type, name, data, lang)]
    placed at rva"""
    def key(x):                          # named entries first, then ids
        return (0, x) if isinstance(x, str) else (1, x)
    tree = {}
    for t, n, data, lang in resources:
        tree.setdefault(t, {}).setdefault(n, {})[lang] = data
    # sizes: directories, then data entries, then strings, then data
    dirs = []                            # (key, list of children keys)
    out = {}

    def dsize(n):
        return 16 + 8 * n
    # lay out directories breadth first
    layout = []                          # (kind, path)
    layout.append(('dir', ()))
    for t in sorted(tree, key=key):
        layout.append(('dir', (t,)))
    for t in sorted(tree, key=key):
        for n in sorted(tree[t], key=key):
            layout.append(('dir', (t, n)))
    off = 0
    for kind, path in layout:
        if len(path) == 0:
            cnt = len(tree)
        elif len(path) == 1:
            cnt = len(tree[path[0]])
        else:
            cnt = len(tree[path[0]][path[1]])
        out[path] = off
        off += dsize(cnt)
    dataent = {}
    for t in sorted(tree, key=key):
        for n in sorted(tree[t], key=key):
            for lang in sorted(tree[t][n]):
                dataent[(t, n, lang)] = off
                off += 16
    strings = {}
    for t in tree:
        for n in [t] + list(tree[t]):
            if isinstance(n, str) and n not in strings:
                strings[n] = off
                off += 2 + 2 * len(n)
    off = (off + 3) & ~3
    datapos = {}
    for t in sorted(tree, key=key):
        for n in sorted(tree[t], key=key):
            for lang in sorted(tree[t][n]):
                datapos[(t, n, lang)] = off
                off = (off + len(tree[t][n][lang]) + 3) & ~3
    b = bytearray(off)

    def ent(pos, ident, target, isdir):
        nm = (strings[ident] | 0x80000000) if isinstance(ident, str) else ident
        struct.pack_into('<II', b, pos, nm, target | (0x80000000 if isdir else 0))

    def table(path, children, leaf):
        pos = out[path]
        named = sum(1 for c in children if isinstance(c, str))
        struct.pack_into('<IIHHHH', b, pos, 0, 0, 4, 0, named, len(children) - named)
        for i, c in enumerate(children):
            if leaf:
                ent(pos + 16 + 8 * i, c, dataent[path + (c,)], False)
            else:
                ent(pos + 16 + 8 * i, c, out[path + (c,)], True)
    table((), sorted(tree, key=key), False)
    for t in sorted(tree, key=key):
        table((t,), sorted(tree[t], key=key), False)
        for n in sorted(tree[t], key=key):
            table((t, n), sorted(tree[t][n]), True)
    for (t, n, lang), pos in dataent.items():
        struct.pack_into('<IIII', b, pos, rva + datapos[(t, n, lang)],
                         len(tree[t][n][lang]), 0, 0)
    for s, pos in strings.items():
        enc = s.encode('utf-16-le')
        struct.pack_into('<H', b, pos, len(s))
        b[pos + 2:pos + 2 + len(enc)] = enc
    for k, pos in datapos.items():
        d = tree[k[0]][k[1]][k[2]]
        b[pos:pos + len(d)] = d
    return bytes(b)


def pe(resources, dll=True, osver=(4, 0), subsys=(3, 10)):
    """a PE image with the resources [(type, name, data, lang)]"""
    stub = _stub()
    falign, salign = 0x200, 0x1000
    nsec = 1
    hdrsize = len(stub) + 4 + 20 + 224 + 40 * nsec
    hdralign = (hdrsize + falign - 1) & ~(falign - 1)
    rva = salign
    rs = _rsrc(resources, rva)
    rawsize = (len(rs) + falign - 1) & ~(falign - 1)
    image = rva + ((len(rs) + salign - 1) & ~(salign - 1))
    coff = struct.pack('<HHIIIHH', 0x14C, nsec, 0, 0, 0, 224,
                       0x0102 | (0x2000 if dll else 0) | 0x000E)
    dd = [(0, 0)] * 16
    dd[2] = (rva, len(rs))
    opt = struct.pack('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', 0x10B, 2, 50, 0, len(rs), 0, 0,
                      salign, salign, 0x10000000 if dll else 0x400000, salign, falign,
                      osver[0], osver[1], 0, 0, subsys[0], subsys[1], 0, image, hdralign, 0,
                      2, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    opt += b''.join(struct.pack('<II', a, s) for a, s in dd)
    sec = struct.pack('<8sIIIIIIHHI', b'.rsrc', len(rs), rva, rawsize, hdralign, 0, 0, 0, 0,
                      0x40000040)
    stub = bytearray(stub)
    struct.pack_into('<I', stub, 0x3C, len(stub))
    head = pad(bytes(stub) + b'PE\0\0' + coff + opt + sec, falign)
    return head + pad(rs, falign)


# ---- the Win32 version resource
def node32(key, value=b'', children=(), text=False):
    """a Win32 version node: wLength, wValueLength (characters for text),
    wType, the UTF-16 key, padding to 4, the value, padding, children"""
    vlen = len(value) // 2 if text else len(value)
    body = struct.pack('<HHH', 0, vlen, 1 if text else 0) + key.encode('utf-16-le') + b'\0\0'
    body = pad(body, 4) + value
    for c in children:
        body = pad(body, 4) + c
    return struct.pack('<H', len(body)) + body[2:]


def versioninfo32(fixedinfo, tables=(('040904E4', ()),), translations=((0x409, 1252),)):
    """a whole Win32 VS_VERSION_INFO: tables [(key, [(name, value)])]"""
    sfi = node32('StringFileInfo', children=[
        node32(key, children=[node32(k, (v + '\0').encode('utf-16-le'), text=True)
                              for k, v in strs])
        for key, strs in tables], text=True)
    vfi = node32('VarFileInfo', children=[
        node32('Translation', b''.join(struct.pack('<HH', l, c) for l, c in translations))],
        text=True)
    return node32('VS_VERSION_INFO', fixedinfo, [sfi, vfi])
