# w16 keyboard driver (`KEYBOARD.DRV`, module `KEYBOARD`)

w16's own keyboard driver for Windows 3.1: the PC/AT keyboard behind the
8042 controller (scan-code set 1 as the controller translates it), the US
layout built in and code page 437 for the OEM character set: what Windows
3.1's `SYSTEM\KEYBOARD.DRV` does with `SYSTEM.INI`'s `[keyboard]`
`keyboard.dll=` and `oemansi.bin=` empty, as Setup leaves them for a US
install (`[keyboard] type=4`). Like every w16 driver it is a genuine
Windows 3.1 driver (ARCHITECTURE.md 6.7): the keyboard-driver interface
of the Windows 3.1 DDK and nothing w16-specific; it replaces Windows
3.1's driver as it is. Standard mode; 386 Enhanced mode not tried.

`keyboard.asm` (NASM) and `keyboard.lnk` (wlink); `make` builds
`build/dos/KEYBOARD.DRV` (two fixed code segments, one fixed data
segment, expected Windows version 3.10) and the install tree's copy. The
ANSI/OEM tables are made at build time by `tools/kbtables.py` (below).

## Interface

Module name and ordinals are those of Windows 3.1's KEYBOARD.DRV (its NE
export table, `docs/exports/KEYBOARD.txt`; `tools/neimports.py KEYBOARD`).
USER imports 1-7, 134 and 135, the display driver 100, applications 5, 6,
128, 129, 131, 132, 133 and 134; KERNEL calls 1, 3 and 136 through
GetProcAddress (Pietrek, ch. 1). The bytes each entry takes off the
stack were measured on Windows 3.1's driver (KB1, `popped`), and so were
the registers each leaves (KB1, `regs`: SI, DI, DS, BP kept; AX the result;
DX only for the two DWORD ones).

| Ord | Name | Stack | What it does here |
|---|---|---|---|
| 1 | `Inquire(LPKBINFO)` | 4 | KBINFO: no double-byte ranges (FFh, FEh, FFh, FEh), state block 4 bytes; returns 6 |
| 2 | `Enable(lpEventProc, LPBYTE lpKeyState)` | 8 | takes INT 9, sets the key-state array's Shift, Ctrl, Alt (80h) and Caps, Num, Scroll Lock (01h) from the BIOS flags, every time; clears the BIOS's 40:96h bit 4; INT 16h AH=01h |
| 3 | `Disable()` | 0 | gives INT 9 back |
| 4 | `ToAscii(vk, scan, lpKeyState, lpState, flags)` | 14 | below |
| 5 | `AnsiToOem(lpSrc, lpDst)` | 8 | through the NUL, across 64K (+`__AHINCR`); returns -1 |
| 6 | `OemToAnsi(lpSrc, lpDst)` | 8 | the same |
| 7 | `SetSpeed(rate)` | 2 | -1: 0 (it can); else the typematic rate and delay to the keyboard, returns `rate` |
| 8 | `WEP(bSystemExit)` | 2 | Disable if enabled; 1 (exported by name, resident) |
| 100 | `ScreenSwitchEnable(w)` | 2 | kept (OS/2 only, DDK) |
| 126 | `GetTableSeg()` | 0 | the selector of the table segment, the one GetTableSeg itself is in |
| 127 | `NewTable()` | 0 | `[keyboard] type=`, `subtype=` again |
| 128 | `OemKeyScan(ch)` | 2 | DX:AX = shifts (2 Shift, 4 Ctrl) : scan code; -1:-1 |
| 129 | `VkKeyScan(ch)` | 2 | AH shifts (1 Shift, 2 Ctrl), AL the VK; -1 |
| 130 | `GetKeyboardType(which)` | 2 | 0 type (4), 1 subtype (0), 2 function keys (12), else 0 |
| 131 | `MapVirtualKey(code, type)` | 4 | VK to scan code, scan code to VK, VK to character |
| 132 | `GetKBCodePage()` | 0 | 437 |
| 133 | `GetKeyNameText(LONG lParam, LPSTR, int size)` | 10 | the key's name |
| 134 | `AnsiToOemBuff(lpSrc, lpDst, n)` | 10 | n bytes, within the segment |
| 135 | `OemToAnsiBuff(lpSrc, lpDst, n)` | 10 | the same |
| 136 | `EnableKBSysReq(w)` | 2 | Ctrl+Alt+SysReq: 1/2 INT 2 on/off, 4/8 CVWBreak on/off (offs win); returns the state |
| 137 | `GetBIOSKeyProc()` | 0 | DX:AX the INT 9 handler before Enable's (what Disable puts back) |

Signatures from the 3.1 DDK ("Keyboard Driver Functions"); the DDK
documents Inquire, Enable and Disable only for the mouse and network
drivers: the keyboard's take the arguments above (USER's calls, KB2).
Details observed and reproduced:

- **ToAscii.** The state block carries the result and the Alt+keypad
  number. A key-up does nothing (returns 0, writes nothing), except Alt
  (VK_MENU) up, which ends an Alt+keypad number: lpState[0] 0 gives 0 (a
  0 word written); otherwise 1, the state block converted OEM to ANSI in
  place (as a NUL-terminated string) unless the number began with 0
  (Alt+0233 gives E9h, Alt+130 OEM 82h, so E9h too; Alt+300 44, ","). A
  keypad digit (by its scan code, Num Lock on or off) with Alt held and
  Ctrl not: lpState[0] = lpState[0] * 10 + digit (a byte; a word
  written), 0 returned; the same digit key again before it is released
  is ignored. VK 0 sets the lights. Otherwise the character for the
  shift state: one byte written, 1; none: a 0 word written, 0, and the
  lights set from the key-state array's lock toggles (the BIOS flags'
  bits 4-6, then INT 16h AH=01h, which has the BIOS set the keyboard's
  lights: twice for Num Lock). Shift states: Alt alone changes nothing,
  Ctrl+Alt gives nothing (the US layout has no AltGr characters), Caps
  Lock affects the letters only, Shift+Caps Lock gives lower case; Ctrl
  gives 01h-1Ah for the letters, 7Fh for Backspace, 0Ah for Enter, 1Bh,
  1Ch, 1Dh for [ \ ] (and the 102nd key), 03h for VK_CANCEL, Esc and
  Space as they are; Shift+Ctrl the letters, 00h for 2, 1Eh for 6, 1Fh for
  -. The keypad's digit VKs give digits in every state but Ctrl; the
  scan code and the flags argument change nothing else. No dead keys.
- **VkKeyScan** searches letters, then the keys unshifted and shifted (the
  keypad left out, DDK), then the Ctrl characters, then Ctrl+letter; the
  low byte of the argument only. Its quirks are 3.1's: 80h gives Ctrl+2,
  9Eh Ctrl+6, 9Fh Ctrl+- (the Shift+Ctrl entries' marker bit), 0Ah
  Ctrl+Enter, 03h VK_CANCEL. **OemKeyScan** is VkKeyScan's key turned into
  the scan code (03h: Ctrl+C), -1 for 80h-FFh.
- **MapVirtualKey**: the type's low byte; 0: VK (low byte) to the first
  scan code with it (the keypad's digits and decimal point to theirs; no
  key, VK_DIVIDE, VK_SNAPSHOT, VK_PAUSE: 0); 1: scan code (the whole word;
  above 58h: 0) to VK, FFh for no key; any other: the VK's unshifted
  character, letters upper case, the high byte of the argument kept
  (141h gives 141h, 1BAh 13Bh), the keypad's digits none.
- **GetKeyNameText**: lParam bits 16-23 the scan code, 24 extended, 25
  "don't care" (Right Shift, Right Ctrl, Right Alt, the keypad's Enter
  named as their left ones). The names (user-visible text: "Esc",
  "Backspace", "Num 7", "Right Shift", "Prnt Scrn" for E0 01, "<00>" for
  54h, F11-F18 for 60h-67h, ...), else the character of a key that types
  one (digits, letters upper case, punctuation), else 0. A name is cut to
  size-1 characters plus NUL and the result is then the size (Windows
  3.1's: "Space" with size 2 gives "S" and 2); a character is written
  with its NUL for any size but 0.
- **AnsiToOem/OemToAnsi** (and Buff): tables from the public code pages
  (below). The string forms go on into the next selector at a 64K
  boundary; the Buff forms wrap within the segment (KB1, "huge").
- **SetSpeed**: Windows' speed (bits 0-4, 31 the fastest) is the
  keyboard's rate counted the other way (IBM: 0 the fastest), the delay
  (bits 5-6) as it is; command F3h to port 60h, the answers polled with
  interrupts off. Repeats per held key match Windows 3.1's (`tests/kbprobe.sh
  rep`: KB2 with `kb2speed` 31, 95, 0, 96).
- **EnableKBSysReq** starts at 0 (DDK); KERNEL turns CVWBreak on at
  boot (Pietrek ch. 1), hence 2 when a program first asks (KB1).

## Interrupt time

INT 9 is taken in protected mode (INT 21h AH=25h: the DOS extender routes
IRQ 1 to it from real mode too). Scan codes are read from port 60h and
the interrupt is ended at the PIC here: keys never reach the BIOS while
Windows runs (KB4 sees no real-mode INT 9 for them on Windows 3.1's
driver either), except the controller's own answers (FAh, FEh, EEh, 00h,
FFh: the BIOS is waiting for them) and Ctrl+Alt+Del (DDK). USER's event
procedure gets AL = VK, AH = 0 (down) or 80h (up), BL = scan code, BH =
1 for an E0-prefixed key and for Num Lock (as 3.1's), with interrupts
enabled and IRQ 1 masked at the PIC meanwhile (one key into USER at a
time); SI, DI, CX, DX are not meaningful (3.1's leaves what it had). The
BIOS's keyboard flags (40:17h, 40:18h) are kept as 3.1's keeps them, set
before the event is called: Shift (left 02h, right 01h), Ctrl 04h with
40:18h bit 0 for the left one, Alt 08h with 40:18h bit 1 for the left
one, Num Lock toggled on the make (not on a repeat) with 40:18h bit 5
while held; Caps Lock and Scroll Lock are not touched here: ToAscii sets
them from USER's key-state array. 40:96h is not used.

Keys (KB2, every event against Windows 3.1's under the same scan codes):

- E0 prefix: the cursor block, keypad Enter and /, right Ctrl and Alt,
  Print Screen (E0 37h: VK_SNAPSHOT), Ctrl+Break (E0 46h: VK_CANCEL, then
  KERNEL's DoSignal); E0 2Ah/36h (the fake Shifts some keyboards send) are
  ignored. Pause (E1 1D 45 E1 9D C5): VK_PAUSE down and up, scan code 45h,
  with or without Ctrl.
- The keypad without E0: cursor VKs (VK_CLEAR for 5); with Num Lock on,
  VK_NUMPAD0-9 and VK_DECIMAL. With Num Lock on and Shift held a keypad
  key is its cursor key, and Shift is reported up first (VK_SHIFT up,
  scan code 36h); when Shift is released Shift is reported down again
  (scan code 36h, interrupts off: 3.1's does that too) and then up.
- Shift: each make reports its own scan code; the first release of
  either reports VK_SHIFT up with the scan code of the last Shift pressed
  and clears both, the other's release then reports nothing. Ctrl and Alt:
  every make and break reported with its own code and E0 flag.
- Typematic repeats come as further makes.
- Alt+Print Screen (scan code 54h): VK_SNAPSHOT with BX 0, Print Screen BX
  1 (DDK: active window, whole screen). Ctrl+Alt+SysReq: CVWBreak (KERNEL)
  or INT 2 as EnableKBSysReq says.

While a DOS program runs (Standard mode), USER calls Disable before and
Enable after (WINOLDAP through DisableOEMLayer/EnableOEMLayer, ARCHITECTURE.md
9.8): INT 9 is the BIOS's again in between (boot-test case `kbdwoados`).

## The translation tables

`tools/kbtables.py` writes `build/keyboard/kbtables.inc`, `ansi_to_oem`
and `oem_to_ansi`. Nothing comes from Microsoft's driver file: each
character maps to the same character of the other code page, by the
public definitions of code pages 1252 and 437 (Python's codecs); where
the other set has no such character, Windows 3.1's choice as observed
(KB1 passes all 256 codes through AnsiToOemBuff and OemToAnsiBuff) is
listed in the tool, 75 ANSI and 78 OEM codes (accented letters to the
plain letter, box drawing to `+`, `-` or the broken bar, the rest mostly
`_`), and so are the few places where 3.1 differs from the exact mapping
(ANSI A0h to 20h, OEM FFh to 5Fh, OEM 0Fh, 14h, 15h to ¤, ¶, §).

The keyboard tables (scan code to VK, the characters of each key, the
Ctrl characters, the key names) are written in `keyboard.asm` from the
IBM PC/AT scan-code set 1 and the VK_ constants of the DDK and the SDK's
windows.h, the US key caps, and corrected by KB1's comparison.

## Verified

- `tests/kbprobe.sh kb1` (KB1, `tests/apps/kb1.c`, as the shell on
  Microsoft's KRNL286, DOSX and USER): every export over all its inputs
  (2435 log lines: every character, VK, scan code, 14 shift states, the
  Alt+keypad sequences, the state-block bytes, the stack use and registers
  of every entry, Enable's key-state initialisation for each BIOS flag,
  the INT 9 vector through Disable and Enable, GetBIOSKeyProc,
  GetTableSeg's segment): identical to Windows 3.1's driver.
- `tests/kbprobe.sh kb2` (KB2, `tests/apps/kb2.c`, with KBREC.DRV in
  front of the driver under test and KB4.COM in real mode, the input
  script `tests/scripts/input/kbprobe.txt`): the scan codes, every event
  procedure call (VK, scan code, extended flag, interrupt flag, the BIOS
  flags at the time), every call made to the driver (USER's Inquire,
  SetSpeed, Enable, ToAscii with its result; KERNEL's EnableKBSysReq;
  applications' AnsiToOem/OemToAnsi), the INT 16h calls each makes, the
  keyboard messages the program gets: identical but for typematic
  repeat counts (timing).
- Boot-test cases `kbd` (typing in Notepad: `tests/scripts/input/kbtype.txt`,
  and the scripts of tabs, alttab, helpmenus, pmkeys, icons, menus; w16's
  driver on Microsoft's stack against all-Microsoft), `kbdall` (every w16
  part with w16's driver against every w16 part with Microsoft's),
  `kbdwrite` (Windows Write), `kbdwoados` (the MS-DOS Prompt): every
  screen pixel-identical (the woados screens up to the text cursor).
- The install tree (`build/w16/SYSTEM/KEYBOARD.DRV`, boot-test `tree`).

## Not verified / not done

- 386 Enhanced mode (VKD virtualises the keyboard; the driver is the
  same in the DDK's account).
- Print Screen and Alt+Print Screen: DOSBox under the harness sends no
  Print Screen scan codes, so VK_SNAPSHOT's BX is the DDK's account.
- Ctrl+Break (E0 46h) and DoSignal: DOSBox sends Pause for Ctrl+Pause.
- Ctrl+Alt+Del, Ctrl+Alt+SysReq (the debugger hooks), ScreenSwitchEnable
  (OS/2 only).
- Language libraries (`[keyboard] keyboard.dll=`, GetKbdTable,
  GetKeyString) and `oemansi.bin=` (other code pages): not supported, the
  US tables and code page 437 always. GetKeyboardType's function-key
  counts for types other than 4 are Windows' documented ones, not
  observed.
- 40:96h: Enable clears bit 4 as 3.1's does; whether 3.1 clears the
  other bits is not known (DOSBox sets none).
- The order of a pending controller answer against SetSpeed's return
  (KB2: the real-mode INT 9 for the ACK is seen inside SetSpeed with
  w16's driver, partly after it with 3.1's; not compared).
