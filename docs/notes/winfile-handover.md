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
  nearly (see the baseline); WFH1-WFH4 identical requests.
- Layer 2 (views): Name/All/Partial Details, the sorts, By File Type,
  Indicate Expandable Branches, the tree commands, Tree/Directory Only,
  Confirmation, Font (COMMDLG with a hook and template, Lowercase box):
  WF03, WF07. Not done: Split (the mouse/keyboard split mode, IDM 414),
  dragging the split bar.
- Layer 3 (file operations), begun: Open (directories, programs,
  documents through ShellExecute, Minimize on Use), Move, Copy, Rename,
  Delete (MessageBox confirmations: texts UNVERIFIED), Confirm File
  Replace (the original's dialog, WF7), Properties, Run, Print
  (ShellExecute "print"), Create Directory, Select Files: WF08. Not done:
  the Copy/Move dialog showing progress while it works (3.1 keeps it
  open, disabled, with "Copying:" and the name in IDs 104/201), Copy to
  Clipboard, Associate, Search and its WFS_Search window, drag and drop,
  the real texts of the delete confirmations (observe: WF7's script went
  astray; write a cleaner one).
- Layers 4 (Disk menu) and 5 (extensions, FMEXT): not started. The SDK
  (Programmer's Reference vol. 1, "File Manager Extensions") documents
  the messages and structures but not their numbers: those must be
  observed with an extension DLL of our own loaded by the original
  through WINFILE.INI [AddOns] (log the wMsg values; probe FM_* with
  SendMessage from it).
- Not yet in the install tree or the Main group (it cannot stand in for
  3.1's until layer 3 is done).

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
  minimised window active (pane items grey: fixed after the baseline,
  not yet re-run).
- WF07 S08-S10: COMMDLG's Font box lists one size fewer (no 24 for MS Sans
  Serif, so no scroll bar) than 3.1's File Manager asks for: some
  ChooseFont flag or LOGFONT field differs; the list after the font
  change follows from it.
- WF08 S08-S12: after Create Directory and Delete, 3.1's tree and list
  differ in detail (which line is selected, focus): observe again.

## Last full run

Commit 4d44dbe on installation-05 (`tools/remote-test.sh -n
w16-winfile`): make test pass; boot test 129 ok, 5 failures, all of the
known list (taskman@tree, ddeml conn, lzexpand quirks, soundtime,
trails@timed); w16winfile 8 of 8 parts.
