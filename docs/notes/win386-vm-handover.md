# Win32s on w16's WIN386: virtual memory and W32S.386 (handover)

Branches `w16-vm` (first agent), `w16-vm2` (second), `w16-vm3` (third:
w16-vm2 with master merged, the fixes below), a fourth (W32S.386
observed, then stopped) and `w16-w32s2` (fifth: observed fully, stopped). Spec: ARCHITECTURE.md 9.8
(PE files and W32SYS.DLL), 12.17 (paging), 12.18 (W32S.386), 20.2 (VER
and Win32 files).

## Done

- **Paging file and demand paging** (w16-vm, w16-vm2; `win386/ldr/swap.c`, `win386/vmm/pager.c`): temporary WIN386.SWP or permanent 386SPART.PAR, demand-zero client memory, clock eviction, DPMI 0500h/0600h/0601h with 3.1's figures, PageSwap 0007h; paging through the loader's transfer buffer, four pages per DOS call (TK63 at 128 MB ends).
- **KERNEL ExecPE** (w16-vm2): WinExec of a PE file calls W32SYS.DLL's ordinal 3 (TK80, case `pestart`).
- **VER.DLL and Win32 files** (w16-vm3, 20.2): 3.1's VER.DLL reads a PE file's resources through Win32s's W32SYS.DLL (exports 2 IsPEFormat, 7 GetPEResourceTable, 6 LoadPEResource; observed with the VER probe on PE files made by `tools/mkpe.py`). w16's VER does the same; boot-test `ver@w32s` compares the logs (W32SYS.DLL from Win32s's disk 3). W32SYS no longer says "At least one system component is out of date": WIN32S.EXE starts.
- **KERNEL registers Win32s relies on** (w16-vm3, TK43 on 3.1): FarSetOwner keeps AX, BX, CX, DX, ES (WIN32S16 passes the DX it had, GlobalDOSAlloc's segment, to W32S.386); KRNL386's GlobalUnlock returns the handle in DX.
- **Debugging aids** (w16-vm3): `[w16] trace=` now also covers procedures found with GetProcAddress; `[w16] vxdlog=1` logs every call through a VxD API entry (INT 2Fh 1684h) with 32-bit registers before/after (`kernel/vxlog.asm`). Earlier: `vmmtrace=3/4` (W32S.386's Device_Init watched/stepped under w16's WIN386).

## W32S.386 on w16's WIN386: stopped, after observing it fully (12.18)

Fourth round (run directory `w16-w32s`): the searches and markers learnt
(the 56h search fails on 3.1 and WfW alike; the C1h search compares 13
bytes and reads operands A and B; the 87h search compares 8 bytes and reads
operand T). The user chose to go on, under the rule that w16 may carry the
minimal bytes, expose its own paging bookkeeping in the observed layout and
a fault-table entry with the observed convention, all learnt at run time.

Fifth round (branch `w16-w32s2`, run directory `w16-w32s2`): observed in
full with VXY (now: values before and after each access, no per-field cap,
several ranges, VMM EIPs skippable, memory-service hooks, the DDB chain for
attribution, 115200 baud, and exception handlers that pass V86 and ring-3
faults straight on: the old ones read their flags through a null DS and
stopped Windows, so the fourth round's watch runs never reached Freecell).
Findings (12.18 has them in full):
- [A] = 80400000h, [B] = 80050000h, [B+4] = 8008C000h on 3.1 16 MB: one
  table of 16-byte records for the arena 80400000h-83FFFFFFh. The fourth
  round's 8004D000h/80089000h and its "W32S.386 code at 80281xxxh" were
  wrong: 8028xxxxh is everyone's discardable init code; W32S.386's locked
  object is at 80013E14h-80015D07h (from its DDB and LE object table).
- T is a pager table by page type (PTE bits 9-11). W32S.386 leaves T[0]
  alone and puts its pager in T[2] (80015094h); it marks its pages type 2
  in their PTEs and records (207h -> 403h, 006h -> 402h); its own page
  tables (directory entries 221h, 2BFh, 2C0h...) hold type-2 entries naming
  arena pages, and T[2] maps them (copy-on-write-like aliases).
- Of the records W32S.386 touches only +0 (the PTE copy: read, written)
  and the word at +0Ah (read).
- The VMM calls T[type] for any not-present entry: EAX address, EBX VM,
  ECX page, EDX the PTE's address via the 4 MB physical alias, ESI type*4,
  the PTE on the stack; T[0] returns ESI the record, ECX 6, EAX n<<16|200h;
  T[2] returns ECX the new PTE for its own pages.
- Decision: this is the VMM's paging architecture (typed PTEs with a pager
  per type, one arena for system and client memory with records kept in
  step, the dispatching fault path), not a marker; stopped and reported,
  as the user's rule says. Nothing of it is built; the interop page has no
  marker. If the user decides to build it anyway, 12.18 "What W32S.386
  depends on" is the interface and "Size" what would change.

Hand runs on installation-05, `/mnt/build/w16-runs/w16-w32s2` (synced by
hand with rsync; scripts in its `build/`): `vx2.sh NAME 31|wfw [args]`
(VXY over the records, T[0] hooked, services hooked, Freecell),
`vx3.sh` (the same without the VMM's own EIPs), `vx5.sh` (Freecell with
any VXY, `VXY=none` for none). Logs: `build/NAME.com1`. Analysis: one
line per access, grep by EIP; W32S.386's first object is
80013E14h-80015D07h on 3.1 (80013E28h- on WfW).

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
