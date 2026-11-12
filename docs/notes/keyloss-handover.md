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
2. Boot test case input: 01-booted differs by 110 pixels (the pointer: 3.1
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
- Also found: at Windows' start the display driver reads 30 rows from the
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
