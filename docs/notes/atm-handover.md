# Adobe Type Manager 3.0 on w16: handover (2026-11-28, branch atm2)

## Where it stands

- ATM 3.0 installs on w16 as on 3.1 (boot-test case `atm`, part install;
  `tests/atm.sh`), and now runs: ATMSYS.DRV hooks w16's KERNEL LoadModule,
  sees GDI load, hooks w16's GDI, loads ATM32.DLL when USER creates the
  display's DC, and Acrobat Reader 2.0 shows its help document with its
  Type 1 text drawn through ATM (part acro: a real check now, the red link
  text counted in the document pane against 3.1's).
- The interface ATM checks, how it was observed, what w16 carries and
  what w16's code behind it does: ARCHITECTURE.md 11.13 (an
  interoperability interface, the user's decision: minimal bytes learnt at
  run time only; never disassemble, dump or read ATM's or Microsoft's
  code). In short: LoadModule's export starts `inc bp/push bp/mov bp,sp/
  push ds` (kentry.asm KLOADMODULE; LoadLibrary goes through it with -1);
  CreateDC's export is 3.1's layout (gdi/atm.asm: jmp short over three
  far calls whose operands GDI calls through, then a far procedure
  `mov ax,DGROUP` + that frame); every GDI export begins with 5+ bytes of
  movable whole instructions (stubs changed, EnumFontFamilies split from
  EnumFonts); GDI's code segments moveable.

## Open (next steps)

1. Reader's page on w16 sits 9 px lower and to the right: Reader's
   vertical scroll bar and its status bar ("1 of 18", "100%") are missing
   on w16 (USER or Reader's layout; not looked into: another agent works
   in USER). Compare atmtest/acro-cmp.png.
2. ATM's text spacing differs slightly from 3.1's (same glyphs, other
   advances; GDI's GetCharWidth/GetTextExtent/ExtTextOut with ATM's
   realized fonts, the DDK's device-font interface). ™ (99h) shows another
   glyph, also in Reader's outline pane (a raster face: GDI's mapping).
3. Not checked: ATM's Control Panel (ATMCNTRL.EXE) showing ATM on, Type 1
   text in Write or other programs, printing (ATM hooks StartDoc..AbortDoc).
4. Two other pairs of GDI exports share their code (CreateBitmap and
   CreateUserBitmap, RealizeDefaultPalette and IsDCCurrentPalette); ATM
   hooks neither.

## Tools (in commit ba4595f, removed after it; restore with git show)

KERNEL `[w16]` settings, all investigation aids: `aliasoracle=1..3` (a data
alias of the last GetProcAddress'd procedure limited below it and grown a
byte per GP fault: how far a check reads; KERNEL's code and GDI's module
table compared before/after a hook; =3 logs GetProcAddress by name and the
GDI.53 import), `steptrace=MODULE` (its LibMain step by step to
C:\STEP.BIN: a word count, then 32 bytes a step, CS IP flags AX BX CX DX
SI DI BP SP SS DS ES and two words at SS:SP) with `steptraceload=FILE` (a boot
load, calls out of MODULE skipped: LSTEP.BIN), `steptracebait=1|SEG:OFF`
(the procedure at CreateDC's first call operand: BSTEP.BIN),
`steptracenp=1` (MODULE's code segments made not present while USER loads;
the not-present fault starts a trace wherever its code is entered:
NSTEP.BIN). Stepping KERNEL/GDI/driver code itself hangs or takes too long:
skip it. DOSBox-X has no debug-register watchpoints (DPMI 0B00h set, never
hit). Re-running a LibMain in the same boot hung (ATMSYS).

Hand runs on installation-05: /mnt/build/w16-runs/w16-atm2 (build/arun.sh
NAME [dropin args]: the installed drive build/atminst/c (w16 tree) or, with
INST=build/atminst/ref WD=WINDOWS, 3.1 with w16's KRNL386 (`SIDE=-r` for
Microsoft's); ACRO=1 adds Reader and its fonts; `-f
build/dos/GDI.EXE=WINDOWS/SYSTEM/GDI.EXE` w16's GDI on 3.1). Inputs:
/mnt/build/w16-atm-in, /mnt/build/w16-acro-in/acroread; installed drives
from the w16-atm run's build/atmtest.

## Test state

See the commit adding this note.
