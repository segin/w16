#!/usr/bin/env python3
"""gdcalls - one program's GDI calls from a GD2 log, without handles.

A GD2 log (tests/refrun.sh with REFRUN_GDLOG=1, or tests/gdiboot.sh -L)
records every call into GDI with its caller's address. This tool keeps the
calls one program makes (those whose caller is in the code segment given
by --seg, or found by --find: the segment of the first call whose line
contains the text), plus the PATBLTs USER makes right after them on the
program's behalf (FillRect), and prints them with the values that do not
change between runs removed: device contexts and GDI object handles,
far pointers, call numbers, callers. Two programs that draw the same way
give the same output; `diff` shows where they part (black-box comparison
of w16's programs with the originals, ARCHITECTURE.md 3).

usage: gdcalls.py GD.LOG (--seg XXXX | --find TEXT) [--all]
    --all   keep SELECTOBJECT, DELETEOBJECT, ISGDIOBJECT and the returns
"""
import re
import sys

NOISE = {'DELETEOBJECT', 'ISGDIOBJECT', 'GETSTOCKOBJECT',
         'UNREALIZEOBJECT', 'SETDCSTATE', 'GETDCSTATE', 'SETBRUSHORG',
         'GETOBJECT', 'GETNEARESTCOLOR', 'RESTOREDC', 'SAVEDC'}
# calls whose first argument is a DC (dropped: handles vary)
HDC_FIRST = {'MOVETO', 'LINETO', 'RECTANGLE', 'PATBLT', 'EXTTEXTOUT',
             'TEXTOUT', 'GETTEXTEXTENT', 'SETBKMODE', 'SETBKCOLOR',
             'SETTEXTCOLOR', 'SETTEXTALIGN', 'SETROP2', 'GETDEVICECAPS',
             'ELLIPSE', 'POLYGON', 'POLYLINE', 'SETPIXEL', 'BITBLT',
             'STRETCHBLT', 'CREATECOMPATIBLEBITMAP', 'CREATEDISCARDABLEBITMAP',
             'GETTEXTMETRICS', 'GETTEXTFACE', 'INTERSECTCLIPRECT',
             'SETMAPMODE', 'SETVIEWPORTORG', 'SETWINDOWORG', 'GETTEXTCOLOR'}


def main():
    a = sys.argv[1:]
    keep_all = '--all' in a
    seg = a[a.index('--seg') + 1].upper() if '--seg' in a else None
    find = a[a.index('--find') + 1] if '--find' in a else None
    lines = open(a[0], 'rb').read().decode('latin-1').splitlines()
    if not seg and find:
        for l in lines:
            if l.startswith('G>') and find in l:
                m = re.search(r'@([0-9A-F]{4}):', l)
                if m:
                    seg = m.group(1)
                    break
    if not seg:
        sys.exit('gdcalls: no segment (--seg or --find)')
    out = []
    labels = {}                     # GDI handle -> what made it
    pending = None                  # the call whose return names a handle
    pending_font = [None]
    keep = False                    # the current call is kept
    prev_app = False                # the last non-noise call was the app's
    for l in lines:
        if l.startswith('G>'):
            parts = l.split()
            name = parts[1] if len(parts) > 1 else ''
            m = re.search(r'@([0-9A-F]{4}):', l)
            caller = m.group(1) if m else ''
            mine = caller == seg
            args = [x for x in parts[2:] if not x.startswith('@') and ':' not in x]
            pending = None
            if name in ('CREATEPEN', 'CREATESOLIDBRUSH', 'GETSTOCKOBJECT', 'CREATEFONTINDIRECT'):
                pending = name[6:] if name.startswith('CREATE') else 'STOCK'
                pending += ' ' + ' '.join(args)
            if name in NOISE and not keep_all:
                keep = False
                continue
            if name == 'SELECTOBJECT' and not keep_all:
                # by what the handle is, when this program made it
                if mine and len(args) == 2:
                    out.append('SELECT ' + labels.get(args[1], '?'))
                    prev_app = True
                keep = False
                continue
            keep = mine or (name == 'PATBLT' and prev_app)
            if not keep:
                prev_app = False
                continue
            prev_app = True
            if name in HDC_FIRST and args:
                args = args[1:]
            out.append(name + ' ' + ' '.join(args))
        elif l.startswith('G<'):
            if pending:
                v = l.split('=', 1)[1].strip()
                labels[v[-4:]] = pending
                if pending.startswith('FONTINDIRECT'):
                    labels[v[-4:]] = pending_font[0] or 'FONT'
                pending = None
            keep = keep and keep_all
            if keep:
                out.append('  = ' + l.split('=', 1)[1].strip())
        elif keep and l.startswith('  .'):
            out.append(l.rstrip())
            if l.startswith('  .lf:') and out[-2].startswith('CREATEFONTINDIRECT'):
                b = l.split()[1:3]
                h = int(b[1] + b[0], 16)
                labels_font = 'FONT %d' % (h - 65536 if h > 32767 else h)
                out[-2] = 'CREATEFONTINDIRECT'
                pending_font[0] = labels_font
        elif keep and l.startswith('    '):
            out.append(l.rstrip())
    print('\n'.join(out))


if __name__ == '__main__':
    main()
