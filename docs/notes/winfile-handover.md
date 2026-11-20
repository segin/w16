# File Manager (WINFILE.EXE) handover

w16's own File Manager, `shell/winfile/`, a clean-room clone of Windows
3.1's. Task brief: five layers (frame and windows; views; file operations;
Disk menu; the extension interface), each matched to 3.1 pixel for pixel
where tests compare. ARCHITECTURE.md 15.7 is the reference description;
this note is the working state.

## Clean-room rules for this program (hard)

- Never look at Microsoft's open-source winfile (github.com/microsoft/winfile)
  or any other File Manager source. Facts come from the SDK books in
  `.tools/docs/` and black-box observation of 3.1's WINFILE.EXE.
- Never read the original's resources with a dumper (menus, dialogs,
  strings): the menu, the dialogs' layouts and the texts come from the
  running program (SNAP `dump`, `dumpmenu`, `info`, screenshots;
  `tools/snapdlg.py` turns a dump into a template). Reading the NE header
  (imports, exports, the resource table's types and IDs) is fine. The
  bitmap strip (100) and the icons (200, 201, 202, 204) are linked into a
  scratch test copy only (`tests/wfmsres.sh`, `tools/ne2bmp.py`,
  `tools/ne2ico.py`), so screen comparisons measure drawing code.
- The art is w16's own: `tools/mkwfbmp.py` (the strip, in the original's
  cell layout: 13 16x16 pictures on blue, the same on magenta, 6 27x14
  drives) and `tools/mkwfileicon.py` (program 200; minimised windows: tree
  only 201, list only 202, both 204).

## Tools

- `tests/wfobs.sh SCRIPT OUT`: a SNAP script on the reference 3.1
  (`WFOBS_EXE=` runs ours instead, padded to the original's size over
  `C:\WINDOWS\WINFILE.EXE` so both list the same bytes). The test tree
  `tests/scripts/winfile/mktree.py` (C:\WFTEST, fixed dates) is laid over
  C:\ (`REFRUN_TREE`, added to `tests/refrun.sh`). Directives: `; ini:`,
  `; wfini:` (WINFILE.INI keys), `; get:`, `; put: SRC=DEST`, and for
  the extension probe `; fmx: FMX1 FMX2 ...` (copies of FMX.DLL, each its
  own module) and `; fmxini: FMX1:Key=value` (how a copy answers: see
  tests/apps/fmx.c). With `; fmx:` the script starts with SNAP's
  `memlog C:\FMX.LOG`: FMX's log and the screens stay in memory until
  the script ends, because 3.1's File Manager reacts to any file made on
  the disk while it runs (see layer 5); `append C:\FMX.LOG text` puts
  marks in the log.
- `tests/scripts/winfile/obs/WF1..WF19.txt`: the observation scripts
  (`tests/scripts/winfile/obs/obs.sh WF5 ...`, results in
  `build/ref/wfobs/`; WF7's key sequence went astray after the first
  copy, its first screens and dumps are still good).
- `tests/wfscenarios.sh [WFnn...]`: the regression, original vs w16 (with
  Microsoft's art), screens against `tests/scripts/winfile.baseline`, the
  WINFILE.INI each leaves compared. Remote:
  `tools/remote-run.sh -n w16-winfile -g build/ref/wfscen sh tests/wfscenarios.sh WF01`
  (remote-run joins its arguments into one `sh -c` string: pass `'&'`,
  `';'` as words, never a quoted `sh -c "..."`; use another `-n` for a
  second simultaneous run, e.g. `w16-winfile-obs`).
- `tests/wfhelp.sh [WFHn...]`: WinHelp requests, original vs w16
  (`REFRUN_TIMEOUT=240` keeps a hung run short).
- `build/scratch/` (not committed) held px.py/align.py/cmp.py, small
  pixel tools: print a region as colour letters; find the shift that
  aligns w16's region with the original's; count differing pixels.

## State

- Layer 1 (frame, drive bar, tree, list, split, status bar, menus, INI
  restore, New Window/Tile/Cascade/Arrange, F5, Ctrl+letter drives and
  the drive error box, About, Help): scenarios WF01-WF06 at 0 pixels or
  nearly (see the baseline); WFH1-WFH8 identical requests.
- Layer 2 (views): Name/All/Partial Details, the sorts, By File Type,
  Indicate Expandable Branches, the tree commands, Tree/Directory Only,
  Confirmation, Font (COMMDLG with a hook and template, Lowercase box),
  the split dragged with the mouse (WF09) and View > Split (arrows 2
  pixels a press from the split, Enter, Escape: obs WF9): WF03, WF07, WF09.
- Layer 3 (file operations): Open, Move, Copy, Rename, Delete, Properties,
  Run, Print, Associate (REG.DAT classes and WIN.INI [Extensions]),
  Create Directory, Search and its WFS_Search window, Select Files, Copy
  to Clipboard (registered "FileName" format: UNVERIFIED which formats
  3.1 adds), drag and drop (DragObject; our panes, drive bar and icons
  take drops with Confirm Mouse Operation; other programs get
  WM_DROPFILES: Program Manager makes an item, WF12 identical to 3.1's),
  the confirmations as 3.1's (Replace, File Delete, Directory Delete with
  each file inside, Mouse): WF08, WF10, WF11, WF12. The operation's box
  stays up disabled with "Deleting..." (observed) / "Copying...",
  "Moving..." (UNVERIFIED: DOSBox copies too quickly to catch).
  Not done: the "Search Results: The contents of this drive have changed"
  box (3.1 watches the file system through KERNEL's FileCDR hook,
  KERNELCHANGEFILESYSTEM is exported; w16's search window does not
  refresh), file commands from the Search Results window.
- Layer 4 (Disk menu): the boxes and every error path as 3.1's without a
  floppy (WF13; obs WF13, WF14): Copy Disk, Label Disk, Format Disk (the
  capacity list from DOS's device parameters, "Drive Capacity" when DOS
  cannot say), Make System Disk (its progress box), Select Drive. The
  floppy work itself (BIOS INT 13h through DPMI; w16's own boot sector;
  IO.SYS, MSDOS.SYS and COMMAND.COM copied) is UNVERIFIED: no floppy in
  DOSBox here. Network drives: the menu has no Connect/Disconnect items
  without a network (3.1 the same: WF1); the WNet calls are not made.
- Layer 5 (extensions, FMEXT, `ext.c`): done; scenario WF14 (the probe's
  log byte-identical, screens at 0 pixels), obs WF15-WF19. The probe
  FMX.DLL (`tests/apps/fmx.c`, w16's own) logs every FMExtensionProc
  call; its menu runs every FM_* query, a scan of WM_USER+0x1F0..0x21F,
  Refresh All/Active, Reload, Uninstall. What 3.1 does (ARCHITECTURE.md
  15.7 and ext.c's header have it all):
  - FMEVENT_LOAD 100, UNLOAD 101, INITMENU 102, USER_REFRESH 103,
    SELCHANGE 104; no help strings, no toolbar (NT's). FM_GETFOCUS is
    WM_USER+0x200, then GETDRIVEINFO, GETSELCOUNT, GETSELCOUNTLFN,
    GETFILESEL, GETFILESELLFN, REFRESH_WINDOWS, RELOAD_EXTENSIONS (+0x207).
    MENU_TEXT_LEN 40 (FMS_LOAD is 48 bytes).
  - [AddOns] in file order, before the frame shows. Deltas 600, 700, ...
    per extension loaded. Refused (answer 0, hMenu 0, dwSize != 48):
    FreeLibrary, no UNLOAD, delta not used up. Missing DLL: silent. At most
    5; no key read after the fifth. 3.1's KERNEL upper-cases the name
    GetProcAddress asks for: an extension must export FMEXTENSIONPROC in
    capitals (w16's KERNEL matches any case: more lenient, see below).
  - Item IDs + delta (submenus too), item state dropped (grayed shows
    enabled). Popups before Window. WM_COMMAND: File Manager's own IDs
    first (ID 1003 = Refresh even with a fifth extension), then the
    extensions, then DefFrameProc (ID 1006 goes to the fifth extension's
    item 6, not to the first MDI window).
  - UNLOAD with hwnd 0 at exit (after the frame is gone) and on reload,
    FreeLibrary after each. Reload re-lays the menu bar (it wraps then).
  - Not done (needs KERNEL): 3.1 gets KERNEL's FileCdr callback for every
    file made or changed; then FM_GETDRIVEINFO says FFFFFFFFh free until
    the drive is read again, and Search Results asks "The contents of this
    drive have changed...". w16's KERNEL stores the FileCdr callback but
    never calls it (kernel/kmisc.c), so w16's File Manager cannot notice.
- Install: WINFILE.EXE is in the install tree (WINDOWS\WINFILE.EXE),
  the Main group (after Control Panel, so the other items keep their
  places) and assets/README.TXT.

## Observed behaviour worth knowing (all in the code's comments too)

- Status bar: MS Sans Serif 10, 22 high, boxes 276 wide at x 8 and 292
  whatever the window width ((cxscreen - 88) / 2 assumed). The left box
  is event-driven (winfile.h ST_*), not focus-driven.
- "Total n file(s)" counts directories, and adds what DOS says of their
  size (DOSBox reports a size for directories); sorts by size and date
  order directories too.
- The year is printed as '0' + (year - 1900) / 10: 2026 shows "<6".
- Indicate Expandable Branches is bit 0x20 of the dirN view word (per
  window), not a [Settings] key; Tree Only straight to Directory Only
  drops it and rebuilds the tree.
- Window numbers ":n": one more than the others' with the same directory,
  given when the title is set (F5 renumbers).
- 3.1's ListBox grows a created window by its border: the list is moved
  back after CreateWindow (`dir_set_view`). 3.1's Edit takes WS_BORDER
  off its style and draws it: dumps never show it (templates add it).
- USER's FillRect takes no COLOR_ index (use `fill_sys`).
- WINFILE.INI Window= is x,y,width,height; dirN= is left,top,right,bottom.
- UNVERIFIED: WF5 E06 once showed the details view a line down where
  WF02 S05 (the same keys) did not.

## Known differences left (tests/scripts/winfile.baseline)

- WF05: the attributes column, cut by the list's edge, comes 2 rows lower
  and a pixel right of 3.1's (cause not found); the View menu with a
  minimised window active now matches but for that.
- WF07 S08-S10: the Font box (fixed: WS_VSCROLL on the combo boxes, which
  3.1's ComboBox passes to its list and takes off its own style) is
  closer; what is left is in the sample and after the font change.
- WF10 S03: 26 pixels in the tree behind Confirm Directory Delete.
- WF13: the Label Disk error box (2007: the box's size or line breaks),
  Format Disk's capacity line, Make System Disk's box, Select Drive
  (S11, 524: the list box's WS_TABSTOP is gone, as in 3.1's dump, so the
  focus goes to OK, which shows a focus frame where 3.1 shows none: where
  3.1's focus is is not known; and 3.1's list box is 2 pixels taller:
  USER's whole-item height, the user/ agent's area).
- Not compared: WF5 E06's one-line scroll; the Copy/Move progress text.

## Found outside File Manager (reported, not changed)

- KERNEL GetProcAddress: 3.1 upper-cases the name asked for and compares
  it with the export tables as they are, so a mixed-case export
  ("FMExtensionProc" in FMX's first build) is never found, by any spelling
  (obs WF15: both "FMExtensionProc" and "FMEXTENSIONPROC" gave 0); w16's
  `find_in_table` (kernel/module.c) folds both sides and finds it.
- KERNEL FileCdr (KERNEL.130): stored, never called (above).

## Last full run

Commit fd6e140 (after master ba697bf was merged in) on installation-05
(`tools/remote-test.sh -n w16-winfile`): make test pass; boot test 138
ok, 4 failures, all of the known list (lzexpand quirks, toolhelp ref386,
soundtime, trails@timed); w16winfile (13 parts, WF01-WF13) ok.

## Where to resume

The known differences above; then FileCdr in KERNEL (with whoever owns
it) and the Search Results refresh and free-space reset on top of it.
Everything else of the brief is in place.
