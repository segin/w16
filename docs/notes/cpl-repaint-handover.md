# Control Panel repaint: handover (2026-11-16, finished)

The work is done; ARCHITECTURE.md 14.4 has the results. Kept as a record of the runs.

## Causes and fixes (branch worktree-agent-a5eaa259271e5b931)

- **The Color sample left on the status line** (the user's report) and **cplclick@co-tree 02-palette** (the expanded Color dialog without its controls): USER's paint count. paint_message zeroed the count but left WF_INPAINT on hidden windows. Destroyed later, those windows took counts from windows still waiting to paint, and windows shown again with a stale flag were never counted. Fixed in ff50a13.
- **The desktop's dither phase** changed between repaints: the brush origin left in a cache DC. Fixed in d6c6053.
- **GDI heap leak on 3.1's GDI**: DeleteObject of a USER-private object freed nothing, so the Desktop applet's blink sample used up the heap and USER hung. Fixed in c95bd2c (delete_private).
- CONTROL.EXE's selected-name colours (a32a5f8), icon image choice (db33b5e), the MAIN.CPL install rule (d59644d; master's cda9698 did the same).
- The boot-test case `cpclose` (22602b8, 9ab33cd).

## Verified (installation-05)

- Run directory a5ea-cpl: `cpclose` (us, usw, ctl, tree) and `cplclick` (all parts) pass with every fix (boottest-102251).
- Run directory a5ea-cpl-nofix: cpclose@us with ff50a13 reverted fails at 02-color-ok, with the status line left unpainted.
- Run directory a5ea-cpl2: ad hoc probes. With ff50a13 reverted, cplclick-co on the tree shows the expanded dialog's controls unpainted under both KERNELs. GetHeapSpaces logs show the leak and the fix.

## Not fixed (ARCHITECTURE.md 14.4)

An owner-drawn combo box's dropped list; a dropped list's scroll position; focus in the Color dialog reopened by double click (Microsoft's MAIN.CPL on w16's USER); Alt+C in the Color dialog on the tree; desktop patterns and wallpaper (not drawn by w16's USER).
