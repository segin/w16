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
- Boot test: swapdos ok; swapalt, swapall fixed after the first runs
  (INT 16h blocking reads; comparisons); w16groups, woados, woadosbat,
  winprompt, winoldap*, wincom, allw16, exitwin, tree ok; make test pass;
  tests/wincom.sh 25/25.

## Open

- **w16 DOSX + w16 WSWAP + a mouse click faults in DOSX** ("Standard Mode:
  Fault in MS-DOS Extender", EC=4440 CS=0070 IP=14C1, every time, plain
  DOSBox, w16's install tree; build/swapw/s/click.txt). Not with
  Microsoft's DOSX on w16's WSWAP, not with Microsoft's WSWAP on w16's
  DOSX, not with WSWAP 4 KB larger (so not the conventional-memory
  layout). Difference that matters: Microsoft's WSWAP owns all XMS and
  hands DOSX memory through its own XMS entry; w16's leaves DOSX to XMS
  directly (as DOSX run on its own). Probably a w16 DOSX bug in the PS/2
  path that its usual extended-memory layout hides: for the DOSX owner.
  swapall runs by keys only meanwhile.
- tests/swap.sh -W (w16 WIN.COM + WINOLDAP + DOSX on w16's side): the
  switches work, but WO2's block log has an extra "block at start", and in
  "two" after EXIT with another prompt swapped out w16's stack goes to
  Windows where 3.1 resumes the other prompt directly (run sw3). Run sw4,
  scenario two: with w16's WIN.COM on both sides (-w) Microsoft's and
  w16's swappers already part at the first Alt+Tab; with w16's WINOLDAP
  on both sides (-O) they part after an EXIT (3.1's swappers make a
  fourth switch, w16's do not). On Microsoft's WIN.COM and WINOLDAP the
  two are byte-identical, so some field w16's WIN.COM / WINOLDAP write
  differently from 3.1's is read by Microsoft's swappers or by w16's:
  compare the block snapshots of sw4 (installation-05:
  /mnt/build/w16-runs/agent-a26a1e6e924e5e02b/build/sw4/{w,O}/two) next.
  The single-program scenario passes on w16's whole stack in the boot
  test (swapall); woadosbat/winprompt (w16's WIN.COM with Microsoft's
  swappers) pass with the new WIN.COM.
- UNVERIFIED: 3.1's disk-space figure, Ctrl+Esc with several DOS
  programs, what SI=2's BX means (3.1's WIN.COM ignores the call unless
  BX=0), graphics-mode programs' screens (w16 saves VGA planes itself),
  real DOS (DOSBox only: the INT 28h/SDA path is untested).
