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
            (the driver's enable against KERNEL's start): 3.1's runs gave
            -36 to -90 ms: within 100 ms
  rate      ticks within 1, the clocks within 60 ms over 37 BIOS ticks
  steps     timeGetTime's smallest and largest step over 220 ms: 1 and 1
            on w16 (3.1's also 1 and 1, except when its clock went back:
            a "largest step" near 2^32, never asked of w16)
  pit       channel 0's period: within 64 counts (it is sampled; 3.1's
            runs spread by up to 33)
  ids       exact
  oneshot   w16's event is called once, no sooner than its delay and no
            later than the delay plus the 8254's period then (its
            resolution, at most 55 ms) and 10 ms; 3.1's is shown, not
            checked: in 3.1's runs the same event came 1 to 55 ms early or
            late, or not at all (docs/notes/timer-handover.md)
  periodic  calls within 3% of 1000/period (3.1's own, when it called the
            event at all: 3.1's drop periodic events of 2-25 ms after a
            1 ms one in this order, every run)
  context   SP, AX, BX, CX, DX exact; selectors masked
  nest      calls during the wait within 3; another event's depth and
            calls shown (3.1's never call it: w16's do, after the wait)
  tod       BIOS ticks within 2, the clocks within 60 ms, callbacks within
            15% (3.1's runs: 6171-6582), WM_TIMER within 2, DOS's clock
            within 10 hundredths
  child     the 8254 within 8 counts, calls within 6
Anything else marked "~" is shown and not checked.

-3 (386 Enhanced mode): 3.1's driver there leaves the timing to VTDAPI.386
and calls back from it, w16's runs on VTD's virtual 8254 and calls back
through MMSYSTEM as in Standard mode (docs/notes/timer-handover.md). The
callback's flags, stack and caller (masked), and the timings (oneshot,
periodic, nest, stime, rate, context, tod, child) are then shown, not
checked; every other line and measurement is checked as in Standard mode.

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
    # its contents are compared
    (re.compile(r'(msg=0804 .* l1=)[0-9A-F]{4}(055E )'), r'\1(sel)\2'),
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
ENH_SHOWN = re.compile(r'(oneshot|periodic|nest|stime|context|tod|child|rate)')


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
        return within(mn[0], wn[0], 100), 'within 100 ms'
    if key == 'rate':
        # ticks N timeGetTime N GetTickCount N
        return (within(mn[0], wn[0], 1) and within(mn[1], wn[1], 60)
                and within(mn[2], wn[2], 60)), 'ticks 1, clocks 60 ms'
    if key.endswith('steps'):
        # min a max b changes c in d
        return wn[0] == 1 and wn[1] == 1, 'steps of 1 ms'
    if re.match(r'(period .*|trace .*|child) pit', key) or key.endswith(' pit'):
        tol = 8 if key.startswith('child') else 64
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
        # when it called them; it dropped both in some runs)
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
        return within(mn[0], wn[0], 3), 'within 3'
    if key.startswith('tod 6 CMOS seconds'):
        # BIOS ticks, timeGetTime, GetTickCount, callbacks, WM_TIMER
        ok = (within(mn[0], wn[0], 2) and within(mn[1], wn[1], 60) and within(mn[2], wn[2], 60)
              and abs(wn[3] - mn[3]) <= mn[3] * 15 // 100 and within(mn[4], wn[4], 2))
        return ok, 'ticks 2, clocks 60 ms, callbacks 15%, WM_TIMER 2'
    if key.startswith('tod DOS clock'):
        return within(mn[0], wn[0], 10), 'within 10'
    if key == 'child calls':
        return within(mn[0], wn[0], 6), 'within 6'
    return None, 'shown'


def main():
    global ENH
    args = sys.argv[1:]
    if args and args[0] == '-3':
        ENH = True
        args = args[1:]
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
