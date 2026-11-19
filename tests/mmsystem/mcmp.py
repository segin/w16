#!/usr/bin/env python3
"""mcmp - compare two logs of the MMSYSTEM probe MMP.EXE
(tests/mmsystem/mmp.c): Microsoft's MMSYSTEM.DLL (the reference) against
w16's, on the same system (ARCHITECTURE.md 23).

Every line must be identical after the masks below. MASKS hide what
differs between two runs of Microsoft's own MMSYSTEM (each one seen in
such a pair of runs); KNOWN lists what differs between Microsoft's and
w16's for a reason given there: those lines are shown, not failed.

usage: mcmp.py [--drop REGEX] MS.log W16.log
  --drop REGEX  leave out the lines it matches on both sides (a case that
                compares only part of what is logged says why)
Exit status 0 when they agree.
"""
import re
import sys

MASKS = [
    # StackEnter's flags after a switch: AF differs between two of 3.1's
    # runs (3202h, 3212h)
    # (and SF in a later pair: 3286h; the arithmetic flags after the
    # switch are left from 3.1's own code)
    (re.compile(r'^(  StackEnter: .* flags )([0-9A-F]{4})'),
     lambda m: m.group(1) + '%04X' % (int(m.group(2), 16) & ~0xD5)),
    # the owner GetExePtr gives for the interrupt stack: TIMER in some of
    # 3.1's runs, MMSYSTEM in others
    (re.compile(r'(size 600 owner )\w+'), r'\1(owner)'),
    # a timer callback's SI (left over from the timer driver's code)
    (re.compile(r'^(  cb \d+: ax .* si )[0-9A-F]{4}( di [0-9A-F]{4} ds TIMER)'),
     lambda m: m.group(1) + '(si)' + m.group(2)),
    # GetTempFileName's number (MMIO_GETTEMP): from the time and the task
    (re.compile(r'(name "C:\\WINDOWS\\~...)[0-9A-F]{4}(\.TMP")'), r'\1(n)\2'),
    # the word above MMTASK's stack (not the task's: whatever was there)
    (re.compile(r'^(  task: ret to .* beyond: \S+ \S+ \S+ )[0-9A-F]{4}'),
     lambda m: m.group(1) + '(any)'),
    # ---- the device sections (layer 3; tests/mmsystem/mmdev.c) ----
    # the handles MMSYSTEM gives its own sounds (sndPlaySound) and its
    # window's handle, which the probe has not named: from the heaps
    (re.compile(r'(\[p1 h )(?!0000)[0-9A-F]{4}\b'), r'\1(h)'),
    (re.compile(r'^(  DCB cb \S+ flags \S+ dev )[0-9A-F]{4}\b'), r'\1(h)'),
    (re.compile(r'\bcb 0000[0-9A-F]{4}\b'), 'cb (wnd)'),
    # MMSYSTEM's own sound headers (sndPlaySound) and MCIWAVE's: where the
    # block is (it may be gone when the record is printed: MCIWAVE's task
    # ends at another time, TASKEXIT below), and the fields left as
    # they were in that memory (rec, user, next, reserved)
    (re.compile(r'(\{data )(\[\w+\]|[0-9A-F]{8})( len \S+ rec )\S+( user )\S+'
                r'( flags \S+( loops \S+)? next )\S+( res )\S+\}'),
     lambda m: m.group(1) + '(ptr)' + m.group(3) + '(x)' + m.group(4) + '(x)' +
     m.group(5) + '(x)' + m.group(7) + '(x)}'),
    (re.compile(r'( p1 )(\[MMSYSTEM\]|[0-9A-F]{8})( p2 \S+ -> \S+ .*\{data \(ptr\))'),
     r'\1(ptr)\3'),
    (re.compile(r'^(  DCB .* p1 )(\[\w+\]|[0-9A-F]{8})( p2 .*\{data \(ptr\))'),
     r'\1(ptr)\3'),
    # what is on the stack where WODM_OPEN's instance goes before the
    # driver writes it
    (re.compile(r'\[user [0-9A-F]{8}( ->|\])'), r'[user (x)\1'),
    # a header's reserved field as the probe prints it: the driver's own
    # pointer (its data segment's selector follows the heap)
    (re.compile(r'( next \S+ res )(?!00000000)[0-9A-F]{8}\b'), r'\1(drv)'),
    # MIDIMAP's calls back into MMSYSTEM: its stack offsets follow how much
    # stack the MMSYSTEM below it took; the buffer it hands
    # midiOutGetDevCaps is its stack as it was
    (re.compile(r'^(  FWD MMSYSTEM\.2\d\d from=\S+ args ).*'), r'\1(args)'),
    # an MCI driver's mmTask calls (its stack and task handles)
    (re.compile(r'^(  FWD MMSYSTEM\.90\d from=\S+ args ).*'), r'\1(args)'),
    (re.compile(r'^(  MD \S+ mod 0002 .* d=\d from=MMSYSTEM \[p1 )[0-9A-F]+ -> '), r'\1(stack) -> '),
    # the messages MMSYSTEM's window gets: its own sound's handle and
    # header
    (re.compile(r"^(  to MMSYSTEM's window: \S+ wParam )[0-9A-F]{4}( lParam )(?!00000000)[0-9A-F]{8}"),
     r'\1(h)\2(ptr)'),
    (re.compile(r"^(  to MMSYSTEM's window: \S+ wParam )[0-9A-F]{4}( lParam 00000000)"), r'\1(h)\2'),
    # the MIDI mapper's capabilities as MIDIMAP leaves them: after the
    # name's end, its stack (MDSM2 is the mapper's shim in mrun.sh)
    (re.compile(r'^(  MD MDSM2 mod 0002 .* -> )[0-9A-F]+\]'), r'\1(caps)]'),
    # ---- the MCI sections (layer 4; tests/mmsystem/mmci.c) ----
    # the MCI helpers that answer a BOOL or a UINT, as a driver calls them
    # (MMSYSLOG's records): DX is what the function's code left there
    (re.compile(r'^(  FWD MMSYSTEM\.7(?:05|07|10|11|13|17) .* -> )[0-9A-F]{4}([0-9A-F]{4} d=)'),
     r'\1(dx)\2'),
    # mciDriverNotify's answer is PostMessage's AX, which 3.1's USER (on
    # both sides) makes the message's place in the queue's ring: how many
    # messages the probe's queue saw before is timing (00F2 and 0108 for
    # the same step); compared: 0 or not
    (re.compile(r'^(  FWD MMSYSTEM\.711 .* -> \(dx\))(?!0000)[0-9A-F]{4}'), r'\1(posted)'),
    # "open" parsed with a device's own entry (MCI_OPEN_DRIVER with device
    # ID 0 in the block): 3.1 hands each string 4 bytes into a copy of
    # it; past the end of a copy of up to four characters is what its
    # local heap holds there (3.1 shows "e" and "e3" again, "ab" as "")
    (re.compile(r'^(  MD \S+ DriverProc 0801 .* \[p2 [0-9A-F]{8}0000.*\+16")[^"]{0,4}(")'),
     r'\1(s)\2'),
    # mciLoadCommandResource as a driver calls it: the name's address (on
    # the driver's stack, how deep depends on the MMSYSTEM below) and the
    # driver's instance handle
    (re.compile(r'^(  FWD MMSYSTEM\.705 from=\S+ args )[0-9A-F ]+(")'), r'\1(args) \2'),
    # MCIWAVE's own wave headers: it leaves dwLoops as its heap had it
    # unless it loops (the flags without WHDR_BEGINLOOP, 4), and the
    # MMTIME it asks the position in is its task's stack as it was
    (re.compile(r'( flags [0-9A-F]{3}[0-389AB] loops )-?\d+'), r'\1(x)'),
    (re.compile(r'^(  MD \S+ wod 000D .* from=MMSYSTEM \[p1 )[0-9A-F]+ -> ([0-9A-F]{12})[0-9A-F]{4}\]'),
     r'\1(stack) -> \2(st)]'),
    # MMSYSTEM closing a device it opened for one command, once the driver
    # has notified it: 3.1 hands the driver a block on the stack it never
    # filled (what its window procedure's frame held)
    (re.compile(r'^(  MD \S+ DriverProc 0802 .* p2 \[stack\] -> \S+ if=\d d=\d from=USER) \[p2 .*$'),
     r'\1 [p2 (stack)]'),
    # how many records MMSYSLOG had no room for
    (re.compile(r'^(  MD lost )\d+'), r'\1(n)'),
    # MIDI input's time stamps (MIM_DATA .. MIM_LONGERROR: dw2)
    (re.compile(r'^(  DCB .* msg 03C[3-6] .* p2 )[0-9A-F]{8}'), r'\1(time)'),
    (re.compile(r'^(  cb h \S+ msg 03C[3-6] .* dw2 )[0-9A-F]{8}'), r'\1(time)'),
]

# lines of timings: never compared (also the WM_TIMER ticks MMSYSTEM's
# window gets while a sound ends: how many come in a pump is timing)
SKIP = re.compile(r"^~|^  to MMSYSTEM's window: 0113 ")

# Known difference, compared apart (counted, not in order): the drivers'
# DRV_EXITAPPLICATION when an MCI driver's background task (MMTASK.TSK,
# MCIWAVE's and MCISEQ's) ends. w16's task ends as soon as the function
# returns; 3.1's later, at a step two Microsoft runs agree on but that
# w16 does not reproduce yet (UNVERIFIED why: ARCHITECTURE.md 23.5).
TASKEXIT = re.compile(r'^  MD \S+ DriverProc 000C ')

KNOWN = [
    # The task function's BX, CX and DI: the instance handle of MMTASK's
    # data (DI) and what is left from the start-up calls (BX, CX); handle
    # values follow the global heap's state, which w16's MMSYSTEM's own
    # allocations change.
    (re.compile(r'^(  task: dwInst .* regs ax \S+ )bx \S+ cx \S+( dx \S+ si \S+ )di \S+'),
     r'\1bx (h) cx (r)\2di (h)'),
    # the function's code selector in MMTASK's data: MMC.DLL's, loaded
    # where the heap puts it
    (re.compile(r'^(  task: data from 10h: \S+ )[0-9A-F]{4}'), r'\1(sel)'),
]


def norm(lines, rules):
    out = []
    for ln in lines:
        for rx, rep in rules:
            ln = rx.sub(rep, ln)
        out.append(ln)
    return out


def main():
    args = sys.argv[1:]
    drop = None
    if len(args) == 4 and args[0] == '--drop':
        drop = re.compile(args[1])
        args = args[2:]
    if len(args) != 2:
        sys.exit(__doc__)
    la = [l for l in open(args[0], errors="replace").read().splitlines()
          if not SKIP.match(l) and not (drop and drop.match(l))]
    lb = [l for l in open(args[1], errors="replace").read().splitlines()
          if not SKIP.match(l) and not (drop and drop.match(l))]
    ea = sorted(re.sub(r' d=\d', '', l) for l in la if TASKEXIT.match(l))
    eb = sorted(re.sub(r' d=\d', '', l) for l in lb if TASKEXIT.match(l))
    if ea or eb:
        print('known: %d and %d task-exit records (DRV_EXITAPPLICATION) compared apart%s'
              % (len(ea), len(eb), '' if ea == eb else ': they differ'))
    a = norm([l for l in la if not TASKEXIT.match(l)], MASKS)
    b = norm([l for l in lb if not TASKEXIT.match(l)], MASKS)
    ka = norm(a, KNOWN)
    kb = norm(b, KNOWN)
    if len(a) != len(b):
        print('FAIL: %d lines against %d' % (len(a), len(b)))
    bad = known = 0
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else '(none)'
        y = b[i] if i < len(b) else '(none)'
        if x == y:
            continue
        if i < len(ka) and i < len(kb) and ka[i] == kb[i]:
            known += 1
            print('known %d:\n  - %s\n  + %s' % (i + 1, x, y))
            continue
        bad += 1
        print('FAIL %d:\n  - %s\n  + %s' % (i + 1, x, y))
    print('%s: %d lines, %d differ, %d known differences'
          % ('FAIL' if bad or len(a) != len(b) else 'ok', len(a), bad, known))
    sys.exit(1 if bad or len(a) != len(b) else 0)


main()
