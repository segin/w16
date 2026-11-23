# WSWAP/DSWAP audit triage

An outside, read-only audit of `swap/` (wswap.asm, dswap.asm, swap.inc;
written by another AI model, not in the repository) listed 2 Critical, 8
High, 6 Medium and 4 Low findings (C1-C2, H1-H8, M1-M6, L1-L4 here, in
its order). Each is checked here against the code at 0b3c22b (line numbers
below are that version's), the callers (WIN.COM, DOSX and WINOLDAP through
the switcher block and INT 2Fh 4A05h/4B80h, ARCHITECTURE.md 9.5, 9.8,
9.9), and against Windows 3.1 where a fix would change what a program or a
TSR sees: the new probe SW2.COM (`tests/dos/sw2.asm`), an MS-DOS 5 task
switcher client loaded before WIN that logs the notifications it is sent,
declares a startup-info structure without instance data (0000:0000) ahead
of one with a one-byte item, and logs that byte and the BIOS keyboard
flags on command; `tests/swap.sh` runs it with Microsoft's WSWAP/DSWAP and
with w16's and compares the logs (scenarios `client`, `clientdf`,
`nodisk`). The audit's line numbers were close.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong, or
nothing a working session does reaches it), **3.1-BEHAVIOUR** (3.1 does
the same: kept), **HARDENING** (only with failing hardware, a broken
client or hostile input; fixed when cheap and invisible to working
programs), **UNSURE** (needs a probe not run yet). Where 3.1 itself
crashes, w16 may survive instead (the user's ruling).

Status: every finding has a verdict and is closed (2026-11-22). Counts:
REAL 9 (C2, H1, H2, H3, H4, H5, H7, M2, L4; H3, M2 and L4 with a FALSE
part), HARDENING 6 (C1, H6, M1, M3, M4, L2), FALSE 3 (M6, L1, L3), FALSE
with a HARDENING part 2 (H8, M5), 3.1-BEHAVIOUR 0, UNSURE 0.

Tests (installation-05, run w16-swapaudit): `tests/swap.sh` all eight
scenarios PASS (prompt, two, ctrlesc, pif, exitwin, nodisk, client,
clientdf); before the fixes client, clientdf and nodisk failed (SW2.LOG;
clientdf also every screen after the first Alt+Tab). Boot test swapdos,
swapalt, swapall, swapesc, wincomswap, winoldap, winoldapk, winoldapw,
woados, woadosbat: 10 ok; make test pass. No regression test for C1, H1,
H2, H6, H7, M1, M3, L2 and M5: a write or read error after a successful
INT 21h 36h, a critical error, 64 KB clusters or instance data and a
timing window cannot be made in DOSBox.

## Critical

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | HARDENING (disk full: FALSE) | True that no AH=40h write compared AX with CX (dswap.asm:801-812, 872-888, 1234-1257, 1539-1565; wswap.asm:529-535) and that `chunks_walk`'s CF was dropped (dswap.asm:881). But a full disk cannot get there: DSWAP sizes the whole file first (`file_size`, dswap.asm:784, the same `chunks_walk` counting) and asks INT 21h 36h for that many bytes (`disk_room`, :785-786, :1447-1475: free clusters times cluster size, so a file of N <= free bytes fits); WSWAP writes 34 bytes after asking for the DOS memory's size in bytes (wswap.asm:432-453). Only a failing write (a disk error, a subdirectory that must grow by a cluster when exactly the file's room is free) reaches it; then the session was marked swapped and ended with a truncated file (DSWAP), or Windows' state file was short and the next WSWAP stopped with "cannot be swapped back" (wswap.asm:138-139). | Every write checked for CF and AX < CX. DSWAP: a failure before the switch is decided as before (the file deleted, a beep, the program goes on); after it (the memory) the vectors and instance data go back and the program goes on as after Esc in Alt+Tab; the block (record byte 2Dh, 173h, SI=4) is written only once the file is complete. WSWAP: a short state file undoes the switch as when WINOLDAP's buffer is missing (172h = 08h). Not testable in DOSBox (no write error after a successful 36h). | 7fed0e8 |
| C2 | REAL | wswap.asm:913-916 loaded the startup-info structure's instance list and read `[es:si]` with no check for 0000:0000, which the interface allows (a structure with no instance data). SW2 declares one: w16's WSWAP then took the vector table at 0000:0000 for items, their lengths together over 512, and dropped all instance data, WIN.COM's own item (the last SFT block's link) too: SW2's byte, 'W' in Windows, stayed 'W' in the MS-DOS Prompt (`client` before the fix). 3.1: skips it, 'A' (the start's value) in the prompt. | A null list is skipped (`inst_scan`, swap.inc). Test: swap.sh `client` (SW2.LOG "V A"). | 3bddb55, 7fed0e8 |

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | REAL (narrow) | try_switch (dswap.asm:366-384) tested `in_switch` and set it nine instructions later with the caller's IF. From INT 16h's first pass and INT 28h IF is 0 (an INT clears it), so no race there; but in INT 16h's blocking-wait loop after the first `hlt` (:303-315) IF is 1, and the timer's try_switch (int08, :329-336, which needs in_bios 0, as it is in that loop: H7) could make a whole switch inside that window; back in the loop's try_switch, `pending` was 0 and switch_out took it for Alt+Esc (:818-825). A window of a few instructions per hot key. | Tested and set with interrupts off (pushf/cli ... popf). No test (timing). | 7fed0e8 |
| H2 | REAL (by reading) | try_switch looked at InDOS only (:371-378). During INT 24h DOS has InDOS 0 and its critical error flag (the byte before InDOS, DOS 3.1+; RBIL INT 21h 34h) set. A program whose own INT 24h handler waits for a key through the BIOS (INT 16h, our int16's blocking loop calls try_switch) would be switched away inside DOS's critical error, and switch_out's INT 21h calls (3Ch, 40h, 47h, ...) re-enter DOS. COMMAND.COM's own handler reads through DOS (InDOS 1, no switch). Not producible in DOSBox (no critical errors from its drives). | No switch while the critical error flag is set. | 7fed0e8 |
| H3 | REAL (in part FALSE) | switch_out (:758-780) went on without keeping the SDA when InDOS was set and `sda_len` was 0 (INT 21h 5D06h failed) or over SDAMAX (2048). InDOS is set there only on the INT 28h path (try_switch lets InDOS 1 through only from INT 28h); the switch's own DOS calls are allowed from INT 28h (functions above 0Ch), so the claimed crash is not during switch_out but at the resume, when the console call's stack and state (in the SDA) were another session's. On DOSBox 5D06h gives CX 80h (fits); a DOS with more than 2048 bytes to keep in DOS would reach it. | try_switch does not switch from INT 28h when the SDA cannot be kept; the SDA's buffer is sized from 5D06h's CX (up to 8 KB, after the image) instead of 2048 fixed bytes. | 7fed0e8 |
| H4 | REAL | wswap.asm:389-396 told the clients 1 (query suspend) and 2 (suspend) and on no room or no buffer (`.nodisk`, `.copyfail`, :590-631) returned without telling them Windows was active again. 3.1 (SW2, `nodisk`): N1 100F, N2 100F, then N3 100F CX=1, N4 100F CX=1. w16: nothing after N2. | 3 and 4 (CX=1) after an undone switch. Test: swap.sh `nodisk` (SW2.LOG). | 3bddb55, 7fed0e8 |
| H5 | REAL | Neither notify (wswap.asm:364-373, dswap.asm:563-570) cleared DF after calling a client; switch_out's `rep movsw` of the vector table followed (wswap.asm:477). SW2 F (every notification returns with DF set, `clientdf`): 3.1 runs the whole scenario as with SW2 (but SW2's first command in the prompt logged nothing, twice: by all appearances it ran with DF still set and read its command tail backwards; SW2 now clears DF itself); w16 started the prompt but hung at Alt+Tab out of it (a blank text screen, Windows never back, no SW2.LOG). | `cld` after each client. Test: swap.sh `clientdf`. | 3bddb55, 7fed0e8 |
| H6 | HARDENING | The items' total (wswap.asm:936-948) was summed in 16 bits; items of 64 KB or more together could wrap under 512 and overflow inst0/instw. Only a client declaring that much instance data, which real-mode clients do not. | The carry counts as too large (no items kept). | 7fed0e8 |
| H7 | REAL (minor) | int16's blocking loop polled the BIOS (`call far [old16]`, dswap.asm:309-312) without `in_bios`, so the timer could switch from inside the BIOS's INT 16h. The other BIOS calls go through int10/int13/int16 `.c`, which count it. | in_bios counted around the poll (flags kept). No test (timing). | 7fed0e8 |
| H8 | FALSE (in part HARDENING) | ivt_out (dswap.asm:847) comes after the file I/O and Alt+Tab's screen. The program's INT 08h/1Ch hooks running then are the program's own code, still in memory (it is frozen only at the end), as they ran a moment before; Alt+Tab installs its own INT 09h (:997-1006) and draws with INT 10h; the PIC masks are the program's, and a program that had IRQ 1 masked could not have sent the hot key. The one real exposure is INT 24h: a disk error in the swap file's I/O went to the program's handler or COMMAND.COM's "Abort, Retry, Fail", whose Abort would end DSWAP (the current PSP) with its hooks in place. | DSWAP's own INT 24h (fail) during its file I/O for a switch; the program's handler is the one kept with its vectors. | 7fed0e8 |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | HARDENING | `mul cx; mul bx` (wswap.asm:440-441, dswap.asm:1459-1460) dropped DX of sectors x bytes: wrong only for 64 KB clusters, which MS-DOS 3-6's FAT does not make (32 KB at most). | 32-bit free bytes, saturated (`FREE_BYTES`, swap.inc). | 7fed0e8 |
| M2 | REAL (DF); FALSE (IF) | try_switch's `cld` (:399) reached the caller of INT 16h: int16's `.c` passes DSWAP's flags to the BIOS and bios_ret (:347-360) returns the BIOS's DF. IF: bios_ret and int28's chain end in an IRET or keep the caller's IF/TF (:353-356), so the caller's IF is never changed. | try_switch keeps the flags (pushf/popf). | 7fed0e8 |
| M3 | HARDENING | True that the program's end (:1875-1884) put the vectors back but not the PIC masks. Not observable: after a program ends DSWAP always goes back to Windows (`to_windows`, :1912-1926), whose WSWAP sets Windows' masks at once (wswap.asm:746-749); only a program that ends with IRQs masked is affected, and then only for WIN.COM's INT 10h and WSWAP's loading. | The masks as at DSWAP's start, with the vectors. | 7fed0e8 |
| M4 | HARDENING | at_unhook (:1053-1056) cleared 0040:0017h bit 3 and 0018h bit 1 but not 0040:0096h bit 3 (right Alt) when Alt+Tab ate the key's release. SW2 K after right Alt+Tab to Windows and back, and after a left Alt tap (`client`): 00 00 10 on 3.1 and on w16, before and after the fix (DOSBox-X's BIOS or Windows clears it); not seen to stick. | Bit 3 of 0096h cleared too (the key is up). Test: swap.sh `client` (the K lines). | 3bddb55, 7fed0e8 |
| M5 | FALSE (reads); HARDENING (map mask) | The resume's screen reads (dswap.asm:1594-1600, .back :920-927) are not checked, but the memory came back first through R's checked reads (r_readp, r_resume), from the same file, and nothing better than going on exists then (the program is back; the screen alone would be wrong). vmem_read's planar error path (:1373) did skip restoring the sequencer's map mask. | The map mask set back on both paths. | 7fed0e8 |
| M6 | FALSE | sda_back (:953-962) copies with IF=1, but the SDA's first two bytes are the critical error flag and InDOS (RBIL), restored first (ascending), so an interrupt during the copy sees DOS busy; DSWAP's own hooks see `in_switch` (1 until sw_back). | None. | - |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | FALSE | wswap's notify uses BP (:353) without keeping it, but every caller keeps BP itself: switch_out and t_resume run inside int21's `pusha`/`popa` (:272, :288, :784), t_init never uses BP. | None. | - |
| L2 | HARDENING | vmem_write's planar path (:1240-1262) left the graphics controller's read mode and read map select as it set them; only after a failed write does the program go on in that mode (on success the program's mode is set again when it comes back). | Both registers read first and set back at the end. | 7fed0e8 |
| L3 | FALSE | R checks only that T is where it was (dswap.asm:124-127, wswap.asm:146-149), but WIN.COM runs every WSWAP and DSWAP into the same arena (nothing allocates between its children: ARCHITECTURE.md 9.5), and the file is the swapper's own, hidden and read-only. Only a stale or forged file. | None. | - |
| L4 | REAL (in part FALSE) | `sw_done` (wswap) was written, never read; `p_kbflags` and `exit_code` (dswap) never used. `rs_sum` is not dead: it makes the state file 34 bytes, the size of Windows 3.1's (wswap.asm:49-59). | sw_done, p_kbflags, dswap's exit_code removed. | 7fed0e8 |

## Beyond the audit

Found by SW2 (`client`) and fixed:

- Windows 3.1's WSWAP answers INT 2Fh 4B80h (WINOLDAP asking for a DOS
  program) itself, registers unchanged (SW1), and does not pass it on: SW2,
  hooked before WIN, never sees it. w16's passed it on. Now answered.
- Windows 3.1's DSWAP asks for instance data (INT 2Fh 4B05h) at a DOS
  session's start, and a session's values stay its own across switches:
  SW2's byte set to 'D' in the prompt was 'D' again after Alt+Tab to
  Windows and back. w16's DSWAP did not handle instance data at all (WSWAP
  loaded the start's values again). DSWAP now keeps the items' values at
  its start, the session's at a switch (with the program's), loads the
  start's when switching away or when the program ends and the session's
  when it comes back. The enumeration and the copies are shared with WSWAP
  (`INST_DATA`, `INST_CODE` in swap.inc).
