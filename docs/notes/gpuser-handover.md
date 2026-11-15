# USER's `__GP`: handover notes

The task: w16's USER exports `__GP` (USER.484, resident, a fixed code
segment, as 3.1's) and, for the USER functions that survive a bad pointer
on Windows 3.1, does what 3.1 does. Everything below was observed black
box with probe US27; Microsoft's USER, KERNEL and GDI were never
disassembled, and 3.1's `__GP` table was never read (HasGPHandler is only
asked, by the probe's fault hook, about the address a fault happened at,
to decide whether 3.1 would have ended the task; its answer is not
logged).

## What is where

- `user/gp.asm`: the primitives that touch a caller's memory (gp_sel,
  gp_copy, gp_fill, gp_strlen), each with its `__GP` range and recovery
  code, and the table itself (`GPRANGE` markers, assembled into segment
  UGPTAB_TEXT, ending with a 0 selector; exported as `__GP.484`,
  user/ordinals.txt). The table is made from w16's own labels when the
  file is assembled.
- `user/user.h`: their prototypes and `GP_BAD(p)` (the selector does not
  load: freed, not present).
- The USER functions (rect.c, win.c, paint.c, str.c, msg.c, input.c,
  class.c, dlg.c, edit.c, menu.c, icon.c, misc.c, pos.c, init.c,
  scroll.c, clip.c, comm.c, accel.c, hook.c, defwnd.c): each comment
  says what 3.1 does with a bad pointer.
- `tools/nemovsel.py` (run on USER.EXE by the Makefile): the stray
  entry 533 was wlink's movable entry for the `mov ax, DGROUP` selector
  fixups of USER's `__loadds` functions (DGROUP is movable); the tool
  links those as internal references by segment number (as 3.1's own
  USER.EXE refers to its movable data segments: relocation type 2,
  segments 44 and 45) and drops the entry. w16's USER now has 3.1's 413
  entries.
- KERNEL (kernel/atom.c, kmisc.c): AddAtom/FindAtom of a name running
  past its segment's end give 0, GetAtomName into a buffer that ends
  first writes what fits and gives 0, lstrcpyn copies up to where 3.1's
  would fault and returns NULL (`ptr_room`). USER's GetClassName,
  GetClipboardFormatName, RegisterClipboardFormat, RegisterWindowMessage
  and DlgDirSelect go through them; on 3.1 those faults are KERNEL's,
  recovered by KERNEL's table.
- Probe US27 (`tests/apps/us27.c`, `us27s.asm`, `us27.rc`), reference
  `tests/apps/us27ref.h`, made by `tests/us27run.sh` (3.1 runs, past the
  cases that end the task), `tools/us27ref.py` (what is checked) and
  `tools/obsref.py`. Boot-test case `gpuser` (4 parts).

## Testing in an emulator

DOSBox checks no segment limits. DOSBox-X's normal core (`dropin.sh -p
386x`) checks them for the string instructions only, not for a MOV, and
neither faults on memory through a null selector (it uses linear 0: the
real-mode interrupt table) nor on a data write to a code segment (the
`full` core checks less). Selector loads (US27's F and P) fault
everywhere. So the reference keeps the F and P cases whole and the N, L,
E and R cases only where 3.1 took a fault in every run
(tools/us27ref.py), and w16's primitives use string instructions and
take a null selector for a fault at the first byte.

## Open

See ARCHITECTURE.md 9.4 and 10.3 (USER's `__GP`): K327, the cases 3.1
ends the task for, what is not checked.

## Results (2026-11-15, installation-05)

- 3.1, US27 through tests/us27run.sh, DOSBox-X -p 386x: two runs on
  Microsoft's KRNL286 and two on KRNL386, the four logs identical in
  every checked value (us27ref.h: 932 values). Four task ends out of the
  hooks' reach on both KERNELs (CreateDialogIndirect NULL, LbAddString
  and CbAddString freed, MapDialogRect at a segment's end): Application
  Errors, no silent end.
- w16 (US27 in check mode, one boot each, no FAIL, no task end): w16's
  USER on Microsoft's KRNL286 and KRNL386, w16's install tree on w16's
  KRNL286 and KRNL386 (boot-test case gpuser, its 4 parts ok).
- tools/remote-test.sh at 50d9f38 (master merged): make test pass; boot
  test 105 ok, 1 failure: holdbtn ("the arrow's repeats are not 200 ms
  then a tick apart") with the host at load 30 from other users. Alone
  it passed 3 times of 3 on this branch and once on master; three
  holdbtn copies run together fail the same way on master (they share
  one output directory), so that is not this work's.
