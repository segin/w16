# MAIN.CPL audit triage

An outside, read-only audit of w16's MAIN.CPL (`shell/cpl/`: cpl.h,
libentry.asm, main/*.c, main.rc; written by another AI model on an older
tree, not in the repository) listed 3 High, 5 Medium and 8 Low findings
(H1-H3, M1-M5, L1-L8 here, in its order). Since it was written the Fonts
and Ports applets came, cpArrow and the file helpers moved to
`shell/cpl` (arrow.c, cplutil.c) and CPWIN386.CPL was split out (its own
audit). Each finding is checked here against the current code, and
against Windows 3.1 where a fix would change what can be seen: scenarios
of `tests/cplscenarios.sh` run on the reference install with Microsoft's
MAIN.CPL and with w16's (INI files byte for byte, screenshots, CP1.LOG,
and the new C:\CMP.LOG).

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong, or
3.1 does the same and w16 already matches), **3.1-BEHAVIOUR** (3.1 does
the same: kept), **HARDENING** (only with malformed input, e.g. a
hand-edited INI file; fixed when cheap and invisible to working use),
**UNSURE**. Where 3.1 itself dies, w16 may live (the user's ruling).

New test tools for this audit:
- CP1.CPL `monitor=1` (`tests/apps/cp1.c`, `cp1-mon.ini`): a hidden
  top-level window in Control Panel's task that logs every
  WM_WININICHANGE (with its section), WM_SYSCOLORCHANGE,
  WM_DEVMODECHANGE and WM_FONTCHANGE into CP1.LOG.
- SNAP (`tests/apps/snap.c`): `blinkrate` (how often a rectangle, a
  control or the caret changes in a time), `caretblink`
  (GetCaretBlinkTime), `kbrlog` (KEYBOARD.SetSpeed calls through
  KBREC.DRV), all into C:\CMP.LOG, which cplscenarios compares
  (`tests/scripts/cmplog.py`: blink counts within a tolerance).
- cplscenarios directives `;! saver` (w16's SSSTARS.SCR with another
  description, `tests/scripts/scrdesc.py`), `;! kbrec`, `;! survive`
  (3.1 dies here, w16 must finish; and any w16 run must finish when
  3.1's did).

Status: every finding has a verdict and is closed (2026-11-23).

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | HARDENING | The 64-byte buffers were there (desktop.c fill_patterns `name[64]`, apply `name[64]`, the Edit Pattern list's CBN_SELCHANGE `name[64]`, `pat_name[64]`, all filled by CB_GETLBTEXT) but a working user cannot make a longer name: Edit Pattern's name box has no CBS_AUTOHSCROLL (template 0x50210102) and its text was read into 64 bytes, so only a hand-edited CONTROL.INI reaches it. Probe dklongpat: a 124-character name that is the current pattern: 3.1 lists it, selects it by its bits, writes them on OK, and Edit Pattern shows its bits; then 3.1's MAIN.CPL dies when Edit Pattern is cancelled (no further SNAP line). w16 overflowed its stack buffers at every open (it lived by luck) and Edit Pattern showed an empty grid (the name cut to 63 characters found nothing). | Every name in a local block as long as it is (win_text, item_text, pattern_bits; pat_name a block); the Remove message sized to the name. Tests dklongpat (OK, INI files as 3.1's), dklongpat2 (`;! survive`: Edit Pattern then Cancel, where 3.1 dies). | f1606c0 |
| H2 | HARDENING; FALSE (CB_ERR) | Overflow, the negative `sizeof item - n` and the cut colour list all need a scheme name of 220 characters or more: Save Scheme limits its box to 79 (EM_LIMITTEXT, color.c SaveSchemeProc), so only a hand-edited CONTROL.INI has one. Probe colongsch (450 characters): 3.1's Color applet dies as it opens; so did w16's (the overflow). `scheme_name` ignoring CB_ERR: FALSE: every caller checks first (apply `i < 0`, save_scheme `sel >= 0`, remove_scheme `i < 0`; WM_DRAWITEM handles itemID -1 before calling it). | Items built in a block of the name's length plus 256 for the colours; scheme_name_h gives the whole name for [current] and Remove. Test colongsch (`;! survive`: w16 lists it, chooses it, cancels). | f1606c0 |
| H3 | REAL (by reading) | desktop.c had its own dos_find: INT 21h 1Ah to MAIN.CPL's DGROUP, never put back (the shared cpl_find, cplutil.c, already saved and restored it for Fonts and CPWIN386). Control Panel's DTA was left in MAIN.CPL's data segment, freed with the library; nothing seen to use it afterwards (CONTROL.EXE finds its *.CPL files before), so not observable on the reference install. | Desktop uses cpl_find (and cpl_ne_desc for the savers' names). Tests dksaver, dkwall, dksvorder (the lists as 3.1's). | f1606c0 |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | FALSE | main.c CPlApplet: `case CPL_DBLCLK: if (l1 >= 0 && l1 < AP_COUNT) run(hwnd, (int)l1);` (and CPL_INQUIRE checks the same); not_yet is reached only through run's default case with a checked index. | None. | - |
| M2 | REAL | zzdisc, dkcolors: 3.1 inverts the 2x13-pixel static IDC_DK_BLINKER at the blink rate (yellow on a navy window colour, black on white); w16 showed and hid an empty SS_LEFT static, which paints nothing. Found with the user's report "Cursor Blink Rate does not do anything": also the bar started at GetCaretBlinkTime (3.1: WIN.INI CursorBlinkRate, 530), the caret's rate changed only on OK (3.1: as the bar moves; Cancel leaves it), a page was 100 ms (3.1: 250), and on w16's USER Alt+R went to the label after the group box. | Blink commit: the sample inverted on the timer; the rest as 3.1; USER's mnemonic skips labels. Tests dkblk* (CMP.LOG: the sample's and Notepad's caret's blinks per 4 s, GetCaretBlinkTime, WIN.INI), dkcolors. | 920dd9a |
| M3 | FALSE (3.1-BEHAVIOUR) | msbtn, real input: the left button pressed over the dialog and released outside it leaves 3.1's L box inverted too; TEST double-clicked then repainted (the dialog moved half off the screen and back) loses its inversion on 3.1 too; the button held through a repaint and released: the same on both. Every screen as 3.1's. | None (msbtn kept as the test). | - |
| M4 | REAL | msdbl: WIN.INI DoubleClickSpeed=1000: 3.1's thumb at Slow, w16's at Fast (unsigned 900 - 1000). One step faster then writes 900 on both. msdbl2 (60): Fast on both. | The bar's position from the time held to 100..900. Tests msdbl, msdbl2. | f1606c0 |
| M5 | REAL | wnchange, CP1's monitor window: 3.1 sends WM_WININICHANGE with lParam "colors" after Color's OK (after SetSysColors' WM_SYSCOLORCHANGE), "Desktop" then "Windows" after Desktop's (then the WM_SYSCOLORCHANGE 3.1's USER sends when the desktop is set), "windows" (lower case) after Keyboard's, "Windows" after Mouse's; nothing on Cancel. w16 sent none. | The four broadcasts, in that order; Desktop sets the pattern and wallpaper after them. Test wnchange. | f1606c0 |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | REAL | zzdisc, dkcolors: 3.1's Edit Pattern never lists "(None)"; opened on (None) the name is empty. w16 deleted it only when it was selected, so opened on 50% Gray it listed "(None)" above. | "(None)" found by name and deleted; the name emptied when it was the choice. Also found there and fixed: the grid and sample colours (set bits in the window text colour, the others in the desktop's: w16 drew black on white and an inverted sample) and Add and Change disabled at first, Remove only with a pattern. Test dkcolors. | f1606c0 |
| L2 | 3.1-BEHAVIOUR (order, buttons, ScreenSaveActive); REAL (selection, SCRNSAVE.EXE, the first entry's block); HARDENING (CB_ADDSTRING failure) | dksvorder, with a saver named "!Starfield Simulatio": 3.1 sorts it first too and treats the first entry as "no saver" for Test, Setup and ScreenSaveActive (0), but writes the chosen entry's path (SCRNSAVE.EXE=C:\WINDOWS\SSAAA.SCR; w16 wrote (None)) and starts on "(None)" by name (w16 on the first entry). dksvorder2: "(None)" second chosen: (None) and ScreenSaveActive=1, Test enabled, on both. free_savers skipped index 0, leaking the first saver's block; a CB_ADDSTRING failure (CB_ERR) leaked the block. | "(None)" found by name for the start; SCRNSAVE.EXE from the entry's data; every entry freed; no block for a failed entry. The same index-0 assumption in the pattern list (fill_patterns, apply) replaced by the name. Tests dksvorder, dksvorder2. | f1606c0 |
| L3 | HARDENING | cpl_ne_desc (now also behind the savers' names) read the MZ header as a description when the NE header's non-resident table offset is 0; only a hand-made file has that, and 3.1 would not list such a saver anyway (no "SCRNSAVE :"). | An offset or length (NE+20h) of 0: no description. | f1606c0 |
| L4 | FALSE | windir asks GetWindowsDirectory for at most size - 14 (146 of 160) bytes; an MS-DOS path is at most 64 characters plus the drive, so the backslash and an 8.3 name always fit. | None. | - |
| L5 | FALSE | WEP calls arrow_unregister, which unregisters only when `registered` (arrow.c), set by cpl_libmain's RegisterClass; before initialisation it is 0 (BSS). The 3.1 SDK (WEP) says 3.1 calls WEP on the application's stack, so Windows functions may be called. | None. | - |
| L6 | HARDENING | make_field kept a created bitmap when CreateCompatibleDC failed, undrawn, and showed it from then on (only when GDI's heap is full). | The bitmap deleted then; the next paint tries again. | f1606c0 |
| L7 | HARDENING | Only a typed 32767 (Desktop, signed) or 32768 and up (Color selector, unsigned cast to int) wraps. dkspin: -32768 and down, 99999 and down, 600 and down give 1, 50, 50 on both; 3.1 also selects the new value (w16 did not). | The sums in a long; the Desktop's value selected after an arrow. Test dkspin. | f1606c0 |
| L8 | REAL (the other way round) | The hit test was proportional; the paint was not. zzdisc: 3.1's grid cells are 19 and 20 pixels in turn (156 / 8, proportional: its clicks and its cells agree); w16 drew 19-pixel cells and left 4 pixels, so a click near a cell's left edge toggled its left neighbour. | The paint proportional, as 3.1's; the hit test kept. Test dkcolors (the grid). | f1606c0 |

## Beyond the audit

- The user's two reports, committed on their own: the cursor blink rate
  (920dd9a, under M2) and the Keyboard applet (c92f61a: USER
  never gave WIN.INI's or the applet's delay and rate to the keyboard
  driver; the driver mapped the speed wrongly; USER's scroll bar sent no
  SB_ENDSCROLL for keys).
- A saver or pattern sorting before "(None)" (L2) and the long names (H1,
  H2): w16 now lives where 3.1's MAIN.CPL dies.
- Not fixed, for USER's maintainers: w16's SystemParametersInfo has no
  SPI_SETDESKPATTERN, SPI_SETDESKWALLPAPER, SPI_SETBORDER,
  SPI_SETGRIDGRANULARITY, SPI_ICONHORIZONTALSPACING (set),
  SPI_SETICONTITLEWRAP or SPI_SETFASTTASKSWITCH, and its
  SPI_SETDESKWALLPAPER sends no WM_SYSCOLORCHANGE (3.1's does: wnchange
  on 3.1's USER), so on the install tree those Desktop settings take
  effect only at the next start.
