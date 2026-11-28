# w16's own W32S.386 (handover)

Branch: `w16-w32s3`, from master at "Notes: w32s-replacement handover, test
state". Spec: ARCHITECTURE.md 12.18 (Microsoft's W32S.386 on w16's VMM:
stopped), 12.20 (W32S.386's device API, observed, and w16's replacement).

## Roadmap (the user's)

1. Microsoft's W32S.386 on w16's VMM: stopped (12.18).
2. **This branch:** w16's own W32S.386 replacement.
3. Later: w16's own Win32s.

## Tools

- VXY.386 (`tests/vxd/vxy.c`) on Windows 3.1 with Microsoft's W32S.386:
  `VXYApi=1` (every PM API call, registers in and out, descriptors, dumps
  at the registers' pointers), `VXYApiDeref=1` (EDX's block's dwords as
  pointers too), `VXYApiDump=100` (256 bytes a dump: a whole CONTEXT),
  `VXYApiPte=211`, `VXYSvc=2`, `VXYFaults=1` (`VXYFaultsNP=1` adds
  demand-paging faults).
- w16's KERNEL: `[w16] trace=MOD.ORD,...` with `tracefrom=MODULE`,
  `traceret=1`, `int21log=1` and `debuglog=C:\W16DBG.LOG` logs a module's
  imported calls with their results. Never `KERNEL.*` from WIN32S16
  (InitTask's registers do not survive the trace thunk); `KERNEL.91`
  excluded, the rest of WIN32S16's KERNEL imports work (the list is in
  `tools/neimports.py --imports WIN32S16 .tools/ref/win31s/cdrive/WINDOWS`).
  K327's note (debuglog) prints the caller's frame and each selector
  argument's base, limit, handle and size.
- Hand runs on installation-05 in `/mnt/build/w16-runs/w16-w32s3` (synced
  by `build/sync.sh` of the worktree: rsync, make, `build/mktree.sh`):
  `build/fct.sh NAME [dropin args]` Freecell on `build/w32t` (this build's
  tree with Win32s's files from the w16-w32s2 run's Setup result) under
  w16's WIN386 (`-p 386 -i w16:vmmtrace=1` for the device's trace on
  COM1, `build/NAME.com1`); W32M: `-f build/W32M.EXE=W32M.EXE -i
  "WIN.INI:windows:run=C:\W32M.EXE"`. `build/vx6.sh NAME 31 [args]`
  Freecell on the Win32s reference with VXY (`VXY=none` without, `R=`
  empty: w16's KRNL386, `RUN=` empty: no Freecell, `T=` seconds).

## State (2026-11-28)

Done on this branch:
- W32S device: base relocations applied to each section it reads (Win32s
  writes none: 04h's record [7] is the delta); the arena at 88000000h, a
  chosen address only inside it, the memory of the system below it; 06h
  and 07h's NT statuses; file mappings as 3.1's (0Dh numbers a mapping,
  11h maps a view by number, 13h/12h unmap by address, 0Fh closes; views
  share the mapping's pages; a file's contents read at 0Dh); 14h (debug
  registers) and 15h (coprocessor state) as 3.1's.
- WIN386: a DOS buffer of a protected-mode client need not be writable
  from ring 3 (Win32s reads a file into PAGE_READONLY memory).
- KERNEL: the default resource handler returns a block holding data
  untouched (TK81). That was the k386 "stack wrap" (GDI went on past a
  K327 for a bitmap whose bits lay past the block).
- Tests: boot-test cases `w32m` (W32M's log on w16's device = 3.1's but
  for addresses) and `rsrchandler` (TK81 on both kernels = 3.1's).

Where Freecell stands: on w16's tree (w16's WIN386 and W32S) and on 3.1
with w16's KRNL386 (Microsoft's WIN386, W32S.386, USER, GDI) Freecell
starts, makes the same 125 device calls as on 3.1, and shows its own box
"Out of memory. Close other applications and try again." Both have w16's
KERNEL in common. Found so far (3.1 with w16's KRNL386, USER/GDI calls
from WIN32S16 traced with `trace=USER.*,GDI.*`, `tracefrom=WIN32S16`; a
debug print in FindResource): WIN32S16 calls USER's LoadBitmap with
hInstance NULL and ids 35h, 43h, 44h (CARDS.DLL's card bitmaps' numbers,
not OBM_ ones); USER looks for them in DISPLAY's bitmaps (8035h ... are not
there) and returns 0 three times, then Freecell's MessageBox. So Win32s
takes CARDS.DLL's Win32 hInstance as NULL (or as no Win32 module) by the
time CARDS loads its bitmaps: find out what of w16's KERNEL makes it so
(what CARDS's DLL entry gets, GetModuleHandle/GetModuleFileName/GetExePtr
answers for Win32s's module, the W32SXXXX module table that Win32s
rewrites at run time: its resource table there holds Freecell's icons at
00BEh/017Eh, which WIN32S.EXE's file does not have). On 3.1 itself the same
three calls should be checked for (a USER-side trace is not possible
under Microsoft's KERNEL; a VXY-free way: TOOLHELP NotifyRegister in a
helper program, or run Freecell with w16's KRNL386 but a probe DLL).

Open (next steps, in order):
1. Freecell's "Out of memory" (above).
2. Exceptions: routing a fault in Win32 code to Win32s (on 3.1 the next
   call after a fault is 14h with the faulting registers in the CONTEXT);
   W32M's exception section hangs on 3.1 itself.
3. K327 (HandleParamError) with an error code: 3.1's makes the API return
   0 to its caller (GDI's Rectangle on a bad DC returns 0); called from an
   application it ends the task silently (TK41, 9.x). w16's returns to the
   caller, so a Microsoft module's validation goes on with the bad
   parameter. The mechanism is still not observed.
4. Committed Win32 pages are backed at once; w16's pager could back them
   (large reservations).
5. Tests: the win32s case's Freecell lines become checks once it runs.

## Test state

See the end of this file once the branch's test runs are done.
