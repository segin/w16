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
  `; wfini:` (WINFILE.INI keys), `; get:`.
- `tests/scripts/winfile/obs/WF1..WF7.txt`: the observation scripts
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
  File-system changes by other programs (2026-11-20, the KERNEL FileCdr
  work, tests/apps/tk64.c): main.c installs a FileCdr callback (KERNEL.130)
  that keeps the changes and posts WM_FILESYSCHANGE (0034h); the frame
  applies each to the windows showing its directory (trees too after
  39h/3Ah) and, with the Search Results window up for
  that drive, asks "The contents of this drive have changed. Do you want
  to update the Search Results window?" (Yes searches again): obs WF15,
  scenario WF14 (SNAP's copyfile as the other program). As on 3.1, only
  a new name (or one that goes) rereads a window (a file written again
  keeps its old size in the list: WF01), the question is posted, not
  asked at once (WF10's close, posted just after a change, closes File
  Manager), and a change while the box is up asks again after it (SNAP's
  own screenshot files do that: obs WF15 shows three boxes). UNVERIFIED: the
  "0xFFFFFFFF free bytes until the drive is reread" the brief mentions
  (not visible in WF15: w16 reads the free space at every paint); whether
  3.1 asks for a change on another drive or for File Manager's own
  operations. Not done: file commands from the Search Results window.
- Layer 4 (Disk menu): the boxes and every error path as 3.1's without a
  floppy (WF13; obs WF13, WF14): Copy Disk, Label Disk, Format Disk (the
  capacity list from DOS's device parameters, "Drive Capacity" when DOS
  cannot say), Make System Disk (its progress box), Select Drive. The
  floppy work itself (BIOS INT 13h through DPMI; w16's own boot sector;
  IO.SYS, MSDOS.SYS and COMMAND.COM copied) is UNVERIFIED: no floppy in
  DOSBox here. Network drives: the menu has no Connect/Disconnect items
  without a network (3.1 the same: WF1); the WNet calls are not made.
- Layer 5 (extensions, FMEXT): not started. The SDK (vol. 1 "File Manager
  extensions", vol. 3 FMS_LOAD, FMS_GETFILESEL, FMS_GETDRIVEINFO and the
  FM_*/FMEVENT_* entries) documents the protocol but not the numbers of
  the messages, nor MENU_TEXT_LEN. Plan: a test extension DLL of our own
  (tests/apps), listed in WINFILE.INI [AddOns], loaded by the original:
  it logs every FMExtensionProc call (wMsg, lParam; the first is
  FMEVENT_LOAD, which it answers with FMS_LOAD: dwSize, a short menu
  name, a popup of two items; log wMenuDelta); SNAP opens its menu
  (INITMENU), presses F5 (USER_REFRESH), changes the selection
  (SELCHANGE), closes (UNLOAD); then from one of its menu commands it
  sends WM_USER-range candidates to File Manager's frame with a large
  buffer as lParam and logs the answers and the buffer, to find FM_*
  (beware FM_RELOAD_EXTENSIONS). Then w16's WINFILE implements [AddOns]
  loading (LoadLibrary, GetProcAddress "FMExtensionProc": KERNEL upper-cases
  the name and finds only an export spelled FMEXTENSIONPROC, as 3.1, TK65;
  the test extension must export it in capitals), the menus
  after Window... as 3.1 places them, the events and the messages, and a
  boot-test part runs the same DLL under both File Managers.
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
  Format Disk's capacity line, Make System Disk's box, Select Drive (the
  focus: 3.1's list box has no WS_TABSTOP and is not focused).
- Not compared: WF5 E06's one-line scroll; the Copy/Move progress text.
- WF14 S04, S05: behind the "contents have changed" box 3.1's Search
  Results list keeps its selection filled; w16's draws it framed (the
  list lost the focus).

## KERNEL's side (for reference)

FileCdr's rules as observed (ARCHITECTURE.md 9.7, tests/apps/tk64.c):
one callback system-wide, owned by the installing task; called after a
successful 39h, 3Ah, 3Ch, 41h, 4301h, 56h, 5Ah, 5Bh, 6Ch with the caller's
AX and "D:\cwd\name as given"; USER's AppExit removes it when File Manager
ends. Microsoft's WINFILE.EXE on w16's KRNL386 gives obs WF15's screens
pixel for pixel.

## Last full run

Commit fd6e140 (after master ba697bf was merged in) on installation-05
(`tools/remote-test.sh -n w16-winfile`): make test pass; boot test 138
ok, 4 failures, all of the known list (lzexpand quirks, toolhelp ref386,
soundtime, trails@timed); w16winfile (13 parts, WF01-WF13) ok.

## Where to resume

Layer 5 (the plan above), then the known differences. Everything else
of the brief is in place.
