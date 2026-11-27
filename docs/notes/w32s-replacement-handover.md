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
  W32SReplace=0; the VMM's w32s_link): functions 00, 01, 02, 04, 06, 08,
  0A, 16, 1A, 1D so far; the LDT made user-accessible at function 00.

Open (next steps, in order):
1. Freecell on 3.1 with w16's KRNL386 (Microsoft's WIN386) still ends:
   a 16-bit function reached through a Win32s thunk runs on a 16-bit
   alias of the Win32 stack (SS base 881B8000h) and its SP wraps past 0
   (7DB8h at the first thunk, F698h when it faults in an uncommitted page
   above the stack's top): something under w16's KERNEL uses more than
   32 KB of stack there, or the thunk's SP is set from a value w16's
   KERNEL gives differently. Not found yet which module 0557h is.
2. w16's W32S on w16's tree: Win32s's client calls 00, 16, 16, 1A, then
   WIN32S16 read its LDT through the flat selector (fault: w16's LDT was
   supervisor-only; now made user-accessible). Next: 1D and on.
3. The exception contract (functions 09 = VirtualQuery, 14/15 the
   faulting context, 0B, 07, 05 at the process's end) and the fault
   routing to W32SKRNL (Hook_PM_Fault); W32M's exception section hangs
   on 3.1 itself (to look at: my handler or Win32s).
4. Functions 0D, 0F, 11, 12, 13, 1F (file I/O through DOS and file
   mappings) not implemented.
