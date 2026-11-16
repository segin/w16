# Control Panel repaint: handover (2026-11-16)

Branch `worktree-agent-a5eaa259271e5b931` (worktree `.claude/worktrees/agent-a5eaa259271e5b931`), master merged at 2b27626.

## Bug 1: Color dialog's sample left on Control Panel's status line (fixed)

Report: the install tree under KRNL386, Ocean scheme; after the Color dialog closed, the panel's bottom strip showed the sample's "Highlighted", "Window Text" and a piece of its scroll bar.

- Reproduced headless on the tree under both KERNELs whenever a scheme is picked from the scheme drop-down list **with the mouse** (OK or Cancel). Picking with the keys, double-click open and click Cancel alone are fine.
- With Microsoft's CONTROL.EXE/MAIN.CPL on w16's USER: reproduced. With w16's CONTROL/MAIN.CPL on Microsoft's USER: not reproduced. So the bug is in USER.
- **Cause** (USER `paint.c` paint_message): when no visible window had anything to paint, the queue's paint count was set to 0 but WF_INPAINT was left on hidden windows (here the dropped ComboLBox, invalidated by the selection, then hidden). When they were destroyed with the dialog, free_window called dec_paint and took counts away from windows that were still waiting. The status line (the panel's last window to paint) never got WM_PAINT. Instrumented log (BeginPaint, hide and destroy events; OutputDebugString to W16DBG.LOG): "STALE 1", then after the dialog hid, the frame and the list got BeginPaint but the status line (0db0) did not. With the fix it does.
- **Fix** ff50a13: in the stale case, clear the flags of the windows that do not show (`drop_hidden_paints`). They keep their update regions, and showing a window invalidates it again.

## Bug 2: cplclick@co-tree 02-palette differs between KRNL286 and KRNL386 (probably the same cause; NOT yet verified)

Master failed this 3 times in a row (run dir /mnt/build/w16-runs/w16/build/boottest-083356, read-only). In the bad shot (t286) the expanded dialog shows its grids (painted by the dialog itself) but none of the child controls: Screen Element and its combo box, the labels, Define Custom Colors, Help. This fits the same miscount: children never got WM_PAINT after the dialog was resized (expand() in shell/cpl/main/color.c is a SetWindowPos widening). It only started failing once master's cda9698 put MAIN.CPL in the tree; before that the tree showed "Cannot find the Control Panel components" on both KERNELs.
- An instrumented **pre-fix** run (ff50a13 reverted, logs for STALE, NOPAINT of a hidden flagged window, and NEEDPAINT of a flagged window while the count is 0) was started on installation-05 in **/mnt/build/w16-runs/a5ea-cpl2** (job build/x/j11.txt, out x/out/cc-286*, cc-386*, `.dbg` = W16DBG.LOG). Read its .dbg files to confirm the mechanism. That instrumentation is not in git.
- Next: run `tools/remote-test.sh -n a5ea-cpl -b cplclick` 3 times with the fix (co-tree and co-ms must pass every time).

## Commits

- d59644d Makefile: copy MAIN.CPL into the install tree again (master's cda9698 did the same; merged cleanly).
- ff50a13 USER: hidden windows' pending paints leave the paint count with their flags (**the fix for bug 1, probably bug 2**).
- a32a5f8 CONTROL.EXE: the selected applet's name on COLOR_ACTIVECAPTION in COLOR_CAPTIONTEXT (observed on 3.1 with distinctive WIN.INI colours; it differed from w16 only after a scheme change).
- db33b5e USER icon.c pick(): among images with equal bits, the most colours the display has (DRIVERS.CPL's and SND.CPL's 8- and 16-colour 4-bit images; 3.1 shows the 16-colour one).
- 22602b8 boot-test case `cpclose` (parts us, usw, ctl, tree), tests/scripts/input/cpclose.src → cpclose.txt via the new tools/inputgoto.py, ARCHITECTURE.md 14.4.
- 2b27626 merge of master (boottest.sh case-list conflict resolved: cpclose added).

## Verified (installation-05, run dir a5ea-cpl)

- cpclose run 2 (before db33b5e and before the scenario's step 04 changed): ctl 0 pixels on every shot; tree passed. us/usw failed on: (a) the Drivers/Sound icons, now fixed by db33b5e; (b) step 04, which went back to the default scheme with a click or keys in the dropped list (both behave differently on w16's USER). Step 04 is now "Color, OK with nothing changed", mouse only.
- cpclose run 3 (with these changes, before the merge) was running when this note was written: build/x/rt3.log locally, result dir the newest build/boottest-* in a5ea-cpl.

## Next steps

1. Read rt3's result. If us/usw still differ, look at the shots (`build/x/cmpdirs.sh REF W16` and `build/x/bbox.sh A B` in the worktree's build/x; fetch the shots with rsync from the result dir). Fix the USER cause, or, if it is one of the known USER differences listed in ARCHITECTURE.md 14.4, keep it out of the scenario (never loosen pixel tolerances).
2. Show that the case catches bug 1: revert ff50a13 in the working tree, run `tools/remote-test.sh -n a5ea-cpl -b cpclose@us`, and expect a failure at 02-color-ok or 03-color-cancel. Then restore.
3. Confirm bug 2 from a5ea-cpl2's .dbg logs, then run cplclick (co-tree, co-ms) 3 times; add a line to ARCHITECTURE.md 14.5 or 14.4.
4. Once, at the end: `tools/remote-test.sh -n a5ea-cpl` (the full suite plus make test).

Known USER differences found and not fixed (ARCHITECTURE.md 14.4): an owner-drawn combo box's dropped list prints its items' strings; a dropped list's scroll position with a non-first selection; focus missing on the Color dialog's scheme box when Microsoft's MAIN.CPL reopens it by double click on w16's USER; Alt+C in the Color dialog on the tree moved the focus to the dropped list's window.
