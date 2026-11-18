# TOOLHELP handover (w16's TOOLHELP.DLL)

Task: w16's own TOOLHELP.DLL, a drop-in for 3.1's SYSTEM\TOOLHELP.DLL
(module TOOLHELP, WEP 1 and the 34 functions 50-83), every behaviour
probed black-box on 3.1 (KRNL286 and KRNL386, Standard and 386 Enhanced
mode), installed in the tree, a boot-test case with real callers.
ARCHITECTURE.md 22 has the observations, the design and the results;
this note is the working state.

Rules followed: no Microsoft binary disassembled or hex-read for code
(TOOLHELP's NE header read with tools/neinfo.py, its importers with
tools/neimports.py); facts from the SDK reference (Vol. 1 ch. 8, Vol. 2,
Vol. 3), Schulman's Undocumented Windows ch. 5, 6, 10 and Pietrek's
Windows Internals (facts only, each confirmed by the probe); all DOSBox
work on installation-05 under the run names `w16-toolhelp` and
`w16-toolhelp-w` (`tools/remote-run.sh`, `tools/remote-test.sh`);
Microsoft's TOOLHELP only copied and renamed at test time (section d,
tests/toolhelp/thrun.sh), never committed.

## Design in one paragraph

w16's KERNEL and USER already keep 3.1's layouts (THHOOK, both global
heap layouts, local heap information blocks, module tables, TDBs,
USER's CLS list), so w16's TOOLHELP reads those layouts, and it runs
under Microsoft's KERNEL as well: that makes the strongest test possible,
both TOOLHELPs side by side in one session (probe section d) answering
every structure byte for byte. Notifications come through KERNEL's
ToolHelpHook (KERNEL.341), faults through DPMI 0203h/0205h, task control
through the scheduler's frame (now 3.1's in w16's KERNEL), the class
list through USER's UserSeeUserDo (now implemented in w16's USER).

## Status

- [x] shell/toolhelp/: all 35 exports, fixed code and data, flags
  8309/08, version 3.10.0.103; SYSTEM\TOOLHELP.DLL in the tree; README.TXT
- [x] probe THP.EXE + THC.EXE + THD.DLL (tests/toolhelp/), runners
  thrun.sh, thscen.sh, thcmp.py (masks: same.mask, tree.mask),
  thwatson.sh, thcase.sh (the boot-test parts)
- [x] section d: 0 differences on ref286, ref386, ref3, tree286, tree386
  (w16's beside Microsoft's), and Microsoft's beside w16's on the tree
- [x] KERNEL: scheduler frame, InitTask's zero word and BP, FREESEG
  order, ES of the 50h/64h notifications; USER: UserSeeUserDo
- [x] boot-test case toolhelp (parts ref286 ref386 ref3 tree286 tree386
  watson qt)
- [x] ARCHITECTURE.md 22, TESTING.md

## Found on the way

- Microsoft's TOOLHELP on w16 (before: every read-only answer the same
  now, section d): USER's UserSeeUserDo was a stub, so ClassFirst found
  one empty class (fixed: user/class.c); MemoryWrite to a code segment
  ends Windows on w16 (its write path relies on something w16's KERNEL
  or DOSX does otherwise; not looked into: w16's TOOLHELP is the one in
  the tree); its fault handlers never see a fault on w16's tree (w16's
  KERNEL reports the fault itself); TaskSwitch/TerminateApp did not
  complete there. Those are the places where w16's system differs below
  TOOLHELP; they matter only to programs that bring their own TOOLHELP.
- w16's KERNEL differences left (tree.mask, each explained there): a
  code segment gets its minimum allocation where 3.1 gives one byte more
  (rounded: 2B0h becomes 2D0h under KRNL286, 0E0h becomes 100h under
  KRNL386);
  module tables fixed under KRNL286 (wcLock 4Dh, handle = selector) and
  smaller; a copy of a program's DGROUP kept for further instances;
  KERNEL's own code not in the global heap (StackTraceFirst's first
  frame has no module); the high bits of a segment's flags in the module
  table (NFYLOADSEG.wType, bit 0400h); the order a program's segments are
  freed in at its end; the scheduler's other frame words (3.1 has the
  task's TDB in DS); under KRNL386 a code segment without relocations is
  20h bytes smaller, and a locked RCDATA's block handle is not the one
  LoadResource gave (UNVERIFIED which).
- DOSBox does not raise a fault for a write to a code segment, a load
  through a null selector, or a stack access past its limit: the probe
  faults with divide (0), INT 3 and an invalid opcode (6) only. A
  write to the probe's own code "succeeds" and corrupts it (the probe no
  longer does that).
- tests/refrun.sh: a DOSBox hung in the guest ignored `timeout`'s TERM
  and blocked a run for good; it is `timeout -k 10` now.

## Next steps (if continuing)

- Low-stack faults (8000h | number, SS:SP pushed), Ctrl+Alt+SysRq (100h)
  and INT 1 under DOSBox-X (`-p 386x`, which checks limits): the stack
  and GP faults DOSBox does not raise.
- TerminateApp(UAE_BOX) and Dr. Watson catching a fault, compared by
  screenshot (w16's KERNEL reports an unhandled fault and leaves to DOS;
  3.1 shows its Application Error box).
- The scheduler's DS (3.1: the task's TDB) if a debugger turns out to
  rely on it.

## Runs

Full run (installation-05, run w16-toolhelp, 7f9701e,
`W16_QTDIR=/mnt/build/w16-qtfiles`): make test pass; boot test 120 ok, 3
FAIL, all known on master 9ba5e31: ddeml (conn, w16-ms), lzexpand
(quirks: eof, zero, long matches), trails (timed: two run-downs 439 and
549 ms against 440-660). toolhelp passes in every part (ref286, ref386,
ref3, tree286, tree386, watson, qt), quicktime too (with w16's TOOLHELP
in the tree now: 12 s on 3.1 and on w16, the last frame 0 pixels).

Retries (thscen.sh reruns a group that did not finish; boottest
toolhelp-*/retries.txt): Microsoft's TOOLHELP, section e, under KRNL386
(ref386 and ref3) ended Windows once each in the full run, in the middle
of the TimerCount dwSize checks, while 13 boot-test cases ran beside it;
once before as well (ref386, the first toolhelp-only run). w16's TOOLHELP
needed no retry. Not looked into further (it is Microsoft's DLL); if it
becomes frequent, run section e alone at fewer jobs.

Earlier runs, for reference: build/th/{a..e} (thscen.sh on all systems,
both TOOLHELPs), boottest-085739 and -090525 (the case alone; they found
the boot-to-boot order of FREESEGs, the code segment sizes, KERNEL's
50h/64h registers and InitTask's zero word).
