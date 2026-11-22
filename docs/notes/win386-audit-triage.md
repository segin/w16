# WIN386 audit triage

An outside, read-only audit of `win386/` (written by another AI model;
`.tools/docs/win386_audit_report.md`, not in the repository) listed 10
Critical, 34 High, 29 Medium and 16 Low findings. Its Medium and Low
sections are paragraphs of several findings each; here every finding has
its own ID in the audit's order (M1-M30, L1-L23), so there are more IDs
than the audit counted. Line numbers in the audit were close but not
exact.

Each finding is checked against the code, and against Windows 3.1 where a
fix would change what a program or a VxD can see:

- **VX11** (`tests/apps/vx11.c`, `vx11s.asm`): a Windows program run as
  the shell in 386 Enhanced mode (`tests/refrun.sh VX11.EXE /3`; with
  `REFRUN_FILES=build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE` under
  w16's WIN386). The file name picks the run: `VX11.EXE` the main one,
  `VX11E`, `L`, `M` (which 3.1 survives) and `VX11P`, `S` (which end
  Windows 3.1).
- **VXP.386** (`tests/vxd/vxp.asm`): the probe VxD gained two
  VPICD_Call_When_Hw_Int callbacks and a look at its DDB_Next
  (`tests/vxpprobe.sh ms|w16`).

Boot-test case **`win386audit`**: VX11's OBS lines identical under both
WIN386s (runs main, E, L, M), runs P and S finish under w16's, and VXP's
callback line as 3.1's.

Verdicts: **REAL** (confirmed; fixed unless noted), **FALSE** (the claim
is wrong), **3.1-BEHAVIOUR** (3.1 does the same: kept or matched),
**HARDENING** (only reachable with malformed input or hostile code;
fixed when cheap and invisible to working programs), **UNSURE** (needs a
probe not run yet).

The "security" findings: Windows 3.1's WIN386 is no security boundary
(ring 3 has the LDT alias, flat selectors and more), so they were taken
as fidelity and robustness questions. VX11 settled them: 3.1's own DPMI
0009h/000Ch refuse system descriptors and DPL 0-2 and its 0800h refuses
below 1 MB, which w16 now matches; 3.1 maps physical memory at 1 MB and
above (its own too) and checks no segment limits, which w16 keeps; and a
bad pointer handed to its DPMI host ends 3.1, where w16 now fails the call.

Commits: 536b4e2 (VX11), 18dba68 (loader), 0c54980 (DPMI, DOS
translation, VMM core), 8ee8cc0 (I/O, VxD calls, time-outs, DOS VMs,
SHELL), 4d40a9a (VDD), 3b471c2 (VPICD, VXP), f15fef3 (VKD), 0e6d225
(REBOOT), and the one with this note and the boot-test cases.

## 3.1 facts found (VX11, VXP)

- DPMI 0009h: data or code with DPL 3 taken (not present too, and CH's
  bits); DPL 0, 1, 2 and system types (call gate ECh, LDT E2h, TSS E9h)
  fail with carry, **AX unchanged** (0009h), the descriptor unchanged.
- DPMI 000Ch: a call gate, DPL 0 data, DPL 0 code fail the same way (AX
  000Ch); DPL 3 data taken.
- DPMI 0800h: below 1 MB (A0000h, 0, B8000h) fails, AX unchanged; 1 MB
  and up mapped somewhere else (not identity); the same range twice gives
  the same address; size 0 is taken; a range that wraps past 4 GB fails,
  so does size FFFFFFFFh; an offset within the page is kept.
- DPMI 0501h/0502h: 300 times 64 KB allocated and freed: the same address
  every time. 0503h smaller and larger: same address, same handle. 0501h
  size 0: carry, AX unchanged (0501h). 0502h with a handle that is none:
  carry.
- DPMI 0101h with DX=0: carry, AX unchanged (0101h).
- DPMI 0205h and 0203h take a selector that is not allocated (and keep
  it); 0202h/0203h with exception 20h: carry.
- DPMI 0001h freeing the selector in ES: ES is 0 afterwards (run E).
- DPMI 000Bh with ES:DI past ES's limit writes the 8 bytes there (run
  L): no limit check. With ES:DI over linear memory nothing is mapped at
  (40000000h), and 0300h with CX=4000h, Windows 3.1 ends (runs P, S).
- INT 21h AH=48h from a Windows program gives a selector of host memory,
  not DOS memory (no DOS memory was left: 0100h failed with AX 8); the
  first selector's limit is the whole block (N*16-1: 1FFFFh for 2000h
  paragraphs, FFFDFh for FFFEh), then one selector per further 64 KB
  with limit FFFFh; FFFFh paragraphs fail with AX 8, BX FFFEh. 4Ah grows
  and shrinks in place (same selector, the limit follows), and fails past
  what its selectors cover with AX 8, BX 0FFFh for a one-selector block.
  49h frees the block and its selectors. (After that failed 4Ah, 3.1's
  49h of the same block failed too: not copied.)
- A real-mode interrupt handler reached through 0300h, or from protected
  mode through the host's reflection, is entered with IF clear, whatever
  the caller's IF; 0300h's structure comes back with the flags the
  handler's IRET restored (the structure's own).
- By the time a program runs as the shell, KRNL386 has taken all DOS
  memory under 3.1 (0100h fails, BX 0); under w16's WIN386 some is left
  (not investigated; VX11 marks those values `~`).
- VPICD_Call_When_Hw_Int: two callbacks are both called, the second's
  Next_Callback is the first, the first's is VPICD's own (VXP).
- A VxD's DDB_Next is set after initialisation (VXP, the last device in
  init order).
- DPMI 0008h: a limit of 00100000h fails (carry, AX kept, limit
  unchanged); 00100FFFh and FFFFh are taken.
- Ctrl+Alt+Del with the MS-DOS Prompt full screen (`dosbox386cad.txt`):
  3.1 shows a screen of its own for the DOS application: the title the
  window's ("MS-DOS Prompt", reversed, row 6), then "You can use
  CTRL+ALT+DEL to quit an application that has stopped / responding to
  the system. When possible, you should quit the / application by using
  the method recommended in the documentation / provided with the
  application." (rows 8-11, column 5), "*  Press ESC to cancel and return
  to the non-Windows application." (13), "*  Press ENTER to close this
  application that is not responding." and "You will lose any unsaved
  information in this application." (14-15), "*  Press CTRL+ALT+DEL again
  to restart your computer. You will / lose any unsaved information in all
  applications." (16-17), "Press ENTER for OK or ESC to Cancel: OK" (19,
  column 23). Escape goes back to the box.
- VMD_Set_Mouse_Type at Sys_Critical_Init fails (EAX 1, carry) under 3.1
  (seen in passing; w16's VMD takes the IRQ then: not changed).

## Critical

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| C1 | REAL | `info.xms_phys` was the block's address rounded up to a page, but `xms_move_to(dst, ...)` wrote at offset `dst` of the handle: with a block not page aligned the page directory, tables and VMM sat `xms_phys - phys` bytes below where CR3 and the tables pointed. (The "more blocks" half: moves went to the first handle only; the image always fits the first block, at least 1 MB, so that half was not reachable.) | `xms_move_to` adds the skew and goes through whichever block (first or continuation) holds each part, splitting at block ends (18dba68). No test: DOSBox's and DOSBox-X's XMS hand out whole 4 KB pages, so no run in the harness gets an unaligned block. |
| C2 | REAL | 0009h did `d[5] = CL | 0x60` and 000Ch `|= 0x60` with no type check: a DPL 3 call gate to SEL_CODE could be made. VX11 "ar"/"sd": 3.1 refuses system types and DPL 0-2 (carry, AX kept). | Same checks as 3.1 (0c54980). VX11 `ar.*`, `sd.*`. |
| C3 | REAL (below 1 MB, wrap, VMM window, partial maps) / 3.1-BEHAVIOUR (mapping RAM at or above 1 MB) | 0800h returned the address itself below 1 MB, looped on `size + (phys & 0xFFF)` overflow, used `next_lin` with no bound, left partial mappings. VX11 "map": 3.1 refuses below 1 MB and wraps, maps 1 MB (its own memory) and up. | Below 1 MB and wrapping ranges fail as on 3.1; the address comes from the linear allocator (bounded by the VMM's window); a failed map is undone; the same range returns the same address (0c54980). VX11 `map.*`, run M. |
| C4 | HARDENING (pointers; 3.1 ends) / REAL (`sel_base` past the GDT) / 3.1-BEHAVIOUR (no limit check) | `sel_base` indexed `gdt + (sel & ~7)` for any selector; 000Bh/000Ch/0300h/0500h used ES:DI unchecked. VX11 P, S: 3.1 itself ends; L: 3.1 writes past the limit. | `sel_base` gives no base for selectors past the GDT/LDT; `cl_ptr(sel, off, len, write)`: a present DPL 3 data/code descriptor and every byte reachable by ring 3 (`lin_user_ok` on the page tables), no limit check (as 3.1); a failed check fails the call (carry, AX kept) (0c54980). VX11 runs P, L, S. |
| C5 | HARDENING | `dosx.c` dereferenced `pm_lin(sel, off)` for every translated call, and `disk_pm` took a selector from the client's packet into `sel_base`. | Every copy goes through `cl_ptr` (16-bit offsets may not run past FFFFh); a bad pointer: carry, AX 9 (UNVERIFIED: 3.1 dies on bad DPMI pointers, its DOS translation was not tried) (0c54980). |
| C6 | HARDENING | `simulate()` read/wrote `sel_base(seg) + off` for INS/OUTS. | A string element the client cannot reach ends the string there (count and index left as far as it got) (8ee8cc0). No test: needs a trapped port and string I/O into unmapped memory. |
| C7 | HARDENING | SHELL's `create()`, function 9 and function 7 used WINOA386's ES:DI and the cached `s->block` unchecked. Only WINOA386 calls SHELL's API, and its block lives as long as the VM (3.1's SHELL keeps the same pointer: UNVERIFIED). | `create()` checks the block and its three far pointers (`fptr_lin` with a length), function 9 its block, function 7 the cached address before writing (8ee8cc0). |
| C8 | REAL | VPICD_Call_When_Hw_Int left ESI the caller's own procedure and VPICD called every procedure itself: a DDK callback (`jmp [Next_Callback]`) jumped to itself for ever. DDK: "The ESI register contains the address of the next procedure"; VXP under 3.1: both called, next2 = callback 1. | A chain: the newest first, each handed the previous one, the oldest handed `vpicd_hw_end` (a RET); VPICD calls the head only (3b471c2). VXP's line in `win386audit`. |
| C9 | HARDENING / FALSE (EBX=0) | `vkd_api` function 1 used the caller's EBX as a VM handle unchecked. The "DDK says EBX=0 means focus VM" claim: the DDK documents no VKD PM API; function 1 is WINOA386's (observed), which always names the VM. | VMM Validate_VM_Handle first; a bad handle fails (carry; UNVERIFIED what 3.1 does) (f15fef3). |
| C10 | REAL | `screen()` saved the current VM's client state but wrote INT 10h's registers into the System VM's client and ran Exec_Int in the current VM: with a DOS VM current (the time-out fires in whichever VM runs) the BIOS got that VM's registers and the System VM's were left changed. Also the planes and the screen went through linear A0000h/B8000h of the current VM (a windowed VM's are RAM). | The current VM's client registers (Get_Cur_VM_Handle, CB_Client_Pointer) throughout; the VGA through the System VM's high linear window (0e6d225). Test: boot-test case `win386doscad` (the box goes on after Escape, VER and EXIT as under 3.1; the screen itself differs: Left open). |

## High

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| H1 | REAL | `kfree` read `b[0]` before testing NULL; `size + 16` and the page count could wrap; large blocks were never given back; a large allocation that ran out half way had already taken pages. | NULL first; sizes past the window fail; all pages or none; large blocks' pages go back to the free list (0c54980). |
| H2 | REAL | DS/ES were popped in `_resume` unchecked (FS/GS went through `fixsel`); 0001h/0101h left a freed selector in the client's segment registers (the next return to ring 3 faulted in ring 0); `trap()`'s CS check read `gdt + (cs & ~7)` past the GDT and allowed a non-conforming DPL 0-2 CS. VX11 E: 3.1 zeroes ES. | `fixsel` for DS and ES too; `sel_gone` zeroes the client's registers holding a freed selector (0001h, 0101h, INT 21h 49h); the CS check bounds the index and wants DPL 3 (0c54980). VX11 run E. |
| H3 | REAL | `code_ptr` used `sysvm.client32` for the offset size and read code bytes unchecked; CLI/STI and I/O decoding stepped EIP by the client's size, not CS's D bit (a 16-bit client running 32-bit code: Win32s, Watcom's Windows extender). A #GP in a DOS VM's V86 code ended the whole VMM (panic). | `seg_32()` (CS's D bit, SS's B bit) for code, stack and I/O; code the VMM cannot read is the client's #GP (0c54980, 8ee8cc0). A DOS VM's V86 #GP or #PF ends that VM as 3.1 (second round). |
| H4 | HARDENING | `load_le` read `lastpage` bytes (up to anything) into a 4 KB buffer and indexed its tables unchecked. Only a damaged .386 file reaches it. | Tables and record lists checked against what was read; the last page at most 4 KB (18dba68). |
| H5 | REAL (3.1 ends) | 0300h copied `CX*2` bytes from the client stack onto the 8 KB reflection stack unchecked. VX11 S: 3.1 itself ends with CX=4000h. | The source must be readable and the copy must fit the stack (256 bytes kept free), else carry (0c54980). VX11 run S. |
| H6 | HARDENING | `dpmi_switch` put the environment's selector in PSP:2Ch (DPMI 0.9 says the host does) and never put the segment back. Only DOS's view of the ended client's PSP. | `terminate_client` puts the segment back (0c54980). |
| H7 | REAL | `mem_alloc` bumped `next_lin` for ever; page tables were never freed; 0503h always moved and changed the handle. VX11 mem: 3.1 reuses addresses, resizes in place, keeps the handle. | A linear range allocator (first fit, merged on free), empty page tables freed, 0503h in place when it can (else moved, same handle) (0c54980). VX11 `mem.*`. |
| H8 | HARDENING (not fixed) | The transfer buffer is one per host; reentry needs an INT 21h from a hardware interrupt's handler while DOS is inside a call that uses the buffer, which DOS itself does not allow (InDOS). | None. |
| H9 | REAL | IOCTL 2-5 clamped the copy to the buffer but passed the client's CX to DOS: a read past 16 KB overran the buffer. | DOS gets the clamped count; the client's CX is put back (0c54980). No test: needs a device whose IOCTL read gives more than 16 KB. |
| H10 | REAL | Without the program's own DTA (no 1Ah yet) 4Eh/4Fh/11h/12h left the result in the buffer and DOS's DTA pointing at it; the DTA selector is kept as given (the program's). KRNL386 sets a DTA per task before searching, so Windows programs did not see it. | DOS's DTA is read (2Fh) and given back after; without a program DTA the result goes to DOS's (0c54980). No test (KRNL386 always has a DTA). |
| H11 | HARDENING | `disk_pm` used `pm_lin(bsel, boff + done*512)`: with a 16-bit offset past FFFFh it wrapped to the segment's start. Only a buffer running off its segment's end. | The whole buffer must be the client's and inside 64 KB, else carry, AX 9 (0c54980). |
| H12 | REAL | With the free arena's MCB on a page boundary `priv_start` was the MCB itself, which became a zeroed private page in every new VM. | The private range starts at the first page after the MCB's paragraph (8ee8cc0). No test: whether the arena starts on a page depends on DOS's memory layout before WIN. |
| H13 | REAL | `vm_create` leaked blocks on a partial failure and panicked when private pages ran out. | Blocks freed (or kept in the pool), pages given back, NULL returned (8ee8cc0). |
| H14 | REAL | `io_trap` gave handlers a local copy of the client registers while CB_Client_Pointer still pointed at the VM's (stale) block: services a handler called (Map_Flat, Exec_Int, Simulate_Push...) saw the wrong state. | The VM's own block is filled for the call (an outer user's contents put back after) (8ee8cc0). |
| H15 | REAL | A segment prefix was recorded as the segment's value, and 0 meant "none": a V86 `ES:` with ES=0000h then defaulted to DS for OUTS. | A separate flag for a prefix (8ee8cc0). |
| H16 | REAL | `create()` linked the SHELL record before CREATE_VM; a refusal destroyed the VM but left the record; a failed `vm_create` or `kmalloc` leaked the other. | The record is unlinked and freed; a VM made without its record is destroyed (8ee8cc0). |
| H17 | REAL | A deferred event kept `struct vm *` only; a VM destroyed and its slot reused before the event ran got the event. | VMs carry a generation (`gen`); the event is dropped if it changed (8ee8cc0). |
| H18 | REAL | A VM windowed from its start (its video memory RAM, `valid` 0) going full screen: `vdd_focus` found no saved state and only set the mode: the VM kept writing into RAM, the screen showed nothing of it, 32 pages stayed taken. | Its text is kept, the RAM unmapped and freed, the BIOS sets the mode in the VM and the text and cursor come back (4d40a9a). No test yet: see Left open (a PIF that starts windowed). |
| H19 | REAL | `vga_save`/`vga_restore` copied linear A0000h, which is the current VM's mapping: RAM when a windowed VM is current. | The VGA through the System VM's high linear window, always the VGA (4d40a9a). |
| H20 | REAL | `to_ram` checked `font` and `zero` but not `plane1`; NULL went to `make_user(0, ...)` and `memset(0, 0, 8192)` (the vector table). | Checked (4d40a9a). |
| H21 | REAL | `save_of` linked a record whose `vga` allocation failed; the next lookup returned it and `vga_save(NULL)`. | Not linked without it (4d40a9a). |
| H22 | HARDENING | `d->zero` (1 KB) came from the slab and `make_user` opened its whole page (other VMM data) to ring 3. | A page of its own (4d40a9a). |
| H23 | HARDENING | `vdd_api` used ES:DI unchecked. Only the grabber calls it. | `cl_ptr` (functions 1-3 fail without a buffer, 0 writes only to one) (4d40a9a). |
| H24 | HARDENING / UNSURE (VxDjmp) | A device's service table entry 0 was linked as a CALL to address 0. A jump (`VxDjmp`) to a device that is not loaded goes on after the INT 20h like a call (3.1's behaviour for calls, VXQ; for jumps not observed). | An empty entry is treated as "not there" (8ee8cc0). The jump case is kept. |
| H25 | REAL | `vxd_call_api` filled the VM's client block and left it: an API call nested inside another (from an interrupt's handler while a device procedure ran V86 code) overwrote the outer call's registers. | The outer contents are put back after (8ee8cc0). |
| H26 | FALSE (IF) / HARDENING (DF) | w16's ring 0 always runs with interrupts off (every IDT entry is an interrupt gate; `trap()` returns at once from an IRQ in ring 0); every caller passes flags with IF 0. A callee returning with DF set would leave it for the C code. | IF and DF cleared in what is popped, `cld` after the call, in `_vmm_call_proc`, `_vxd_call_proc` and VXDCALL (0c54980). |
| H27 | REAL | VPICD ignored DESTROY_VM: pending IRET time-outs stayed armed and the control block went to the next VM. | DESTROY_VM cancels them; a default owner that was the VM is cleared (3b471c2). |
| H28 | HARDENING | `deliver_pending` ignored `deliver()` failing (possible only if a device's Virt_Int procedure turned the VM's IF off). | The in-service bit and IRET record are undone (3b471c2). |
| H29 | REAL (VPICD's own IRQs) / UNSURE (VID_EOI_Proc) | A specific EOI for an IRQ not in service cleared `phys_isr` for VPICD's own IRQs (unmasking one whose interrupt still waited). Whether 3.1 calls a device's VID_EOI_Proc for such an EOI is not known. | VPICD's own: nothing for an IRQ not in service (as an 8259). The device's procedure is still called (3b471c2). |
| H30 | REAL (DOS VMs; the System VM kept: Left open) | E0h went to the VM before the next byte was known: an extended hot key left the VM a lone E0h. Windows 3.1 gives neither a DOS VM (VX12 K) nor the System VM (VX11 K) that E0h. | DOS VMs: held back (second round). The System VM: see Left open. |
| H31 | REAL | A keyboard byte waiting for the VM and a mouse byte in the controller at once: port 64h showed OBF and AUXB, port 60h gave the keyboard byte. An 8042 shows the byte in its buffer. No collision could be made in the emulator to see 3.1 do it. | Port 64h: the VM's own byte first (OBF, AUXB clear), the mouse's after it (second round). |
| H32 | REAL (A20) / UNSURE (reset) | Writes to 60h/64h went to the 8042 as they were: a DOS program in a DOS box switching A20 off through the output port (D1h) or a pulse (F0h-FDh) folded the VMM's memory onto low memory. 3.1 keeps A20 per VM (the DDK's V86MMGR). Whether 3.1 lets the reset pulse (FEh) through is not known. | A20 stays on in D1h's data and in pulses; the reset line goes as written (f15fef3). No test yet (a DOS program toggling A20 in a DOS box: Left open). |
| H33 | REAL | VKD ignored DESTROY_VM: the next VM with the same ID inherited disabled hot keys, and the keyboard could stay with a VM that was gone. | DESTROY_VM clears the VM's bits and moves the keyboard to the System VM (f15fef3). |
| H34 | REAL / FALSE (NULL checks) | REBOOT's `vga_save` read the planes with the registers as they were: a text mode (odd/even, B8000h) or mode 13h (chain 4) gave garbage, and the restore then wrote it over the font and text. `sane` is checked; `kern_sel` is set with it (0201h). Build_Int_Stack_Frame failing (a DOS VM current) leads to screen C after 1.5 s: see Left open. | Sequencer 4 = 06h, GC 5 = 0, GC 6 = 05h before reading (0e6d225). |

## Medium

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| M1 | REAL | `v86_int` entered the handler with the caller's IF and TF and pushed `flags | IF`. VX11 ifl: 3.1 enters with IF clear and 0300h returns the structure's flags. | IF and TF cleared at entry, the caller's flags in the frame (0c54980). VX11 `ifl.*`. |
| M2 | FALSE | Physical page 0 can no longer be asked for (0800h refuses below 1 MB, as 3.1) and the VMM never maps it; mappings above the VMM's base are opened to ring 3 by `make_user`, by design. | None. |
| M3 | REAL (page buffer, A20) / FALSE (leaks) / UNSURE (V86 monitor) | `_fmalloc(PAGE)` unchecked (NULL then memset the vector table); XMS 03h's result unchecked. The XMS blocks are unlocked and freed on every failure path (`xms_free_all`), the DDB list is DOS memory freed at exit. The empty "V86 monitor without the 1605h callback" branch: 3.1's message not known. | Checked (18dba68). |
| M4 | HARDENING (not fixed) | Nested `exec_v86` has no ring-0 stack check; the reflection stack's own check (`refstk_check`) comes first in the nesting seen. | None. |
| M5 | REAL | 0503h allocated the new size first, so a shrink failed when memory was nearly full. | In place (H7). |
| M6 | REAL | 0100h leaked the DOS block when no selector was left; 0102h with BX=0 set limit FFFFFFFFh; 0101h with DX=0 freed segment 0 through DOS (VX11: 3.1 fails with AX kept). | All three (0c54980). VX11 `dos.free0.*`. |
| M7 | REAL (chaining) / 3.1-BEHAVIOUR (selectors) | 0202h's "no handler" answer was the frame's own return address: a handler chaining to it with a far jump had its frame misread. VX11 vec: 3.1 takes unallocated selectors in 0203h/0205h. | 0202h hands out PMBP_EXCDEF, which takes the whole frame and does the default (0c54980). |
| M8 | REAL | 440Ch/440Dh copied 256 bytes in and out; 440Dh's track I/O forms carry a pointer inside the block that DOS got untranslated. Windows 3.1 translates it (VX11 io on MS-DOS 4.00). | Checked copy (0c54980); 41h/61h translated (second round). |
| M9 | REAL | With CX*3 past the buffer, INT 10h 1012h/1017h went to the BIOS with ES 0 (the vector table read or written). | Nothing is done (0c54980). |
| M10 | REAL | INT 21h 48h/49h/4Ah from protected mode went to DOS. VX11 d48: 3.1 gives host memory and selectors. | `dpmi_dosmem` as 3.1 (0c54980). VX11 `d48.*`, `d4a.*`. |
| M11 | REAL | A V86 INT 21h whose return frame was abandoned (a program ended inside DOS, Ctrl+C at the prompt) kept `indos` up for good: the VM was never preempted while busy. | Each call's frame (SS:SP) is recorded; one the VM's stack has passed above no longer counts (8ee8cc0). |
| M12 | REAL / UNSURE (status bit) | Suspend_VM/Resume_VM were a flag: no count, no VM_Suspend/VM_Resume. The DDK's VMStat_Suspended value is not known. | A count with the messages (Resume refused by a device: carry, count 1), apart from SHELL's own suspending (8ee8cc0). |
| M13 | HARDENING | `_Allocate_Device_CB_Area`: `size + 3` and `cb_next + n` could wrap. | Checked (8ee8cc0). |
| M14 | REAL | Word/dword I/O at FFFFh dispatched port 10000h (no handler: simulated, i.e. port 0 untrapped). | Ports wrap at 16 bits (8ee8cc0). |
| M15 | FALSE | Function 15h takes the newest record without a window; records are added at the head, and WINOA386 calls 15h right after creating the VM, so it is the VM just made. | None. |
| M16 | FALSE (header) / HARDENING (ring, not fixed) | The event block is never freed, so ring 3 changing its header does nothing. The ring overwriting unread records after NEVENTS posts: what 3.1 does is not known. | None. |
| M17 | REAL | A focus change while a DOS VM was current skipped 4001h; a VM destroyed with the display left Windows' screen unrestored; a switch during the display driver's critical section was not deferred (the DDAG: up to 1000 ms or 4004h). | 4001h as an event in the System VM; destroy gives the display back (4d40a9a); the deferral (second round). |
| M18 | REAL / FALSE (VPICD slot) | DDB_Next was never set (VXP: 3.1 sets it). `vtd_init` runs only after `vxd_init` found VPICD (it ends Windows otherwise). | Each DDB names the device before it in init order, the VMM's ends the list (order UNVERIFIED) (8ee8cc0). VXP in `win386audit`. |
| M19 | REAL | A time-out set to 0 ms from its own callback ran again in the same tick, for ever. | One set during the tick waits for the next (8ee8cc0). |
| M20 | REAL / FALSE (nest_level) | Exec_Int passed a protected-mode VM's selectors as V86 segments. `nest_level` is only counted, never used. | DS/ES translated (`sel_to_seg`, else the buffer's segment) and translated back when the call changed them (8ee8cc0). |
| M21 | REAL | 1684h set only DI for 32-bit clients; DOS programs got no VxD V86 API entries (VX12 V: 3.1 gives VPICD, VTD, VMD, DOSMGR). | EDI whole (0c54980); V86 entries (second round). |
| M22 | REAL | Convert_Handle_To_IRQ also set EAX. DDK: "Uses ESI, Flags". | EAX kept (3b471c2). |
| M23 | REAL | Force_Default_Owner and Force_Default_Behavior did nothing. | Owner: carry for a bad or virtualised IRQ, else the IRQ's default requests go to that VM (0: the System VM, UNVERIFIED); Behavior: the IRQ is VPICD's again, the handle dead (3b471c2). |
| M24 | REAL | Get_Status returned Get_Complete_Status's bits; the DDK lists IRET pending and in service only. | Masked to those (3b471c2). UNVERIFIED against 3.1. |
| M25 | REAL | ICW1 cleared the virtual ISR without ending VPICD's own physical in-service IRQs (masked for good) and without `phys_update`; ICW4's automatic EOI was ignored. | Both; AEOI ends an IRQ as it is delivered (3b471c2). |
| M26 | REAL | `track_shift` took E0h 2Ah/36h (fake shifts) and E1h 1Dh (Pause) for real ones, cleared Shift when one of two held Shifts went up, and Force_Keys moved it without `phys_mods`. | The shift state comes from the per-key state (`phys_mods`); E0h and E1h sequences understood; Force_Keys updates the same state (f15fef3). |
| M27 | REAL | A held hot key's repeats called its procedure even without CallOnRepeat; a NULL procedure was called. | Repeats only with HK_REPEAT; no procedure, no call (f15fef3). |
| M28 | REAL (Reflect_Hot_Key) / HARDENING (EBX checks, not fixed) | Reflect_Hot_Key ignored the DDK's EBX (the VM). Local_Enable/Disable take the VM handle of a VxD caller unchecked, as VxD-to-VxD services do. | EBX's VM (0: the keyboard's) (f15fef3). |
| M29 | 3.1-BEHAVIOUR | VMD takes IRQ 12 at Device_Init whether or not a PS/2 mouse is there. Windows 3.1 does the same (VXP with DOSBox-X's PS/2 port off: IRQ 12 virtualised at System_Exit). | Kept. |
| M30 | REAL | REBOOT's key loop dropped E0h, so an extended key's release (right Ctrl) went back to the VM as the plain one's. | Its E0h is kept with it (0e6d225). |

## Low

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| L1 | REAL | The VM stub's 48h failure popped the saved words into AX, losing DOS's error code. | Kept in CX across (18dba68). |
| L2 | REAL (cosmetic) | `I_SIZE` in `w386.inc` was stale and unused. | Removed (18dba68). |
| L3 | HARDENING | `load_ini` took `lseek`'s -1 as a size. | Fails (18dba68). |
| L4 | HARDENING | `argv[0]` copied into 128-byte buffers with names appended. | Too long a path fails (18dba68). |
| L5 | FALSE | `panic()`/`fatal()` halting for ever is the intent: the VMM stops with its message in the trace. | None. |
| L6 | HARDENING | A format ending in `%` read past its NUL (no such format exists). | Stops there (8ee8cc0). |
| L7 | REAL | 0008h with a limit past 1 MB whose low 12 bits are not FFFh was rounded up to 4 KB granularity silently. VX11 lim: 3.1 fails it (carry, AX kept, the limit unchanged) and takes 00100FFFh. | The same (8ee8cc0). VX11 `lim.*`. |
| L8 | REAL | `seg_to_sel` returned a kept selector whose descriptor KRNL386 had since freed or reused (it writes the LDT directly). | A kept selector is used only while its descriptor is still the one made; else renewed (0c54980). |
| L9 | FALSE | 0500h reports free pages for the linear-space fields; nothing unbounded is computed. | None. |
| L10 | HARDENING (not fixed) | An exception handler runs on the host stack without `hstk_depth`; a trap from another stack while it runs resets `hstk_low`. KRNL386's handlers stay on the host stack. | None. |
| L11 | HARDENING | FCB search copied 44 bytes in and out of a 37-byte FCB and DTA. | 37 or 44 by the FCB's first byte (0c54980). |
| L12 | FALSE | 5Ah copies back what DOS wrote (the path plus the 13 bytes DOS appends), the caller's documented buffer. | None. |
| L13 | FALSE (timing) | One `boost` VM: a second VM's event waits for its turn. Timing only. | None. |
| L14 | FALSE (timing) | No reschedule check right after switching back: the next trap does it. Timing only. | None. |
| L15 | REAL | Two 66h (or 67h) prefixes toggled the size back. | A prefix sets the other size (8ee8cc0). |
| L16 | REAL | A REP with count 0 stepped EIP without the 16-bit wrap. | As the other paths (8ee8cc0). |
| L17 | REAL | The VDD and REBOOT saved the CRTC at 3D4h always; a VGA set for a monochrome display (mode 7) has it at 3B4h. | Port from Misc Output bit 0 (4d40a9a, 0e6d225). |
| L18 | REAL | The windowed text was 80x25 (TEXT_BYTES 4000); 3.1 shows 80x50's 50 rows (VX12 T). | Columns and rows from the BIOS data, Get_Mem's size in pages (second round). |
| L19 | HARDENING | List_Get_Next(0) read address -4; List_Allocate's size + 4 could wrap. | Checked (8ee8cc0). |
| L20 | FALSE | `cur_api` only names the device in a trace line. | None. |
| L21 | REAL | A spurious IRQ 7/15 (in-service bit clear) went to the VM as an interrupt. | Dropped (the master's cascade ended for 15) (3b471c2). |
| L22 | REAL | Virtualize_IRQ moved a pending default request to the device but left `phys_isr` set: the device never saw it come, so it never Phys_EOI'd it, and the IRQ stayed masked. | Cleared (3b471c2). |
| L23 | REAL | VMD gave the mouse to the System VM only (no Set_Device_Focus, DESTROY_VM). | It follows VMD's focus (second round). |

## Counts

Each ID counted once, by its first verdict (several have a second part:
C3, C4, M7, H32 partly 3.1-BEHAVIOUR; H26, M2, M3, M16 partly FALSE).
After the second round:

| | REAL | HARDENING | FALSE | 3.1-BEHAVIOUR | UNSURE | total |
|---|---|---|---|---|---|---|
| Critical | 5 | 5 | 0 | 0 | 0 | 10 |
| High | 25 | 8 | 1 | 0 | 0 | 34 |
| Medium | 24 | 2 | 3 | 1 | 0 | 30 |
| Low | 11 | 6 | 6 | 0 | 0 | 23 |
| all | 65 | 21 | 10 | 1 | 0 | 97 |

Not fixed: H8, M4, M16 (ring), L10, M28 (EBX checks) as hardening not
worth its cost; H30's System VM half (Left open).

## Tests

- `win386audit` (new): VX11 main, E, L, M identical to Windows 3.1's OBS
  lines (138 + 2 + 2 + 2 compared); P and S finish under w16's WIN386;
  VXP.386's callback chain and DDB_Next as 3.1's.
- `win386doscad` (new): Ctrl+Alt+Del with the DOS box full screen, Escape,
  VER, EXIT, against Microsoft's WIN386 (the screen itself left out).
- The WIN386 cases after the fixes (remote, installation-05): win386,
  win386input, win386cad, win386hung, win386close, win386dos, win386dosw,
  win386switch, win386two, win386exitbox, win386w16dos, win386tree,
  win386woa, win386plain, k386e, exittree, treealone: all ok; make test
  passes. win386cad caught the E0h change (H30), which was reverted.
- The full boot test at the end (90a30ed): 147 ok, 8 failed, all of them
  the known-flaky ones (holdbtn, lzexpand quirks, toolhelp ref286, trails
  timed, cplclick co-tree, w16write tree, taskman tree) but `control`
  (tree 06-keyboard), which passed when run again alone; make test passes.
- Not testable in the harness, by reasoning only: C1 (DOSBox's XMS gives
  page-aligned blocks), H12 (the arena's alignment depends on DOS's
  layout), H9 (no device with a 16 KB IOCTL read), H10 (KRNL386 always
  has a DTA), H13/H20/H21 (out of memory), H18 (needs a PIF that starts
  windowed: tools/mkpif.py does not write the 386 Enhanced block),
  H32 (a DOS program switching A20 through the 8042 in a DOS box), C6
  (string I/O to a trapped port into unmapped memory).

## Second round: the open items closed

Probes: **VX12** (`tests/dos/vx12.asm`, a DOS program in the MS-DOS
Prompt; runs K, G, A, B, R, V, T, F), **VX11 K** (the System VM's
keyboard bytes) and **VX11 io** (440Dh), **VXSH.386** (now also dumping
SHELL's event records), **VXP.386** (an STI test), a windowed PIF
(`tools/mkpif.py windowed=1`). Boot-test cases: `win386doscad`,
`win386dosprobe`, `win386dospif`, and `win386audit`'s VXP line.

3.1 facts found:

- A #GP in a DOS VM's V86 code (VX12 G: LGDT): the VMM suspends the VM,
  VM_Suspend, VM_Not_Executeable with EDX 1, the devices' and the
  execution focus to the System VM, SHELL event 41Bh (wParam 4).
  WINOA386 then calls SHELL 16h, which answers EAX 00010004h, ECX 0083h,
  and shows a stop box with the window's title: "This application has
  violated system integrity due to an invalid general protection fault
  and will be terminated." / "Quit all applications, quit Windows, and
  then restart your computer."; OK destroys the VM.
- Ctrl+Alt+Del with a DOS program in front: Begin_Message_Mode, the
  screen (title from SHELL function 3's block, +2Eh "MS-DOS Prompt");
  Enter: End_Message_Mode, SHELL event 415h, Close_VM_Notify (19h, ECX 1),
  INT 21h AX=4CFFh in the VM, then the VM ends (VM_Not_Executeable EDX
  20h, event 418h); Windows is back, no box. Escape goes back to the box.
- Neither a DOS VM (VX12 K: `9C 1D 38 9D B8 81 2D AD 39 B9 1C`) nor the
  System VM (VX11 K: `1D 38 9D B8 B9 1C 9C`) gets the E0h of Ctrl+Alt+grey
  Del; the Ctrl and Alt releases come back Ctrl first, whichever was let
  go first.
- OUT 64h,FEh from a DOS box resets the machine (DOSBox-X restarted
  Windows); A20 switched off through the 8042 (D1h DDh) or port 92h ends
  Windows 3.1 (the emulator went away).
- V86 API entries for a DOS program: VPICD (0003h), VTD (0005h), VMD
  (000Ch), DOSMGR (0015h); function 0: 030Ah, 030Ah, 0300h, 030Ah.
- A window of 80x50 text shows all 50 rows; VDD Get_Mem gives the text's
  size as whole pages (2000h for 80x50); 40x25 is shown as such.
- VMD takes IRQ 12 even with the PS/2 port off (DOSBox-X `aux=false`:
  VXP's IRQ 12 status at System_Exit still virtualised).
- INT 21h 440Dh read track (61h) from a Windows program into a buffer
  outside DOS memory works (MS-DOS 4.00): the block's pointer is
  translated. (DOSBox's own DOS has no 61h.)
- A PIF whose 386 Enhanced flags have bit 3 clear starts its program in
  a window; SHELL function 3's block then has bit 2 of its first byte set.

| Item | Verdict now | Fix | Test |
|---|---|---|---|
| C10's screen | REAL, fixed | REBOOT's screen D for a DOS program in front (Get_Execution_Focus), the title through a link with the VMM (`struct reboot_link`, w16vxd.h; SHELL keeps the block's title); Enter: event 415h, Close_VM_Notify, the VM's programs end with INT 21h 4CFFh (the loader's `vm_close`), the VM after them (also when DOS's 4Ch returns, as DOSBox's shell's does) | `win386doscad` (vx12gc.txt, dosbox386cad.txt): every screen as 3.1's but the cursors |
| H3 | REAL, fixed | A #GP or #PF in a DOS VM's V86 code ends that VM as 3.1 (`vm_crashed`, `shell_vm_crashed`: event 41Bh, SHELL 16h's answer); w16's WINOA386 shows the same box ("w16"); the System VM's still stops the VMM (3.1 ends Windows there too) | `win386doscad` (with Microsoft's WINOA386: the box identical) |
| H30 | REAL (DOS VMs; the System VM kept: Left open) | E0h went to the VM before the next byte was known: an extended hot key left the VM a lone E0h. Windows 3.1 gives neither a DOS VM (VX12 K) nor the System VM (VX11 K) that E0h. | DOS VMs: held back (second round). The System VM: see Left open. |
| H31 | REAL | A keyboard byte waiting for the VM and a mouse byte in the controller at once: port 64h showed OBF and AUXB, port 60h gave the keyboard byte. An 8042 shows the byte in its buffer. No collision could be made in the emulator to see 3.1 do it. | Port 64h: the VM's own byte first (OBF, AUXB clear), the mouse's after it (second round). |
| H32 | 3.1-BEHAVIOUR (reset), REAL (A20, port 92h), fixed | FEh passes (3.1 resets too); port 92h trapped by VKD, A20 kept on (3.1 dies) | `win386dosprobe` (VX12 A, B: w16 goes on) |
| M8 | REAL | 440Ch/440Dh copied 256 bytes in and out; 440Dh's track I/O forms carry a pointer inside the block that DOS got untranslated. Windows 3.1 translates it (VX11 io on MS-DOS 4.00). | Checked copy (0c54980); 41h/61h translated (second round). |
| M17 | REAL | A focus change while a DOS VM was current skipped 4001h; a VM destroyed with the display left Windows' screen unrestored; a switch during the display driver's critical section was not deferred (the DDAG: up to 1000 ms or 4004h). | 4001h as an event in the System VM; destroy gives the display back (4d40a9a); the deferral (second round). |
| M21 | REAL | 1684h set only DI for 32-bit clients; DOS programs got no VxD V86 API entries (VX12 V: 3.1 gives VPICD, VTD, VMD, DOSMGR). | EDI whole (0c54980); V86 entries (second round). |
| M29 | 3.1-BEHAVIOUR | VMD takes IRQ 12 at Device_Init whether or not a PS/2 mouse is there. Windows 3.1 does the same (VXP with DOSBox-X's PS/2 port off: IRQ 12 virtualised at System_Exit). | Kept. |
| L18 | REAL | The windowed text was 80x25 (TEXT_BYTES 4000); 3.1 shows 80x50's 50 rows (VX12 T). | Columns and rows from the BIOS data, Get_Mem's size in pages (second round). |
| L23 | REAL | VMD gave the mouse to the System VM only (no Set_Device_Focus, DESTROY_VM). | It follows VMD's focus (second round). |
| STI in ring 0 | REAL, fixed | An IRQ arriving in an outside VxD's code (its STI) goes to VPICD at once, as 3.1's VMM takes interrupts whenever IF is set; w16's own entries and the VMM's service entry run with IF clear | VXP: STI for about 300 ms at Init_Complete, then a 100 ms time-out must fire (`win386audit`) |
| H18 test | added | A VM that starts windowed (SHELL 3's bit 2) gets RAM, an 80x25 BIOS data area and blank text at once; no display event before its window exists | `win386dospif` |
| Also | REAL, fixed | Hook_V86_Int_Chain (VMM 41h), needed by VXS.386 under w16 | `win386doscad` runs no VXS; checked by hand with VXSH |

Tests of the second round: every WIN386 case after each group (win386,
win386input, win386cad, win386hung, win386close, win386dos, win386dosw,
win386switch, win386two, win386exitbox, win386tree, win386woa,
win386plain, win386w16dos, win386audit, win386doscad, win386dosprobe,
win386dospif, k386e, exittree, treealone): all ok, make test passes. The
full boot test at the end (f83ac16): 151 ok, 7 failed: holdbtn, lzexpand
quirks, trails timed, w16write tree, taskman tree (known flaky), control
tree and scrnsave desktop, which both passed when run again.

## Left open

- **H30 in the System VM**: there an extended hot key's E0h still goes
  to the VM. Holding it back (as 3.1, VX11 K) made w16's REBOOT show
  screen C ("busy or unstable") in 1 of 3 `win386cad` runs where 3.1 and
  w16 otherwise show screen A: w16 asks KERNEL's Ctrl+Alt+Del procedure
  (Build_Int_Stack_Frame) when the VM idles, and without that byte's
  interrupt the procedure ran at another point and did not set the
  sanity byte. Closing it needs to know how Windows 3.1's REBOOT and
  KERNEL decide that a task responds (Reboot API 0204h, which KERNEL
  calls and w16 refuses, is part of it) — not found black-box yet.
- **w16's WIN386 on the MS-DOS 4.00 image** (`INPUTRUN_HDD=1`, as
  `tests/vxsrun.sh` uses): KRNL386 faults at 0117:A789 (and master's,
  before this audit, at 011F:0B8C until the host stack overflows). Not an
  audit finding; it keeps M8's fix from being run under w16.
