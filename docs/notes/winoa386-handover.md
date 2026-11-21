# WINOA386 work: handover note

w16's own WINOA386.MOD (module WINOLDAP), 386 Enhanced mode's side of DOS
programs: `winoa386/` (winoa386.c, woa386s.asm, winoa386.rc, .lnk),
built as build/dos/WINOA386.MOD and installed in the tree's SYSTEM
(`make install-tree`). Spec: ARCHITECTURE.md 12.11. SHELL's side: win386/
vmm/shell.c. Clean room: Microsoft's WINOA386.MOD, WIN386.EXE and VGA.3GR
were never disassembled, dumped or read; everything comes from VXS.386's
logs of the calls into SHELL/VDD/VKD/VMD (tests/vxsrun.sh), GR3's grabber
records, w16's VMM trace (`[w16] vmmtrace=1`) and screenshots.

## How to run

- `make build/dos/WINOA386.MOD`; the tree: `make install-tree`.
- Scenarios (DOSBox-X, on installation-05 in your own run directory):
  `tests/inputrun.sh -s -b 200 -t 300 SCRIPT OUT -p 386 -T build/w16 -3
  -c "WIN /3 /B"` with DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150
  INPUTRUN_STAY=1; scripts tests/scripts/input/dosbox386tree.txt (the
  tree's Main group), dosbox386{s,t,x,w}.txt (3.1's Main group: `-r`,
  Microsoft's KRNL386 via -f, `-f build/dos/WINOA386.MOD=WINDOWS/SYSTEM/
  WINOA386.MOD`, `-f build/win386/WIN386.EXE=...` for w16's WIN386).
- SHELL calls under w16's WIN386: `-i w16:vmmtrace=1` and DROPIN_CONF
  with `[serial] serial1=file file:...`; grep `SHELL:` in the COM1 file.
- Under Microsoft's WIN386: `VXS_FILE=VXSH.386 VXS_NOHDD=1 VXS_EXTRA="-f
  build/dos/WINOA386.MOD=WINDOWS/SYSTEM/WINOA386.MOD" sh tests/vxsrun.sh
  NAME SCRIPT`.

## State

See the end of this file (kept current).

### Done (2026-11-21)

- The full-screen bug (the user's report; commit 64f5554, separate): SHELL
  function 6's two calls, w16's VGA.DRV INT 2Fh 4000h-4007h, USER's
  RepaintScreen deferred; boot-test case `win386tree`.
- WINOA386 core (winoa386/): start, full screen, Alt+Enter to a window and
  back (the window 0 pixels from Microsoft's WINOA386's with the same
  fonts), painting through the grabber (41Ah), scrolling, the end (418h),
  switching keys (41Eh, SHELL 10h), the icon reopening the box,
  WM_QUERYENDSESSION's "Application still active" box. In the install
  tree; README.TXT no longer asks for Microsoft's file. Boot-test cases:
  `win386woa` (w16's WIN386 + WINOA386 on 3.1: switching, Exit Windows
  with a box open), `treealone` (the tree alone: DOS box full screen,
  window, full screen, EXIT).

### Facts observed (see also ARCHITECTURE.md 12.10, 12.11)

- SHELL 6: (AX flags, CX ...) then (AX, CX FFFFh). Windowed (1, 7B7h|1),
  (1, FFFFh); full screen with DOSPRMPT.PIF (0, 1), (10h, FFFFh); without
  a PIF (0, 0), (1, FFFFh). SHELL 7: 4 windowed, 0 full screen.
- SHELL 3's block (3.1, DOSPRMPT.PIF): +00 1000h, +02 4002h, +04 001Fh,
  +08/+0E/+14 48-bit pointers (program, tail, directory), +1A FFFFFFFFh,
  +1E 100, +20 50, +22 400h, +26 400h, +2A WINOA386's window, +2C its
  instance, +2E the title. Registers EAX 1902h, EBX F0h (3Eh without a
  PIF), ECX 207CFh (20F30h), ESI 8: meaning UNVERIFIED (PIF flags?).
- SHELL 10h after 41Eh (3.1, Alt+Tab once): (EBX FFFFFFFFh, ECX Program
  Manager's window, DS:SI a block of dwords 0Fh 9 0Fh 0 0Fh 0Fh 0 and the
  title at +1Ch = ES:DI) -> EAX 1; 170 ms later (EBX the box's VM, ECX
  its own window, its title) -> SHELL gives the System VM the focus, ends
  message mode (control 11h), EAX 0, EBX FFFFFFFFh, ECX Program Manager's
  window, EDX 1. Alt+Esc: one call, EAX 0 EDX 0.
- GetFontList: Microsoft's VGA.3GR writes more than 4 x 17 bytes (a
  76-byte buffer lost WinMain's return address); 512 now.
- The window: class name, menus and the exact style are w16's choice
  (UNVERIFIED against 3.1: a probe enumerating windows, GetClassName,
  GetWindowLong, GetSystemMenu, is the next observation to make).

### Left to do (in order)

1. Under Microsoft's WIN386, Alt+Tab/Alt+Esc out of a full-screen box
   with w16's WINOA386: Microsoft's SHELL answers every 10h with EAX 1
   (VXSH log: tests/vxsrun.sh with VXS_EXTRA as above). Find what 3.1's
   WINOA386 does between its two calls (a VXSH run logging SHELL's calls
   into the System VM and the 41Eh record's contents; compare EAX 153Fh,
   which 3.1 passes and w16 does not).
2. The system menu (Edit: Mark, Copy, Paste, Scroll; Settings...; Fonts...)
   and its dialogs, texts and layouts from 3.1's screens; Alt+Space in a
   windowed box (3.1 opens the menu: a SHELL hot key?).
3. "Inactive" in the title when suspended; Settings' Display Usage,
   Tasking Options (Exclusive, Background), Priority, Terminate.
4. The PIF's 386 section (memory, display, exclusive/background,
   priorities) into SHELL 3's block and registers: vary PIFs on 3.1 and
   record function 3 with VXS.
5. DOS-box fonts of w16's own (EGA80WOA.FON etc., Terminal-like OEM cells
   8x12, 16x12, 8x8, 16x8: fonts/, tools/fontc.py), so that windows match
   3.1's glyph sizes without the user's files.
6. `[NonWindowsApp]` settings; Alt+PrintScreen (GrabEvent/GrabComplete).

### Test results (2026-11-21, 1292383, installation-05 under load ~40)

make test: pass. Full boot test: 142 ok, 6 failures: holdbtn, lzexpand
(quirks), trails (timed), cplclick (co-tree), toolhelp (tree386; alone
again: ref386, Microsoft's side, did not finish) - all on the known-flaky
list - and w16write (tree 03-character), which passed alone. New cases
win386tree, win386woa and treealone (with its DOS box) pass.

### The user's two bugs on master 47fcb7d (2026-11-21)

The user's set-up: the tree alone, plain DOSBox 0.74, `WIN /3` (no /B).
- **Exit Windows hung after a DOS box** ("w16: back to DOS, code 0000",
  then nothing; DOSBox still showing WIN386). DOS saves the caller's SS:SP
  in the current PSP (2Eh) at every INT 21h call but 50h/51h/62h/64h
  (DOSBox's built-in DOS as MS-DOS). A new DOS VM starts with the current
  PSP of Windows' start, the loader's, which it shares with the System VM,
  so its stub's first calls (48h, 55h) wrote the VM's stack there; when
  KERNEL ended, DOS "returned" to the loader on that stack (traced: the
  loader's saved stack 1563:1FE0, the stub's segment). The stub now keeps
  the loader PSP's saved stack and puts it back once it has its own PSP
  (win386/ldr/ldrasm.asm vm_stub).
- **A full-screen box stayed in 640x480 graphics.** WIN.COM's start-up
  logo (shown for `WIN /3`, not for `WIN /3 /B`, which every earlier test
  used) leaves the BIOS's mode 12h, and a new VM's BIOS data are those of
  Windows' start, so the VDD set the VM's mode 12h (DOSBox's shell then
  writes its text into the graphics screen). A graphics mode there now
  means mode 3 (win386/vmm/vdd.c bios_mode).
- Harness note: tests/inputrun.sh -s waits for BOOTLOG.TXT, which only /B
  writes; a run without /B used up its boot wait and DOSBox was ended by
  the run's time limit, which looked like DOSBox quitting at a box's EXIT.
  The new case waits for Program Manager's caption instead.
- Boot-test case `win386plain` (plain DOSBox, tree alone, `WIN /3`: the
  box a text screen, EXIT, Exit Windows back to the prompt).
