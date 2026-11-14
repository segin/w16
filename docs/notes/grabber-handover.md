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

## VGA.3GR (the 386 Enhanced-mode grabber; the 386-grabber agent)

Everything below was observed black-box on Windows 3.1 in 386 Enhanced
mode (Microsoft's WIN386, VDD, WINOA386.MOD) under DOSBox-X, with the
recorder GR3 in front of Microsoft's VGA.3GR: arguments, results and the
EXTPAINTSTRUC bytes that change across each call, the calls the grabber
makes to the VDD and what the VDD returns, the grabber's own GDI/USER/KERNEL
calls (USTRACE forwarders on its imports), screenshots and the clipboard
(CB1). Microsoft's file was only read for its NE tables. The DDK's
documentation gives the names and the prototypes; where it and the
observation disagree, the observation is written here.

### Tools

- `tests/apps/gr3.asm` = `build/dos/GR3.3GR`: module GRABBER with the 24
  exports, installed as SYSTEM\VGA.3GR in front of GRABTGT (a scratch copy
  of the grabber under test, module renamed by `tools/nerename.py` at test
  time). Records every export call (registers, 8 stack words), the return
  (DX:AX, flags, bytes popped), the EXTPAINTSTRUC (full once, then the
  changed byte runs before and after each call), GetFontList's buffer,
  SetPaintFnt's two words; hooks protected-mode INT 2Fh so that AX=1684h
  BX=000Ah gets a thunk that records the VDD calls (32-bit registers in and
  out, the ES:DI buffer), and for the first Get_Mem calls 4 KB of each
  plane. Log C:\GR3.LOG, decoded by `tools/gr3dump.py`.
- `tests/grabber3.sh -r` (recorder) / `-u` (also USTRACE on the grabber's
  imports; `tools/ustrace.py` shows the grabber's calls as GRABBER.>Name and
  GRABBER.<Name among them), `-g FILE` the grabber under test.
- `tests/grabber3/*.txt`: scenarios (text, gfx, grab, misc);
  `tests/grabber3cmp.sh`: Microsoft's grabber against w16's.
- `tests/dos/gt3.asm` = GT3.COM: fixed text output with keys for new lines,
  an attribute line, cursor moves and shapes, a direct video write.

### Call sequences (WINOLDAP)

- Start of a DOS box: GrbGetTextColor(lp, 00FFFFFFh) and (lp, 0),
  GetFontList, CheckGRBVersion (three times in all).
- Each display change: GetDisplayUpd(lp, 0, lParam) [VM now held] ->
  SetPaintFnt (twice) -> UpdateScreen(lp, 0, lParam) -> CursorPosit; or,
  when nothing is to be painted (only cursor tracking), GetDisplayUpd ->
  GrbUnLockApp.
- WM_PAINT: TranslateWinToFScreen, GetDisplayUpd(lp, -1, 0), SetPaintFnt,
  PaintScreen.
- Edit > Mark: BeginSelection(0,0), CursorOff; each arrow key KeySelection
  (StartType 0), EndSelection, BeginSelection(new point); the first
  Shift+arrow: KeySelection(1, dir), AdjustInitEndPt, InvertSelection,
  ConsSelecRec, MakeSelctRect, InvertSelection; later Shift+arrows the same
  without AdjustInitEndPt. Copy (Enter): RenderSelection(lp, -1, 0),
  ConsSelecRec, InvertSelection, EndSelection, CursorOn.
- Alt+PrintScreen, windowed text: the same as a Copy of the whole screen
  (CF_OEMTEXT of 25 lines).
- Alt+PrintScreen, full screen: SetPaintFnt, BeginSelection(0,0),
  MakeSelctRect(full height, full width), RenderSelection(lp, 00A0h,
  lParam), EndSelection, GrabComplete(lp, 00A0h, lParam). GrabEvent was
  never called.
- End of the DOS box: CursorOff, ScreenFree.

### The exports (ordinal: arguments -> result; what changes)

Coordinates are pixels of the full DOS screen ("screen": client + ColOrg,
RowOrg) unless said otherwise; POINT results are x in AX, y in DX.

- 1 PaintScreen(lp) -> 1: paints ps.rcPaint (client pixels): Get_Mem,
  draw, Free_Mem; PGVDRect = rcPaint in screen pixels, PTVDRect the cells
  (exclusive, e.g. 0,0,4Eh,1Ah for a 624x301 client), TDXpos = left of the
  cells painted, TDYpos = bottom of the rows painted (client pixels); caret.
- 2 ScreenFree(lp): frees what the grabber keeps for the box.
- 3 BeginSelection(lp, Y, X) -> (X, Y): SelctSRect = GrabArea = X,Y,X,Y;
  SelecFlags |= 1.
- 4 KeySelection(lp, StartType, Mfunc) -> point: start point (StartType 0)
  or end point (1) moved by one cell (FntWid/FntHgt) right/left/down/up
  (Mfunc 0-3); structure unchanged.
- 5 MakeSelctRect(lp, Y, X) -> AX 1 if changed, DX Y: SelctSRect.r,b and
  GrabArea.r,b = X,Y.
- 6 InvertSelection(lp) -> (SelctSRect.r, .b). Draws nothing: WINOLDAP
  inverts the rectangle itself (no InvertRect call was recorded).
- 7 RenderSelection(lp, wParam, lParam) -> DX format, AX handle:
  CF_OEMTEXT (7) in text modes: GlobalAlloc(2042h, rows * (cols + 2) + 1),
  each line with trailing blanks removed, CR LF after every line, NUL;
  writes the cell rectangle as bytes at SelStruc+10h (c0 r0 c1 r1 w h).
  wParam -1: windowed Copy (Get_State/Get_Mem); otherwise a full-screen grab
  (VDD functions 8 and 6, see below), GGrbArea+4 = 1. CF_BITMAP (2) in
  graphics modes.
- 8 ConsSelecRec(lp): SelctSRect = GrabArea.
- 9 EndSelection(lp): SelecFlags &= ~1.
- 10 GetFontList(lpBuf): 4 entries of 17 bytes: 13-byte file name (SYSTEM.INI
  [386Enh] key = the name, default the name: EGA80WOA.FON, EGA40WOA.FON,
  CGA80WOA.FON, CGA40WOA.FON), then cell height and width (12,8 12,16 8,8
  8,16). AX = the last GetPrivateProfileString result.
- 11 SetPaintFnt(lp, lpW, lpH) -> DX rows, AX cols (graphics: height,
  width in pixels): FntHgt, FntWid = the font's cell (0 in graphics modes);
  *lpW, *lpH = the full screen in PIXELS (640 and 300 for 80x25 with 8x12;
  the DDK says cells). Refreshes the state (Get_State) when EPStatusFlags
  bit 17 (20000h) is clear, then sets it.
- 12 UpdateScreen(lp, wParam, lParam) (10 bytes of arguments, not 4 as the
  DDK has it) -> 1: Get_Mem, paint what changed, Clear_Mod, Free_Mem; caret.
- 13 CursorOn(lp): CreateCaret + SetCaretPos (text modes).
- 14 CursorPosit(lp) -> the caret's client position; FFFFFFFFh in graphics.
- 15 CursorOff(lp) -> AX 1: DestroyCaret; 1D2h/1D4h = FFFFh, 21Ah = 00FFh.
- 16 CheckGRBVersion(lp) -> DX VDD version (030Ah), AX 0: VDD function 0
  with ES:EDI = lp+23Ah (VerStrng, "VIDEOVGA").
- 17 AdjustInitEndPt(lp, Y, X) -> (X + FntWid, Y + FntHgt).
- 18 GetDisplayUpd(lp, wParam, lParam) -> AX flags: Get_Mod into lp+1D6h,
  Get_State into lp+1B6h; AX = the Get_Mod flags (8 controller, 10h VRAM,
  20h cursor) | 80h when state byte +2 has bit 1; text modes: the changed
  cells from the VDD's registers AL, AH, BL, BH (left, top, right, bottom,
  inclusive; FFh = all) stored as words at lp+1DAh..1E0h. Sets
  EPStatusFlags bit 17.
- 19 GrabEvent(lp, wParam, lParam): not seen.
- 20 GrabComplete(lp, wParam, lParam): VDD function 7 (Free_Grab).
- 21 GrbGetTextColor(lp, DWORD rgb) -> colour index (FFFFFFh 0Fh, 0 0);
  8 bytes of arguments.
- 22 GrbUnLockApp(lp): VDD function 9 (Unlock_App).
- 23 TranslateWinToFScreen(lp) (4 bytes): no visible effect.
- 24 WEP.

### EXTPAINTSTRUC offsets that matter (from what WINOLDAP and the VDD write)

000 PAINTSTRUCT (hdc at 0, rcPaint at 4); 020 PGVDRect; 028 HScrlPos,
VScrlPos; 02C ColOrg, RowOrg (pixels the window is scrolled by); 030
VM1_Hnd; 034 DefFont and 03A AltFnt1-6, 6 bytes each (hFont, height,
width): WINOLDAP puts the four GetFontList fonts in AltFnt1-4 (80x25,
40x25, 80x43/50, 40x43/50); a font picked in the Font dialog goes to all
seven; 05E WhtBrshH, 060 BlkBrshH, 062 RNG1DSSEL (7Bh), 064 ShellCall; 068
GGrbArea (12 bytes, the grabber's); 074 PTVDRect; 07C TDXpos, TDYpos; 080
FntHgt, FntWid; 084 ThisVMHand (EBX of every VDD call); 088 WindHand, 08A
BoxPrntHand, 08C hThisInst; 08E SelStruc: SelctSRect, 096 GrabArea, 09E
the cell rectangle RenderSelection writes, 0A6 SelecFlags; 1B6 the VDD
state (1Ch bytes), 1D2/1D4 the caret shape the grabber made, 1D6 the VDD
modification buffer, 21A the mode painted (FFh: caret off), 21C.. the
graphics memory DC, bitmap, buffer; 236 EPStatusFlags (bit 16 set while
windowed, bit 17 "state current"), 23A VerStrng[8]; size 242h.

### The VDD's protected-mode API (INT 2Fh AX=1684h BX=000Ah)

EAX = function, EBX = VM handle (ThisVMHand), ES:EDI = buffer, ECX = its
size (the VDD does not seem to need it):

| EAX | name (DDK) | buffer | returns |
|---|---|---|---|
| 0 | Get_Version | 8 bytes | EAX 030Ah; "VIDEOVGA" in the buffer |
| 1 | Get_Mem | 28h | see below |
| 2 | Get_State | 1Ch | the state below |
| 3 | Get_Mod | 100h | flags word, count word, entries; text: changed cells in AL AH BL BH |
| 4 | Clear_Mod | - | - |
| 5 | Free_Mem | - | - |
| 6 | Get_GrbMem | 28h | as Get_Mem, the grabbed copy |
| 7 | Free_Grab | - | - |
| 8 | Get_GrbState | 1Ch | as Get_State (called with EAX 00020008h) |
| 9 | Unlock_App | - | - |

- State (1Ch): +0 mode, +1 0, +2 flags (bit 1: cursor tracking wanted, the
  grabber's 80h), +3 rows (19h), +4 cursor column, +6 row, +8 cursor start,
  +0Ah end (scan lines of a 16-line cell), +0Ch the 16 palette registers.
- Get_Mem (28h): four dwords = linear addresses of planes 0-3, a dword 0
  (start offset?), four dwords = their sizes, one more address. Text modes:
  plane 0's address shows the text as the CPU sees it at B800:0000
  (character, attribute; 160 bytes a row), plane 2 the font (32 bytes a
  character), plane 3 size 0. Mode 12h: four planes of 0A000h bytes.
- Get_Mod (100h): +0 flags (8 controller state, 10h video memory, 20h
  cursor), +2 count; text: one entry, the rectangle in the registers
  (FFFFh in AX/BX: everything); graphics: count = 4 KB pages of a plane
  (0Ah for 12h), +4 a bit per page.
- Mode 13h cannot be windowed (WINOLDAP's "cannot run in a window" box).

### Drawing (what Microsoft's grabber draws, reproduced by w16's)

Text: the font from the slot for the mode, a solid brush of palette entry
0 (GetNearestColor, CreateSolidBrush) for what lies beyond the DOS screen
(PatBlt PATCOPY), then per row one TextOut per run of one attribute with
SetBkColor/SetTextColor; colours: the palette register value as EGA
colour (bits 2,1,0 = R,G,B at A8h, bits 5,4,3 the same at 54h), background
attribute bits 4-7 all four (no blink). Caret: CreateCaret(hwnd, 0,
FntWid, h), top = ceil(start * FntHgt / 16), h = (end - start + 1) *
FntHgt / 16 + 1 (0Dh-0Eh: 10/2 with 12 pixels, 15/3 with 18; 0-0Fh: 0/13),
at row * FntHgt + top. Graphics (planar):
CreateCompatibleDC/Bitmap (width x 53 lines for 640), SetBitmapBits of
bands (scan line = planes 2,1,0,3: VGA.DRV's R,G,B,I), BitBlt SRCCOPY, no
caret (DestroyCaret); the planes are copied raw (no palette). Low-resolution
modes are doubled: SetPaintFnt answers 640x400 for 04h-06h, 0Dh, 0Eh,
640x350 for 10h, 640x480 for 12h; Microsoft's grabber builds them a line at
a time (SetBitmapBits of one line 608 pixels wide into a large compatible
bitmap, then BitBlt). Full-screen graphics grab: GetDC, CreateCompatibleDC,
CreateCompatibleBitmap(640, 480) selected into it, SetDIBits of 4-bit DIB
lines from the bottom scan up (scan 479 = the top line), a 16-entry colour
table.

### w16's VGA.3GR: state and results

`drivers/grabber/vga3gr/` (C + nasm); what is built and the test results:
`drivers/grabber/README.md` "VGA.3GR".

### Open

- GrabEvent (19) was never called; TranslateWinToFScreen's purpose.
- Full-screen grab of a graphics screen: w16's bitmap differs from
  Microsoft's (Microsoft's has more bits set in places; its DIB colour
  table could not be seen: USTRACE keeps 24 bytes of a pointer argument).
- Text with a changed DAC (GV1 03 p): Microsoft's colours follow the DAC,
  which VDD_Get_State does not return (Get_Mem's last dword, which the
  grabber keeps at GGrbArea+8, may point at the VDD's register copy);
  w16's uses the palette registers only.
- Mode 0Dh/0Eh windows: a few thousand pixels differ around the BIOS-drawn
  title (row 0).
- EAX's high word in some of Microsoft's calls (00020002h, 00120008h): the
  VDD ignores it (leftovers); Get_Mem's fifth dword (0 always).
