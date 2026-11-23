# DOSX audit triage

An outside, read-only audit of `dosx/` (written by another AI model;
`.tools/docs/dosx_audit_report.md`, not in the repository) listed 11
Critical, 11 High and 15 Medium findings and a Low section (C1-C11,
H1-H11, M1-M15 here, in its order; its three Low paragraphs are L1-L3).
A verification run (two read-only reviews per Critical finding and a
critic) added five defects the audit missed (X1-X5). Each finding is
checked here against the code, and against Windows 3.1 where a fix could
change what a program or KERNEL sees:

- **DX9** (`tests/dos/dx9.asm`): a DPMI client in the KERNEL slot, as
  DX5, one case per run, chosen by the command tail (`tests/dosxtest.sh
  -a NN DX9`, under Microsoft's DOSX and w16's). Its INT 13h case reads a
  floppy image whose sectors hold their own numbers (`tests/scripts/
  mkfloppy.py --pattern`, mounted by `dosxtest.sh` for DX9); its INT 25h
  case runs under DOSBox-X (`dosxtest.sh -c 386x`), whose INT 25h reads
  FAT drives (DOSBox 0.74's does nothing and returns AX=0).
- **DX10** (`tests/apps/dx10.c`): a Windows program, run as the shell,
  that starts itself again; that copy ends with INT 21h AH=00h.

Boot-test case **`dosxaudit`**: DX9's OBS lines (not the `OBS ~` ones)
and any "Standard Mode:" message the same under both DOSXes (runs 01,
02, 08-16, 23, 24; 0E and 17 under DOSBox-X too); runs 03-07, 20 and 21,
which Windows 3.1's DOSX does not survive or does not check, end cleanly
under w16's; DX10 on w16's KRNL386 and KRNL286 with w16's DOSX: the task
ends and Windows goes on.

Note for reading the probes: DOSBox checks no segment limits on memory
accesses, so a pointer past its selector's limit is not a fault there
under either DOSX (DX9 09 passes under both, and 20 shows Windows 3.1's
DOSX reading past the limit); the present bit is checked.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**3.1-BEHAVIOUR** (Windows 3.1 does the same: kept), **HARDENING** (only
reachable with malformed input or hostile code; fixed where cheap and
invisible to working programs), **UNSURE** (needs a probe not run). Where
Windows 3.1 ends Windows or hangs, w16 may survive instead, failing the
call or ending just the client (the user's ruling); robustness fixes take
no `[w16]` key.

Status: every finding has a verdict and is closed (2026-11-22).

## Critical

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | REAL | `ret3_entry` had no CLD and a callback's procedure got the real-mode caller's flags, DF included; its IRET brought DF=1 back to ring 0, whose REP MOVSW then copied down over the host stack. DX9 0D (real mode calls a 0303h callback with DF set): w16's DOSX hung; Windows 3.1's starts the procedure with DF clear (`cb_df=0`) and real mode gets its DF back afterwards. | CLD (and NT cleared) in `ret3_entry`; NT cleared at the gate entry too (an IRET at ring 0 with NT set would be a task return); a callback's flags have DF cleared, as 3.1's. Test: DX9 0D. | 51febbb |
| C2 | FALSE | On a 386 `to_pm` loads GDTR and IDTR with `o32` (the 24-bit truncation was fixed in 6fb9d5c, ARCHITECTURE.md 9.6 "More than 64 MB"). | None. | - |
| C3 | HARDENING | INT 21h 0Ah read its buffer's first byte at ring 0 with no check. Only a bad DS:DX reaches it (DX9 20: past the limit, which DOSBox does not check). | The first byte read through `cpin`; error 9 for a buffer that cannot be read. Test: DX9 20 (w16 survives). | 51febbb |
| C4 | HARDENING | `cpin`, `cpout`, `cpin_z` checked VERR/VERW/LSL, none of which looks at the present bit. A register cannot hold a not-present selector at ring 3, so only pointers held in memory reach it: DX9 21 (an INT 25h/26h packet whose buffer selector is not present) ended Windows under w16's DOSX; Windows 3.1's copies through the descriptor's base anyway (CF clear). | `clamp` checks the present bit; the INT 25h/26h, 3Fh/40h, INT 13h and track-I/O buffers go through `cl_setup`, which wants a present segment: a not-present one fails (0408h for INT 25h/26h) instead of writing through a stale base (a discarded segment's memory is someone else's). Test: DX9 21 (w16 survives). | 51febbb |
| C5 | HARDENING | 3Fh/40h took `tb_left & FFF0h` without checking for 0: a read or write made from an interrupt handler while another had the whole buffer called DOS with CX=0 (40h then truncated the file and looped). | Under 16 bytes left: what is left; nothing left: error 5. The outer call's state is kept anyway (H10). | 51febbb |
| C6 | HARDENING | INT 10h 1Bh patched the client's buffer at ring 0, unchecked, also when the BIOS has no 1Bh. | The far pointer is turned into a selector in the transfer buffer, before the copy, and only when the BIOS returned AL=1Bh. | 51febbb |
| C7 | HARDENING | 10h/12h, 10h/17h, 11h/00h, 11h/10h and 13h sizes wrapped in 16 bits (CX of 5556h DAC registers and more); only 10h/17h could write past the transfer buffer. | Sizes that do not fit refused (nothing called). | 51febbb |
| C8 | REAL | INT 13h AH=02h/03h: w16's DOSX refused more than 8 sectors (0900h) and wrapped its byte count at 128 sectors (the BIOS writing 64 KB over DOSX's image; 129-136 overran its stacks). DX9 0E (drive 0, a pattern image): Windows 3.1's DOSX reads 1, 8, 9, 80h and 81h sectors with AX = the count; past 32 sectors its data are not the disk's (sector 32 of a read holds sector 16's), w16's are. | Pieces of the transfer buffer, each one BIOS call within a track, the next cylinder, head and sector from the drive's geometry (AH=08h); the client's buffer by its linear address (as 3Fh, H8). Deliberate difference: the right data past 32 sectors (`OBS ~` in DX9). Test: DX9 0E under DOSBox and DOSBox-X. | 51febbb |
| C9 | REAL | INT 25h/26h: the same wrap (`shl cx, 9`) and refusal of more than 8 sectors (0408h). DX9 0F (DOSBox) and 17 (DOSBox-X, drive A: as a FAT image): Windows 3.1's DOSX transfers 80h sectors, plain and as a packet, the data right; after a transfer that worked AX is the caller's (DOS's is not passed back), after one that failed DOS's error. | Pieces, each one DOS call (plain or packet as the client's), the sector size from DOS (X1); AX as 3.1's. Test: DX9 0F, 17; DX8 `a25` now matches 3.1. | 51febbb |
| C10 | HARDENING | INT 26h's `abs_buf` cleared CF after a failed `cpin`; the packet's `cpin` was unchecked. Only bad pointers reach it. | Rewritten with C9: packet and buffer checked before DOS is called. | 51febbb |
| C11 | HARDENING | INT 33h 09h/16h/17h: a failed copy (a bad pointer, a state larger than the transfer buffer) called the driver with DOSX's image at the client's offset. The "R_ES clobbered" and "15h shares BX" parts are FALSE (the values are the intended ones). | The driver is not called then. | 51febbb |

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | REAL | DX9 01, 02: Windows 3.1's DOSX gives 0 in ES or DS when INT 31h 0001h frees the selector held there; w16's ended Windows ("Fault in MS-DOS Extender", popping it at ring 0). DX9 03-06 (0502h with the block's selector in ES, 0009h making ES's descriptor not present, 000Ch the same for DS, INT 21h 49h with the block in DS): Windows 3.1 ends Windows ("Fault in MS-DOS Extender"); w16's did too. | `gate_exit` and `exc_exit` load 0 into DS/ES for a selector ring 0 would not load (null, or present and readable at its RPL); `r0_call3` the same for the contexts it starts. 01/02 as 3.1; 03-06 survive with 0 (the ruling). FS/GS keep TK61's rules. Test: DX9 01-06. | 51febbb |
| H2 | REAL | An exception handler returning with a ring-0 CS (DX9 24): Windows 3.1 ends Windows with "Standard Mode: Invalid DPMI return from 02E7:12DC", the address of the handler's RETF; w16 ran the client's frame at ring 0 (`G_HC0`) and ended with "Fault in MS-DOS Extender". The NT part is FALSE as stated (NT in the popped flags does not affect ring 0's IRET), but NT set by ring 3 was live at ring 0 (C1). | The return as Windows 3.1's: the frame's return address is 00E0:140C, a ring-0 selector (DX5 F00D's `f_retcs`/`f_retip` now match 3.1); the handler's RETF faults and that fault is the return (`dpmi_ret`). The frame's CS must be ring-3 code, its SS ring-3 writable data, else 3.1's message with the RETF's address; IOPL 3 and NT clear forced. Test: DX9 24 (message identical), DX5 F00D. | 51febbb |
| H3 | HARDENING | `G_HD3`'s limit is FFFFh over a 6 KB stack and `h3_top` was not checked: a seventh nested callback or hooked interrupt wrote into the client's memory. The limit is visible to the handlers (LSL of SS 004Bh; Microsoft's not measured), so it stays. | No seventh level: DOSX ends the client ("Fault in MS-DOS Extender"); the host stack likewise (H7). | 51febbb |
| H4 | REAL | `is_softint` guessed the error code from the bytes before the return address, reading through the faulting IP taken as a selector. DX9 0B (#GP with IP = the value of a not-present code selector): w16 ended Windows; DX9 0A (the bytes there CD 0D): w16 gave the fault to the INT 0Dh handler with the error code on the stack. Windows 3.1 gives both to the exception handler (error code FFF4h, the right CS:IP). The limit case (DX9 09) does not show in DOSBox. | The error code is known from the frame's depth below the TSS's SP0 (Intel: from ring 3 only exceptions 08h, 0Ah-0Eh push one); `is_softint` only for frames without one, through `uchk`. Test: DX9 09-0B. | 51febbb |
| H5 | HARDENING | An IRQ or software INT delivered onto a ring-3 stack that cannot take the 6-byte frame faulted at ring 0. Only a broken stack reaches it. | Checked (`uchk`); the client gets a stack fault (0Ch) instead. | 51febbb |
| H6 | HARDENING | `ldt_alloc` with a count near 10000h wrapped the run's end; reads stayed in DOSX's data, no writes (the critic's "BSS corruption" is FALSE). DX9 23 (CX=F000h): both DOSXes fail at once. | Counts over the LDT's size fail at once; X5's scan fix. Test: DX9 23. | 51febbb |
| H7 | HARDENING | 0300h-0302h copied CX words from the client's stack and the structure at ES:DI at ring 0 unchecked; with SS:SP = 0 a large CX ran past the real-mode reserve; nesting could underflow `hs_stack`. ("SHL faults" is FALSE.) | The structure and the stack words checked (`uchk`), CX of 8000h or more, more than RM_RESERVE - 100h bytes on DOSX's stack or more than SP on the client's: CF. `r0_rmx` ends the client when the host stack has under 200h bytes left. | 51febbb |
| H8 | REAL | DX9 11: INT 21h 3Fh of 2000h bytes at F000h of a 64 KB selector, and at 7000h of one with limit 7FFFh: Windows 3.1's DOSX reads all 2000h (AX=2000h), the bytes past the selector's end going to the memory after it (linear), none to offset 0; 40h writes from there likewise. w16's wrapped to offset 0 (64 KB case) or stopped at the limit with error 5 and lost what DOS had read. | 3Fh/40h reach the client by its linear address (selector base + DX), as 3.1's; a not-present selector or a range over DOSX's own memory (its image, the LDT, IDT and ring-3 stack) gives error 5. `cpin`/`cpout` now return the count they copied. Test: DX9 11. | 51febbb |
| H9 | REAL | DX9 12 (limit 1FFFFh, 2000h bytes at F000h): Windows 3.1's DOSX goes on at offset 10000h; w16's wrapped to 0. | As H8 for 3Fh/40h; `clamp` treats a limit above FFFFh as FFFFh for the other copies (which use 16-bit offsets). Test: DX9 12. | 51febbb |
| H10 | HARDENING | The handlers' state between their DOS/BIOS calls was in static variables; only an interrupt handler calling DOS or the BIOS while another call is under way reaches it (INT 24h's legal functions use none). | `st_save`/`st_restore` keep that state (`st_start`-`st_end`) on the stack around each INT 21h, 13h, 25h/26h and 33h call. | 51febbb |
| H11 | REAL | INT 21h AH=00h was reflected to DOS, which ended DOSX's own context. DX9 07: Windows 3.1's DOSX hangs too (its DOSBox would not even end on SIGTERM); DX10: Windows 3.1 hangs when a Windows program makes the call, under KRNL386 and KRNL286 alike. | w16 survives: its KERNEL takes a task's AH=00h as 4C00h (`kernel/kentry.asm`), and its DOSX ends its client with exit code 0 if one gets there. Test: DX9 07, DX10 (task ends, Windows goes on). | 51febbb |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | HARDENING | The 16th nested exception moved `exc_sp` below the stack: a ring-0 fault, reported as "Fault in MS-DOS Extender". | The same message, without the fault: `exc_deliver` checks the room first. | 51febbb |
| M2 | HARDENING | `cb_stacksel`'s failure (no LDT entry left) was not checked; it also returned a random CF on success. | CF exact; on failure DOSX ends the client. | 51febbb |
| M3 | REAL | DX9 0C: Windows 3.1's DOSX gives a software INT 09h to the 0205h handler; w16's ended the client as exception 9 ("Fault outside of MS-DOS Extender"). Part B (an exception during an in-service IRQ 3-5 taken for the IRQ) is REAL by reading; it needs an IRQ source DOSBox does not give. | Both by H4's frame-depth test: an error code means an exception whatever the PIC says; without one, the in-service bit, then CD nn (now also for 09h). Test: DX9 0C. | 51febbb |
| M4 | REAL | DX9 08: under Windows 3.1's DOSX real-mode code finds FS and GS as real-mode code last left them (0 at the first 0301h call, its own values at the next), the client's FS/GS untouched. w16's gave real mode 0068h and lost them across an IRQ it passed to protected mode. (Also seen: Windows 3.1's DOSX did not pass IRQ 0 from real mode to the 0205h handler during the 0301h call; w16's does, as DPMI 0.9 says; kept, `OBS ~pm_ticks_seen`.) | `to_rm` loads the real-mode FS/GS last saved (`rm_fsv`, `rm_gsv`); `rm_ret`, `rmx_common` and `pm_entry` save them; a hooked interrupt or callback restores the interrupted code's (`R_FS`/`R_GS`, which the callback's structure now carries). Test: DX9 08. | 51febbb |
| M5 | REAL | After INT 2Fh 1605h, XMS failing ended DOSX without 1606h; a CX other than 0 from 1605h was ignored. The Windows 3.1 DDK (INT 2Fh 1605h, 1606h): Windows then prints nothing of its own, broadcasts 1606h and returns to DOS. (Not observable in DOSBox without a refusing TSR; the DDK is the source.) | 1606h on every exit after 1605h (`end_bcast`, once); CX not 0: 1606h and exit FFh (UNVERIFIED exit code). | 51febbb |
| M6 | HARDENING | `own_path`: a null environment segment read the IVT, a path longer than the transfer buffer's room ran over the EXEC block. Unreachable through DOS's EXEC. | Null environment: no path; one or two NULs before the count; the path capped (no path when too long). | 51febbb |
| M7 | REAL (TRACE build only) | `r0_terminate` flushed the trace reading a gate frame it might not have (a fault in the flush recursed) and skipped the last records; `r0_rmx` logged a real-mode structure as a gate frame. The LOW_MIN part is FALSE (448 KB with TRACE). | The final flush on the fresh stacks, once, with no frame (`trc_flush.all`); `r0_rmx` logs the real-mode registers and CS:IP, SS:SP. | 51febbb |
| M8 | HARDENING | `cb_grow` at 0FFD0h records wrapped `cb_cap + 64` to a 0-byte block. Needs 65,000 callbacks (3.1 has 16). | The wrap is the end (`cb_max`). | 51febbb |
| M9 | HARDENING | Callback entry e reached as (cb_seg + e - i):(cb_off + 16 i) runs its near call 100h bytes on: for i - e of 0FF0h or more the offset wrapped (only with cb_seg of 0FF0h or more). | i at most 0FEFh (`tables_init`); the bound is 16 x min(0FF0h, cb_seg + 1) (ARCHITECTURE.md 9.6). | 51febbb |
| M10 | FALSE | Windows 3.1's DOSX leaves 1Fh's and 63h's DS as DOS gave them (DX4, ARCHITECTURE.md 9.6). | The comments that still named them corrected; the unused `I_DSSI`/`h_dssi` removed. | 51febbb |
| M11 | REAL | DX9 10: Windows 3.1's DOSX prints a 6000-byte string whole; w16's copied 4 KB without a '$' and DOS printed on past the transfer buffer. (The 16-bit LSL part: `clamp` handles 386 limits; the unchecked `cpin`: fixed.) | Printed in pieces of the transfer buffer, a '$' after each; ends at the segment's end. Test: DX9 10. | 51febbb |
| M12 | REAL (a); HARDENING (b, c) | DX9 14: after 4B01h Windows 3.1's DOSX gives the client the child's SS:SP and CS:IP at block offsets 0Eh-15h, as DOS's real-mode values; w16's left the client's bytes. AX=03h fell into `h_dxz` with ES:BX untranslated; unreadable tail/FCB pointers copied garbage. The "EPB layout" part is FALSE. | 4B01h copies the 8 bytes back; 4B03h translates the name and the two segment words; other AL values get an empty buffer; an unreadable tail or FCB pointer gives an empty one. Test: DX9 14. | 51febbb |
| M13 | HARDENING | `sel_to_seg` ignored `desc_base`'s failure and truncated bases of 1 MB and more. | CF for a selector with no real-mode segment (past the LDT, base of 1 MB or more, not a multiple of 16): 26h/50h/55h do nothing, EXEC's environment gives error 0Ah. GDT selectors and 0 unchanged (0 is EXEC's "inherit"). | 51febbb |
| M14 | REAL (47h, 5F02h); 3.1-BEHAVIOUR (38h, 11h/12h); HARDENING (the unchecked copies in) | DX9 15: after an error Windows 3.1's DOSX leaves 47h's buffer (drive Y:) and 5F02h's two buffers as they were; w16's copied the transfer buffer over them. 38h with an error and 11h finding nothing: 3.1 copies its transfer buffer over the client's anyway (the bytes are whatever was there; w16's the same, `OBS ~`). 60h and 65h are w16's own translations (3.1 has none), 5Eh unobserved: kept. | 47h and 5F02h copy back only on success; `cpin`/`cpin_z` failures in 29h, 44h, 56h, 5Ah, 5Fh, 65h, 4Eh/4Fh's DTA give error 9. Test: DX9 15. | 51febbb |
| M15 | HARDENING (a, c); FALSE (b); harmless (d) | `v_in` ignored `cpin`'s failure; C207h stored the client's handler before the BIOS took it. R_ES = the client's ES for INT 15h is intended (C207h 0:0). | `v_in` does not call the BIOS then; the handler is kept only when the BIOS took it (0:0 then clears it). | 51febbb |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | HARDENING (A, C); 3.1-BEHAVIOUR (B, E); FALSE (D, F) | (A) 0002h could take a slot 000Dh had given. (B) DX9 16: past the descriptors kept for 0002h, Windows 3.1's DOSX also gives a new selector for a segment mapped before; it gives LDT entry 0 (0007h) as the 80th, which w16's did not. (C) 0303h took DS=0, the free mark. (D) size 0 blocks are valid. (E) DX9 16: 0008h with 00100000h: both make it page-granular 100h. (F) one client per DOSX. | (A) 0002h skips a slot in use; entry 0 used as 3.1's 80th; (C) refused. | 51febbb |
| L2 | HARDENING | FS/GS at the client's start: the IRET nulls DPL-0 ones anyway (invisible). No check that the image fits 64 KB. | Both loaded with 0 on a 386; an assembly-time check (`dosx.asm`). | 51febbb |
| L3 | HARDENING (a); REAL (b); 3.1-BEHAVIOUR (c); FALSE (d) | (a) 4Ah's failure BX was not capped at FFFFh. (b) DX9 15: 3Fh of 0 bytes from handle 99: Windows 3.1's DOSX gives error 6 (DOS checks the handle); w16's returned 0 without asking DOS. (c) DX9 15: 32h of drive Y: (AL=FFh): 3.1 maps DS anyway, as w16's. (d) 65h 02h-07h's pointer: 3.1 translates no 65h; w16's translation is its own. | (a) capped; (b) CX=0 goes to DOS. Test: DX9 15. | 51febbb |

## Missed by the audit

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| X1 | REAL (by reading) | INT 25h/26h assumed 512-byte sectors: a drive with larger ones (RAM drives may use 1024) overran the transfer buffer. Not observable in DOSBox. | The sector size from DOS (440Dh 0860h's default BPB, no disk access; 512 when DOS does not say); the track-I/O pieces (X3) the same. | 51febbb |
| X2 | REAL | BIOS calls passed on with pointers wrote into DOSX's image (R_ES = the image, the client's offset). DX9 13: INT 10h 1C01h overwrote w16's DOSX (the run hung); Windows 3.1's DOSX passes 4F00h, 4F01h and 1C01h on untranslated (AX 004Fh/1C1Ch, the client's buffer unchanged). | Those functions (INT 10h 1Ch 01h/02h, 4Fh 00h/01h/04h/09h; INT 13h 0Ah/0Bh, 42h-44h, 47h, 48h) get an empty buffer in the transfer buffer (`bios_scr`), as `h_scr` gives untranslated INT 21h functions: the client's buffer unchanged as with 3.1. Test: DX9 13. | 51febbb |
| X3 | REAL (by reading) | 440Dh 41h/61h (write/read track) carry a far pointer to their sectors in the block, passed to DOS untranslated; 40h's track layout can be longer than the 64 bytes copied. Windows 3.1 translates the pointer (the WIN386 audit's M8, VX11 io on MS-DOS 4.00). Not observable on DOSBox's DOS. | 41h/61h: the sectors through the transfer buffer in pieces, each call with its own first sector, count and pointer; 40h: 28h + 4 x the layout's count bytes. | 51febbb |
| X4 | HARDENING | Client pointers used at ring 0 unchecked: 000Bh/000Ch's ES:DI, 0300h-0302h's structure (H7), 0500h's buffer, the default exception handler's reflection onto the client's stack (`op_excdef .refl`), a callback's structure. | `uchk` before each: 000Bh/000Ch/0500h/0300h fail (CF); a reflection that does not fit ends the client as a fault; a callback structure that cannot be reached ends the client. | 51febbb |
| X5 | HARDENING | `ldt_alloc`'s search was quadratic: every chain candidate rescanned the bitmap. | Counts over the LDT's size fail at once (H6); a candidate that fails rules out every one up to the allocated descriptor it met (`la_lo`, `la_hi`), so an ascending chain is walked once. | 51febbb |

## Beyond the audit

Found by the probes and fixed with them: the exception return address now
matches Windows 3.1's (00E0:140C, DX5 F00D), INT 25h/26h leave AX as it
was after a transfer that worked (DX8 `a25`), and 0002h gives LDT entry 0
as 3.1 does. Seen and kept: Windows 3.1's DOSX does not pass IRQ 0 from
real mode to a 0205h handler during a 0301h call (DX9 08); w16's does,
as DPMI 0.9 says. DOSX's resident size grew with the fixes (DX1
`dosx_block_paras` 07A8h to 0861h paragraphs; Microsoft's 0575h).
