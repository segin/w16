#!/usr/bin/env python3
"""kfcomp - compile a KEYFEED script into the binary key stream that
tools/dos/keyfeed.asm feeds to a DOS program.

Script, one command per line. A line starting with '#' is a comment, and
every command except `type` may have a trailing '# comment'.
    type <text>      the characters of <text> (rest of the line, verbatim)
    key <name>...    named keys: ENTER ESC TAB BKSP SPACE UP DOWN LEFT
                     RIGHT HOME END PGUP PGDN INS DEL F1..F10, or a raw
                     BIOS key word such as 0x1C0D
    snap             record the text screen when the program next asks
                     for a key
    wait <ticks>     hold further keys back for <ticks> timer ticks
                     (18.2 per second)

Stream: little-endian words. A BIOS key word (scan code << 8 | ASCII),
FFFFh = snap, FFFEh n = wait n ticks.

usage: kfcomp.py script.txt out.kfs
"""
import struct
import sys

# US keyboard: ASCII -> scan code
SCAN = {}
for row, codes in (
        ("1234567890-=", range(0x02, 0x0E)),
        ("qwertyuiop[]", range(0x10, 0x1C)),
        ("asdfghjkl;'`", range(0x1E, 0x2A)),
        ("\\zxcvbnm,./", range(0x2B, 0x36))):
    for ch, sc in zip(row, codes):
        SCAN[ch] = sc
SHIFTED = {'!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6',
           '&': '7', '*': '8', '(': '9', ')': '0', '_': '-', '+': '=',
           '{': '[', '}': ']', ':': ';', '"': "'", '~': '`', '|': '\\',
           '<': ',', '>': '.', '?': '/'}

NAMED = {
    'ENTER': 0x1C0D, 'ESC': 0x011B, 'TAB': 0x0F09, 'BKSP': 0x0E08,
    'SPACE': 0x3920, 'UP': 0x4800, 'DOWN': 0x5000, 'LEFT': 0x4B00,
    'RIGHT': 0x4D00, 'HOME': 0x4700, 'END': 0x4F00, 'PGUP': 0x4900,
    'PGDN': 0x5100, 'INS': 0x5200, 'DEL': 0x5300,
}
for n in range(1, 11):
    NAMED['F%d' % n] = (0x3A + n) << 8


def char_key(c):
    if c == ' ':
        return 0x3920
    lo = c.lower()
    base = SHIFTED.get(c, lo)
    if base not in SCAN:
        raise ValueError('no scan code for %r' % c)
    return (SCAN[base] << 8) | ord(c)


def compile_script(text):
    out = []
    for num, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        cmd, _, arg = line.lstrip().partition(' ')
        if cmd != 'type':                   # type's text is verbatim
            arg = arg.split('#', 1)[0].strip()
        try:
            if cmd == 'type':
                out += [char_key(c) for c in arg]
            elif cmd == 'key':
                for k in arg.split():
                    out.append(int(k, 16) if k.lower().startswith('0x') else NAMED[k.upper()])
            elif cmd == 'snap':
                out.append(0xFFFF)
            elif cmd == 'wait':
                out += [0xFFFE, int(arg)]
            else:
                raise ValueError('unknown command %r' % cmd)
        except (ValueError, KeyError) as e:
            sys.exit('%s:%d: %s' % (sys.argv[1], num, e))
    return b''.join(struct.pack('<H', w) for w in out)


def main():
    with open(sys.argv[1]) as f:
        data = compile_script(f.read())
    with open(sys.argv[2], 'wb') as f:
        f.write(data)


if __name__ == '__main__':
    main()
