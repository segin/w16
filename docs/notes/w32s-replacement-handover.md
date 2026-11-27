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

Where it stands (12.20 "Where it stands"): on w16's tree with Win32s,
WIN32S16 and W32SKRNL start on w16's device, images and DLLs load, the
NLS files are mapped, then the program's own code faults because its
relocations were not applied (W32M reads 0x403090 = its preferred base
0x400000 + 3090h while loaded at 85040000h Win32; Freecell's CARDS.DLL
writes 77E8xxxxh). 0Dh's DOS handle is block [6] ([1] only happened to
equal it on 3.1).

Hand runs: `build/fct.sh NAME -p 386 -i w16:vmmtrace=1` (Freecell from
WIN.INI run=; DOSBox-X drops letters typed into File > Run), W32M:
`... -f build/W32M.EXE=W32M.EXE -i "WIN.INI:windows:run=C:\W32M.EXE"`;
`build/mktree.sh` makes build/w32t (this build's tree + Win32s's files
from the w16-w32s2 run's fc.t); copy a new WIN386.EXE/KRNL386.EXE into
build/w32t/SYSTEM after a sync. 3.1 reference of W32M: run `vx6.sh` with
VXY=none (log in the handover's run dir, build/m31r/c/W32M.LOG).

Open (next steps, in order):
1. Relocations: find what W32SKRNL decides from 06h at an image's
   preferred base (linear 3F0000h: refused, below 4 MB) and whether 3.1
   gave it; VXY's 06h calls with VXYApiDeref on W32M under 3.1 show what
   came back. Then check the mapped NLS views' contents.
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

## Test state (2026-11-27, at "Tests: win32s's summary line ...")

installation-05, `tools/remote-test.sh -n w16-w32srepl-t`: make test passes;
full boot test 196 ok, 1 failure: lzexpand quirks (fails on master the
same). Targeted run before it (win32s pestart vmprobe ver bigmem holdbtn
vdmad toolhelp win386 win386dos win386two win386tree msvc msvccl
win386dpmi kernel k386): all ok but holdbtn (timing; ok on rerun).
win32s: k386, treems and tree ok (tree: Setup installs and Windows
restarts with w16's W32S); Freecell's lines still open in all three.
