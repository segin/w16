#!/usr/bin/env python3
"""mkmcitab.py - writes w16's MMSYSTEM command tables (ARCHITECTURE.md 23)
as RCDATA resources in the format MCI drivers' own tables have (each item
a NUL-terminated keyword, a DWORD and a WORD type: 0 MCI_COMMAND_HEAD with
the command message, 1 MCI_STRING, 2 MCI_INTEGER, 3 MCI_END_COMMAND, 4
MCI_RETURN with the return type, 5 MCI_FLAG, 6 MCI_END_COMMAND_LIST, 7
MCI_RECT, 8 MCI_CONSTANT, 9 MCI_END_CONSTANT; the format as Wine's
mmddk.h of before 2002, X11 licence, gives it).

The tables are w16's own: the commands, keywords, flags and parameter
blocks are those the Multimedia Programmer's Guide (ch. 2 and 4) and the
SDK's mmsystem.h document, and each keyword, its flag and where its value
lands in the parameter block were confirmed on Windows 3.1 by sending the
command string to a test driver (tests/mmsystem/mci/explore3.txt,
explore8.txt: which device types know which keyword, and what reaches the
driver). Nothing is taken from Microsoft's files. Where a keyword lands
follows from the order of the items: after dwCallback, each item that
takes a value takes the next DWORD (a rectangle two), the return value
first.

usage: mkmcitab.py OUT.rc
"""
import sys

STRING, INTEGER, RETURN, FLAG, RECT, CONSTANT = 1, 2, 4, 5, 7, 8

NOTIFY = [('notify', 1, FLAG), ('wait', 2, FLAG)]


def cmd(verb, msg, items, ret=None):
    return (verb, msg, ret, items)


def const(kw, flag, values):
    return (kw, flag, CONSTANT, values)


# ---- the core table: the commands every device has ----
OPEN = [('type', 0x2000, STRING), ('element', 0x200, STRING),
        ('alias', 0x400, STRING), ('shareable', 0x100, FLAG)]
FORMATS = [('milliseconds', 0), ('ms', 0)]
SETCORE = [('door open', 0x100, FLAG), ('door closed', 0x200, FLAG),
           const('audio', 0x800, [('all', 0), ('left', 1), ('right', 2)]),
           ('video', 0x1000, FLAG), ('on', 0x2000, FLAG), ('off', 0x4000, FLAG)]
STATUSITEMS = [('position', 2), ('length', 1), ('number of tracks', 3), ('mode', 4),
               ('time format', 6), ('ready', 7), ('current track', 8)]
CAPITEMS = [('can record', 1), ('has audio', 2), ('has video', 3), ('device type', 4),
            ('uses files', 5), ('compound device', 6), ('can eject', 7), ('can play', 8),
            ('can save', 9)]
SEEK = [('to start', 0x100, FLAG), ('to end', 0x200, FLAG), ('to', 8, INTEGER)]


def status(extra=()):
    return cmd('status', 0x814, NOTIFY + [const('', 0x100, STATUSITEMS + list(extra)),
                                         ('track', 0x10, INTEGER), ('start', 0x200, FLAG)],
               ret=INTEGER)


def capability(extra=(), flags=()):
    return cmd('capability', 0x80B, NOTIFY + [const('', 0x100, CAPITEMS + list(extra))] + list(flags),
               ret=INTEGER)


def info(extra=()):
    return cmd('info', 0x80A, NOTIFY + [('product', 0x100, FLAG)] + list(extra), ret=STRING)


def setcmd(formats=(), extra=()):
    return cmd('set', 0x80D, NOTIFY + [const('time format', 0x400, list(formats) + FORMATS)] +
               SETCORE + list(extra))


CORE = [
    cmd('open', 0x803, NOTIFY + OPEN, ret=INTEGER),
    cmd('close', 0x804, NOTIFY),
    cmd('play', 0x806, NOTIFY + [('from', 4, INTEGER), ('to', 8, INTEGER)]),
    cmd('record', 0x80F, NOTIFY + [('from', 4, INTEGER), ('to', 8, INTEGER),
                                   ('insert', 0x100, FLAG), ('overwrite', 0x200, FLAG)]),
    cmd('seek', 0x807, NOTIFY + SEEK),
    cmd('stop', 0x808, NOTIFY),
    cmd('pause', 0x809, NOTIFY),
    cmd('resume', 0x855, NOTIFY),
    setcmd(),
    status(),
    capability(),
    info(),
    cmd('sysinfo', 0x810, NOTIFY + [('quantity', 0x100, FLAG), ('open', 0x200, FLAG),
                                    ('name', 0x400, INTEGER), ('installname', 0x800, FLAG)],
        ret=STRING),
    cmd('break', 0x811, NOTIFY + [('on', 0x100, INTEGER), ('off', 0x400, FLAG)]),
    cmd('sound', 0x812, NOTIFY + [('', 0x100, STRING)]),
    cmd('save', 0x813, NOTIFY + [('', 0x100, STRING)]),
    cmd('load', 0x850, NOTIFY + [('', 0x100, STRING)]),
]

# ---- the device-type tables (3.1 has them for these six types) ----
WINDOW = cmd('window', 0x841, NOTIFY + [const('handle', 0x10000, [('default', 0)]),
                                        ('state', 0x40000, INTEGER), ('text', 0x80000, STRING),
                                        ('stretch', 0x100000, FLAG), ('fixed', 0x200000, FLAG)])
STYLE = const('style', 0x10000, [])
VIDEODISC = [
    capability([('can reverse', 0x4002), ('fast play rate', 0x4003),
                ('slow play rate', 0x4004), ('normal play rate', 0x4005)],
               [('clv', 0x10000, FLAG), ('cav', 0x20000, FLAG)]),
    cmd('escape', 0x805, NOTIFY + [('', 0x100, STRING)]),
    cmd('play', 0x806, NOTIFY + [('from', 4, INTEGER), ('to', 8, INTEGER), ('fast', 0x20000, FLAG),
                                 ('slow', 0x100000, FLAG), ('speed', 0x40000, INTEGER),
                                 ('reverse', 0x10000, FLAG), ('scan', 0x80000, FLAG)]),
    cmd('seek', 0x807, NOTIFY + [('reverse', 0x10000, FLAG)] + SEEK),
    setcmd([('frames', 3), ('hms', 1), ('track', 0x4001)]),
    cmd('spin', 0x80C, NOTIFY + [('up', 0x10000, FLAG), ('down', 0x20000, FLAG)]),
    status([('speed', 0x4002), ('forward', 0x4003), ('media type', 0x4004), ('side', 0x4005),
            ('disc size', 0x4006), ('media present', 5)]),
    cmd('step', 0x80E, NOTIFY + [('reverse', 0x20000, FLAG), ('by', 0x10000, INTEGER)]),
]
CDAUDIO = [
    setcmd([('msf', 2), ('tmsf', 10)]),
    status([('media present', 5)]),
]
OVERLAY = [
    cmd('open', 0x803, NOTIFY + OPEN + [STYLE, ('parent', 0x20000, INTEGER)], ret=INTEGER),
    capability([('can stretch', 0x4001), ('can freeze', 0x4002), ('windows', 0x4003)]),
    cmd('freeze', 0x844, NOTIFY + [('at', 0x10000, RECT)]),
    cmd('unfreeze', 0x845, NOTIFY + [('at', 0x10000, RECT)]),
    info([('file', 0x200, FLAG), ('window text', 0x10000, FLAG)]),
    cmd('load', 0x850, NOTIFY + [('', 0x100, STRING), ('at', 0x10000, RECT)]),
    cmd('save', 0x813, NOTIFY + [('', 0x100, STRING), ('at', 0x10000, RECT)]),
    cmd('put', 0x842, NOTIFY + [('at', 0x10000, RECT), ('source', 0x20000, FLAG),
                                ('destination', 0x40000, FLAG), ('frame', 0x80000, FLAG),
                                ('video', 0x100000, FLAG)]),
    status([('window handle', 0x4001), ('stretch', 0x4002), ('media present', 5)]),
    cmd('where', 0x843, NOTIFY + [('source', 0x20000, FLAG), ('destination', 0x40000, FLAG),
                                  ('frame', 0x80000, FLAG), ('video', 0x100000, FLAG)], ret=RECT),
    WINDOW,
]
ANIMATION = [
    cmd('open', 0x803, NOTIFY + OPEN + [STYLE, ('parent', 0x20000, INTEGER),
                                        ('nostatic', 0x40000, FLAG)], ret=INTEGER),
    capability([('can reverse', 0x4001), ('fast play rate', 0x4002), ('slow play rate', 0x4003),
                ('normal play rate', 0x4004), ('uses palettes', 0x4006), ('can stretch', 0x4007),
                ('windows', 0x4008)]),
    info([('file', 0x200, FLAG), ('window text', 0x10000, FLAG)]),
    cmd('play', 0x806, NOTIFY + [('from', 4, INTEGER), ('to', 8, INTEGER), ('fast', 0x40000, FLAG),
                                 ('slow', 0x80000, FLAG), ('scan', 0x100000, FLAG),
                                 ('reverse', 0x20000, FLAG), ('speed', 0x10000, INTEGER)]),
    cmd('put', 0x842, NOTIFY + [('at', 0x10000, RECT), ('source', 0x20000, FLAG),
                                ('destination', 0x40000, FLAG)]),
    cmd('realize', 0x840, NOTIFY + [('normal', 0x10000, FLAG), ('background', 0x20000, FLAG)]),
    setcmd([('frames', 3)]),
    status([('speed', 0x4001), ('forward', 0x4002), ('window handle', 0x4003),
            ('palette handle', 0x4004), ('stretch', 0x4005), ('media present', 5)]),
    cmd('step', 0x80E, NOTIFY + [('reverse', 0x10000, FLAG), ('by', 0x20000, INTEGER)]),
    cmd('update', 0x854, NOTIFY + [('at', 0x10000, RECT), ('hdc', 0x20000, INTEGER)]),
    cmd('where', 0x843, NOTIFY + [('source', 0x20000, FLAG), ('destination', 0x40000, FLAG)],
        ret=RECT),
    WINDOW,
]
WAVEAUDIO = [
    cmd('open', 0x803, NOTIFY + OPEN + [('buffer', 0x10000, INTEGER)], ret=INTEGER),
    capability([('inputs', 0x4001), ('outputs', 0x4002)]),
    cmd('cue', 0x830, NOTIFY + [('input', 0x400000, FLAG), ('output', 0x800000, FLAG)]),
    cmd('delete', 0x856, NOTIFY + [('from', 4, INTEGER), ('to', 8, INTEGER)]),
    info([('file', 0x200, FLAG), ('input', 0x400000, FLAG), ('output', 0x800000, FLAG)]),
    setcmd([('bytes', 8), ('samples', 9)],
           [('input', 0x400000, INTEGER), ('output', 0x800000, INTEGER),
            const('format tag', 0x10000, [('pcm', 1)]), ('channels', 0x20000, INTEGER),
            ('samplespersec', 0x40000, INTEGER), ('bytespersec', 0x80000, INTEGER),
            ('alignment', 0x100000, INTEGER), ('bitspersample', 0x200000, INTEGER),
            ('any input', 0x4000000, FLAG), ('any output', 0x8000000, FLAG)]),
    status([('format tag', 0x4001), ('channels', 0x4002), ('samplespersec', 0x4003),
            ('bytespersec', 0x4004), ('alignment', 0x4005), ('bitspersample', 0x4006),
            ('level', 0x4007), ('input', 0x400000), ('output', 0x800000),
            ('media present', 5)]),
]
SEQUENCER = [
    info([('file', 0x200, FLAG)]),
    setcmd([('song pointer', 0x4001), ('smpte 30 drop', 7), ('smpte 24', 4), ('smpte 25', 5),
            ('smpte 30', 6)],
           [('tempo', 0x10000, INTEGER),
            const('port', 0x20000, [('none', 0xFFFD), ('mapper', 0xFFFF)]),
            const('slave', 0x40000, [('file', 0x4002), ('midi', 0x4003), ('smpte', 0x4004),
                                     ('none', 0xFFFD)]),
            const('master', 0x80000, [('midi', 0x4003), ('smpte', 0x4004), ('none', 0xFFFD)]),
            ('offset', 0x1000000, INTEGER)]),
    status([('tempo', 0x4002), ('port', 0x4003), ('slave', 0x4007), ('master', 0x4008),
            ('offset', 0x4009), ('division type', 0x400A), ('media present', 5)]),
]

TABLES = [('CORE', CORE), ('VIDEODISC', VIDEODISC), ('CDAUDIO', CDAUDIO),
          ('OVERLAY', OVERLAY), ('ANIMATION', ANIMATION), ('WAVEAUDIO', WAVEAUDIO),
          ('SEQUENCER', SEQUENCER)]


def item(out, kw, val, typ):
    out.append('    "%s\\0", 0x%XL, %d,' % (kw, val, typ))


def emit(out, name, cmds):
    out.append('%s RCDATA' % name)
    out.append('BEGIN')
    for verb, msg, ret, items in cmds:
        item(out, verb, msg, 0)
        if ret:
            item(out, '', ret, RETURN)
        for it in items:
            kw, val, typ = it[0], it[1], it[2]
            item(out, kw, val, typ)
            if typ == CONSTANT:
                for v, n in it[3]:
                    item(out, v, n, INTEGER)
                item(out, '', 0, 9)
        item(out, '', 0, 3)
    out.append('    "\\0", 0L, 6')
    out.append('END')
    out.append('')


def main():
    out = ['/* written by tools/mkmcitab.py: w16\'s MCI command tables */', '']
    for name, cmds in TABLES:
        emit(out, name, cmds)
    open(sys.argv[1], 'w').write('\n'.join(out))


main()
