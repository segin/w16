#!/usr/bin/env python3
"""tcmp - compare two logs of the timer probe TPR.EXE (tests/timer/tpr.c):
Microsoft's TIMER.DRV (the reference) against w16's, both under
Microsoft's MMSYSTEM (ARCHITECTURE.md 21).

Lines not starting with "~" must be identical, after masking what is not
the driver's to decide: the millisecond count TMS.DRV records for message
0808h (the time) and the registers SI and DI a callback finds (left over
from the driver's code; 3.1's change from call to call). Lines starting
with "~" are measurements and are checked by the rules below, each one
justified by the spread of repeated runs of Microsoft's own stack or by a
documented difference (docs/notes/timer-handover.md):

  stime     timeGetTime - GetTickCount: the two clocks' starting points
            (the driver's enable against KERNEL's start): w16's between
            -1000 and 55 ms. 3.1's runs gave -36 to -90 ms on an idle
            host and down to -469 under load (the boot's emulated length
            grows with the host's load, on both sides alike); a clock not
            started at the driver's enable would be seconds off
  rate      ticks within 1, the clocks within 60 ms over 37 BIOS ticks
  steps     timeGetTime's smallest and largest step over 220 ms: 1 and 1
            on w16 (3.1's also 1 and 1, except when its clock went back:
            a "largest step" near 2^32, never asked of w16)
  pit       channel 0's period: within 64 counts or 0.5%, whichever is
            more (it is sampled, the largest count over 3 BIOS ticks:
            3.1's runs spread by up to 33 on an idle host, up to 164 at
            54-55 ms under load; one millisecond more or less
            is 1193 counts, 1.8% at 55 ms)
  ids       exact
  oneshot   w16's event is called once, no sooner than its delay and no
            later than the delay plus the 8254's period then (its
            resolution, at most 55 ms) and 10 ms; 3.1's is shown, not
            checked: in 3.1's runs the same event came up to 55 ms early
            or late by its own clock, which steps 55 ms back and on again
            (docs/notes/timer-handover.md)
  periodic  calls within 3% of 3.1's own count (of 1000/period if 3.1's
            gave 0: before TPR's waits stopped trusting 3.1's clock, they
            ended at once there and its events looked dropped)
  context   SP, AX, BX, CX, DX exact; selectors masked
  nest      calls during the wait within 5 (3.1's 58-60 in 24 runs,
            w16's 60-61: what is checked is that the calls missed while
            the callback waits are not made up, about 100, nor the event
            lost, about 1); another event's depth while a callback waits
            exact (1: not called inside it), its calls shown; the waiting
            one-shot event's calls and kill exact (1 and 97)
  tod       over 6 CMOS seconds, which follow the host's clock, so that
            the emulated time in them shrinks with the host's load (3.1:
            108-110 BIOS ticks): w16's clocks against its own timeGetTime
            (3.1's hold the same in all 49 runs kept, idle and loaded):
            BIOS ticks x 54.925 and GetTickCount within 60 ms of it,
            callbacks of the 1 ms event (set up to a second before the
            measure starts) from 97% of it to 103% of it + 1000, WM_TIMER
            of 100 ms within 2 of it / 109.85 (two BIOS ticks each: 3.1's
            55-56 in 6 s), DOS's clock within 10 hundredths of BIOS
            ticks x 5.4925
  child     the 8254 within 8 counts, calls within 6
Anything else marked "~" is shown and not checked.

-3 (386 Enhanced mode): 3.1's driver there leaves the timing to VTDAPI.386
and calls back from it, w16's runs on VTD's virtual 8254 and calls back
through MMSYSTEM as in Standard mode (docs/notes/timer-handover.md). The
callback's flags, stack and caller (masked), and the timings (oneshot,
periodic, nest, stime, rate, context, tod, child, period steps, and how
many of the event section's 1, 2 and 10 ms one-shot events came before
they were killed: 3.1's 1, w16's 2 under load) are then shown, not
checked; every other line and measurement is checked as in Standard
mode.

usage: tcmp.py [-3] MS.log W16.log
Exit status 0 when everything checked agrees.
"""
import re
import sys

MASKS = [
    (re.compile(r'(msg=0808 .*-> )[0-9A-F]{8}'), r'\1(time)'),
    (re.compile(r'(msg 0808 counted )\d+'), r'\1(n)'),
    # TMS's nesting depth: whether a timer interrupt came while the probe
    # was inside the driver for another call (two of 3.1's runs differ)
    (re.compile(r' d=\d+ '), ' d=(n) '),
    # a structure on MMSYSTEM's interrupt stack (set from a callback): its
    # selector, which depends on the size of the timer driver's segments;
    # its offset (near the top of the 600h-byte stack: 055Eh, 0558h since
    # TPC's callback has more locals) and contents are compared
    (re.compile(r'(msg=0804 .* l1=)[0-9A-F]{4}(05[0-9A-F]{2} )'), r'\1(sel)\2'),
    # the same for a structure on a second task's stack (the child): its
    # DGROUP's selector
    (re.compile(r'(msg=0804 .* l1=)[0-9A-F]{4}([0-9A-F]{4} l2=00000000 -> [0-9A-F]{8} if=\d d=\(n\) from=MMSYSTEM task=other)'), r'\1(sel)\2'),
]


def mask(line):
    for rx, rep in MASKS:
        line = rx.sub(rep, line)
    return line


def nums(s):
    return [int(x) for x in re.findall(r'-?\d+', s)]


def split(lines):
    exact, meas = [], {}
    for ln in lines:
        ln = ln.rstrip('\n')
        if ln.startswith('~'):
            body = ln[1:].strip()
            key, _, val = body.partition(':')
            k = key
            n = 2
            while k in meas:
                k = '%s #%d' % (key, n)
                n += 1
            meas[k] = val.strip()
        else:
            exact.append(mask(ln))
    return exact, meas


def within(a, b, tol):
    return abs(a - b) <= tol


def period_of(delay):
    return min(delay, 55)


ENH = False
ENH_SHOWN = re.compile(r'(oneshot|periodic|nest|stime|context|tod|child|rate|period steps)')
MA, MB = {}, {}


def todok(n):
    """BIOS ticks, timeGetTime, GetTickCount, callbacks, WM_TIMER of one
    run, against its own timeGetTime"""
    ticks, t, g, cb, wmt = n[:5]
    return (abs(ticks * 54.925 - t) <= 60 and abs(g - t) <= 60
            and t * 0.97 <= cb <= (t + 1000) * 1.03 and abs(wmt - t / 109.85) <= 2)


def dosok(d, ticks):
    return abs(d - ticks * 5.4925) <= 10


def check(key, ms, w):
    """-> (ok, why) for one measurement; ok None: shown only"""
    if ENH and ENH_SHOWN.match(key):
        return None, "386 Enhanced mode: shown"
    if w is None:
        return False, 'missing on w16'
    if ms is None:
        return False, 'not in the reference'
    mn, wn = nums(ms), nums(w)
    if key.startswith('stime timeGetTime - GetTickCount'):
        return -1000 <= wn[0] <= 55, '-1000 to 55 ms'
    if key == 'rate':
        # ticks N timeGetTime N GetTickCount N
        return (within(mn[0], wn[0], 1) and within(mn[1], wn[1], 60)
                and within(mn[2], wn[2], 60)), 'ticks 1, clocks 60 ms'
    if key.endswith('steps'):
        # min a max b changes c in d
        return wn[0] == 1 and wn[1] == 1, 'steps of 1 ms'
    if re.match(r'(period .*|trace .*|child) pit', key) or key.endswith(' pit'):
        tol = 8 if key.startswith('child') else max(64, mn[0] // 200)
        if mn[0] >= 65400 and wn[0] >= 65400:
            return True, 'both 65536'
        return within(mn[0], wn[0], tol), 'within %d counts' % tol
    if key == 'ids first':
        return mn == wn, 'exact'
    m = re.match(r'oneshot (\d+)( res (\d+))?$', key)
    if m:
        d = int(m.group(1))
        per = period_of(int(m.group(3)) if m.group(3) else d)
        # "calls C id same after A kill K" / "calls C after A"
        calls, after = wn[0], wn[1]
        same = 'id same' in w or m.group(2)
        ok = calls == 1 and bool(same) and d <= after <= d + per + 10
        if not m.group(2):
            # killed after it fired: 97 (in 386 Enhanced mode 3.1's says 0
            # to any kill)
            ok = ok and wn[2] == (0 if 'kill 0' in ms and 'calls 1' in ms else 97)
        return ok, 'once, after %d-%d ms' % (d, d + per + 10)
    m = re.match(r'periodic (\d+)$', key)
    if m:
        p = int(m.group(1))
        want = mn[0] if mn[0] > 0 else 1000 // p
        return abs(wn[0] - want) <= max(2, want * 3 // 100), 'calls within 3%% of %d' % want
    if key.startswith('periodic 10 and 15'):
        # 900 ms of a 10 ms and a 15 ms event: 90 and 60 (3.1's own count
        # when it has one: see periodic)
        a = mn[0] if mn[0] else 90
        b = mn[1] if mn[1] else 60
        return within(a, wn[0], 3) and within(b, wn[1], 3), 'within 3 of %d, %d' % (a, b)
    if key.startswith('context sp'):
        # sp ax bx cx dx si di (hex)
        mh = re.findall(r'([a-z]{2}) ([0-9A-F]{4})', 'sp: ' + ms)
        wh = re.findall(r'([a-z]{2}) ([0-9A-F]{4})', 'sp: ' + w)
        md, wd = dict(mh), dict(wh)
        ok = all(md.get(r) == wd.get(r) for r in ('sp', 'ax', 'bx', 'dx'))
        return ok, 'SP AX BX DX exact (CX, SI, DI: the driver\'s)'
    if key.startswith('context selectors') or key.startswith('stack before'):
        return None, 'selectors'
    if key == 'nest calls during the wait':
        return within(mn[0], wn[0], 5), 'within 5'
    if key.startswith('nest another event while a callback waits'):
        return ms == w, 'exact'
    if key.startswith('nest the waiting one-shot event'):
        return ms == w, 'exact'
    if key.startswith('tod 6 CMOS seconds'):
        return todok(wn), 'w16\'s clocks against its timeGetTime'
    if key.startswith('tod DOS clock'):
        t = [v for k, v in MB.items() if k.startswith('tod 6 CMOS seconds')]
        if not t:
            return False, 'no tod line'
        return dosok(wn[0], nums(t[0])[0]), 'within 10 of BIOS ticks x 5.4925'
    if key == 'child calls':
        return within(mn[0], wn[0], 6), 'within 6'
    return None, 'shown'


def main():
    global ENH
    args = sys.argv[1:]
    if args and args[0] == '-3':
        ENH = True
        args = args[1:]
        # the event section's one-shot events of 1, 2 and 10 ms: how many
        # came before they were killed (3.1's VTDAPI: 1; w16's on VTD's
        # virtual 8254: 1, 2 under load)
        MASKS.append((re.compile(r'^slot 1 calls: \d+$'), 'slot 1 calls: (n)'))
        MASKS.append((re.compile(r'^  flags [0-9A-F]{4} \(IF (\d) DF (\d)\).* task (\w+) depth (\d+)$'),
                      r'  flags (IF \1 DF \2) task \3 depth \4'))
        # a set from a callback: its structure is on the callback's stack
        # (VTDAPI's on 3.1, MMSYSTEM's on w16): offset and the word after
        # it masked, the contents compared
        MASKS.append((re.compile(r'(msg=0804 .* l1=)[0-9A-F]{8}( .*\[l1 [0-9A-F]{28})[0-9A-F]{4}\]'),
                      r'\1(stack)\2(..)]'))
    if len(args) != 2:
        sys.exit(__doc__)
    a = open(args[0], errors='replace').read().splitlines()
    b = open(args[1], errors='replace').read().splitlines()
    ea, ma = split(a)
    eb, mb = split(b)
    MA.update(ma)
    MB.update(mb)
    fails = 0
    if ea != eb:
        import difflib
        d = list(difflib.unified_diff(ea, eb, 'ms', 'w16', n=1, lineterm=''))
        print('FAIL lines differ:')
        for ln in d[:60]:
            print('  ' + ln)
        fails += 1
    else:
        print('ok   %d lines identical' % len(ea))
    shown = 0
    for k in list(ma) + [k for k in mb if k not in ma]:
        ok, why = check(k, ma.get(k), mb.get(k))
        if ok is False:
            print('FAIL ~ %s: 3.1 "%s", w16 "%s" (%s)' % (k, ma.get(k), mb.get(k), why))
            fails += 1
        elif ok is None:
            shown += 1
            if ENH and ma.get(k) != mb.get(k):
                print('     ~ %s: 3.1 "%s", w16 "%s" (shown)' % (k, ma.get(k), mb.get(k)))
    print('%s  %d measurements, %d checked, %d shown only' %
          ('ok  ' if not fails else 'FAIL', len(ma), len(ma) - shown, shown))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
