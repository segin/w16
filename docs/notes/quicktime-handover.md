# QuickTime handover (the Movie Player hang)

Task: a user's report: on a copy of the reference Windows 3.1 with every
file of w16's install tree copied over Microsoft's (INI, GRP and README
files left out; MMSYSTEM, TIMER.DRV, the MCI drivers, TOOLHELP ... stay
Microsoft's), `WIN /S`, QuickTime for Windows 2.1.2 (16-bit) installs,
and playing its sample movie hangs all of Windows. Find why, fix it,
compare with 3.1, add a regression test.

Rules followed: QuickTime and Microsoft's modules observed from outside
only: screens, w16's own fault and watchdog reports (module, segment,
offset), `[w16] trace=` of the imports they call, NE import and export
tables (tools/neimports.py), and probes against 3.1; nothing
disassembled, no Apple or Microsoft file committed. QuickTime's files
live outside the tree ($W16_QTDIR; on installation-05
`/mnt/build/w16-qtfiles`, with the user's prepared overlay as
`overlay-c/` and a stock 3.1 drive with QuickTime installed as
`ref-installed/c/`). All DOSBox work on installation-05 under
`/mnt/build/w16-runs/w16-qt` (and `w16-qt-base`, master 89db308, for
the before/after run).

## What 3.1 does

`tests/qtplay.sh` on stock 3.1 (Microsoft's KRNL386): File > Run
C:\TMP\QTINSTAL.EXE; the licence box (Agree), "Begin Install" (Install),
"Check Existing Versions" (Start), "Complete Install" (Install), copying,
"Success" (Play Sample). Play Sample starts the Movie Player (PLAYER.EXE
SAMPLE.MOV): a "sample" MDI child, 140 x 204, its first frame black, the
movie controller under it. Space plays it: a turning cube over a green
grid, a white flash, then the QuickTime logo, which stays. The last frame
comes in the 6th shot (shots every 2 s): about 12 s, the same under w16
now. Installing takes about two minutes of host time at `cycles=max`.

## The causes

Four bugs, found one under the other.

1. **USER had no installable-driver API** (OpenDriver ... GetNextDriver
   were stubs returning 0, and `[boot] drivers=` was LoadLibrary only).
   MMSYSTEM opens TIMER.DRV from its DRV_LOAD with OpenDriver("timer")
   (US1's boot trace), so on w16's USER it had no timer device.
   - Seen: `[w16] hangwatch` (new, below) reported the processor in
     USER:2/3/4 (PeekMessage's code) and QTIM:7; `trace=` showed
     QTIM:7 calling USER.109 PeekMessage(lpMsg, 0, 0, 0,
     PM_REMOVE | PM_NOYIELD) for ever, nothing else; a temporary log of
     PeekMessage's results: WM_PAINT for the Player's windows every
     time, so the loop never ends, and with PM_NOYIELD no other task
     runs. The "sample" window had collapsed to its caption. USER's log
     had "unimplemented USER ordinal 00FC" (OpenDriver) and "0103"
     (BeginDeferWindowPos).
   - Bisection (`tests/qtplay.sh -m FILE`, or the user's overlay with one
     file put back): with Microsoft's USER the hang went (and the next
     bug showed); Microsoft's KRNL386, GDI, VGA.DRV, SYSTEM.DRV,
     MMSOUND.DRV, MOUSE.DRV or KEYBOARD.DRV alone changed nothing.
   - Fix: `user/driver.c`, everything ARCHITECTURE.md 10.7 lists,
     observed with the new probe DRP (`tests/drv/`); also
     Begin/Defer/EndDeferWindowPos (`user/pos.c`, from the SDK).
2. **KRNL386 gave a DLL's fixed block an even handle.** With drivers
   working, Windows ended at once: DOSBox "Exit to error:
   CPU_SetSegGeneral: Stack segment with invalid privileges"; in
   DOSBox-X (`-p 386`, which raises the #GP) w16's fault report:
   exception 0D at MMSYSTEM:1 0353, error code = a 600h-byte block's
   selector. `trace=` with the new `tracefrom=MMSYSTEM`: MMSYSTEM's only
   allocation is GlobalAlloc(GMEM_SHARE, 600h) (its interrupt stack,
   made at the first timeSetEvent); TIMER's interrupt then calls
   MMSYSTEM's StackEnter, which faults loading that stack. It worked with
   w16's KRNL286 (fixed handle = selector) and with Microsoft's KRNL386.
   DRP's `galloc` section on 3.1's KRNL386: GlobalAlloc(GMEM_FIXED)
   from a *program* gives an even handle (a moveable block, GlobalFlags
   0), from a *DLL* the selector (odd); Pietrek ch. 2 (GbTop) says why:
   once Windows is up, KRNL386 turns GMEM_FIXED into GMEM_MOVEABLE unless
   the calling code belongs to a DLL (or KERNEL). w16 had taken "every
   handle is even" from TK58, a program. Fix: `kernel/global386.c`
   GlobalAlloc. The same bug made w16's KERNEL with Microsoft's USER
   die the same way (the user's setup plus Microsoft's USER).
3. **KRNL386's GlobalFree left FS and GS alone.** Closing the player
   (Alt+F4) then ended Windows with "Standard Mode: Fault in MS-DOS
   Extender" (w16's DOSX and Microsoft's alike), EC = a freed selector,
   in w16's DOSX at the `pop fs` of its interrupt gate's return
   (`nasm -l` listing of dosx/dosx.asm, offset 0E58): QuickTime frees
   blocks it still holds in FS, and an interrupt arriving then brought
   the dead selector back into FS. Bisection: fine with Microsoft's
   KRNL386, faulting with w16's KERNEL under Microsoft's USER. DRP
   (`galloc`): 3.1's KRNL386 GlobalFree returns FS = GS = 0 (even GS =
   DS, not the freed block); KRNL286 keeps them. Fix: `kernel/kentry.asm`
   AFTER_FREE (KRNL386 only).
4. **SetWindowPos built WM_WINDOWPOSCHANGED too late.** The movie's MDI
   window sat 32 pixels right and 4 down of 3.1's place. `trace=` (12
   argument words, a scratch build) of PLAYER's and QTIM's USER calls,
   on Microsoft's USER and w16's: the same calls, except that QTIM's
   MoveWindow(movie window, x, y) ran twice on w16 (24,4, then 56,8). A
   scratch USER logging the messages after it: the player's ShowWindow
   sent the WM_SIZE/WM_MOVE held back since CreateWindow; QuickTime
   answered the WM_MOVE by moving the window (the first MoveWindow);
   then the ShowWindow's own WM_WINDOWPOSCHANGED, built from the place
   at the end, said "moved", DefWindowProc sent a second WM_MOVE, and
   QuickTime moved it again. Fix: `user/pos.c` decides the message's
   contents when the window is placed (ARCHITECTURE.md 10.7).

The reason the user saw a frozen screen rather than a fault: bug 1 came
first. With all four fixed the movie plays under w16 as under 3.1: the
same window in the same place, the same frames, the last one
pixel-identical, the same length.

## Debugging aids added (docs/w16-settings.md)

- `[w16] hangwatch=SECONDS`: the boot watchdog kept running after the
  boot; progress is a task switch or a pass of KERNEL's idle loop. It
  prints the timer samples, the INT 21h ring and the trace ring, as the
  boot watchdog does.
- `[w16] tracefrom=MODULE`: `trace=` keeps only calls from that module's
  code (MMSYSTEM's few calls were pushed out of the 400-entry ring by
  everyone else's GlobalAlloc).
- The fault report now prints the access rights and owner of the
  selector an error code names (`err=18A4 ar=F300 (base ... limit 05FF)`).

## Tests

- `tests/qtplay.sh` (boot-test case `quicktime`): stock 3.1 against the
  user's setup; the installer from Program Manager, Play Sample, Space;
  checks: no hang (hangwatch=30), the movie plays, its last frame and its
  length as on 3.1 (see the script's header for what is compared).
  Needs $W16_QTDIR/QTINSTAL.EXE; skips without it. `-m FILE` leaves w16's
  file out (bisection). On master 89db308 it fails: w16 never reaches
  the Player's screen (hangwatch: no progress, USER and QTIM:7).
- `tests/drv/drun.sh` (boot-test case `drivers`): DRP's log on
  Microsoft's USER and on w16's, plain, with DRT.DLL a boot driver, and
  under KRNL386: identical.

## Left over (not the hang; seen in the same run)

The installer's boxes look different under w16's install tree, all of
them w16 bugs or w16 art, none in the way of installing or playing:

- the dialogs' background is white where 3.1's is gray (the installer
  colours its dialogs; probably WM_CTLCOLOR's brush from a dialog
  procedure), and the licence text is not word-wrapped (a multiline edit
  control without ES_AUTOHSCROLL);
- the banner bitmap behind the boxes is black;
- the default push button has no bold frame in the licence box;
- in the movie controller under the last frame, 37 pixels differ (stray
  dots by the slider's thumb; the frame itself is identical);
- the caption buttons and the pointer are w16's VGA.DRV art
  (ARCHITECTURE.md 6.8);
- with Microsoft's USER on w16's KRNL386 and VGA.DRV (`tests/qtplay.sh
  -m "USER.EXE VGA.DRV"`, not a setup anyone uses) the installer's "Play
  Sample" did not start the player; started from File > Run it plays.
  Not looked into.
