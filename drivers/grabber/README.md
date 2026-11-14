# Display grabbers

A display grabber is the display-specific part of running DOS programs
under Windows (Windows 3.1 DDK, "Display Grabbers"): it saves and restores
a DOS program's screen around a switch and copies the screen to the
clipboard. SYSTEM.INI's `[boot]` names the two:

| File | Mode | Loaded by | Kind | w16's |
|---|---|---|---|---|
| `VGACOLOR.2GR` (`286grabber=`) | Standard | DSWAP (real mode) | binary image | `vgacolor.asm` |
| `VGA.3GR` (`386grabber=`) | 386 Enhanced | KERNEL, for WINOA386.MOD (module GRABBER) | NE DLL | see the VGA.3GR section, when there |

Spec and observations: ARCHITECTURE.md 9.11; the working notes:
docs/notes/grabber-handover.md. Everything here was learnt from the DDK's
documentation and black-box observation of Windows 3.1's grabbers (probes
below); Microsoft's grabbers were never disassembled.

## VGACOLOR.2GR (`vgacolor.asm`)

`make` builds `build/dos/VGACOLOR.2GR` (nasm `-f bin`, 8086 code) and
installs it as `build/w16/SYSTEM/VGACOLOR.2GR`. It is a drop-in for Windows
3.1's with Microsoft's DSWAP.EXE and WINOLDAP.MOD (w16's DSWAP saves the
screen itself and needs no grabber; w16's WINOLDAP only checks that the
file SYSTEM.INI names is there).

The image starts with the documented table of near jumps (offsets 00h
InquireGrab, 03h-09h obsolete, 0Ch InquireSave, 0Fh SaveScreen, 12h
RestoreScreen, 15h InitScreen); DSWAP calls them with CS = DS = the
image's segment, and InquireGrab with AX = FFF4h-FFFFh reaches the
extended functions (GetBlock FFF8h, GetVersion FFFAh, DisableSave FFFBh,
EnableSave FFFCh, SetSwapDrive FFFDh, GetInfo FFFEh).

What it does:

- **SaveScreen / RestoreScreen.** The save buffer (InquireSave: 8953
  bytes) holds every VGA register (miscellaneous output, sequencer, CRTC,
  graphics controller, attribute controller), the PEL mask and the DAC,
  and the BIOS's video fields (0040:0049-0066, 0084-008A). The video
  memory goes to a file of its own, `~GRBxxxx.TMP` in the directory of
  SetSwapDrive's template (the second swap directory, where WSWAP clears
  stale `~GRB*.*` files at start): all four planes, 64 KB each, in EGA/VGA
  graphics and text modes (pages, fonts and memory off the screen
  included); the 32 KB at B800:0 in the CGA modes. Without a swap
  directory (or when the file cannot be written) a text mode's page is
  kept in the save buffer instead. RestoreScreen sets the mode through the
  BIOS without clearing, writes the memory back, deletes the file, then
  writes every register, the DAC and the BIOS fields. Every mode comes
  back exactly (tests/grabber.sh grt), CGA modes included, where 3.1's
  grabber does not let DSWAP switch at all.
- **InitScreen(lines).** Text mode 3 with 25 lines, 43 or 50 (INT 10h
  1201h/1202h, 0003h, 1112h for 8 x 8 characters, cursor 0607h), as 3.1's.
- **GetInfo.** GRABINFO and INFOCONTEXT's further fields per mode, as
  3.1's answers them (a table; the modes 3.1's was not observed in are
  marked UNVERIFIED there).
- **GetBlock.** Text modes: the clipboard format (GT_TEXT, the rows'
  characters with CR LF between and two NULs) and the native one
  (character/attribute pairs), for the whole screen or a block (extents of
  0 meaning to the edge; ERR_BOUNDARY beyond it). CGA modes: the whole
  screen as GT_NEWBITMAP, 640 x 200 x 1. EGA/VGA graphics modes:
  ERR_UNSUPPORTED, as 3.1's (Standard mode copies no graphics screen).
  DSWAP calls it for PrintScreen; WINOLDAP puts the text on the clipboard.
- **EnableSave / DisableSave** do nothing (the state is read from the VGA
  at SaveScreen; 3.1's hooks INT 10h). **GetVersion**: 0201h, as 3.1's.
  **SetSwapDrive** keeps the drive and the template. Obsolete functions
  (DSWAP calls FFF5h and FFF4h around a grab) return AX 0, CF clear.

Not done: the EGA register interface calls 3.1's grabber makes (INT 10h
F1h/F3h/F7h, for EGA.SYS); modes beyond the BIOS's standard ones.

## Tests

- `tests/grabber.sh` (needs `make dos grprobes`): `grt` calls Microsoft's
  grabber and w16's directly (GRT.COM, no Windows) in modes 0-7 and
  0Dh-13h and compares their answers, and checks that w16's restores
  everything; `text`, `cga`, `ega`, `prompt` (tests/scripts/grabber/) run
  DOS programs under Windows 3.1's DSWAP and WINOLDAP (`-O`: w16's
  WINOLDAP) and compare the screens and the clipboard (CB1.EXE).
- Boot test: `swapall` boots w16's install tree, VGACOLOR.2GR included.
- Probes: GR1.2GR (a recording stand-in DSWAP calls, forwarding to a
  grabber appended to it at test time), GRT.COM, GV1.COM, CB1.EXE
  (docs/notes/grabber-handover.md).
