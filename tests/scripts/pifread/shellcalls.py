"""shellcalls.py COM1.TXT: WINOA386's calls into SHELL from a VXS.386 log
(tests/vxd/vxs.asm), as tests/pifread.sh keeps them: each call as
"fn eax ebx ecx esi -> eax" (EDX the function), and for function 3 its
block (the ES:DI dump before the call; +08h, +0Eh, +14h are pointers and
+2Ah, +2Ch WINOA386's window and instance, left out) with the program,
tail and directory SHELL was given (the "ptr" dumps after it, to their
first NUL; the tail as length, text). Functions 0Ch (an event record
handed back) and 10h are left out: their arguments are addresses."""
import re
import sys

lines = [l.rstrip('\r\n') for l in open(sys.argv[1], 'rb').read().decode('latin-1').splitlines()]
regs = re.compile(r'(\w+)=([0-9A-F]+)')
dumpre = re.compile(r'^VXS\s+\S+ [0-9A-F]+: (.*)$')


def dump(l):
    m = dumpre.match(l)
    return m.group(1).split() if m else []


i = 0
pending = None
while i < len(lines):
    l = lines[i]
    if l.startswith('VXS api 00000017 '):
        r = dict(regs.findall(l))
        fn = int(r['edx'], 16) & 0xFFFF
        if ' in ' in l:
            pending = (fn, r, lines[i + 1] if i + 1 < len(lines) else '')
        elif pending and pending[0] == fn and fn not in (0x0C, 0x10, 0x09, 0x00):
            fn_, ri, esdi = pending
            s = 'fn %02X eax=%s ebx=%s ecx=%s esi=%s -> eax=%s' % (
                fn, ri['eax'], ri['ebx'] if fn != 5 else '(vm)', ri['ecx'],
                ri['esi'] if fn == 3 else '(hwnd)', r['eax'] if fn != 3 else '(vm)')
            if fn in (5, 6, 7, 0x15, 4, 0x16):
                s = s.replace('ebx=' + ri['ebx'], 'ebx=(vm)') if ri['ebx'].startswith('80') else s
            print(s)
            if fn == 3:
                b = dump(esdi)
                blk = ['..' if 8 <= k < 0x1A or 0x2A <= k < 0x2E else b[k] for k in range(min(len(b), 0x2E))]
                title = bytes(int(x, 16) for x in b[0x2E:]).split(b'\0')[0].decode('latin-1')
                print('   block', ' '.join(blk), 'title=%r' % title)
                j, n = i + 1, 0
                while j < len(lines) and lines[j].startswith('VXS   ') and n < 4:
                    if lines[j].startswith('VXS   ptr'):
                        bb = bytes(int(x, 16) for x in dump(lines[j]))
                        if n == 1:
                            print('   tail', bb[0], repr(bb[1:1 + bb[0] + 1].decode('latin-1')))
                        else:
                            print('   %s %r' % ('program' if n == 0 else 'directory',
                                                 bb.split(b'\0')[0].decode('latin-1')))
                        n += 1
                    j += 1
            pending = None
    i += 1
