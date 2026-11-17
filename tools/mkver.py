#!/usr/bin/env python3
"""mkver - write a Windows 3.x version resource (VS_VERSIONINFO, the
16-bit layout) as raw data, for an .rc to include as resource type 16.

OpenWatcom's resource compiler writes a 16-bit VERSIONINFO whose length
words do not match its data (by some bytes, depending on the strings),
and Windows 3.1's VER.DLL then finds no version in the file:
GetFileVersionInfoSize returns 0 (observed with w16's LZEXPAND.DLL and
DDEML.DLL; ARCHITECTURE.md, LZEXPAND). This writes the block itself.

The layout (Windows 3.1 SDK, Programmer's Reference Vol. 3,
VS_VERSIONINFO and VS_FIXEDFILEINFO; Vol. 4, the resource script's
VERSIONINFO statement): each node is WORD wLength, WORD wValueLength, the
key (NUL-terminated), padding to 4 bytes, the value, padding to 4, then
its children; wLength covers the node and its children, not the padding
after its last byte. The root's key is "VS_VERSION_INFO" and its value a
VS_FIXEDFILEINFO (signature FEEF04BDh, structure version 1.0); then a
"StringFileInfo" node holding one string table per language (key: the
language and code page as 8 hex digits) of NUL-terminated strings, and a
"VarFileInfo" node holding "Translation" (pairs of WORDs).

usage: mkver.py OUT --file 3.10.0.103 [--product 3.10.0.103] [--os 10001]
                [--type 2] [--subtype 0] [--flagsmask 3F] [--flags 0]
                [--lang 040904E4] [--translation 409,1252] KEY=VALUE ...
"""
import argparse
import struct


def pad4(b):
    return b + b'\0' * (-len(b) % 4)


def node(key, value=b'', children=(), text=False):
    head = struct.pack('<HH', 0, len(value)) + key.encode('latin-1') + b'\0'
    body = pad4(head)
    body += value
    for c in children:
        body = pad4(body) + c
    return struct.pack('<H', len(body)) + body[2:]


def ver4(s):
    a, b, c, d = (int(x) for x in s.split('.'))
    return (a << 16) | b, (c << 16) | d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--file', required=True)
    ap.add_argument('--product')
    ap.add_argument('--os', default='10001')
    ap.add_argument('--type', default='1')
    ap.add_argument('--subtype', default='0')
    ap.add_argument('--flagsmask', default='3F')
    ap.add_argument('--flags', default='0')
    ap.add_argument('--lang', default='040904E4')
    ap.add_argument('--translation', default='409,1252')
    ap.add_argument('strings', nargs='*')
    a = ap.parse_args()
    fms, fls = ver4(a.file)
    pms, pls = ver4(a.product or a.file)
    fixed = struct.pack('<13I', 0xFEEF04BD, 0x00010000, fms, fls, pms, pls,
                        int(a.flagsmask, 16), int(a.flags, 16), int(a.os, 16),
                        int(a.type, 16), int(a.subtype, 16), 0, 0)
    strs = []
    for kv in a.strings:
        k, v = kv.split('=', 1)
        strs.append(node(k, v.encode('latin-1') + b'\0'))
    lang, cp = (int(x, 16) if i == 0 else int(x) for i, x in enumerate(a.translation.split(',')))
    sfi = node('StringFileInfo', children=[node(a.lang, children=strs)])
    vfi = node('VarFileInfo', children=[node('Translation', struct.pack('<HH', lang, cp))])
    root = node('VS_VERSION_INFO', fixed, [sfi, vfi])
    open(a.out, 'wb').write(root)


if __name__ == '__main__':
    main()
