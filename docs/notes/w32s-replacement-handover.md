# w16's own W32S.386 (handover)

Branch: the agent worktree branched from `w16-w32s2` with master merged.
Spec: ARCHITECTURE.md 12.18 (Microsoft's W32S.386 on w16's VMM: stopped),
12.20 (W32S.386's device API, observed, and w16's replacement).

## Roadmap (the user's)

1. Microsoft's W32S.386 on w16's VMM: stopped (12.18).
2. **This branch:** w16's own W32S.386 replacement.
3. Later: w16's own Win32s.

## Tools

- VXY.386 (`tests/vxd/vxy.c`) gained, for Windows 3.1 with Microsoft's
  W32S.386: `VXYApi=1` (Hook_Device_PM_API on the next device: every
  call with the client's registers in and out, the descriptors of the
  selectors involved, dumps at the registers' pointers), `VXYApiPte=211`
  (page table changes at each call), `VXYSvc=2` (every VMM service and
  PageSwap's, logged when called from VXYSvcLo..VXYSvcHi), `VXYFaults=1`
  (Hook_PM_Fault in front of the others; `VXYFaultsNP=1` adds demand
  paging's faults).
- Hand runs on installation-05: `/mnt/build/w16-runs/w16-w32srepl`
  (synced by hand: rsync, then make), `build/vx6.sh NAME 31|wfw [dropin
  args]` (Freecell on the Win32s reference with VXY; `R=` empty runs w16's
  KRNL386, default Microsoft's; `T=` seconds). Logs `build/NAME.com1`.

## State

Done:
- VXY's API/fault/PTE/service logging (committed). Logs of Freecell on 3.1
  (all Microsoft) and on 3.1 with w16's KRNL386 were the basis of the API
  map (12.20).
- KERNEL fixes found that way (committed, "KERNEL: the PDB's first FCB
  ..."): the PDB's 5Ch-6Bh is LoadModule's 16-byte lpCmdShow block (W32SKRNL
  loads PDB 62h as a selector); GetExePtr's registers as 3.1's (ES the
  task: WIN32S16 reads TDB F2h through it).
- w16's W32S module (win386/vxd/w32s.c, w32sddb.asm; W3 table module
  W32S; the loader maps device=...W32S.386 to it unless [w16]
  W32SReplace=0; the VMM's w32s_link): functions 00, 01, 02, 04, 05, 06,
  07, 08, 09, 0A, 0D, 0F, 11, 12, 13, 16, 1A, 1D (0B, 14, 15, 1F answered
  without effect); page faults on reserved pages commit them (a stack's
  next page; any page of a reservation made with access); the LDT and GDT
  user-accessible at 00; file mappings' DOS calls in the task's PSP.
- The interop page / oracle (e0d9eb8) reverted. Master's memory-services
  audit merged (it took 12.19; W32S is 12.20).

Where Freecell gets on w16's tree (build/w32t, `fct.sh`): WIN32S16 and
W32SKRNL start, FREECELL.EXE, CARDS.DLL and Win32s's system DLLs load
(sections read by 02h), relocations and imports run (08h), then the
file-mapping path (0Dh..) and a page fault in CARDS.DLL's code writing to
77E8xxxxh (an unresolved import: NT's bound address) ends Windows via
KERNEL's fault report. Last seen: 0Dh's lseek on the DOS handle it is
given returns 0 (wrong handle or PSP); being traced (t13: the handle and
the task's DOS opens with int21log).

Open (next steps, in order):
1. The file-mapping contract: 0Dh's block [1] is the DOS handle (5 on
   3.1, FREECELL's), the size came from lseek 4202h in the task's PSP;
   on 3.1 the mapped bytes are read when the view is touched (13h only
   marks pages). Check the handle under w16, then the view's contents.
2. Freecell on 3.1 with w16's KRNL386 (Microsoft's WIN386) still ends:
   a 16-bit function reached through a Win32s thunk runs on a 16-bit
   alias of the Win32 stack (SS base 881B8000h) and its SP wraps past 0
   (7DB8h at the first thunk, F698h when it faults in an uncommitted page
   above the stack's top). Not found which module 0557h is.
3. Exceptions: 14h/15h (the faulting context), routing faults in Win32
   code to W32SKRNL (on 3.1 the next call after a fault is 14h); W32M's
   exception section hangs on 3.1 itself.
4. Tests: a boot-test case for the device (W32M's log under both, the
   API's deterministic answers), the win32s case's open lines.
