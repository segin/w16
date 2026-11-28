# w16's own W32S.386 (handover)

Branch: `w16-w32s4`, from master at "Notes: w32s-replacement handover, test
state" (w16-w32s3 merged). Spec: ARCHITECTURE.md 12.18 (Microsoft's W32S.386 on
w16's VMM: stopped), 12.20 (W32S.386's device API, observed, and w16's
replacement; Win32s's 16-bit module tables).

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
- Hand runs on installation-05 in `/mnt/build/w16-runs/w16-w32s4` (synced
  by `build/sync.sh` of the worktree: rsync, make, `build/mktree.sh`):
  `build/fct.sh NAME [dropin args]` Freecell on `build/w32t` (this build's
  tree with Win32s's files from the w16-w32s2 run's Setup result) under
  w16's WIN386 (`RUNTXT=` an input script in build/fc, `T=` seconds; `-p
  386 -i w16:vmmtrace=1` for the device's trace on COM1,
  `build/NAME.com1`); `build/fc31.sh NAME SCRIPT [args]` the same on the
  Win32s reference (`R=` empty: w16's KRNL386, default Microsoft's).
  Input scripts: build/fc/play3.src (game 1, three cards to the free
  cells, resign; `python3 -I build/fc/gen.py X.src > X.txt` turns "at X
  Y" into pointer moves, with `-i WIN.INI:windows:MouseSpeed=0`).
  `build/pbrun.sh PROBE` a probe as the shell on 3.1, with w16's USER and
  on w16's defaults, logs side by side; `build/tk83run.sh` TK83 on 3.1 and
  w16's KRNL386. W32M: `-f build/W32M.EXE=W32M.EXE -i
  "WIN.INI:windows:run=C:\W32M.EXE"`. `build/vx6.sh NAME 31 [args]`
  Freecell on the Win32s reference with VXY (`VXY=none` without, `R=`
  empty: w16's KRNL386, `RUN=` empty: no Freecell, `T=` seconds).

## State (2026-11-28, w16-w32s4)

Done on w16-w32s3 (merged in master): the W32S device's relocations,
arena, file mappings, 14h/15h; WIN386's DOS buffers; KERNEL's default
resource handler (TK81); boot-test cases w32m and rsrchandler.

Done on this branch:
- KERNEL: Win32s's 16-bit module of a Win32 DLL (a block "NE", its own
  owner, not in the module list, with an 80h-byte instance block it owns)
  is taken by GetExePtr, GetModuleUsage, GetModuleHandle(MAKELP(0, h)),
  GetModuleFileName, GetExpWinVer, DirectResAlloc and the resource calls,
  as on 3.1 (TK83;
  `module_of_exe` in kernel/module.c). That was Freecell's "Out of
  memory": LoadBitmap(CARDS's instance, 35h) found nothing (w16's USER), or
  (Microsoft's USER) took the bitmaps for Windows 2's (GetExpWinVer 0).
- USER: LoadBitmap makes a 1-bit DIB whose colours are not black and
  white a colour bitmap (US40: Freecell's red cards were black);
  WM_NCACTIVATE repaints the caption only, not the menu bar (US41:
  Freecell's "Cards Left" went when a message box came up).
- Tests: rsrchandler runs TK81 and TK83 (k286, k386) and US40 and US41
  (part user); win32s plays Freecell in every part (game 1 dealt, three
  cards to the free cells, three home by themselves, resigned, ended) and
  fails without it.

Where Freecell stands: it plays on w16's tree (w16's WIN386, W32S,
KERNEL, USER, GDI) as on 3.1 and on 3.1 with w16's KRNL386: the screens
of the play script equal 3.1's but for w16's caption buttons and
message-box icon. A game was not played to the end (winning needs a long
script); dealing, moves, the auto-moves home, an illegal move's message
box, resigning and the exit all match.

Open (next steps, in order):
1. Exceptions: routing a fault in Win32 code to Win32s (on 3.1 the next
   call after a fault is 14h with the faulting registers in the CONTEXT;
   what 3.1's W32S.386 does to the client between the fault and that call
   is not observed: VXY's fault hook runs in front of W32S.386's). W32M's
   exception section hangs on 3.1 itself, so it gives no reference yet: a
   Win32 probe whose exception is simpler (an int 3, or a read of address
   0 inside a frame of its own) might.
2. K327 (HandleParamError) with an error code: 3.1's makes the API return
   0 to its caller (GDI's Rectangle on a bad DC returns 0); called from an
   application it ends the task silently (TK41, 9.x). w16's returns to the
   caller. Freecell no longer needs it (the k386 stack wrap was TK81's).
3. Committed Win32 pages are backed at once (3.1 maps a committed page
   on first touch, 12.18); a lazy commit needs a not-present PTE state
   the device's own ring-0 writes (section reads, relocations, mapping
   copies) do not fault on, and w16's pager to page them out.
4. A whole game of Freecell to its win (a long script), Solitaire-style
   drags (Freecell moves by clicks only), and other Win32s programs.

## Test state

2026-11-28, at "Merge master (USER's hqAppExit, TK82)", on installation-05:
- `tools/remote-test.sh -n w16-w32s4-t` with make test and the cases
  win32s w32m rsrchandler appexit pestart vmprobe ver memsvc bigmem
  toolhelp win386 win386dos win386two win386tree msvc msvccl win386dpmi
  kernel k286 k386: make test passes, 21 ok (win32s: Freecell plays in
  k386, treems and tree).
- Full boot test (`-b -n w16-w32s4-f`): 196 ok, 5 failures.
  - lzexpand quirks: fails the same way on master.
  - holdbtn, trails timednl (timing), win386dpmi w16d (no CWX.LOG) and
    drivers k386 (the probe DRP faulted at 005B:0B58 after its GlobalFree
    with GS = DS): all four ok when run again on their own
    (`-b -n w16-w32s4-t drivers win386dpmi holdbtn trails`: 4 ok). The
    drivers fault under the full run's load was not seen again; noted in
    case it comes back.
