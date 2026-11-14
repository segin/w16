# KRNL386: handover notes

w16's KRNL386.EXE is built from the same kernel/ sources as KRNL286.EXE
with `-DKRNL386` (objects in build/kernel386). KRNL286.EXE stays
byte-identical to the build before this work (checked after every step
against a `git archive 43230ae` build). Everything below was observed on
Windows 3.1 with the probes named; nothing of Microsoft's KRNL386 or
KRNL286 was disassembled.

## Export table

`tools/neinfo.py` on both files (NE format facts): the same 237 entries,
names, ordinals, segment numbers, fixed/moveable and flags; only offsets
and the segment lengths differ (KRNL386 seg 1 C571h, seg 2 2AB8h, DGROUP
13D2h). So KRNL386 shares kernel/kernel.lnk. w16's KRNL386 has one code
segment more (HEAP_TEXT: global386.c), which nothing can see.

## Build

- Makefile: `K386OBJS` (KOBJS mapped to build/kernel386 plus k386.o,
  k386c.o, global386.o), `$(DOS)/KRNL386.EXE`, in `dos` and the install
  tree. global.c is empty under -DKRNL386, global386.c empty without it
  (and not linked into KRNL286).
- The C code is 386 code: `wcc -3 -zfp -zgp` (Makefile `K386C`). Plain
  `-3` made the boot fault inside Microsoft's DOSX (GP at 005B:0B5C/0B58,
  the error code an LDT selector) and w16's ("Fault in MS-DOS Extender").
  Found (2026-11-14) by building one file at a time with -3: util.c,
  task.c, loader.c each faulted alone; Watcom's 386 code generator keeps
  selectors in FS (util.c: `mov fs,cx` / `mov dl,fs:[si]`), a selector
  stays there after it is freed, and a 386 DOS extender saving and
  reloading FS around an interrupt faults on it. With FS and GS pegged
  the boot, make test and the boot test pass. Hand-written 386 code is in
  kernel/k386.asm (LSL's 32-bit limit).

## Differences found and implemented (KRNL386 build only)

| What | KRNL286 | KRNL386 (observed) | Probe | Where |
|---|---|---|---|---|
| Global heap layout | arenas 10h below blocks | 20h arenas at 32-bit offsets in the Burgermaster (>64 KB), seltab dwords at its end (THHOOK 18h dword, 16h 8000h), header 80h | TK58 | global386.c |
| GlobalSize | odd multiple of 10h | round up to 20h | TK3, TK12, TK17 | global386.c |
| Handles | fixed = selector | even for fixed and moveable; GlobalDOSAlloc's = selector | TK58 | global386.c |
| GlobalAlloc(FIXED, 0) | 0 | a discarded block (4000h) | TK58 | global386.c |
| GlobalFix / GlobalFlags | moveable only | fixed blocks too, count shown | TK17, TK58 | global386.c |
| GlobalPageLock/Unlock | GlobalFix, returns 1/0 | lock + page-lock counts, returns the page-lock count; w16 also DPMI 0600h/0601h under WIN386 | TK17, TK58 | global386.c |
| Huge fixed ReAlloc | needs GMEM_MOVEABLE | also without | TK17A | global386.c |
| Local heap info block | 24h, "LH" at 22h | 2Ah, dword sentinels, "LH" at 28h | TK53, TK3, TK13 | local.c |
| GetSelectorLimit | reads the descriptor | LSL 32-bit, 0 on failure | TK17 | selector.c, k386.asm |
| PrestoChangoSelector | dst | dst \| 1 | TK17 | selector.c |
| LongPtrAdd | < 1 MB | up to FF0000h ok, 7FFF0000h not (w16: < 16 MB, UNVERIFIED between) | TK17, TK17A | selector.c |
| THHOOK | hGDI/hUSER/shell 24h-28h | 28h/2Ah/2Ch | TK51 | toolhelp.c |
| GetWinFlags (Enhanced) | - | WF_ENHANCED (+WF_PAGING with a paging file in DPMI 0500h), CPU from DPMI 0400h | TK57 | k386c.c, task.c |
| Reboot device | - | 0201h at the first InitTask (handler in KERNEL code, a sanity byte KERNEL keeps at 1) | TK57, VX8 (WIN386 notes) | k386c.c, k386.asm |
| DOS programs (Enhanced) | WINOLDAP.MOD | SYSTEM\WINOA386.MOD (module WINOLDAP), and module GRABBER from [boot] 386grabber (VGA.3GR, usage 2) | TK57 mod | loader.c |

TOOLHELP: Microsoft's TOOLHELP.DLL picks the heap layout by itself; with
the Burgermaster's selector limit forced above 64K on the KRNL286 layout
it stopped reading "LH" at 22h (experiment, not committed): it seems to
go by the Burgermaster. It did not go by the kernel's file name or by
THHOOK's SelTableLen. With global386.c it reads w16's KRNL386 heap as
KRNL386's (TK51, TK53, TK58 equal 3.1's).

## Probes

- TK57 (tests/apps/tk57.c, tk57s.asm; tk57ref.h from `/S mod` and `/3
  mod`): WinFlags, INT 2Fh 1600h/1683h, Reboot 0202h; with tail `mod`,
  COMMAND.COM started and left running, then which modules (WINOLDAP's
  file, GRABBER's file and usage) are loaded; with `woa`, WinExec of a
  DOS program for TK33 copied over WINOLDAP.MOD/WINOA386.MOD. Run as the
  shell: `tests/refrun.sh build/dos/TK57.EXE '/3 mod'`, or
  `tests/dropin.sh -3 -c 'WIN /3 /B mod' -f build/dos/TK57.EXE=TK57.EXE
  -i 'boot:shell=C:\TK57.EXE' -W TK.LOG -o DIR` for w16's.
- TK58 (tests/apps/tk58.c, tk58k.lnk; tk58ref.h from /S and /3): the
  KRNL386 global heap, arena by arena, and TOOLHELP's view; in make test.
- TK53 now has tk53ref.h (Microsoft's KRNL386, dropin.sh -3 -r) and
  tk53ref286.h (KRNL286, dropin.sh -r).

## Harness

- `tests/dropin.sh -3` puts build/dos/KRNL386.EXE in the KRNL386.EXE slot
  (not deleted then; KRNL286.EXE stays Microsoft's unless -k); `-K FILE`
  another file; `-3 -r` keeps Microsoft's KRNL386 (the reference).
  With `-c 'WIN /3 /B'` it boots 386 Enhanced mode (DOSBox) on
  Microsoft's WIN386, with `-f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE`
  on w16's.
- `tests/run-tests.sh`: `RUNTESTS_KERNEL="KRNL286 KRNL386"` (make test's
  default) runs every probe under both kernels and both DPMI hosts.
- boot test: k386, k386all (Standard mode), k386e (Enhanced: w16's
  WIN386 and Microsoft's), k386cad (Ctrl+Alt+Del under Microsoft's WIN386).
- Install tree: SYSTEM\KRNL386.EXE; win/README.TXT updated;
  tests/w16tree.sh boots the tree's own KRNL386.

## Ctrl+Alt+Del with a hung task (2026-11-14)

KERNEL's procedure (k386.asm reboot_proc, k386c.c k386_cad), run by
Microsoft's Reboot device as an interrupt when the sanity byte stayed 0,
calls Reboot 0203h with ES:DI the running task's module file name
("VX8.EXE"), DS:SI the sanity byte, BX 7Dh, CX 0, DX the task
(UNVERIFIED: 3.1 passes "a selector"), and on AX nonzero (Return) ends the
task with INT 21h 4C00h (exit code UNVERIFIED), else IRETs (Escape).
Boot-test case k386hung (VX8 mode 2, tests/scripts/input/vx8hung.txt,
Microsoft's WIN386): every screen equals Microsoft's KRNL386's, VX8 is
closed and Windows back after Return. Under w16's WIN386 the procedure is
not called yet: its REBOOT runs the sanity check only with SANITY_CHECK 1
(win386/vxd/reboot.c), and ending a task there waits for the WIN386
agent's fix of "closing any application ends Windows" (the task's INT 21h
4Ch; docs/notes/win386-handover.md). For the WIN386 agent: w16's KRNL386
sets the sanity byte at every task switch and idle pass and in the
procedure, and ends the task with INT 21h 4C00h from inside the procedure
(on whatever stack the interrupt frame was built on).

## USER heaps (US14, US15)

US14 failed 11 checks under w16's KRNL286 and KRNL386 alike (not a
KRNL386 matter): USER's own local heaps (420h and 100h blocks, GMEM_SHARE,
allocated while Windows boots) belonged to KERNEL, so TOOLHELP's walk
skipped them; on 3.1 they belong to USER. GlobalAlloc now notes its
caller's CS (kentry.asm GLOBALALLOCX) and gives a GMEM_SHARE block to the
caller's module (commit f35db68, changes KRNL286.EXE). US14 and US15 pass
under both kernels as the shell (Microsoft's USER, Standard mode). Run:
`T=150 tests/dropin.sh [-3] -t 150 -f build/dos/US14.EXE=US14.EXE -i
'boot:shell=C:\US14.EXE' -W TK.LOG -o DIR` (US15: -t 300).

## Open (UNVERIFIED or not done)

- The call Pietrek's ExitCall makes when the last task ends; Reboot 0100h
  for ExitWindows(EW_REBOOTSYSTEM).
- When KRNL386 loads the 386 grabber (w16: just before WINOA386.MOD, if
  not loaded; 3.1 has it at usage 2 while COMMAND.COM runs) and whether it
  is freed afterwards. TK33 copied over WINOA386.MOD logged nothing (TK57
  woa): it imports no GRABBER and has another module name; not followed.
- KRNL386's placement policy (which blocks go where, the discardable code
  fence), its GlobalCompact/GetFreeSpace values, its LRU sweeping, the
  meaning of THHOOK 1Ah-24h (1Ah 1, 1Ch/1Eh 032Fh, 20h 030Ah, 24h 005Eh
  under 3.1): not observed; w16 keeps global.c's policy.
- The arena room starts at 2048 arenas and grows in 256-arena steps by
  moving the Burgermaster (THHOOK 18h changes then: UNVERIFIED whether
  TOOLHELP rereads it); Windows 3.1 had 3770 at 16 MB.
- US14/US15 (USER heaps) have KRNL286 tables; not run under w16's KRNL386.

## Test results (installation-05, tools/remote-test.sh, commit 93f8083)

- `make test`: pass; 38 probes x 2 DPMI hosts x 2 kernels (KRNL286 and
  KRNL386), 152 ok, with WIN.COM's scenarios and the font checks.
- Full boot test: 90 cases, 0 failures (k386, k386all, k386e, k386em,
  k386cad among them; tree now boots w16's KRNL386).
- Not in make test, run as the shell (dropin.sh -3): TK51, TK53, TK57 (both
  modes), TK58 (both modes, both WIN386s): no FAIL under w16's KRNL386.
  TK50 under w16's KRNL386 equals Microsoft's KRNL386 except for which
  registers each export keeps.

Results, branch krnl386-cad at af8c354 (master merged), installation-05:
make test pass (both kernels, KRNL386 now built with -3 -zfp -zgp);
boot test 92 of 92 (k386hung among them).
