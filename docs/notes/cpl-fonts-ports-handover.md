# Control Panel: Fonts, Ports, 386 Enhanced (2026-11-21)

The record of how the three applets were observed on Windows 3.1 and what is left. The results are in ARCHITECTURE.md 14.3 (Fonts, Ports) and 14.6 (386 Enhanced).

## How 3.1 was observed

Black box only: SNAP scripts (`tests/apps/snap.c`) run by `tests/cpobs.sh` on the reference install on installation-05 (run directory `w16-cplfp`), with `dump`/`info` for every window's class, text, style, ID and rectangle, screenshots, and the INI files afterwards. No resource of MAIN.CPL, CPWIN386.CPL or CONTROL.EXE was read.

- **Layouts.** Templates were recovered from the dumps. The dialogs use 8-point Helv, base units 7 x 13; pixel = dialog unit x 7/4 (x) or x 13/8 (y), rounded half up. A list box shows 1 pixel bigger on every side than its template. An edit control's style loses WS_BORDER. `build/cplfp/du.py` did the conversion; it is not kept.
- **Dialog positions.** They are relative to the owner's client area, with the window's left edge aligned to 8 pixels. Where several unit values give the same pixels, any of them was taken.
- **Combo boxes.** The template heights of the drop-down lists are only fitted to the one dropped list seen (67 pixels: 54 units).

## What each applet does (w16)

- **Fonts** (`shell/cpl/main/fonts.c`): the dialog, the sample, Add (a COMMDLG template and hook), Remove, and TrueType with its restart prompt. WIN.INI [fonts], WIN.INI [TrueType], and CONTROL.INI [installed] 3.1=yes after a copy all come out as 3.1 writes them.
- **Ports** (`shell/cpl/main/ports.c`): Ports, Settings and Advanced. They write WIN.INI [ports] COMn: and SYSTEM.INI [386Enh] COMnBase/COMnIrq, with the COM restart prompt.
- **386 Enhanced** (`shell/cpl/win386`, CPWIN386.CPL): a file of its own, as on 3.1, so that w16's MAIN.CPL laid over 3.1 does not show the icon twice.
  - Device contention, scheduling and validation behave as on 3.1.
  - The Virtual Memory dialog collapses and expands. Temporary and None are written as 3.1 writes them, with the restart dialog for None.
- **WIN386** (`win386/ldr`, `win386/vmm/vm.c`) reads WinTimeslice, MinTimeslice and WinExclusive at start.

## Tests

`tests/cplscenarios.sh`:
- `fnsample`, `fnadd`, `fnaddnc`, `fnremove`, `fntt`, `fnttonly`: Fonts.
- `ptset`, `ptadv`: Ports.
- `en386`, `envalid`, `envm`, and `en386w`/`envmw` (the w16 run on w16's WIN386 as well): 386 Enhanced.

New directive: `;! ref FILE DEST` (a file of the reference install). `;! w16` now also takes a path under build/.

**INI files:** identical to 3.1's in every scenario. **Pixels:** the baseline holds what differs:
- w16's icon art (the panel, the Ports buttons);
- "w16" for "Windows" in user-facing text;
- the swap file's figures.

## Where w16 differs, and what is left (UNVERIFIED)

- **Swap file figures.** 3.1 shows WIN386's own numbers (59,376 KB recommended in DOSBox, presumably from the PageFile device's API). w16 computes them:
  - Space Available: the drive's free space plus the current temporary file;
  - recommended maximum: half of that, at most MaxPagingFileSize.

  To match, observe what CPWIN386 asks the PageFile device: VXS.386 could hook its PM API as it does SHELL's (`tests/vxd/vxs.asm`).
- **Permanent swap file.** DOSBox's drive refuses one on 3.1, so its keys were not seen. w16 writes PermSwapDOSDrive/PermSwapSizeK and makes no 386SPART.PAR. w16's WIN386 does no paging.
- **Saved but not acted on:**
  - ComNAutoAssign (device contention): w16 has no VCD;
  - COMnBase/COMnIrq.
- **Not observed:**
  - whether 3.1 applies WinTimeslice and the rest to the running VMM at once;
  - the idle seconds' range;
  - New Settings for Permanent;
  - 3.1's text when AddFontResource fails;
  - dropped files (WS_EX_ACCEPTFILES);
  - compressed *.FO_/*.TT_ in Add.
- **Copying fonts.** 3.1's MAIN.CPL imports VerFindFile/VerInstallFile (tests/ver/vfonts.sh); w16 copies the file itself, with the same naming (EGA40WOA.FON as EGA40WO0.FON).
- **TrueType.** w16's GDI has no rasterizer, so the TrueType keys act only on Microsoft's GDI.
