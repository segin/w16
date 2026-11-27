# DPMI in DOS boxes: handover

ARCHITECTURE.md 12.16 has the facts and the design; this is the state of the work (2026-11-26).

## Done

- **Probe**: DPMIPRB.COM (`tests/dos/dpmiprb.asm`, `dpmibody.inc`; nasm, the same tests assembled as a 16-bit and a 32-bit client), `tests/dpmirun.sh OUT p|g|c ms|w16|tree` (`DPMIRUN_WIN="WIN /S"` plus `DPMIRUN_INI="NonWindowsApp:SwapDisk=C:\WINDOWS"` for Standard mode), `tools/dpmicmp.sh KIND REF DIR`.
- **Host** (`win386/vmm/dpmi.c`, `struct dpmi_vm` in vmm.h): per-VM LDT, host stack, blocks, callbacks, nested clients, termination cleanup, the 3.1 answers listed in 12.16. Under w16's WIN386 on Windows 3.1 and on the tree alone, DPMIPRB's p, g and c equal Microsoft's WIN386's (boot-test `win386dpmi`).
- **Visual C++ 1.52's CL.EXE** builds and runs hello world in a box with DOSXNT.386 and without, CL's output as 3.1's (boot-test `msvccl`, `tests/msvcvxd.sh cl`; `W16_MSVC_DIR`, on installation-05 `/mnt/build/w16-vcvxd-in/MSVC`). It needed INT 21h 3Fh/40h with 32-bit counts (dosx.c).
- DOS's current directory structure is instance data now (vm.c, the loader's INT 21h 52h).

## Tools for the next DOS extender that fails

- `MSVCVXD_VXCD=1` (or VXCD.386 in any SYSTEM.INI): every INT 21h/2Fh/31h of the DOS VMs with registers in and out on COM1, under either WIN386; diff the two logs for the first difference. Under 3.1, VXCD's logging once made CL.EXE fail with "too many open files": the early part of its log is still good.
- `[w16] vmmtrace=1`: INT 31h calls, translated DOS paths, `{Lxx+n}` LDT allocations in DOS VMs; `=2`: a DOS VM's V86 interrupts, raw switches (`[PM2RM ...]`, `[RMk ...]`).

## Open

- DOSBox's own DOS keeps current directories in the emulator, so KERNEL's task switches move a box's current directory (the CL test uses full paths). Real DOS is fine with the CDS instanced.
- 0500h counts physical memory only (3.1 adds its paging file: 32 MB of blocks in a 16 MB DOSBox); 0B00h's watchpoints never trigger; 0102h keeps the segment's base (3.1 moves it to the VM's high linear address).
- What the extra selector 3.1 allocates for a nested 16-bit client is for (w16 allocates one, mapping the host stack, to keep the numbers).
- Not DPMI: a w16 box shows the MS-DOS Prompt banner for a batch file's PIF (3.1 does not).
- Standard mode boxes: 3.1 gives no DPMI host there; w16's Standard mode was not checked.
