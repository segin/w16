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

## Closing the player (2026-11-18)

Task: a user's report: on w16's install tree alone (`make install-tree`,
booted as C:\W16 with `tests/dropin.sh -T build/w16`, nothing of
Microsoft's; w16's MMSYSTEM, MMTASK, TIMER.DRV and TOOLHELP since
ARCHITECTURE.md 23), QuickTime installed from QTINSTAL.EXE, the sample
movie played in the Movie Player, closing the player ended Windows:
"Standard Mode: Fault in MS-DOS Extender. EC=239C CS=0070 IP=0E58
AX=0001 BX=239F ... DS=0000 ES=0000 SS=0068 SP=7266". Run names on
installation-05: `w16-qtclose` (and `w16-qtclose-base` for master
2f15a87 with the new test); the QuickTime-installed tree kept for quick
runs in `/mnt/build/w16-qtclose-data/inst`.

**Which kernel.** `tests/dropin.sh -T` deletes KRNL386.EXE unless `-3`
is given, so DOSX ran the tree's KRNL286: the user's setup is KRNL286
(Standard mode). The same register dump came back in the first headless
run (SP=7266, SI=0B58, error code a selector, AX=0001 BX=it + 3).

**The cause.** IP 0E58 in w16's DOSX is the POP FS of its call gate's
exit (`nasm -l` listing; `dosx/ring0.inc` gate_exit), and AX=0001,
BX=the selector, DS=ES=0 is KERNEL's INT 31h 0001h (free descriptor;
KERNEL's `call()` zeroes DS and ES). A scratch DOSX recording the ring-3
caller of a 0001h whose FS or GS held the selector: KERNEL:1 0759
(`_dpmi31_host`). A scratch KERNEL ring of every `dpmi_free_desc` (the
selector, FS, GS, the task, the return address), printed in KERNEL's
fault report under Microsoft's DOSX (which faulted the same way, at
005B:0B58, err = the selector, so KERNEL's report ran): the Movie
Player's task (1707) ending, its blocks freed one after another by the
task clean-up (`free_block` and `merge_next` in `kernel/global.c`, the
KRNL286 heap) with FS = 217F all along, the player's last FS; then 217F
itself, and the fault. QuickTime's code leaves one of its blocks in FS
when the player ends.

Bisection (tree alone, KRNL286, one Microsoft file put back): MMSYSTEM +
MMTASK, TIMER.DRV, all three, TOOLHELP, DOSX: all fault; Microsoft's
KRNL386 (`-K`): fine. The tree under w16's KRNL386 (`-3`), w16's DOSX or
Microsoft's: fine (its GlobalFree and task clean-up free through
`global386.c`, and the block was not in FS there). The overlay of the
first part (w16's tree over 3.1, KRNL386): fine; the overlay under w16's
KRNL286: faulted before the fix (as the tree), fine after.

**3.1.** Its KERNEL never gives a freed selector back to the DPMI host:
Free_Sel puts it on KERNEL's own free list (Pietrek ch. 2). A scratch DRP
(`galloc` with a LAR of the freed selector added; Microsoft's KRNL286 and
KRNL386): after GlobalFree the selector's LAR fails on both; KRNL286
leaves it in FS and GS ("kept"), KRNL386 returns 0 (as before). So 3.1
has the same dead selector in FS, but nothing reloads it on its way. On
stock 3.1 with Microsoft's KRNL286 (KRNL386 removed, the reference
install with QuickTime) the player does not even start: "PLAYER caused a
General Protection Fault in module KRNL286.EXE at 0001:78A1", then
QTNOTIFY (KRNL286 0001:37D9) and PROGMAN (GDI 0001:48D4), and Windows
ends; QuickTime needs KRNL386 on 3.1. On stock 3.1 (KRNL386) the close
sequence below lives through every step.

**The fix** (`kernel/dpmi.c` dpmi_free_desc, every KERNEL free goes
through it): on a 386, FS or GS holding the selector being freed is
zeroed before INT 31h 0001h. The one difference from 3.1 it makes:
KRNL286's GlobalFree of a block held in FS returns FS = 0, not the dead
selector. w16's DOSX is left as Microsoft's is (it does not zero them in
0001h either).

**The close sequence now, on w16 as on 3.1 (KRNL386):** after the
movie's end, File > Close (the movie window goes, the empty player
stays), File > Open (the Open box, in the Windows directory, SAMPLE.MOV
listed), SAMPLE.MOV opened again (the black first frame), Space twice
(plays, pauses), File > Exit (Program Manager with the QuickTime group
active, Movie Player selected), Return (the player again, empty), File >
Open SAMPLE.MOV, Space, Alt+F4 (Program Manager, the same screen as after
File > Exit), Alt+F4 and Return (Windows ends; w16 "back to DOS, code
0000"). Tree alone under KRNL286 and KRNL386, the overlay under KRNL286
and KRNL386: the same steps, the same windows.

**Two more differences the sequence showed, both fixed.** With the
crash gone, the close sequence's screens against 3.1's (w16's tree over
3.1, and with only w16's MMSYSTEM, MMTASK and TIMER.DRV over 3.1: all of
those 0 pixels off):
- *The Open box's directory.* The player started by the installer's Play
  Sample opened its Open box in C:\TMP on 3.1, in C:\WINDOWS (C:\W16) on
  w16. SH5 (logs its directory) from Program Manager's File > Run, with
  each of w16's PROGMAN, SHELL and KERNEL in turn: w16's PROGMAN alone
  made the difference. 3.1's File > Run starts a program named by a full
  path in that directory, a bare name in Program Manager's own; the
  installer (C:\TMP\QTINSTAL.EXE) then passes C:\TMP on to the player.
  Fix: `shell/progman/dialogs.c` pm_exec (ShellExecute's directory);
  boot-test part `runassoc@dir`; ARCHITECTURE.md 13.4 (File > Run's
  directory).
- *The reopened movie's place.* File > Close then File > Open of the same
  movie: 3.1 put its window one cascade step (20 pixels here) lower than
  the first, w16 at the same place; a new player's first File > Open
  agreed. `trace=` of PLAYER's and QTIM's USER calls (CreateWindow,
  ShowWindow, MoveWindow, SetWindowPos, SendMessage) with Microsoft's
  USER and w16's: the same calls; PLAYER's WM_MDICREATE asks for the
  default place, and Microsoft's USER on w16's KERNEL gave 3.1's. Probe
  US28 (`tests/apps/us28.c`): 3.1's MDI client counts every child it has
  made (destroyed ones and ones placed by the program too) and puts the
  i-th at i steps modulo the cascade's length; w16 used the number of
  children there. Fix: `user/mdi.c` mdi_create; US28 in boot-test case
  `mdimax`; ARCHITECTURE.md 10.3 (MDI).

Seen, not followed (not the player's): in 3.1's Exit Windows box
(Program Manager, after the player's second run) the QuickTime group
window behind it is drawn empty (no title, no icons); w16's Program
Manager shows it whole.

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
  file out (bisection). On master 89db308 it fails ("the movie did not
  play on w16": the screen froze on the collapsed "sample" window, and
  hangwatch ended Windows); with the fixes it passes (the movie's area
  134x176 at the same place, its last frame 0 pixels off, about 12 s on
  both).
- Since 2026-11-18 every `tests/qtplay.sh` run goes on after the movie's
  end with the close sequence (`tests/scripts/input/qtplay.txt`; the
  script's @WINDIR@ is each side's Windows directory) and checks on both
  sides: no fault or hangwatch report, Program Manager back the same after
  File > Exit and after Alt+F4, Windows ended by Exit Windows (WIN's
  WINEXIT or w16's "back to DOS, code 0000"), and the movie's window
  opened again at 3.1's place. `-T 286|386` runs the w16 side on the
  install tree alone as C:\W16 (QuickTime installed into it the same
  way). Boot test: `quicktime@over` (as before), `quicktime@tree`
  (KRNL286: the user's setup), `quicktime@tree386`; the close sequence
  also runs in `timer@qt`, `mmsystem@qt`, `toolhelp@qt`. On master
  2f15a87 with the new test (run `w16-qtclose-base`): `quicktime@tree`
  fails with "Standard Mode: Fault in MS-DOS Extender." on the w16 side.
- `tests/drv/drun.sh` (boot-test case `drivers`): DRP's log on
  Microsoft's USER and on w16's, plain, with DRT.DLL a boot driver, and
  under KRNL386: identical.

## The full run (installation-05, 2026-11-17)

`tools/remote-test.sh -n w16-qt -e W16_QTDIR=/mnt/build/w16-qtfiles`:
118 boot-test cases ok, `quicktime` and `drivers` among them; zipsetup
skipped (no 7-Zip installer there). Four failures, none from this work:

- taskman (part tree, 21-dosesc, 974 pixels), ddeml (conn, w16-ms) and
  lzexpand (quirks) fail the same way on master 89db308 (rerun there);
- soundtime (one 220 Hz tone 11.2 ms longer than its twin, the limit
  9.3) fails in this checkout every time and passes in master's, although
  every file on the drive is the same in both (Microsoft's KRNL286, USER
  and MMSYSTEM, the identical w16 MMSOUND.DRV and SN1.EXE); the two
  sides' tone lengths shift by a few ms between the checkouts, the
  Microsoft side too. Left for whoever owns the sound tests.

make test failed in that run on TK1, TK17 and TK58 (programs under KERNEL
alone kept GMEM_FIXED fixed: no Windows boot ever "ended"); fixed
(18d74a0), and make test then passed, with the KRNL386 boot cases, tree,
drivers and quicktime run again: all ok.

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
