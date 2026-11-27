# WIN386 memory, paging and page-hooking services: audit

Every memory, paging and page-hooking service and behaviour the Windows
3.1 DDK documents (Virtual Device Adaptation Guide: Memory Management,
Hook_V86_Page, the fault hooks, the V86 data areas, Map_Flat,
Map_Lin_To_VM_Addr) checked against w16's WIN386 (`win386/vmm`), on w16's
own layout (ARCHITECTURE.md 12.17, 12.19). Each was read in the DDK, then
called by the probe VxD VXH.386 (`tests/vxd/vxh.c`) under Windows 3.1's
WIN386 and w16's with PGT.COM (`tests/dos/pgt.asm`) in two DOS boxes
(`tests/memsvc.sh`, boot-test case `memsvc`), the logs compared with the
layout-dependent values (`~` tokens: addresses, handles, free counts,
the first V86 page) left out. Black box: results and the page table
entries left behind, never Microsoft's code.

Verdicts: **OK** (w16 already did what 3.1 does), **FIXED** (it differed
or was missing, "not in w16": a call ended Windows during initialisation
or failed with carry after; now as 3.1), **DIFFERENT** (left different,
and why). "3.1:" is what VXH saw under Windows 3.1.

Status (2026-11-27): the memsvc case passes: VXH's lines, a hooked page
in two DOS boxes included, are the same under both WIN386s.

## Allocation

| Service | Verdict | Findings |
|---|---|---|
| _PageAllocate PG_SYS | FIXED | Was: physically contiguous window pages, present, at once, whatever the flags. 3.1: page-aligned linear block, entries type 1 (bits 9-11); without PageLocked/PageFixed not present (writable, user, type 1) until touched. Now: a linear block of its own after the window, pages present when locked or fixed, else at the first touch (a ring-0 fault gives a zeroed page). A big block no longer needs physically contiguous memory. |
| _PageAllocate PG_VM, PG_HOOKED | FIXED | Was: no VM check. 3.1: VM 0 fails, a VM handle works; PG_SYS with a VM fails; type 5 fails. Now the same (a bad, non-zero VM handle fails in w16; 3.1 does not check it: it ended Windows, so the probe does not try). |
| PageZeroInit | OK | Pages always zeroed (3.1 zeroed them too). |
| PageLocked, PageFixed, PageLockedIfDP | FIXED | Lock counts per page now; LockedIfDP locks when the MS-DOS pager runs (w16's) and was taken at Device_Init by 3.1 too. |
| PageUseAlign, AlignMask, min/maxPhys, PhysAddr | FIXED | 3.1: without PageFixed fails; with it aligned, contiguous, PhysAddr written; minPhys 110h works. Was: no PageFixed check. DIFFERENT: maxPhys 100h (below 1 MB) succeeds on 3.1 by taking a page (9Eh) out of the System VM's conventional memory, which ended Windows before KRNL386 ran; w16's memory is the XMS block, so it fails. Not probed (it ends 3.1). |
| PageContig | OK | Ignored without PageUseAlign (3.1 too). |
| PageMapFreePhysReg | OK | 3.1 refused it at Device_Init (0); w16 refuses it. |
| PageV86, PageMapGlobal, PageDEBUGNulFault | OK | PageV86 and PageMapGlobal are not in the 3.1 DDK (later Windows); PageDEBUGNulFault acts on the debugging VMM only (the DDK). Ignored. |
| _PageReAllocate | FIXED | Not in w16. 3.1: contents kept, new pages zeroed, PageZeroReInit zeroes all, 0 pages fails, the handle kept, and a PageUseAlign block reallocated too (the DDK says it fails). Now the same. |
| _PageFree | FIXED | 3.1: fixed and locked blocks free; 0 fails; a freed handle "frees" again (no check). w16 checks the handle (a freed one fails): DIFFERENT, deliberately (left out of the comparison). |
| _PageGetSizeAddr | FIXED | The nul page's size: 3.1 FFFFFFFFh pages; w16 had 1. |
| _PageGetAllocInfo | FIXED | Not in w16. Free linear space and lockable pages from the pager's figures (values differ with each VMM's size). |
| _PageLock / _PageUnLock | FIXED | Was: no counts. 3.1: past the block's end fails; unlocking an unlocked page succeeds; fixed blocks always succeed; PageMarkPageOut clears the accessed bit. Now the same. |
| _PageCheckLinRange | FIXED | Not in w16. 3.1 returns nPages, even for unmapped addresses (7FFFF000h, page 0). Now the same. |
| _PageOutDirtyPages | DIFFERENT | Not in w16; now PagePDPQueryDirty counts the clients' dirty pageable pages; nothing is flushed (w16 writes a page when it evicts it). The DDK reserves it to the PageSwap device. |
| _PageDiscardPages | DIFFERENT | Not in w16; now clears the accessed (and with PageDiscard the dirty) bit of the clients' pages; a discarded page is still written when evicted (its contents are kept, which the DDK allows: "undefined"). |
| _GetSysPageCount | FIXED | Not in w16 (a device calling it ended Windows at init). Now the VMM's image and PG_SYS pages: 3.1's grew by 4 for 4 pages, as w16's. |
| _GetVMPgCount | OK | Present before (vm.c); values depend on each VMM's layout. |
| _GetFreePageCount, _GetDemandPageInfo | OK | Present before (pager.c, 12.17). |
| _AddFreePhysPage | DIFFERENT | Not in w16; now returns 0 (none added): w16 manages the XMS block the loader gave it, its window maps that only. 3.1 refused page 50h (below 110h) too. |
| _PageResetHandlePAddr | DIFFERENT | Not in w16; still not (V86MMGR's in 3.1). |
| _Allocate_Device_CB_Area | OK | 3.1: dword-aligned, 1 MB fails; w16 the same. |
| _Allocate_Global_V86_Data_Area | FIXED | Not in w16. Now from the start of DOS's free arena before the DOS VMs' memory is fixed (the arena's MCB becomes a system block, owner 8 "SC"); the first V86 page moves when needed (3.1's moved for a page-aligned block); GVDAInquire, GVDAInstance, GVDAZeroInit; fails while a temporary area is out (3.1 too). DIFFERENT: where a small block goes (3.1 put one in high memory, a region its V86MMGR added). |
| _Allocate_Temp_V86_Data_Area / _Free_Temp_V86_Data_Area | FIXED | Not in w16. 3.1: one at a time, paragraph-aligned, zeroed, below 1 MB; freeing twice fails. Now the same. |
| _Add_Global_V86_Data_Area | DIFFERENT | Still not in w16 (V86MMGR's and DOSMGR's in 3.1: w16's own devices do not need it). |

## Mapping

| Service | Verdict | Findings |
|---|---|---|
| _GetNulPageHandle | OK | A handle; _PageLock of it succeeds (3.1 too). |
| _MapIntoV86 | FIXED | Was: pages 10h-10Fh, every entry 7 (type 0), the DOS VM's own page lost (and freed with the VM though a device's page was there then). 3.1: P, W, U set, A and D clear, the block's type; page 0Fh accepted; 10Fh+2 and past the block fail. Now the same; a DOS VM's own pages are kept in a list and freed from it. |
| _PhysIntoV86 | FIXED | Type PG_SYS (1) now, page 0Fh accepted (3.1). |
| _LinMapIntoV86 | FIXED | Not in w16. 3.1: EAX FFFFFFFFh, EDX the V86 page; a page number up to 100h comes back as it is; a VM's high linear page maps that VM's memory. Now the same (a block's page brought in, a client's from the paging file). |
| _LinPageLock / _LinPageUnLock | FIXED | Not in w16. 3.1: succeed (unlocking twice too). Now: the clients' pages through the pager's lock counts, a block's pages brought in. |
| _ModifyPageBits | FIXED | Not in w16. 3.1: other AND bits clear fail, OR bits beyond W/U fail, type other than PG_HOOKED/PG_IGNORE fails, below the VM's first page or past 10Fh fails, setting P fails; clearing W with PG_IGNORE allowed (the DDK says PG_HOOKED is needed); A and D cleared; PG_HOOKED sets type 7. Now the same. |
| _CopyPageTable | FIXED | Not in w16. 3.1: 0 where a directory entry is missing. Now the same; the System VM's first megabyte as its table holds it (the layout differs: left out). |
| _GetV86PageableArray / _SetResetV86Pageable | DIFFERENT | Not in w16. Now kept per VM and given back (3.1: set and clear of the first page work); w16 never pages V86 memory, so the bits change nothing else. |
| _MapPhysToLinear | FIXED | Was: a range past 4 GB wrapped (FFFFF000h + 2000h gave a linear address); 3.1: -1. Now -1. DIFFERENT: nBytes 0 (3.1 gave an address without the offset; w16 keeps the offset). |
| _GetFirstV86Page | FIXED | Not in w16 (VXW's call ended Windows at init). Now the first page of a DOS VM's own memory (during init: where it will be). Its value is each layout's (w16's loader is smaller or larger). |
| _GetLastV86Page | FIXED | Not in w16. 9Fh as 3.1. |
| _SetLastV86Page | DIFFERENT | V86MMGR's in 3.1; w16's returns 0. |
| _TestGlobalV86Mem | FIXED | Not in w16. 3.1: below the first page global (1), instanced (3) where instance data is once Windows runs (the vectors in a DOS VM; 1 at Device_Init), a VM's pages local (0), A0h-10Fh global, 110000h local, mixed 2, 0 bytes as 1. Now the same. |
| _SelectorMapFlat | FIXED | Not in w16. 3.1: CS and DS 0 (high word ignored), 0 and FFF8h -1. Now the same (a VM's own LDT for LDT selectors). |
| _Assign/_DeAssign_Device_V86_Pages, _Get_Device_V86_Pages_Array | FIXED | Not in w16. 3.1: globally A0h-10Fh assigned at the last Device_Init; a DOS VM's own array had 0-10Fh, deassigning a page worked once; local assignment fails during init. Now the same. Assigning 9Fh globally ended 3.1 (not probed). |
| _Allocate_Device_CB_Area | OK | See above. |
| Map_Flat | OK | Present (vxd.c): 16-bit offsets in V86 and 16-bit PM, -1 for a bad selector. |
| Map_Lin_To_VM_Addr | FIXED | Not in w16. 3.1 in V86: page 16h + 123h gave CX 1612h, EDX 3; 123456h carry. Now the same; in protected mode an LDT selector per base and limit (UNVERIFIED against 3.1: no PM client in the probe). |
| CB_High_Linear | OK | The VM's own page table again (12.10): what _MapIntoV86 maps shows there at once; written through it, read in V86 (3.1 and w16 alike). Instance data (4F0h) reads the current VM's through a non-current VM's high linear address on both. |

## Hooking

| Service | Verdict | Findings |
|---|---|---|
| Hook_V86_Page | FIXED | Not in w16 (a display VxD ended Windows at init). 3.1: B0h-B7h and A0h hooked by its VDD (carry), C8h free; 50h and 110h carry; hooking leaves the entry present. Now the same (A0h-BFh are w16's VDD's, the EMS frame and the HMA V86MMGR's). |
| The PG_HOOKED fault path | FIXED | Was: a V86 page fault ended the VM (System VM: panic). Now the callback (EAX the page, EBX the VM, EBP not a client) for faults from V86, protected mode, the VMM or a device, also through a VM's high linear address, then the access again. 3.1 and w16: the same callbacks in two boxes (read: maps the VM's PG_HOOKED page, its ID in every byte; not present again; read-only, a write calls it), and a ring-0 read through the high linear address calls it once. Timing: 2000 fault-and-map cycles in a box both well under a second. |
| Hook_PM_Fault | OK | 12.18 (W32S.386). |
| Hook_V86_Fault | FIXED | Kept but never called. 3.1: a handler hooked at Device_Init gets a previous one; 02h and FFh carry, 50h accepted (beyond the DDK's 4Fh); PGT's invalid opcode reached it. Now called after w16's own handling (breakpoints, INT n, HLT, I/O), its chain ending in w16's (reflect, or end the VM); faults up to 5Fh. |
| Hook_VMM_Fault | FIXED | Kept but never called; now called for the VMM's own faults no pager or hook explains, before w16 stops. EBP is w16's trap frame, not 3.1's re-entrant frame (UNVERIFIED that a device reads it). Not probed (a ring-0 fault in 3.1 would end the run). |
| Set_Physical_HMA_Alias | DIFFERENT | Not in w16: the DDK reserves it to V86MMGR's XMS driver, which is w16's own. |
| Allocate_PM_Call_Back | OK | Present (vxd.c; win386vxc). |
| Hook_Invalid_Page_Fault | DIFFERENT | Not in w16 yet (not probed). |

## Other findings

- `_MapIntoV86` over a DOS VM's own page, then `vm_destroy`, freed the device's page into the free list and lost the VM's own: fixed (`struct vm` own).
- The pager took bits 9-11 of any entry for its own: now only the clients' memory (4 MB up to the window) is the pager's.
- 3.1 does not validate handles or VM handles: `_PageAllocate` PG_VM with a bad VM and `_PageReAllocate` of a bad handle ended Windows, `_SelectorMapFlat` with VM 0 and an LDT selector hung it; the other bad-handle calls were then left out of the probe. An unchecked `_PageFree` of a freed block, at System_Exit, succeeded. w16 checks and fails instead.
