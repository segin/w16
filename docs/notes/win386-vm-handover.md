# Win32s on w16's WIN386: virtual memory and W32S.386 (handover)

Branches `w16-vm` (first agent), `w16-vm2` (second), `w16-vm3` (third:
w16-vm2 with master merged, the fixes below) and a fourth (W32S.386
observed, then stopped). Spec: ARCHITECTURE.md 9.8
(PE files and W32SYS.DLL), 12.17 (paging), 12.18 (W32S.386), 20.2 (VER
and Win32 files).

## Done

- **Paging file and demand paging** (w16-vm, w16-vm2; `win386/ldr/swap.c`, `win386/vmm/pager.c`): temporary WIN386.SWP or permanent 386SPART.PAR, demand-zero client memory, clock eviction, DPMI 0500h/0600h/0601h with 3.1's figures, PageSwap 0007h; paging through the loader's transfer buffer, four pages per DOS call (TK63 at 128 MB ends).
- **KERNEL ExecPE** (w16-vm2): WinExec of a PE file calls W32SYS.DLL's ordinal 3 (TK80, case `pestart`).
- **VER.DLL and Win32 files** (w16-vm3, 20.2): 3.1's VER.DLL reads a PE file's resources through Win32s's W32SYS.DLL (exports 2 IsPEFormat, 7 GetPEResourceTable, 6 LoadPEResource; observed with the VER probe on PE files made by `tools/mkpe.py`). w16's VER does the same; boot-test `ver@w32s` compares the logs (W32SYS.DLL from Win32s's disk 3). W32SYS no longer says "At least one system component is out of date": WIN32S.EXE starts.
- **KERNEL registers Win32s relies on** (w16-vm3, TK43 on 3.1): FarSetOwner keeps AX, BX, CX, DX, ES (WIN32S16 passes the DX it had, GlobalDOSAlloc's segment, to W32S.386); KRNL386's GlobalUnlock returns the handle in DX.
- **Debugging aids** (w16-vm3): `[w16] trace=` now also covers procedures found with GetProcAddress; `[w16] vxdlog=1` logs every call through a VxD API entry (INT 2Fh 1684h) with 32-bit registers before/after (`kernel/vxlog.asm`). Earlier: `vmmtrace=3/4` (W32S.386's Device_Init watched/stepped under w16's WIN386).

## W32S.386 on w16's WIN386: stopped again, after observing it (12.18)

Fourth round (run directory `w16-w32s`), under the decision that w16 may carry
the minimal bytes W32S.386 checks for, learnt at run time:
- Tools: VXY.386 (`tests/vxd/vxy.c`: steps the next device's Device_Init
  under Microsoft's WIN386; `[386Enh] VXYHook=`, `VXYWatch=`/`VXYWatchLen=`/
  `VXYQuiet=` for what happens later) and w16's oracle (`[w16]
  vmmtrace=5`, `w32soracle=`, `w32sprefix=`; the loader's interop page at
  80001000h, `W386_MARK_OFF`).
- Learnt: the 56h search fails on 3.1 and WfW alike (not needed); the C1h
  search compares 13 bytes and reads two operands (the arena base and a
  table of 16-byte per-page records); the 87h search compares 8 bytes and
  reads one operand, a table whose entry 0 W32S.386 replaces.
- What they are: entry 0 is the VMM's page-fault pager for the arena
  (an undocumented register contract both ways); W32S.386 reads and
  writes all four fields of the per-page records (links, page table
  entries, sentinels).
- Reported as more than a marker; the user then chose to go on (fifth
  round, same branch, master merged at 4131ffb, which dropped the taskman
  case): carry both markers in the interop page, keep per-page state where
  W32S.386 edits it, call w16's own not-present handler through a table
  whose entry 0 W32S.386 replaces. Not built yet: 12.18 "Open before
  building it" lists what must be observed first (whether [B]..[B+4] is one
  record table or a pool with page tables and free lists in it; the T[0]
  contract's values; where Win32s's memory must sit given w16's window at
  80000000h). Next: VXY with VXYWatch over [B]..[B+4] logging the value
  after each write too, and with no 6-per-field cap, on 3.1 and WfW; then
  the design.

Hand runs on installation-05, `/mnt/build/w16-runs/w16-w32s` (synced by
hand): `build/vxyrun.sh NAME REFDIR [dropin args]` (VXY on a reference
C:, COM1 to `build/NAME.com1`), `build/fcms.sh NAME REFDIR [args]` (the
same with Freecell started by WIN.INI `run=`), `build/w32boot.sh NAME
[args]` (the treems tree after Setup, `build/wt`, with this build's
WIN386).

## Open

1. **Freecell on w16's tree with Microsoft's WIN386** (`treems`): with the VER and FarSetOwner fixes WIN32S.EXE starts, WIN32S16 calls W32S.386's API 00h, 16h (twice), 1Ah (now with 3.1's register values: EBX = GlobalDOSAlloc's seg:sel, EDX = the segment), allocates its blocks (GlobalAlloc 55F0h moveable, GlobalFix, AllocSelector + PrestoChangoSelector: a code alias; two 2042h blocks), and a little later Microsoft's VMM faults on its own stack (DOSBox "Illegal write to 80005ffc..80005c40, CS:IP 28:80006ec4", descending: exception frames pushed onto an unmapped VMM stack; with `vxdlog=1` it ends as "Reboot requested" instead). The 3.1 KERNEL does not crash, so something else w16's KERNEL hands back differs. Suspects not yet checked: the code alias's descriptor (PrestoChangoSelector/AllocSelector attributes: D bit, limit), GlobalFix'ed block's linear address/locking (W32S.386 may page-lock it), registers of other exports WIN32S16 calls (InitTask, GlobalDOSAlloc, GlobalFix, AllocSelector, PrestoChangoSelector: extend TK43's way of checking each), and whatever WIN32S.EXE does after InitTask returns (trace=WIN32S16.* tracefrom=W32SXXXX).
   Hand runs on installation-05: `/mnt/build/w16-runs/w16-vm3h` (synced by hand), `sh build/fc/go.sh NAME -f build/dos/VER.DLL=W16/SYSTEM/VER.DLL -f build/dos/KRNL386.EXE=W16/SYSTEM/KRNL386.EXE [-i w16:vxdlog=1 -i w16:int21log=1 -i w16:trace=KERNEL.* -i w16:tracefrom=WIN32S16 -i w16:traceret=1]` boots `build/fc.t` (the treems tree after Setup) and runs Freecell; logs in `build/fc/NAME/work/c/W16DBG.LOG`, `build/fc/NAME/dosbox.log`.
2. **taskman tree** (the merge blocker of w16-vm2): not a branch regression. Ten runs of `taskman@tree` on the branch failed 5 times and four on master failed twice, all in the same way: `21-dosesc` after Ctrl+Esc from the full-screen DOS box under KRNL386 + w16's WIN386, the DOS box's icon missing (974,869 pixels). The 39-pixel `10-cascade` difference of w16-vm2 did not come back in those ten. The 21-dosesc flake is master's and needs its own look (the VMM trace goes to COM1 only in DOSBox-X, whose screens do not compare with the 0.74 references).
3. The `win32s` case's Freecell lines are still "open" lines (Freecell does not run yet anywhere on w16's KERNEL). Open 1 was not worked on in the fourth or fifth round.

## Test state (2026-11-27, w16-vm3)

installation-05, `tools/remote-test.sh -n w16-vm3` at "Docs: 12.18 ...": make test passes; the full boot test 192 ok, 4 failures, none the branch's: toolhelp tree286 (`m`: the W16SANS... font modules; fails on master the same), lzexpand quirks (fails on master the same), holdbtn and trails@timednl (timing under the host's load: both pass rerun, on the branch and on master). The targeted run before it: kernel k386 pestart win32s vmprobe bigmem taskman holdbtn vdmad ver (with w32s) msvc msvccl win386dpmi win386 win386dos win386two win386tree all ok (taskman too, this time; see Open 2).

## Test state (2026-11-27, fourth round)

installation-05, `tools/remote-test.sh -n w16-w32s-t` at "Docs: 12.18 what
W32S.386 checks ...": make test passes; the full boot test 193 ok, 4
failures: lzexpand quirks (master's), holdbtn and trails@timed (both pass
rerun), taskman tree `21-dosesc` (974,869 pixels, the same as Open 2:
failed again on the rerun; master's flake, not looked at again here).
