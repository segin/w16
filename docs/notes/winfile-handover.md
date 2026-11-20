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
  running program (SNAP `dump`, `dumpmenu`, `info`, screenshots).
  Reading the NE header (imports, exports, the resource table's types and
  IDs) is fine. The bitmap strip (100) and the icons (200-204) are linked
  into a scratch test copy only (`tests/wfmsres.sh`, `tools/ne2bmp.py`,
  `tools/ne2ico.py`), so screen comparisons measure drawing code.
- The art is w16's own: `tools/mkwfbmp.py` (the strip, in the original's
  cell layout: 13 16x16 pictures on blue, the same on magenta, 6 27x14
  drives) and `tools/mkwfileicon.py` (program, tree, list, tree+list).

## Tools

- `tests/wfobs.sh SCRIPT OUT`: a SNAP script on the reference 3.1
  (`WFOBS_EXE=` runs ours instead, padded to the original's size over
  `C:\WINDOWS\WINFILE.EXE` so both list the same bytes). The test tree
  `tests/scripts/winfile/mktree.py` (C:\WFTEST, fixed dates) is laid over
  C:\ (`REFRUN_TREE`, added to `tests/refrun.sh`). Directives: `; ini:`,
  `; wfini:` (WINFILE.INI keys), `; get:`.
- `tests/scripts/winfile/obs/WF1..WF6.txt`: the observation scripts (run
  with `tests/scripts/winfile/obs/obs.sh WF5 ...`, results in
  `build/ref/wfobs/`).
- `tests/wfscenarios.sh [WFnn...]`: the regression, original vs w16 (with
  Microsoft's art), screens against `tests/scripts/winfile.baseline`, the
  WINFILE.INI each leaves compared. Remote:
  `tools/remote-run.sh -n w16-winfile -g build/ref/wfscen sh tests/wfscenarios.sh WF01`.
  (remote-run joins its arguments into one `sh -c` string: pass `'&'`,
  `';'` as words, never a quoted `sh -c "..."`.)

## State

Layer 1 (frame and windows) in progress; see the scenario list below.
Scenarios WF01-WF04 (first start, focus and selection, WFTEST with views,
sorts and tree commands, maximised details, New Window, Tile, Cascade,
Arrange Icons, F5) at 0 pixels but the last fixes (sort by date's top
line, the status bar after Tree and Directory).

## Observed behaviour worth knowing (all in the code's comments too)

- Status bar: MS Sans Serif 10, 22 high, boxes 276 wide at x 8 and 292
  whatever the window width ((cxscreen - 88) / 2 assumed). The left box
  is event-driven (winfile.h ST_*), not focus-driven.
- "Total n file(s)" counts directories, and adds what DOS says of their
  size (DOSBox reports a size for directories).
- The year is printed as '0' + (year - 1900) / 10: 2026 shows "<6".
- Indicate Expandable Branches is bit 0x20 of the dirN view word (per
  window), not a [Settings] key; Tree Only straight to Directory Only
  drops it and rebuilds the tree.
- Window numbers ":n": one more than the others' with the same directory,
  given when the title is set (F5 renumbers).
- 3.1's ListBox grows a created window by its border: the list is moved
  back after CreateWindow (`dir_set_view`).
- USER's FillRect takes no COLOR_ index (use `fill_sys`).

## Next

Layer 1: opening on a saved WINFILE.INI (scenario with `; wfini:`
Window=, dirN=, StatusBar=0, a maximised and a minimised window), Help
(observe with `tests/apps/thelp.c`), About (ShellAbout), the drive bar's
volume label without a label, Ctrl+letter drives, drive A: with no disk
(the original's message box), boot-test case `w16winfile`.
