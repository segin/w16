# keyloss branch: handover notes

Working notes for the `keyloss` branch (KERNEL audit
build/kernel-audit-20261112.md of the main checkout, master 05e0494), so
that the work can go on from here. Throwaway tools are in tests/dos/notes/
(not built by the Makefile).

## Branch state

All audit items done so far are committed, 0763c0d .. 75a4c00 (group 1:
0763c0d..351a914; medium, low, exports, part of the missing
functionality, heapcheck and GlobalFree's DS after it). Commit messages
carry the observations and the probes (TK3, TK5, TK16, TK17A, TK50).

Suites (snapshot copies, tests/dos/notes/suite.sh REV OUT):

- 351a914: make test 36/36, dosxtest OK, RUNTESTS_DPMI=dosx 36/36; boot
  test 2 failures: allw16nolock (harness: the X window id went stale under
  load; fixed afterwards by b1a81e0) and alttab (below). No retries.
- 75a4c00: dosxtest OK, dosx 36/36; make test 35/36 (TK6, the FPU
  intermittent below); boot test was still running at the time of writing
  (/tmp/dropin/g2.*).
- master (05e0494 build): alttab 3/3 ok; TK8 fails intermittently too.

## Intermittent failures (bugs, not yet explained)

1. FPU condition codes under DOSBox-X (run-tests' TK6/TK8 with fpu=true):
   now and then a status word has C0 or C3 set where 3.1 has the other
   (TK8 87.fprem.4.s 4001h for 0101h; TK6 87.f8.z.sw 0104h for 0004h,
   87.f9.sw 0100h for 0000h). About 4 in 31 runs on keyloss, and on
   master as well (1 in the first 13 of 30), so it predates keyloss.
   Ruled out: the per-task INT 0/2/4/6/7/3Eh/75h vector switch (0dc0c6a;
   still 2/15 with it disabled). Next: whether an interrupt (timer, the
   DPMI host's reflection) runs FPU code or FSAVE/FRSTOR between the
   instruction and FSTSW; tests/dos/notes/tk8x.sh N repeats TK8.
   Measured: master 1 failure in 30 runs (plus 0 in 10), keyloss 4 in 31
   (2 in 15 with the vector switch disabled): present on master, perhaps
   more frequent on keyloss (not significant yet). Hypothesis to test
   next: the probe's deliberately unmasked exceptions raise IRQ 13 (INT
   75h) at a time that depends on the host's load, and the handler
   (WIN87EM's or the default) touches the status word before or after the
   probe's FSTSW. A run with RUNTESTS_DPMI=dosx and one with HDPMI would
   tell whether the DPMI host's reflection matters.
2. (Update after the merge of master 5996cdf: in a failing run the boot
   had finished, Program Manager drawn, before both "home"s, and w16's
   pointer did not move at all afterwards: still in the middle at
   01-booted and at 02-menu after "home; move 150 200", where 3.1's
   moved; keys still worked. So mouse events stop reaching USER
   (MOUSE_EVENT_C would move ptCursor even with the queue full):
   intermittently MOUSE.DRV's events stop or are not passed on. Not the
   boot's timing: waiting for the title bar before the homes did not
   help. USER/MOUSE side.)
   Boot test case input: 01-booted differs by 110 pixels (the pointer: 3.1
   ends at the top-left, w16 sometimes keeps it in the middle). 2 of 3
   runs fail on master too. Pointer motion during boot is lost now and
   then (USER/MOUSE side).
3. Boot test case alttab (once on 351a914; master 3/3 ok): after Alt+Tab
   Program Manager's group icons were not all repainted (one icon without
   its title instead of Accessories, Games, StartUp). Not yet known
   whether keyloss causes it: needs repeated runs on both.

The key loss / "Illegal GRP4" itself: the USER agent owns it (a writer in
USER/GDI; it fails on Microsoft's KERNEL too). KERNEL audit #1 (local heap
growth to 64K moving DGROUP) is fixed but was not that trigger.

## WIN.COM: the MS-DOS Prompt's banner (done; history below)

Fixed after this note was first written: WIN.COM looks for dswap.exe at
the start (find_file is in the start-up part, given back to DOS before
Windows runs: calling it later was what hung) and runs it with WSWAP's
tail when WSWAP ends for a DOS program, then updates the block as for a
program it ran itself; SI=2 returns a zeroed buffer. Boot test case
winprompt (3.1's WIN.COM against w16's, the MS-DOS Prompt, VER, EXIT,
Exit Windows) matches; wincomswap, winoldapw and tests/wincom.sh pass.
tests/dos/wc3.asm built with -DEACH (WC3E) appends its record after
every call. The notes below are as found.

With w16's WIN.COM the MS-DOS Prompt has no "Type EXIT and press ENTER
..." banner, and its text mode is the one Windows left (8x8, 50 lines).
Observed on 3.1 with E10 (tests/dos/notes/e10.asm: a TSR recording INT
10h, INT 21h output and EXEC calls with paths and tails, INT 2Fh 4A05h;
"E10 D" writes C:\E10.LOG, e10dump.py prints it; e10run.sh runs the
woados scenario with it):

- WIN.COM EXECs, from one call site and buffer: wswap.exe " \x08", then
  when WSWAP ends for a DOS program `C:\WINDOWS\system\dswap.exe` with the
  same tail " \x08", then wswap.exe again.
- DSWAP does INT 10h 6F00h, 1202h (BL=30h: 400 scan lines), mode 3,
  cursor 0607h, prints the banner (INT 21h AH=09h, a box drawn with
  C9h/CDh...), and EXECs Z:\COMMAND.COM itself.
- WC3 put in front of DSWAP (tests/dos/notes/wcd.sh: WC3.COM as
  DSWAP.EXE, the real one as DSWAP.EX_) gives DSWAP's INT 2Fh calls on
  3.1: 4A05h SI=5 (CX=FFh, DX=PSP) -> block 0194:06C8; XMS 4300h/4310h;
  4B05h, 4B02h, 4B01h (several); 4A05h SI=5; SI=2 (CX=10h, DX=block
  segment) -> AX=028Ah DX=010Fh SI=0B5Dh DI=06C8h, the 64 bytes at DX:AX
  all zero (WSWAP's SI=2 got 010F:02EE, also zeros); SI=5; SI=4 (DI=block)
  -> AX=0101h SI=8 DI=06C8h.
- tests/dos/notes/dswap-wip.patch: w16's WIN.COM EXECs DSWAP (found as
  dswap.exe like wswap.exe) and SI=2 returns a zeroed buffer of its own
  (w16 returned a pointer into its code). With it DSWAP still hangs
  (black screen). Next: a WC3 variant that appends each call to WC.LOG at
  once (WC3 writes only at the end), to see which call of DSWAP's gets a
  different answer from w16's WIN.COM.
- (Fixed since: WIN.COM puts back 0040:0084h after its logo, as 3.1's
  does; winprompt checks the text mode after Windows.) Also found: at
  Windows' start the display driver reads 30 rows from the
  BIOS under w16's WIN.COM (INT 10h 1130h DL=1Dh) and 25 under 3.1's
  (DL=18h): both show the logo in mode 12h; 3.1's leaves the BIOS's row
  count at 25. That is why the DOS screen after Windows comes back 8x8.

## Remaining audit items

- LocalNotify with 3.1's default handler growing the heap (Pietrek
  LocalNotifyDefault); a real LocalShrink (3.1 shrinks DGROUP back to
  about the module's heap: TK3 "shrink" records LocalShrink(0, 0) = 3FEh,
  LocalHeapSize 3F4h, DGROUP 6A20h on 3.1, not checked).
- ExitWindows when the last task ends (Pietrek).
- INT 24h handler, SetErrorMode, OF_PROMPT/OF_CANCEL: need a design (a
  critical-error handler under DPMI; USER's dialogs).
- The environment copy LoadModule makes is never freed (owner 0); on 3.1
  the copy was still there right after the child ended (TK16 "env: block
  size"), so a PDB owner was not right: needs a probe that looks later.
- The loader's own PATH search (LoadLibrary, WinExec) still reads
  KERNEL's environment; OpenFile's reads the task's (TK16 "envpath").
- Expected Windows version 0 -> 2.01 is from Pietrek only: on 3.1 a probe
  starting such a program stopped (a message box?).
- Self-loading programs and code-segment discard/reload: proposal with
  the coordinator, not to be started.

Order agreed with the coordinator: the 75a4c00 suite and alttab, the
DSWAP banner, the three intermittent bugs, then LocalNotify/LocalShrink
and ExitWindows on the last task.

## Session 2 (master 0075892, 0248006, 8690c87 merged)

Scratch: /tmp/kl (scripts e10.sh, e10p.sh, full.sh, wcw.sh, inp.sh,
inpms.sh), /tmp/klrep (repeated boot-test cases), worktrees
.claude/worktrees/kl-master (master build for comparisons) and kl-exp
(instrumented builds: USER logs mouse/key event counts with
[w16] userlog=1; never commit from it).

Task 1 (regressions?): write 3/3 ok on keyloss and on master, mouseall
3/3 ok on both; alttab: keyloss 1 fail in 4 (Notepad's caret did not
blink: fixed on master by 8690c87, merged since), master 1 fail in 4
(repaint not finished at the shot: fixed by the harness's match waits).
No keyloss regression found.

Task 2 (exit in 640x480): DONE, 8434256 + 8d4cb37 + edb1d06 (WIN.COM
sets mode/cursor after WSWAP for a DOS program and after a normal end or
restart, not after an error: tests/wincom.sh; DOSX leaves the mode; DSWAP
keeps 16Fh/17Ch). woadosbat passes, no longer xfail.

woados (Microsoft's WIN.COM, every other w16 part) intermittent since
master 8690c87 (the caret blink): not the display. FIXED, 32e6fab: a
stale QS_TIMER (a ready timer killed or reset left the bit on) kept
Program Manager going round GetMessage without waiting or yielding, so
WINOLDAP's switch timer waited. Found with KERNEL dumps of the task list
and event counts at WaitEvent/OldYield and a USER log of that loop (kl-exp,
reverted). Side finding, not changed: USER's set_wakebit posts an event
for every bit set, waiting or not (Program Manager had 60 piled up);
Pietrek's SetWakeBit2 posts only for a bit in the wake mask and clears
the mask. A candidate fidelity change; needs a full boot test.

Task 3 (input case): e38cbfa (the right click after the boot) made it
pass 3 of 3 alone, but it failed again in the full parallel boot test
(/tmp/kl/bt-full1: pointer still in the middle, the right click was in
input.log). So the capture race is not the whole story, or the right
click itself did not capture under load. NOT DONE. Facts so far: the
event procedure is never called (instrumented USER); MS1: after C200h no
INT 74h at all, slave PIC IMR EC, ISR/IRR 0, IVT 74h = DOSX's (TK49 NOW
line, tests/apps/tk49.c change in kl-exp only); DOSBox 0.74-3 passes
motion only when it holds the mouse (autolock) - see e38cbfa's message
for the source facts. Next: log DOSBox-side capture (e.g. check whether
SDL grabbed: xdotool getmouselocation before/after the click) and run
the w16 side with INPUTRUN_NOLOCK=1 to separate DOSBox capture from w16.

Task 4 (FPU flake), not started beyond reading: the coordinator saw on
master b1e409d TK6 (87 run) lose exception bits from the status word
(87.div0.sw 0004, 87.inv.sw 0001, 87.inv.masked.sw 0841,
87.div0.masked.sw 0844 on 3.1). w16's WIN87EM IRQ 13 handler
(win87em/win87em.asm irq13_handler) does FNCLEX whatever it finds, even
with no unmasked exception pending: an IRQ 13 that DOSBox-X delivers
late (after the probe's next FP instruction) would clear the bits the
probe's FSTSW then reads. Next: count IRQ 13 handler calls per probe step
(TK6/TK8 already record "h" = handler calls) in a failing run, and probe
what Microsoft's WIN87EM does with an IRQ 13 when no unmasked exception
is pending (black box).

From the coordinator (master a5deb40, merged 047f871; local.c conflict
resolved to keyloss's grow(), which already caps at SEG_MAX = FFF0h):
- ToolHelp's GlobalEntry on w16's KERNEL: no block shows a local heap
  (wHeapPresent / the local-heap flag never set): US14's heap checks
  fail there. To do.
- boot-test icons fails under load on master too (2 of 3, at Alt+F, R
  right after Notepad is minimised): an existing USER activation race,
  noted only.

## Session 3 (branch keyloss2 from master 47251d5)

- input: measured with MS1 (BIOS ticks in place of the depth word, kl-exp
  only) and timestamps in input.log: the time from MOUSE.DRV's detection
  (C205h/C201h, DOSBox drops its capture request) to USER's MOUSE.Enable
  (C207h/C200h) is 2.0-2.6 s on w16 and 0.9-1.1 s on 3.1 (6 runs each,
  same load; 7 s for both under 6-fold load). A click in that window is
  passed on and DOSBox never takes the mouse; before it (DOSBox's
  initial request) and after it the click captures. With e38cbfa's right
  click after the boot: 22 of 22 w16 runs ok (1, 3, 4, 6 at a time); the
  one failure (full boot test /tmp/kl/bt-full1) is not reproduced.
- FPU: 2f329ff. Not WIN87EM: Windows 3.1 itself gives C3/C0 in those
  status words (undefined condition codes). TK6 30/30, TK8 30/30 since.
- ToolHelp: DONE, TK53 e07d28c + bf61200 (KRNL286's information block;
  TK3 checks per layout, tk3ref286.h regenerated, tk13ref286.h new).
- Wake bits: DONE, 7e6ce4f (SetWakeBit2). Remote full run: make test
  pass, boot test 41 ok + winoldapw (known artefact); a later subset run
  had winoldap/winoldapk 371 px once (not looked at; was ok in the full
  run with the same code).
- 286 boot: 8bc4a05. Not a boot failure: the watchdog (dropin's
  watchdog=15) kept running after the boot and ended an idle Windows
  before a fixed-time shot. KERNEL stops it after Final USER init now;
  boot-test k286 (both DOSXes on cputype=286): 0 pixels. The bisect
  (fed5337) was noise from single runs on a loaded host. Under heavy
  local load the 286 boot is very slow (no BOOTLOG in 150 s once); the
  remote host does it in ~21 s.
- Probe instrumentation used for input (TK49's NOW line with the slave
  PIC registers and IVT 74h, MS1 with BIOS ticks, inputrun timestamps):
  tests/dos/notes/input-probes.patch (not applied).
- Next: (5) LocalNotify/LocalShrink, (6) ExitWindows when the last task
  ends. Full suites: tools/remote-test.sh (installation-05).
- Final remote run on 531c5fe: make test pass, boot test 44/44 ok.
- Added to the list (coordinator): winoldap/winoldapk/winoldapw failing
  now and then by ~352.7 or 371.0 px (DOSBox's displayed image only;
  GDI GetPixel reads the right frame; see commit 7480e1c's notes): make
  the cases reliable without loosening thresholds (finish the
  vgaonly/cycles sweep, or compare a GDI-side dump, or force a full
  redraw before the shot). After (5) and (6).
