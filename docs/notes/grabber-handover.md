# Display grabbers handover (grabber agent)

Task (the user's, 2026-11-14): w16's own display grabbers, drop-in
replacements for Windows 3.1's VGACOLOR.2GR (Standard mode) and VGA.3GR
(386 Enhanced mode). Spec: ARCHITECTURE.md 9.11; drivers/grabber/README.md.

## VGACOLOR.2GR

### Where things are

- `drivers/grabber/vgacolor.asm` (nasm -f bin): build/dos/VGACOLOR.2GR,
  installed as build/w16/SYSTEM/VGACOLOR.2GR.
- Probes (`make grprobes`): `tests/dos/gr1.asm` GR1.2GR, a recording
  stand-in (DSWAP calls it; it forwards to the real grabber appended at
  offset 2000h by the test: `cat GR1.2GR VGACOLOR.2GR > x.2GR`; log
  C:\GR1.LOG, written only while InDOS is 0, so calls made inside DOS are
  lost when DSWAP ends first); `tests/dos/grt.asm` GRT.COM, calls a grabber
  directly without Windows (log C:\GRT.LOG, GetBlock data to C:\GRTmmX.BIN,
  a before/after check of every VGA register, the DAC, the BIOS fields and
  all four planes around SaveScreen/RestoreScreen with the screen scribbled
  over in between); `tests/dos/gv1.asm` + `gvdraw.inc` GV1.COM, fixed
  screens per video mode; `tests/apps/cb1.c` CB1.EXE, a clipboard viewer
  (load=) logging every change to C:\CB.LOG (+ CBn.BIN bitmap bits).
- Test: `tests/grabber.sh` (see its header).

### The interface (DDK + observation; 3.1's grabber never disassembled)

- Binary image, no header, loaded by DSWAP (real mode) into its own
  memory; DSWAP puts the GRABREQUEST right after the image (ES = the
  grabber's segment, DI = the file's size). CS = DS = the image's segment
  at every call. Near jumps at 00h-15h, far returns.
- Calls seen from DSWAP (GR1):
  - first run of a session: SetSwapDrive (AX=FFFDh at 00h, BL = drive
    letter of the second swap directory, ES:DI = template
    "<second swap dir>~WOAFFEE.TMP"), InquireSave (0Ch, AX=1; 3.1's
    answers DX:AX = 2452h), EnableSave (FFFCh), InitScreen
    (15h, AX = 25 lines);
  - switch away: SaveScreen (0Fh, AX = the InquireSave size, ES:0 DSWAP's
    buffer), DisableSave (FFFBh), InitScreen(25);
  - switch back (a new DSWAP run, the grabber loaded again): EnableSave,
    RestoreScreen (12h, the same buffer as it was saved);
  - PrintScreen in a full-screen program: FFF5h (obsolete: BX=0100h),
    GetInfo (FFFEh, ES:DI 48 bytes), GetBlock (FFF8h) with grlpData 0 and
    grStyle 80h (the size), GetBlock again into DSWAP's buffer, FFF4h
    (obsolete); WINOLDAP, back in Windows, puts the text on the clipboard
    as CF_OEMTEXT + CF_TEXT (CB1). The data travel through
    `<second swap dir>~WOAfffe.tmp`: under DOSBox-X the second swap
    directory is on A: (its "first local drive"), so tests mount A: and B:
    on C:'s directory.
  - the program's end: InitScreen(25), DisableSave.
- 3.1's answers (GRT, every mode): InquireGrab(1) 100Ah, (2) 4010h;
  InquireSave(1) 2452h, (2) 4412h; GetVersion 0201h; GetInfo AX 1 in text
  modes, 0 in graphics, GRABINFO + INFOCONTEXT's further fields per mode
  (table in vgacolor.asm); GetBlock: text modes: clipboard format = GT_TEXT
  (1), gbSize, the characters row by row, CR LF between rows, two NULs;
  native = character/attribute pairs; blocks honoured, ERR_BOUNDARY;
  CGA modes 4-6: clipboard format, whole screen only = GT_NEWBITMAP (3),
  gbSize 3E8Ch, 640 x 200, 1 plane, 1 bit, 4800 x 1800 (0.1 mm), the
  screen's bytes line by line (banks alternating); the data call answers
  AX = 0708h there (not the size: w16 answers the size); EGA/VGA modes
  0Dh-13h: ERR_UNSUPPORTED (FFh, CF set) for everything.
  InitScreen(n): INT 10h 1202h BL=30h, 0003h, (n >= 43: 1112h BL=0),
  0100h CX=0607h.
- 3.1's own restore (GRT before/after): text and EGA/VGA modes come back
  exact except plane 2 (fonts) in text modes; in CGA modes 4 and 6 it set
  mode 7 (!) and most registers differ: w16's restores everything in
  every mode (all registers, DAC, BIOS fields, all four planes).

### w16's design

Save buffer (8953 bytes, less than 3.1's 2452h): magic, mode, file name,
all VGA registers, PEL mask, DAC, BIOS video fields, and (only without a
file) the text page. The planes (4 x 64 KB, raw: chain-4/odd-even off)
go to `~GRBxxxx.TMP` (xxxx from the timer, INT 21h 5Bh) in the template's
directory, the name WSWAP clears at start; RestoreScreen sets the mode
through the BIOS (no clear), reads the planes back, deletes the file,
writes every register and the DAC, then the BIOS fields. No hooks
(EnableSave/DisableSave do nothing). The RIL calls 3.1's grabber makes
(INT 10h F1h/F3h/F7h, EGA.SYS's register interface) are not made.

## Status / results (installation-05, 2026-11-14)

- `tests/grabber.sh grt`: PASS (every answer and GetBlock's data equal to
  3.1's in modes 0-7, 0Dh-13h; w16's restore exact in every mode).
- `text`, `cga` (3.1's never switches away in CGA modes: its Windows
  shots differ, reported as known), `ega`: PASS; `-O text`: PASS.
- `prompt`: screens equal; the clipboard equal whenever both grabs give
  data. 3.1's grabber left the first PrintScreen's clipboard empty in 5 of
  12 runs; w16's once (the first run, where xdotool also dropped Shift
  under load and a stray key reached the prompt: the scenario now types
  no shifted characters). UNVERIFIED why 3.1's grab comes back empty; the
  script
  notes that case instead of failing.
- Full boot test at 6ec0d93 (tools/remote-test.sh): 92 ok, 0 failures,
  make test pass; swapall boots w16's install tree with w16's grabber and
  only Microsoft's MMSOUND.DRV.

## Open

- Graphics-mode PrintScreen under DSWAP: 3.1's gives no data for
  0Dh-13h; CGA modes give a bitmap: what reaches the clipboard (CB1) to be
  compared in tests/grabber.sh.
- w16's own DSWAP does not use a grabber (it saves the screen itself) and
  has no PrintScreen: Standard-mode PrintScreen works with Microsoft's
  DSWAP + WINOLDAP only.
