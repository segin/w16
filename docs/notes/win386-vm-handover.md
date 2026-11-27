# Win32s on w16's WIN386: virtual memory and W32S.386 (handover)

Branch `w16-vm`. Spec: ARCHITECTURE.md 12.16 (paging) and 12.17 (W32S.386).

## Done

- **Paging file and demand paging** (`win386/ldr/swap.c`, `win386/vmm/pager.c`, hooks in `dpmi.c`, `vmm.c`, `vxd.c`). Temporary WIN386.SWP (2,048 KB as 3.1's) or permanent 386SPART.PAR from PermSwapDOSDrive/PermSwapSizeK; demand-zero client memory, clock eviction to the file through DOS, faults from ring 3 and ring 0; DPMI 0500h with 3.1's figures, 0600h/0601h locks; `_GetFreePageCount`, `_GetDemandPageInfo`, PageSwap device 0007h (Get_Version 0200h/EBX 2 as 3.1, Test_Create, Test_IO_Valid).
- **What Win32s Setup checks**: no `[386Enh]` key, no PageFile API; KERNEL's WF_PAGING / GetFreeSpace from 0500h. On w16's tree with w16's WIN386 Setup now installs.
- **Probes**: VX18 + `tests/vmprobe.sh` (boot-test `vmprobe`): equal to 3.1 in every scenario but GetWinFlags' processor bits (left out); 0501h past physical memory pages through the file and reads back. VXW.386 (`tests/vxd/vxw.c`): page directory and PageSwap under 3.1.
- **Hook_PM_Fault / Hook_V86_Fault / Hook_VMM_Fault** (vxd.c); PM chain called, V86/VMM kept only.
- **`[w16] vmmtrace=3`**: watch of a device's reads of a VMM page it maps with `_MapPhysToLinear` (vxd.c `vxd_watch_fault`).

## Open: W32S.386 fails Device_Init

Trace (boot the Win32s-installed tree with `tests/dropin.sh -p 386 -T TREE -3 -c "WIN /3" -i w16:vmmtrace=1` and DROPIN_CONF `serial1=file`): Call_When_Task_Switched, Hook_PM_Fault 0Dh/0Eh, PageSwap_Get_Version, `_MapPhysToLinear(CR3, 1)`, reads PDEs 201h.. up to the first not present, fails, no more service calls. Tried without effect: PageSwap version as 3.1's; PDEs 200h-210h present; bit 9 and user bit in every PDE; previous handler 0 at Sys_Critical_Init. Ideas: watch the page tables the PDEs point to (W32S may read them by physical-to-linear arithmetic from the directory's mapping); move the System VM's high linear window to 3.1's 84000000h (inside the 200h-210h run); 3.1's client memory base 80604000h (DILinear_Base_Addr). Installed tree for experiments: run `tests/win32sinst.sh -o OUT treems` and take `OUT/treems/work/c/W16`, put w16's WIN386.EXE in its SYSTEM.

## Tests

`win32s` part `tree` is strict (install, restart, Freecell) and fails until W32S.386 loads. Not added: a Virtual Memory dialog check (no existing case covers CPWIN386's 386 Enhanced dialog).

## Test state (2026-11-27, HEAD of w16-vm)

make test passes. Full boot test on w16-vm (before the last two pager commits): win32s, vmprobe, msvc, win386*, k386e, vsd, quicktime, sb16 ok. Failing on master too (not this branch's): cplclick co-tree, trails, lzexpand.

Open regressions of this branch, still to fix:
- **bigmem e128**: with new client pages mapped at once while half the memory is free (the last commit), TK63 (allocates until refused, past the memory) does not end: once memory is short every touch writes a page to the file through DOS. With every page coming on first touch instead (commit "0500h's unlocked pages..."), bigmem passed but holdbtn, taskman (tree 21-dosesc), vdmad and toolhelp (tree386 e) failed; with the current setting those pass in one run, taskman's tree part failed in another (974 pixels, also seen once before). Paging out is slow (one INT 21h write per 4 KB page through V86 DOS) and that is what makes TK63 too slow: write several pages per call, or have 0501h commit against the file without touching pages.
- **holdbtn** failed in three runs here and passed on master, and passed in the last run here: timing; recheck.
