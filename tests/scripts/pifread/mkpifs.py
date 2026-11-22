#!/usr/bin/env python3
"""mkpifs.py OUTDIR [SETUP.PIF]: the PIF fixtures of tests/pifread.sh, the
probes of how WINOLDAP.MOD and WINOA386.MOD read a .PIF (ARCHITECTURE.md
9.8, 12.11, 15.11).

Each fixture runs C:\\WC1.COM (tests/dos/wc1.asm: it logs its command tail
and current directory) with the title "Probe Program", the start-up
directory C:\\WINDOWS\\SYSTEM, the parameters "stdargs" in the base section
(Standard mode's) and "enhargs" in the 386 section, then changes one thing.
The layout is the one 3.1's PIF Editor writes (w16's PIF Editor writes the
same bytes, shell/pifedit/pif.c; tests/pifscenarios.sh PF13), the 386
section first; the "setup*" fixtures are Setup's layout (286 section
first), made from SETUP.PIF (the reference install's DOSPRMPT.PIF) when it
is given. OUTDIR/LIST names each fixture with what it changes.
"""
import os
import struct
import sys

D386 = dict(max=0x00, min=0x02, fg=0x04, bg=0x06, emslim=0x08, emsreq=0x0A,
            xmslim=0x0C, xmsreq=0x0E, f0=0x10, f1=0x11, f2=0x12, video=0x14,
            hkscan=0x18, hkmods=0x1A, hkflag=0x1C, params=0x28)
W386 = ('max', 'min', 'fg', 'bg', 'emslim', 'emsreq', 'xmslim', 'xmsreq',
        'hkscan', 'hkmods', 'hkflag')


def text(d, off, n, s, nul=True):
    b = s.encode('latin-1')
    b = (b + b'\0' + b' ' * n)[:n] if nul else (b + b' ' * n)[:n]
    d[off:off + n] = b


def params(d, off, s):
    """parameters: the text, NUL, blanks from the second byte after it"""
    b = s.encode('latin-1')
    d[off:off + 64] = (b + b'\0\0' + b' ' * 64)[:64]


def checksum(d):
    if len(d) >= 0x171:
        d[1] = sum(d[2:0x171]) & 0xFF


def header(d, at, name, nxt, off, ln):
    d[at:at + 16] = (name.encode() + bytes(16))[:16]
    struct.pack_into('<HHH', d, at + 16, nxt, off, ln)


def editor_pif():
    """a PIF as the PIF Editor saves it (pif.c pif_new + the probe's fields)"""
    d = bytearray(545)
    text(d, 0x02, 30, 'Probe Program', nul=False)
    struct.pack_into('<HH', d, 0x20, 128, 128)
    text(d, 0x24, 63, 'C:\\WC1.COM')
    d[0x63] = 0x10
    text(d, 0x65, 64, 'C:\\WINDOWS\\SYSTEM')
    params(d, 0xA5, 'stdargs')
    d[0xE5:0xEF] = bytes([0x7F, 0x01, 0x00, 0xFF, 0x19, 0x50, 0x00, 0x00, 0x07, 0x00])
    d[0x16F] = 0xE0
    d[0x170] = 0x20
    header(d, 0x171, 'MICROSOFT PIFEX', 0x187, 0, 0x171)
    header(d, 0x187, 'WINDOWS 386 3.0', 0x205, 0x19D, 0x68)
    a = 0x19D
    struct.pack_into('<8H', d, a, 640, 128, 100, 50, 1024, 0, 1024, 0)
    d[a + 0x10] = 0x08
    d[a + 0x11] = 0x10
    d[a + 0x12] = 0x02
    d[a + 0x14] = 0x1F
    params(d, a + 0x28, 'enhargs')
    header(d, 0x205, 'WINDOWS 286 3.0', 0xFFFF, 0x21B, 6)
    checksum(d)
    return d


def find(d, name):
    """the data offset of section NAME, walking the headers from 171h"""
    at, n = 0x171, 0
    while at + 22 <= len(d) and n < 16:
        if d[at:at + 16].split(b'\0')[0] == name.encode():
            return struct.unpack_from('<H', d, at + 18)[0]
        at = struct.unpack_from('<H', d, at + 16)[0]
        n += 1
    return None


def setup_pif(src):
    """Setup's layout (286 section first) with the probe's fields"""
    d = bytearray(open(src, 'rb').read())
    text(d, 0x02, 30, 'Probe Program', nul=False)
    text(d, 0x24, 63, 'C:\\WC1.COM')
    text(d, 0x65, 64, 'C:\\WINDOWS\\SYSTEM')
    params(d, 0xA5, 'stdargs')
    params(d, find(d, 'WINDOWS 386 3.0') + 0x28, 'enhargs')
    return d


def set386(d, **kw):
    a = find(d, 'WINDOWS 386 3.0')
    for k, v in kw.items():
        if k == 'params':
            params(d, a + D386[k], v)
        elif k in W386:
            struct.pack_into('<H', d, a + D386[k], v & 0xFFFF)
        else:
            d[a + D386[k]] = v
    checksum(d)
    return d


def set286(d, xmslim=None, xmsreq=None, flags=None):
    a = find(d, 'WINDOWS 286 3.0')
    if xmslim is not None:
        struct.pack_into('<H', d, a, xmslim)
    if xmsreq is not None:
        struct.pack_into('<H', d, a + 2, xmsreq)
    if flags is not None:
        struct.pack_into('<H', d, a + 4, flags)
    checksum(d)
    return d


def base(d, **kw):
    for k, v in kw.items():
        if k == 'flags':
            d[0x63] = v
        elif k == 'max':
            struct.pack_into('<H', d, 0x20, v & 0xFFFF)
        elif k == 'min':
            struct.pack_into('<H', d, 0x22, v & 0xFFFF)
        elif k == 'params':
            params(d, 0xA5, v)
        elif k == 'b16f':
            d[0x16F] = v
        elif k == 'b170':
            d[0x170] = v
        elif k == 'bed':
            d[0xED] = v
    checksum(d)
    return d


def fixtures(setup):
    E = editor_pif
    f = {}
    f['ed'] = (E(), "the PIF Editor's layout, its defaults")
    f['edwin'] = (set386(E(), f0=0x00), '386: windowed (+10h bit 08h clear)')
    f['edmem'] = (set386(E(), max=600, min=300), '386: KB desired 600, required 300')
    f['edmemneg'] = (set386(E(), max=-1, min=-1), '386: KB desired -1, required -1')
    f['edmem0'] = (set386(E(), max=0, min=0), '386: KB desired 0, required 0')
    f['edprio'] = (set386(E(), fg=200, bg=25), '386: priorities 200/25')
    f['edems'] = (set386(E(), emslim=512, emsreq=64, xmslim=2048, xmsreq=256),
                  '386: EMS 64..512, XMS 256..2048')
    f['edemsneg'] = (set386(E(), emslim=-1, xmslim=-1), '386: EMS and XMS limits -1')
    f['edbg'] = (set386(E(), f0=0x0A), '386: background')
    f['edexcl'] = (set386(E(), f0=0x0C), '386: exclusive')
    f['edclose'] = (set386(E(), f0=0x09), '386: allow close when active')
    f['edres'] = (set386(E(), f0=0xE8, f1=0x1F), '386: reserve Alt+Tab, Alt+Esc, Alt+Space, '
                  'Alt+Enter, Alt+PrtSc, PrtSc, Ctrl+Esc')
    f['edidle'] = (set386(E(), f1=0xA0), '386: no detect idle time, no HMA, EMS locked')
    f['edlock'] = (set386(E(), f2=0x05), '386: XMS locked, no fast paste, lock application memory')
    f['edvhi'] = (set386(E(), video=0x40), '386: high graphics, ports monitored, no text emulation')
    f['edvlo'] = (set386(E(), video=0xA4), '386: low graphics, low ports not monitored, retain video memory')
    f['edhot'] = (set386(E(), f1=0x50, hkscan=0x19, hkmods=0x0C, hkflag=0x0F),
                  '386: shortcut key Ctrl+Alt+P')
    f['ednoclose'] = (base(E(), flags=0x00), 'base: close window on exit off')
    f['edbase'] = (base(E(), flags=0xDE, bed=0x10, b16f=0x10, b170=0x40),
                   'base: COM1, COM2, no screen exchange, prevent switch, graphics, keyboard')
    f['ed286'] = (set286(E(), xmslim=2048, xmsreq=256, flags=0xC03F),
                  '286: XMS 256..2048, every reserved key, no save screen, COM3, COM4')
    f['ednop386'] = (set386(E(), params=''), '386: no parameters (base: stdargs)')
    f['edq386'] = (set386(E(), params='?'), '386: parameters "?"')
    d = E(); d[1] ^= 0x55
    f['edbadsum'] = (d, 'checksum wrong')
    d = E()[:0x171]
    f['ed2x'] = (d, 'Windows 2.x: the base section alone (171h bytes)')
    d = E(); header(d, 0x171, 'MICROSOFT PIFEX', 0x205, 0, 0x171)
    f['edno386'] = (d, 'no 386 section in the chain (the 286 one only)')
    d = E(); d[0x187:0x197] = b'WINDOWS 386 4.0\0'
    f['ednm386'] = (d, '386 section named "WINDOWS 386 4.0"')
    d = E(); d[0x171:0x181] = b'MICROSOFT PIFEY\0'
    f['ednoex'] = (d, 'no "MICROSOFT PIFEX" header (545 bytes)')
    f['edtrunc'] = (E()[:0x1C0], 'cut at 1C0h, in the 386 data')
    d = E(); struct.pack_into('<H', d, 0x187 + 20, 0x40)
    f['edshort'] = (d, '386 section length 40h')
    d = E(); struct.pack_into('<H', d, 0x187 + 18, 0x300)
    f['edfar'] = (d, '386 data offset 300h (past the end)')
    d = E(); struct.pack_into('<H', d, 0x171 + 16, 0x400)
    f['edbadnext'] = (d, "PIFEX header's next 400h (past the end)")
    d = E()
    for i in range(0x171, 545):
        d[i] = (i * 37 + 11) & 0xFF
    f['edjunk'] = (d, 'junk after the base section')
    f['edtiny'] = (E()[:0x100], '100h bytes only')
    f['edcut'] = (E()[:0x180], 'cut at 180h, in the PIFEX header')
    f['edcut2'] = (E()[:0x187], 'cut at 187h, after the PIFEX header')
    d = E(); struct.pack_into('<H', d, 0x187 + 20, 0x70)
    f['edlong'] = (d, '386 section length 70h')
    d = E(); d[0x187:0x197] = b'WINDOWS 386 4.0\0'; struct.pack_into('<H', d, 0x205 + 16, 0x171)
    f['edloop'] = (d, 'a loop: the 286 header\'s next is 171h, no 386 section')
    d = E()[:0x171]; base(d, flags=0x12, bed=0x10, b16f=0x10, max=512, min=256)
    f['ed2xa'] = (d, '2.x: graphics, keyboard (16Fh 10h), 512/256 KB')
    d = E()[:0x171]; base(d, flags=0x0C, bed=0x00, b16f=0x00, b170=0x00, max=-1, min=64)
    f['ed2xb'] = (d, '2.x: no screen exchange, prevent switch, no close; EDh, 16Fh 0; -1/64 KB')
    d = E()[:0x171]; base(d, flags=0xFF, bed=0xFF, b16f=0xFF, b170=0xFF)
    f['ed2xc'] = (d, '2.x: 63h, EDh, 16Fh, 170h FFh')
    d = E()[:0x171]; base(d, flags=0x10, bed=0x07, b16f=0x00, b170=0x00)
    f['ed2xd'] = (d, '2.x: 16Fh, 170h 0')
    for k, v16f, v63, v170, ved in (('e', 0x20, 0x10, 0x00, 0x07), ('f', 0x40, 0x10, 0x00, 0x07),
                                    ('g', 0x80, 0x10, 0x00, 0x07), ('h', 0x0F, 0x10, 0x00, 0x07),
                                    ('i', 0x00, 0xFF, 0x00, 0x07), ('j', 0x00, 0x10, 0xFF, 0x07),
                                    ('k', 0x00, 0x10, 0x00, 0xFF)):
        d = E()[:0x171]; base(d, flags=v63, bed=ved, b16f=v16f, b170=v170)
        f['ed2x' + k] = (d, '2.x: 63h %02Xh, EDh %02Xh, 16Fh %02Xh, 170h %02Xh' % (v63, ved, v16f, v170))
    for v in (0x01, 0x20, 0x40, 0x80):
        d = E()[:0x171]; base(d, flags=0x10 | v, b16f=0x80)
        f['ed2x63_%02x' % v] = (d, '2.x: 63h 10h|%02Xh, 16Fh 80h' % v)
    # Standard mode, one setting at a time
    for v in (0x80, 0x40, 0x08, 0x04, 0x02, 0x01, 0x20):
        f['s63_%02x' % v] = (base(E(), flags=0x10 | v), 'base: 63h 10h|%02Xh' % v)
    for v in (0x00, 0x10, 0x01, 0x02, 0x04, 0xFF):
        f['sed_%02x' % v] = (base(E(), bed=v), 'base: EDh %02Xh' % v)
    for v in (0x00, 0x10, 0x20, 0x40, 0x80, 0x0F):
        f['s16f_%02x' % v] = (base(E(), b16f=v), 'base: 16Fh %02Xh' % v)
    for v in (0x00, 0x40, 0xFF):
        f['s170_%02x' % v] = (base(E(), b170=v), 'base: 170h %02Xh' % v)
    for v in (0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x4000, 0x8000):
        f['s286_%04x' % v] = (set286(E(), flags=v), '286: flags %04Xh' % v)
    f['s286xms'] = (set286(E(), xmslim=2048, xmsreq=256), '286: XMS 256..2048')
    f['s286xmsneg'] = (set286(E(), xmslim=0xFFFF, xmsreq=0), '286: XMS limit -1')
    f['smem'] = (base(E(), max=512, min=256), 'base: KB 512/256')
    f['smemneg'] = (base(E(), max=-1, min=-1), 'base: KB -1/-1')
    f['smem64'] = (base(E(), max=64, min=64), 'base: KB 64/64')
    f['smembig'] = (base(E(), max=640, min=600), 'base: KB 640/600')
    f['edmema'] = (set386(E(), max=64, min=0), '386: KB desired 64, required 0')
    f['edmemb'] = (set386(E(), max=0, min=64), '386: KB desired 0, required 64')
    f['edmemc'] = (set386(E(), max=100, min=200), '386: KB desired 100, required 200')
    f['edmemd'] = (set386(E(), max=-1, min=0), '386: KB desired -1, required 0')
    d = E(); text(d, 0x02, 30, '', nul=False); checksum(d)
    f['edtitle0'] = (d, 'title blank')
    d = E(); d[0x02:0x20] = bytes(30); checksum(d)
    f['edtitlenul'] = (d, 'title NULs')
    d = E(); text(d, 0x02, 30, 'A Title Thirty Characters Long', nul=False); checksum(d)
    f['edtitle30'] = (d, 'title of 30 characters')
    d = E(); text(d, 0x24, 63, 'COMMAND.COM'); checksum(d)
    set386(d, params='/C C:\\WC1.COM cmdargs')
    f['edcmd'] = (d, 'COMMAND.COM /C C:\\WC1.COM cmdargs')
    d = E(); text(d, 0x24, 63, 'C:\\WC1.EXE'); checksum(d)
    f['edexe'] = (d, 'program C:\\WC1.EXE')
    d = E(); text(d, 0x24, 63, 'C:\\WC1.BAT'); checksum(d)
    f['edbat'] = (d, 'program C:\\WC1.BAT')
    d = E(); text(d, 0x24, 63, 'WC1.COM'); checksum(d)
    f['edprogrel'] = (d, 'program WC1.COM (no path)')
    d = E(); text(d, 0x24, 63, 'C:\\NOWHERE.COM'); checksum(d)
    f['edprogbad'] = (d, 'program C:\\NOWHERE.COM (missing)')
    d = E(); text(d, 0x65, 64, 'C:\\NOWHERE'); checksum(d)
    f['eddirbad'] = (d, 'start-up directory C:\\NOWHERE (missing)')
    d = E(); text(d, 0x65, 64, ''); checksum(d)
    f['eddir0'] = (d, 'no start-up directory')
    f['edpblank'] = (set386(base(E(), params='  sa  sb  '), params='  ea  eb  '),
                     'parameters with blanks around them')
    d = E(); a = find(d, 'WINDOWS 386 3.0'); d[a + 0x28:a + 0x68] = b'P' * 64
    d[0xA5:0xE5] = b'Q' * 64; checksum(d)
    f['edplong'] = (d, 'parameters of 64 characters, no NUL')
    f['edplower'] = (set386(E(), params='Mixed Case'), '386 parameters "Mixed Case"')
    if setup:
        f['setup'] = (setup_pif(setup), "Setup's layout (DOSPRMPT.PIF), 286 section first")
        f['setupwin'] = (set386(setup_pif(setup), f0=0x00), "Setup's layout, 386: windowed")
        f['setupprio'] = (set386(setup_pif(setup), fg=300, bg=7), "Setup's layout, 386: priorities 300/7")
        f['setup286'] = (set286(setup_pif(setup), xmslim=2048, xmsreq=256, flags=0xC03F),
                         "Setup's layout, 286: XMS 256..2048, every reserved key, no save screen")
    return f


def main():
    out = sys.argv[1]
    setup = sys.argv[2] if len(sys.argv) > 2 else None
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'LIST'), 'w') as lst:
        for name, (d, what) in fixtures(setup).items():
            open(os.path.join(out, name.upper() + '.PIF'), 'wb').write(d)
            lst.write('%s %s\n' % (name, what))
        lst.write('nopif no PIF of its own: C:\\WC1.COM run (the install\'s _DEFAULT.PIF)\n')


if __name__ == '__main__':
    main()
