# WSWAP/DSWAP handover (swap agent)

Task: w16's own WSWAP.EXE and DSWAP.EXE (Standard mode's task switcher),
black-box observed on 3.1, tested against Microsoft's.

## Where things are

- `swap/wswap.asm`, `swap/dswap.asm`, `swap/swap.inc`: the programs (nasm,
  .COM-format images named .EXE). `make` builds build/dos/WSWAP.EXE,
  DSWAP.EXE, DSWAPD.EXE (debug: appends lines to C:\DSWAP.DBG at the DBG
  points) and installs WSWAP/DSWAP into build/w16/SYSTEM.
- `tests/dos/sw1.asm` + `tests/dos/sw1dump.py`: the SW1 probe (resident
  spy; `SW1` before WIN, `SW1 D` writes C:\SW1.LOG; `SW1 F` keeps only INT
  2Fh in front, `SW1 N` nothing). Note: SW1 + w16's WSWAP crashed DOSBox-X
  (host core dump) once at Exit Windows; without SW1 fine. Not chased.
- Scratch (not in git): build/swapw/ (scenario scripts s*.txt, runs r*
  = Microsoft's, w* = w16's, montage helper mont.py).

## Observed on 3.1 (SW1 runs r7, r9, r11; details in ARCHITECTURE.md 9.9)

- WSWAP start: SI=5; FindFirst ~WOA1*.* (dir 2, dir 1), ~GRB*.* (dir 2);
  4680h; XMS: takes ALL free XMS itself and hands DOSX its memory through
  its own XMS entry (w16's does not); block 163h XMS handle, 167h ?,
  16Fh=1, 183h = arena end - PSP, 4F2h/4F3h (record 15's session 0F/10);
  4B05h, shrink, 4B02h, 4B01h x3 (notify 5, 3 CX=1, 4 CX=1, BX=100Fh),
  hooks 1Ch 21h 2Ah 2Fh 5Ch; EXEC DOSX (189h, tail 05h).
- Switch out: INT 2Fh 4B80h then INT 21h AX=0B80h (WSWAP's INT 21h hook);
  notify 1, 2; break, strategy, UMB, DTA, PSP saved; SI=4, SI=3 (17Eh :=
  next), SI=5; 36h; ~WOA100F.TMP: header 4 + own MCB 16 + 8 + 2 + 4 = 34
  bytes, hidden+RO; image = from its transient part to arena end into
  block 185h; 16Dh=100F, records of session 0F get +2Dh=1; SI=4; 173h =
  100F; own MCB to arena end; 4Ch AL=0.
- WIN.COM between children: sets 173h = record[17Eh] bytes 2Bh/2Ch;
  session 0F -> WSWAP, else DSWAP (0 = new program); INT 10h AX=3 before
  each (not the first WSWAP); 17Ch = FFh -> exit; at exit notify AX=7.
- WIN.COM's 4A05h: SI=2 frees record[17Eh] (flags 0, kind 80, next 0F,
  session/2C/2D 0), unlinks it, 17Eh := its next, 17Ch--; SI=3 17Eh :=
  next; SI=6 BX=n ES:AX = name of the record n steps after 17Eh; SI=7
  BX=n: that record current and moved to the ring's front (after 15).
- DSWAP: see swap/dswap.asm's header; Alt+Tab title screen; Alt+Esc =
  SI=3; Ctrl+Esc = SI=5, 178h|=08h, SI=7 BX=1; exit: notify 6, SI=5, SI=2,
  SI=5, 178h|=08h, SI=4, 4Ch 0.

## Status

- Works with Microsoft's WIN.COM, DOSX, KRNL386, WINOLDAP: prompt, VER,
  Alt+Tab out and back, EXIT, Exit Windows (run w12); and with w16's
  WIN.COM (w13).
- w16's WIN.COM now follows 3.1's semantics (SI=2/3/6/7, runs record
  17Eh's session while 16Dh != 0, mode 3 before each child but the first,
  notify 7 at exit, record 15 re-initialised at a restart).
- tests/swap.sh + tests/scripts/swap/*.txt (prompt two ctrlesc pif exitwin
  nodisk): Microsoft's WSWAP/DSWAP vs w16's. Boot-test cases swapdos,
  swapalt, swapall (tests/scripts/input/swapalt.txt); w16groups no longer
  borrows Microsoft's WSWAP.
- Bugs found on the way (all fixed): BIOS-hook wrappers lost the result
  flags/IF (keyboard dead), at_draw popped one word too many, ES not CS in
  ivt_out/vrestore_regs, the read-only swap file opened for writing,
  SI=2 needs BX=0 (3.1's WIN.COM looks at it), state_name lost AL.
- Remote runs: build/swapw/rrun.sh CMD (rsync + make there).

## Results so far (installation-05)

- tests/swap.sh (Microsoft's stack, DOSBox-X): prompt, two, ctrlesc, pif,
  exitwin, nodisk all PASS (blocks byte-identical but 163h-168h/185h, WC1's
  record, swap files made and left, every screen).
- Full boot test at 14d4f14 (master merged): 85 ok, 0 failures, swapdos,
  swapalt, swapall, w16groups among them; make test pass.
- Earlier boot tests: swapdos ok; swapalt, swapall fixed after the first runs
  (INT 16h blocking reads; comparisons); w16groups, woados, woadosbat,
  winprompt, winoldap*, wincom, allw16, exitwin, tree ok; make test pass;
  tests/wincom.sh 25/25.

## Open

- FIXED (branch swap2, 1984a00): the DOSX fault on a mouse click with
  w16's WSWAP. Root cause: DOSX never zeroed its BSS; the callback-stack
  selector cache (cbsel) held old memory contents, and the first PS/2
  packet (ring0.inc .ps2go -> cb_stacksel) loaded one (4440h) into DS.
  Microsoft's WSWAP happened to leave zeros there. DOSX now zeroes its BSS
  at start; swapall uses the mouse again.
- Two programs off Microsoft's stack (sw4 -O): w16's WINOLDAP leaves the
  ring as 15->12->13->14 where 3.1's has 12 before 14, so when prompt 12
  ends the ring goes on to prompt 13. 3.1's DSWAP then still goes back to
  Windows: after SI=2 and SI=5 it sets 178h bit 08h and SI=7 BX=1 (r13,
  SW1 with w16's WINOLDAP). w16's DSWAP now does the same (to_windows).
  3.1's SI=2 also leaves the freed record's next = its next's next: w16's
  WIN.COM does that now. The ring-order difference itself: resuming a
  program, 3.1's WINOLDAP rotates the ring (13 12 14 -> 12 14 13), w16's
  moved the program to the front (12 13 14) (sw5 d vs O, #2); but in
  tests/winoldap.sh two 3.1's turned 14 12 13 into 12 14 13 as well, so
  it is no rotation: resuming from the Windows application, 3.1's
  WINOLDAP puts the program first and the Windows application second, the
  rest after (winoldap.c to_front: move_front(Windows app), then the
  program). WIN.COM's SI=7 rotates the ring (sw_rotate; every SI=7 seen
  fits it, and Microsoft's DSWAP on w16's WIN.COM needs it: -w two). Since then (da1ac03, run sw7): swap.sh -O two and
  -w two PASS; the default six PASS (sw6); tests/winoldap.sh bat com
  errors noswap pif PASS; full boot test 85/85 and make test pass (also
  at 31601cf, swapall with the mouse).
- After the last fixes: -O two, -w two, default two/ctrlesc PASS
  (sw10); winoldap.sh prompt failed once on Microsoft's side under four
  parallel runs (keys lost: its #2 switch never came) and passed alone
  (sw9).
- Still open: swap.sh -W (w16's WIN.COM + WINOLDAP + DOSX on w16's side,
  DOSBox-X, Microsoft's KRNL386): the icon (388 px, w16's own, known)
  and in ctrlesc, nodisk, pif the run ends early (shots missing): w16's
  DOSX under DOSBox-X with KRNL386 not looked at yet.
- UNVERIFIED: 3.1's disk-space figure, Ctrl+Esc with several DOS
  programs, what SI=2's BX means (3.1's WIN.COM ignores the call unless
  BX=0), graphics-mode programs' screens (w16 saves VGA planes itself),
  real DOS (DOSBox only: the INT 28h/SDA path is untested).
