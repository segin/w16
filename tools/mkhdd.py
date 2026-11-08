#!/usr/bin/env python3
"""mkhdd - make a bootable DOS hard disk image for DOSBox (test tooling).

One active FAT16 partition, formatted with mtools, booting the DOS whose
system files and boot sector are given (in w16: MS-DOS 4.00 built from
Microsoft's MIT-licensed source, see tests/msdos-build.sh). The MBR is
w16's own (tools/dos/mbr.asm).

usage: mkhdd.py OUT.IMG --mbr MBR.BIN --boot MSBOOT.BIN --io IO.SYS
                --dos MSDOS.SYS [--cyl N] [FILE[=DEST] | DIR[=DEST]] ...
Files land in the root, or at DEST (a DOS path such as WIN386/X.EXE);
a directory is copied recursively. Geometry: N cylinders x 16 heads x
63 sectors (default 130, about 64 MB), as for DOSBox's
`imgmount 2 OUT.IMG -size 512,63,16,N -t hdd`.
"""
import argparse
import os
import struct
import subprocess
import sys

HEADS, SECTORS = 16, 63


def chs(lba, cyls):
    c = lba // (HEADS * SECTORS)
    h = (lba // SECTORS) % HEADS
    s = lba % SECTORS + 1
    c = min(c, 1023)
    return bytes([h, s | ((c >> 2) & 0xC0), c & 0xFF])


def mtools(*args, img):
    env = dict(os.environ, MTOOLS_SKIP_CHECK='1', MTOOLS_NO_VFAT='1')
    subprocess.run(args, check=True, env=env)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--mbr', required=True)
    ap.add_argument('--boot', required=True)
    ap.add_argument('--io', required=True)
    ap.add_argument('--dos', required=True)
    ap.add_argument('--cyl', type=int, default=130)
    ap.add_argument('files', nargs='*')
    a = ap.parse_args()

    total = a.cyl * HEADS * SECTORS
    start = SECTORS                         # partition at cylinder 0, head 1
    count = total - start
    with open(a.out, 'wb') as f:
        f.truncate(total * 512)

    # MBR: our code, one active FAT16 (>32M: type 06h) partition
    mbr = bytearray(open(a.mbr, 'rb').read()[:446].ljust(446, b'\0'))
    entry = (b'\x80' + chs(start, a.cyl) + bytes([0x06 if count >= 65536 else 0x04])
             + chs(total - 1, a.cyl) + struct.pack('<II', start, count))
    mbr += entry + bytes(48) + b'\x55\xAA'
    with open(a.out, 'r+b') as f:
        f.write(mbr)

    img = '%s@@%d' % (a.out, start * 512)
    mtools('mformat', '-i', img, '-T', str(count), '-h', str(HEADS), '-s', str(SECTORS),
           '-H', str(start), '-v', 'W16TEST', '::', img=img)

    # DOS's boot code, keeping the BPB mformat wrote (bytes 3..3Dh)
    boot = open(a.boot, 'rb').read()
    if len(boot) > 512:                     # assembled at ORG 7C00h
        boot = boot[0x7C00:0x7C00 + 512]
    with open(a.out, 'r+b') as f:
        f.seek(start * 512)
        vbr = bytearray(f.read(512))
        new = bytearray(boot)
        new[3:0x3E] = vbr[3:0x3E]
        new[3:11] = b'MSDOS4.0'
        new[0x24] = 0x80                    # BIOS drive number
        f.seek(start * 512)
        f.write(new)

    # system files first: the boot code expects them as the first two entries
    for src, name in ((a.io, 'IO.SYS'), (a.dos, 'MSDOS.SYS')):
        mtools('mcopy', '-i', img, src, '::' + name, img=img)
        mtools('mattrib', '-i', img, '+s', '+h', '+r', '::' + name, img=img)
    for spec in a.files:
        src, _, dest = spec.partition('=')
        if os.path.isdir(src):
            dest = dest or os.path.basename(src.rstrip('/'))
            mtools('mcopy', '-i', img, '-s', src, '::' + dest, img=img)
        else:
            dest = dest or os.path.basename(src).upper()
            mtools('mcopy', '-i', img, src, '::' + dest, img=img)


if __name__ == '__main__':
    main()
