# TIMER.DRV handover (w16's multimedia timer driver)

Task: w16's own TIMER.DRV, the installable driver Microsoft's MMSYSTEM.DLL
opens through SYSTEM.INI `[drivers] timer=timer.drv` and that serves the
time* functions; the message protocol found black-box; probes comparing
Microsoft's MMSYSTEM over Microsoft's TIMER.DRV with Microsoft's
MMSYSTEM over w16's; a boot-test case with QuickTime's playback; the
install tree. ARCHITECTURE.md 21 has the facts in order; this note has
how they were found, the data, and what is left.

Rules followed: Microsoft's TIMER.DRV and MMSYSTEM.DLL observed from
outside only: their NE tables and resources (tools/neinfo.py,
tools/resdump.py), the logging pass-through driver TMS.DRV between them,
and probes. Nothing disassembled; no Microsoft or Apple file committed.
Every DOSBox run on installation-05 under `/mnt/build/w16-runs/w16-timer`
(tools/remote-run.sh, tools/remote-test.sh `-n w16-timer`).

## How the protocol was found

1. TMS.DRV (tests/timer/tms.c) loads the real driver as a library and
   forwards every DriverProc call, recording it. With it as
   `[drivers] timer=tms.drv`, TPR.EXE (tests/timer/tpr.c) called each
   time* function with every argument class and printed TMS's records
   after each call. Six messages appeared (0800h kill, 0804h set, 0808h
   time, 080Ch capabilities, 0810h begin, 0814h end), all sent by
   MMSYSTEM directly (identifier 0, its handle), not through USER;
   timeGetTime's records show the application as the caller: MMSYSTEM
   jumps into DriverProc. 0804h's parameter points at a copy of
   timeSetEvent's arguments (delay, resolution, callback, dwUser, flags).
2. The results, the slots and IDs (15 at once, (count << 4) | slot, the
   count wrapping FFFh -> 1: 4100 events set and killed), the flags'
   bits, the messages through USER (sent by the probe with
   SendDriverMessage to MMSYSTEM's open handle) and Windows' end (TMS's
   DRV_FREE record) came the same way.
3. The callback's context: TPC.DLL's entry records every register;
   TPC also calls StackEnter and StackLeave with marker registers. SP at
   entry 05C2h on MMSYSTEM's 600h-byte stack, DS the driver's: w16's
   driver pushes three words more than DriverCallback's arguments to get
   the same SP, and calls StackEnter once at task time in its first
   set-event message, after which GetExePtr of the stack gives TIMER.DRV
   as with 3.1's (in some probe orders: see "Left over").
4. The 8254: TPR reads channel 0's latched count (its period is the
   largest count seen over 3 BIOS ticks) and status (mode 2, B4h), and
   the count right after timeSetEvent returns (already the new period:
   3.1's restarts the count). TPC's INT 8 hook, in front of the driver's,
   counts the interrupts and logs the count at each, so that each
   callback can be placed at an interrupt (sections trace and kin).
5. 386 Enhanced mode: the same probe under WIN /3 (3.1's driver uses
   VTDAPI.386 there). TPR stops reading the 8254 and the PIC there.

## What 3.1's does that w16's does not

All deterministic in DOSBox (repeated runs gave the same interrupt
counts), and all faults rather than a design (data: TPR's sections kin
and trace, `TRUN_ARG=K tests/timer/trun.sh OUT ms plain`; ARCHITECTURE.md
21.2):

- After it restarts the 8254, its clock gains a whole 55 ms period:
  one-shot events of 1-54 ms with resolution 0 come at the 2nd interrupt
  (d ms after they were set) and timeGetTime in the callback reads
  55 + 2d (kin p0d1 ... p0d54).
- One-shot events come early at some delays: at the 55 ms period, 100
  and 109 ms after 55 ms (1st interrupt), 150 after 110, 200 after 165,
  250 after 220; 45 ms at period 10 and 30 ms at period 1 at the first
  interrupt. No simple rule fitted (16- and 32-bit comparisons of
  milliseconds and of 8254 counts, against the interrupt counts); the
  same delays give the same results 7 s later in the same run (TPR
  section k), so it is not the absolute time.
- After a 1 ms periodic event is killed, events set in the next second
  or so are not called (in plain runs, every run: periodic 2, 5, 10,
  25 ms got 0 calls in 1 s; one-shot 10-100 ms none; the nest section's
  two events neither in 150 ms). With TMS between, the timing shifts and
  the events come.

w16's driver keeps exact time across restarts and calls an event at the
first interrupt after its time: one-shot events within their delay plus
one period, periodic ones every period. tcmp.py checks w16's against
those bounds and shows 3.1's (two runs of Microsoft's own fail those
checks, by the faults above: 14 measurements).

## Results (installation-05)

- `tests/timer/tscen.sh` (boot-test case `timer`): Standard mode, plain:
  225 lines identical, 42 measurements within tcmp.py's limits; with
  TMS.DRV: 1042 lines identical after the masks (time values, TMS's
  nesting depth, which differs between two of Microsoft's runs, and the
  selectors of structures on MMSYSTEM's interrupt stack and on a second
  task's stack). 386 Enhanced mode: plain 225, with TMS.DRV 1027 lines
  identical, the timings shown. `qt`: QuickTime's sample movie with only
  w16's TIMER.DRV over stock 3.1 plays as on 3.1 (same window and last
  frame, 0 pixels; 12 s on both). The parts passed three runs in a row
  after the last change.
- The full run (`tools/remote-test.sh -n w16-timer`, W16_QTDIR set):
  make test passed; boot test 119 ok, 4 failures: holdbtn, ddeml (conn),
  lzexpand (quirks), all known on master 9ba5e31, and timer@3, whose one
  difference (a structure on the callback's stack, VTDAPI's on 3.1) is
  now masked; `timer`, `quicktime` (whole tree over 3.1, so w16's
  TIMER.DRV) and `treealone` (the tree's SYSTEM.INI now names
  mmsystem.dll, which the tree lacks: boots as before) passed after it.
- Microsoft's own runs are not stable against each other under these
  rules: its dropped and early events fail 14 of the measurements in a
  second run of its own. The probe therefore waits for a callback (up to
  3 s) where the section is about what the callback sees, and tcmp.py
  falls back to the nominal count where 3.1's dropped an event.

## Left over

- 386 Enhanced mode: 3.1's driver hands the timing to VTDAPI.386 and is
  exact (one-shot events after exactly their delay, callbacks from
  VTDAPI's stack); w16's runs on VTD's virtual 8254, with one-shot events
  20-80 ms late and a 5 ms periodic event about 65 calls a second. The
  next step is VTDAPI's interface: TMS.DRV can hook INT 2Fh before it
  loads the real driver and log the VTDAPI entry point's calls
  (AX=1684h, the VxD's ID) the way it logs DriverProc.
- The owner of MMSYSTEM's interrupt stack: TIMER.DRV on 3.1 in every
  order; on w16 TIMER.DRV when the probe's first event is set in its
  context section, MMSYSTEM when its event section comes first (the
  same calls reach the driver in both). Shown by TPR, not compared.
- In 386 Enhanced mode 3.1's first ID has count 2: whether a refused
  event (delay 0, the probe's first) counts was not told apart; w16
  starts the count at 1 there.
- Not tried: NULL pointers (TPR section n), an 80286, w16's WIN386, real
  hardware.
