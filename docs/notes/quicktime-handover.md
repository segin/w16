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

**3.1** (first look, corrected below). A scratch DRP (`galloc` with a
LAR of the freed selector added; Microsoft's KRNL286 and KRNL386): after
GlobalFree the selector's LAR fails on both; KRNL286 leaves it in FS and
GS ("kept"), KRNL386 returns 0 (as before). On stock 3.1 with
Microsoft's KRNL286 (KRNL386 removed, the reference install with
QuickTime) the player does not even start: "PLAYER caused a General
Protection Fault in module KRNL286.EXE at 0001:78A1", then QTNOTIFY
(KRNL286 0001:37D9) and PROGMAN (GDI 0001:48D4), and Windows ends;
QuickTime needs KRNL386 on 3.1. On stock 3.1 (KRNL386) the close
sequence below lives through every step.

**The first fix** (since replaced): `kernel/dpmi.c` dpmi_free_desc zeroed
FS or GS holding the selector being freed, before INT 31h 0001h. That
left FS = 0 after KRNL286's GlobalFree of a block held in FS, where 3.1
leaves the dead selector.

**What 3.1 really does with a freed selector** (task 2026-11-18, run
names `w16-gfree`; probe TK61, `tests/apps/tk61.c`, in `make test`;
ARCHITECTURE.md 9.2 "Freed selectors" has the full rules):
- KRNL386 (both modes) keeps freed selectors on its own list (Pietrek's
  Free_Sel), never gives them back to the host, and zeroes FS and GS in
  GlobalFree and before a task's blocks go.
- KRNL286 has no list of its own: it gives every freed selector back to
  DOSX at once (INT 31h 0001h) and leaves FS and GS as they were.
- Microsoft's DOSX faults reloading a freed selector left in FS at the
  next timer tick or INT 21h ("Fault in MS-DOS Extender", 005B:0B58);
  WIN386 loads 0. So 3.1's KRNL286 ends Windows the same way when a task
  ends with one of its blocks in FS (TK61's helper task did), and only
  KRNL386 lives through what the Movie Player does. The premise of the
  first fix ("3.1 never hands it to the host, so nothing reloads it")
  holds for KRNL386 only.

**The fix now.** w16's KERNEL does what 3.1's does: KRNL386's free list
(`kernel/dpmi.c`, through the LDT's selector from INT 2Fh 168Ah), FS and
GS zeroed by KRNL386 where 3.1 zeroes them (`kentry.asm` AFTER_FREE,
`kernel/task.c` task_exit_c), KRNL286 giving selectors back in 3.1's
order and leaving FS alone; the first fix is gone. The Movie Player
closes on the tree under KRNL286 because w16's DOS extenders no longer
end Windows on a freed selector in FS or GS: w16's DOSX keeps it in the
register across INT 31h (as Microsoft's DOSX) and loads 0 where it
cannot reload it (as WIN386; Microsoft's DOSX faults there),
`dosx/ring0.inc` fsgs_restore; w16's WIN386 loads 0
(`win386/vmm/vmmasm.asm` fixsel). With Microsoft's DOSX, w16's KRNL286
ends Windows there as 3.1's KRNL286 does.

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

## Dragging the movie (2026-11-19)

Task: a user's report (a hand session on w16's install tree alone, master
4c18391, Standard mode, plain DOSBox). The user installed QuickTime,
played the sample movie and dragged the movie's MDI window by its caption
while it played. The move frame "just dances in place" and the whole UI
hangs. Run names on installation-05: `w16-qtdrag` (scratch:
`/mnt/build/w16-runs/w16-qtdrag-data`, with the QuickTime-installed tree
of `/mnt/build/w16-qtclose-data/inst` and the checkout's build over it).

**Reproduced** under both of the tree's kernels, with the movie playing
and also with it stopped. On the press the frame appears around the
window and never moves, though the pointer does. `[w16] hangwatch=30`
ended Windows with every timer sample in DISPLAY:2: the frame being
drawn. Mouse moves need 1-pixel `glide` steps there, or Windows'
acceleration takes the pointer elsewhere.

**The cause.** A scratch USER logged the messages its tracking loop got:
nothing but message C3D6h (a registered message), hwnd 0, a new lParam
each time. `trace=USER.110,USER.116` showed QTIM:7 calling PostAppMessage
(the player's task, C3D6h) again and again, between PeekMessage(0, C3D6h,
C3D6h, PM_REMOVE | PM_NOYIELD) calls of its own. `trace=USER.291`:
QTIM:7 sets a system-wide WH_GETMESSAGE hook, and QTIM:10 a
WH_CALLWNDPROC hook. So every message the player's task reads makes QTIM
take its message out and post it again, and the player's queue always
holds a posted message. w16's loop (`user/track.c`) was a GetMessage of
everything. GetMessage takes posted messages before input, so the mouse
never got in. Each C3D6h was "dispatched" with the frame erased and drawn
again around it, which made the dance, and the queue never ran dry, so
the task never yielded. A second fault in the same loop hit every
program: it never dispatched WM_PAINT, which then stayed in the queue,
so GetMessage always returned at once and no other task ran during any
drag (US31 below: the other task got no ticks).

**3.1** (probe US31, `tests/apps/us31.c`, as the shell under real mouse
input; ARCHITECTURE.md 10.3 "The move and size loop"): the drag's loop
takes only mouse and keyboard messages. No WM_TIMER, WM_PAINT or posted
message reaches the owner during the drag, the WH_GETMESSAGE hook sees
only WM_MOUSEMOVE and the release, and other tasks keep running. With
QTIM's pattern copied into a hook (US31's second drag), the drag works on
3.1. The window gets messages 231h and 232h around the tracking. While
the frame shows, nothing drawn to a window appears on 3.1. In the Movie
Player the picture stays as it was at the press, the movie's time goes on,
and after the drop it shows the frame due then.

**The fix** (`user/track.c` next_input): the loop takes mouse messages,
then keyboard messages (PeekMessage with PM_REMOVE | PM_NOYIELD for each),
and otherwise waits for input or a sent message (WaitEvent with the wake
mask QS_MOUSE | QS_KEY | QS_SENDMESSAGE, or OldYield while another task's
input is pending). Nothing else is dispatched, so QTIM's hook runs once
per mouse message, as on 3.1. 231h is sent after WM_GETMINMAXINFO, and
232h at the end. The cause is in track.c, not in `msg.c` or `timer.c`.

**After the fix** (both kernels): the frame follows the pointer, the
window drops where 3.1 drops it, the picture holds still during the drag
as on 3.1 (the player's task, which draws it, is in the drag), and the
movie plays on to its last frame at the new place.

**Left over** (ARCHITECTURE.md 10.3):
- 3.1 shows no drawing at all during a drag; w16 lets other tasks draw.
- w16's SetWindowPos sends WM_NCCALCSIZE for a move alone.
- After Esc, 3.1 sends WM_WINDOWPOSCHANGING alone.

The last two are in `user/pos.c`.

## The case with SB16.DRV the default (2026-11-20)

Task: after master f265d2a put SB16.DRV in the tree's SYSTEM.INI,
`quicktime` failed in all four parts, twice, at load average about 32:
"w16 did not get to the Movie Player", hangwatch ending Windows with
every sample in QTINSTAL and LZEXPAND. Run names on installation-05:
`w16-qtsb16` (measurements), `w16-qtsb16-v1`, `w16-qtsb16-drag`
(the old drag loop), `w16-qtsb16-p` (the pass record), `w16-qtsb16-f`
(the final run). Host load came from busy shell loops beside the runs
(`sh -c 'while :; do :; done'` under `timeout`, killed by PID).

**Three causes, one of them SB16.DRV's:**

- **hangwatch=30 was too tight, with or without SB16.DRV.** The
  installer unpacks its files ("Installing Files") without yielding:
  no task switch, no idle pass, so no hangwatch progress. KERNEL now
  prints the longest stretch without progress as Windows ends
  (`exitmsg`). Measured: 15-21 s on an idle host, 25.6-28.1 s at
  load average 28-45, 36-40 s at 47, 43-46 s at 55-57; the same with
  SB16.DRV (tree, 27.1 s) and without it (tree with its `[drivers]`
  wave/midi/aux removed, 28.1 s; over, 27.1 s). The part `over` never
  loads SB16.DRV (3.1's SYSTEM.INI: `[drivers]` has only timer and
  midimapper), yet failed the same way. And the same failure is on
  record before SB16.DRV was the default: `w16-qtdrag`
  boottest-025837 (2026-11-20 03:52), all three parts, the tree's
  `[drivers]` timer alone. DOSBox runs cycles=max, so a loaded host
  gives fewer instructions per emulated second, and the RTC-timed
  hangwatch sees the same unpacking take longer.
- **SB16.DRV changed what QuickTime shows**, a correct difference that
  the test compared against a 3.1 without a sound device. On the tree
  (SB16.DRV, DOSBox's SB16) the Movie Player's speaker button is live,
  and its raised top edge is in the last row of the dragged movie's
  picture (3 pixels: the tree parts failed this once hangwatch no
  longer stopped them first). QuickTime's Control Panel gets Audio
  and MIDI tabs before Files, so qtcpl's click on Files opened Audio
  (part cpl failed in every run since f265d2a).
- **At load average 49 the installer's progress bar held still** for
  more than the script's 5 s settle while it wrote SAMPLE.MOV; Return
  then hit Cancel ("Exit without installing?").

SB16.DRV itself costs nothing measurable here: no DSP probing or
interrupt load shows in the stretch, which is the same without it.

**The fix, in the tests:**

- `tests/qtplay.sh`, `tests/qtcpl.sh`: hangwatch 120
  (`$QTPLAY_HANGWATCH`), over 2.5 times the longest stretch seen (46 s
  at load average 57); qtplay prints it ("hangwatch 120 s; longest
  stretch without progress ...").
- With the tree alone and SB16.DRV in its SYSTEM.INI, the stock 3.1 run
  gets Windows 3.1's SNDBLST2.DRV (from `.tools/ref/win31/src`,
  `tools/szdd.py`, port 220h, IRQ 7: DOSBox 0.74's card), so both sides
  have a sound device: speaker buttons, the tabs and the movie's sound
  alike. qtcpl's script clicks Files where it then is (`@TABX@`,
  `@ARROWX@`).
- `qtplay.txt`, `qtcpl.txt`: after Complete Install, wait for the
  progress bar's first frame (186,262 red), then for it to go, then the
  settle as before.

**Results:**

- Before (126d98f, hangwatch 30 left in): over, tree and tree386 at
  load 28-45 had stretches of 27-28 s, a hair under the limit; idle,
  tree and tree386 failed on the speaker button (3 pixels) and cpl on
  the Audio tab.
- The pass record (`w16-qtsb16-p`, hangwatch 90, five full `quicktime`
  runs in a row, 4 parts each): 20 of 20 parts ok; runs 1-2 at load
  average 5-10 (stretches 17.5-21.1 s), runs 3-5 beside 30 busy loops
  at load 47-57 (36.1-45.7 s: every one of them over the old 30). The
  limit then went to 120 for more room; the final run with it
  (`w16-qtsb16-f`): 4 of 4 ok, make test passes.
- Still catches the drag hang: with user/track.c's loop put back as it
  was before 75fa907 (GetMessage of everything, other messages
  dispatched), parts over and tree both fail at hangwatch 90 and at
  120: "the movie's window dragged while playing" (the window never
  moved) and hangwatch's report in the close sequence, its samples in
  DISPLAY:2 (the frame drawn again and again), as on 2026-11-19. The
  frame's redrawing keeps the screen changing, so the script's settles
  run to their limits and hangwatch ends Windows before the script
  ends. (The longest stretch printed then is the longest one before
  the hang: the fatal one never made progress.)

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
- Since 2026-11-19 the close sequence also drags the movie while it
  plays. After c4-paused, Space plays it on, and the left button is
  pressed on the movie's caption. With the button held, the arrow keys move
  the window: 3.1 and w16 both move the pointer 9 pixels per key during a
  drag. The window is then dropped and the movie plays to its end
  (c4a-dragging, c4b-dropped).
  - Arrow keys instead of a mouse glide: under a loaded host, DOSBox merged
    the 1-pixel xdotool motions, and the drop moved 156 or 160 pixels
    instead of 120.
  - Checked on both sides: the window's old and new places (c4-paused to
    c4b-dropped, the pointer's columns left out) are the same rectangle,
    and the picture inside the dropped window is identical.
  - On master the w16 side hangs there: hangwatch reports, and the shots
    stop at c4a.
  - Boot-test case `trackmove` runs probe US31 on 3.1 and on w16's USER and
    GDI, and compares its OBS lines.
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
  control without ES_AUTOHSCROLL). 2026-11-20 (w16's tree alone, master
  2fe5dbd): the background is gray now, as 3.1's; the licence text is
  still not wrapped (w16's multi-line edit control has no word wrap,
  user/edit.c "Not done") and looks drawn in the System font where 3.1 draws
  it in MS Sans Serif (not looked into);
- the banner bitmap behind the boxes is black. Fixed 2026-11-20: it is an
  RLE4 DIB (487 x 74, DIB_RGB_COLORS, no palette) given to CreateDIBitmap
  and BitBlt'ed from a memory DC; w16's VGA.DRV refused RLE in
  DeviceBitmapBits, so the new bitmap stayed black. The driver decodes
  RLE now (drivers/vga/dib.c, probe GD7, ARCHITECTURE.md 11.4); the banner
  is 3.1's pixel for pixel (tests/qtplay.sh checks it in 02-begin);
- the default push button has no bold frame in the licence box. 2026-11-20:
  it has one now; the push buttons' text (23-pixel buttons) is drawn one
  pixel lower than 3.1's (user/button.c, not changed here);
- in the movie controller under the last frame, 37 pixels differ (stray
  dots by the slider's thumb; the frame itself is identical);
- the caption buttons and the pointer are w16's VGA.DRV art
  (ARCHITECTURE.md 6.8);
- with Microsoft's USER on w16's KRNL386 and VGA.DRV (`tests/qtplay.sh
  -m "USER.EXE VGA.DRV"`, not a setup anyone uses) the installer's "Play
  Sample" did not start the player; started from File > Run it plays.
  Not looked into.
