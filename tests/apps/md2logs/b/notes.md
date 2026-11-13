# MD2: what MOUSE.DRV's PowerEventProc (ordinal 7) does

All black-box: Microsoft's MOUSE.DRV is only called through its export
and watched from outside (MS3 records the BIOS calls; nothing of its code
was read). The probes run with `tests/md2bprobe.sh [1 2 3 4]` on the
reference Windows 3.1 install (Microsoft's KRNL286, DOSX, USER; Standard
mode, DOSBox, headless). The logs quoted here are in `tests/apps/md2logs/b/`
(copies of `build/md2/...`).

| Piece | What it is |
|---|---|
| `tests/apps/md2b.c` (+ `md1s.asm`) | MD2B.EXE, the shell; `[w16] md2phase` 1, 2, 3 |
| `tests/dos/ms3b.asm` | MS3B.COM: MS2 + INT 15h AH=53h (except 05h/06h idle/busy) + an "armed" window that records every INT 15h and INT 2Fh during one call |
| `tests/dos/ms4b.asm` | MS4B.COM: a minimal real-mode APM 1.0 BIOS with scheduled events (DOSBox has no APM BIOS) |
| `drivers/mouse/mouse.asm -DPWRTRACE` | MOUSEPT.DRV, test build only: w16's driver reporting each PowerEventProc / GetSetMouseData call (its words and caller CS:IP) as INT 15h AX=C2E0h... / C2E8h... |
| `-DPWRTRACE -DPWRFAIL` | MOUSEPF.DRV: as MOUSEPT, but returns -1 to event 1 |
| `tests/scripts/input/md2b.txt` | 24 moves of 1..24 pixels (one PS/2 packet each) |
| `tests/szdd.py` | SZDD expander for POWER.DR_ |

## Findings

1. **Signature.** `PowerEventProc(WORD wEvent, DWORD dwParam)` (Pascal
   order; the first word pushed is the event). It takes 6 bytes off and
   returns DX:AX = 0 for every input tried (p1ms.log: 21 codes in each of
   the three words, markers, driver enabled and disabled). Only the
   first-pushed word has an effect: 2 in the second or third word does
   nothing (`P 0000 0002 0000`, `P 1234 5678 0002`), and 2 or 3 in the
   first word re-initialises whatever the other words hold (`P 0002 1234
   5678`). POWER.DRV passes 0 in both other words (p3w16.log: C2E1/C2E2
   BX=0000 every time).
2. **What it does.** With the driver enabled, wEvent 2 or 3 makes
   Microsoft's driver program the PS/2 device again with the same INT 15h
   sequence its Enable uses: C205h (BH 3), C201h, C202h, C204h, C203h
   (BH 3), C207h (handler), C206h (BH 1), C202h (BH 2), C200h (BH 1)
   (p1ms.log, after `P 0002 0000 0000`; compare `== Enable` in the same
   log). Repeated calls repeat it. Every other value (0, 1, 4-0Ch, 0Fh,
   10h, 48h, 80h, 100h, 5300h, 8000h, FFFFh) makes no recorded call: no
   PS/2 BIOS, no APM BIOS, no INT 2Fh, no INT 33h, and no other INT 15h
   (the armed window is empty). After `Disable` (before Enable again),
   2 and 3 do nothing either: the re-initialisation happens only while
   the driver is enabled.
3. **Event codes are WM_POWER's**, not APM's: 1 = PWR_SUSPENDREQUEST,
   2 = PWR_SUSPENDRESUME, 3 = PWR_CRITICALRESUME (win16.h; MSDN WM_POWER).
   This was the experiment that could have proven the hypothesis wrong:
   with APM codes (2 suspend request, 3 normal resume, 4 critical resume)
   4 would re-initialise and 2 would not. Observed: 2 and 3 do, 4 does
   not (p1ms.log).
4. **The caller is POWER.DRV**, by name or ordinal at run time (nothing
   imports MOUSE.7). With POWER.DRV in `[boot] drivers=` (as Setup's APM
   machine types put it) and MS4 as the APM BIOS (p3w16.log, MOUSEPT):
   - APM event 0002h (suspend request): POWER.DRV sends WM_POWER(1) to
     windows, then calls `PowerEventProc(1, 0)`, then APM 5307h set
     state suspend (BX 1, CX 2);
   - 0003h (normal resume): WM_POWER(2), then `PowerEventProc(2, 0)`;
   - 0004h (critical resume): WM_POWER(3), then `PowerEventProc(3, 0)`;
   - 0001h (standby request): straight to 5307h standby (CX 1), no
     WM_POWER and no PowerEventProc; the following 0003h gives
     WM_POWER(2) and `PowerEventProc(2, 0)`.
   The caller's CS belongs to `C:\WINDOWS\SYSTEM\POWER.DRV` (ToolHelp's
   GlobalEntryHandle on the CS the call came from). With Microsoft's
   driver in the same set-up (p3ms.log) the Enable-like C205h...C200h
   sequence follows WM_POWER(2) and WM_POWER(3) each time, and nothing
   follows WM_POWER(1): the same behaviour as under MD2's direct calls.
5. **The return value does not veto a suspend.** MOUSEPF returned -1
   (PWR_FAIL; checked by MD2 phase 1 on MOUSEPF: `P 0001 ... DX:AX=FFFF:FFFF`)
   to `PowerEventProc(1, 0)`, and POWER.DRV still set the suspend state
   (p4w16.log: 5307h CX=2 right after). Microsoft's driver returns 0.
6. **GetSetMouseData is not called** by POWER.DRV or anything else during
   boot, idle and all five power events (no C2E8h record in p3w16.log,
   p4w16.log).
7. **Events after a resume.** The event procedure given to Enable stays in
   place (events keep coming to MD2's procedure after `PowerEventProc(2)`
   and `(3)`), and **the second packet after each re-initialisation is
   dropped, as after Enable** (p2ms.log, packets are the BIOS's calls of
   the C207h handler, `R5 ... X`):
   - after Enable: packets X=0,1,2,3,4 -> events 2,3,4 (0: no change, no
     event; 1 lost)
   - after `PowerEventProc(2)`: 5,6,7,8 -> 5,7,8 (6 lost)
   - after `(1)`: 9,10,11 -> 9,10,11 (none lost)
   - after `(3)`: 12,13,14,15 -> 12,14,15 (13 lost)
   - after `(4)`: 16,17,18 -> all
   - after `(2)`: 19,20,... -> 19,21,22,... (20 lost)
   The dropped packets did reach the driver's C207h handler (MS3 R5
   records), so the driver, not the BIOS, discards them. The drop is
   therefore tied to the shared device set-up sequence, not to Enable as
   such. Why the second packet: still unknown.

w16's driver (stub returning 0) matches 1, 5 and 6 but not 2 and 7: its
event stream after `PowerEventProc(2/3)` has no drop and it makes no BIOS
calls (p1w16.log, p2w16.log).

## Not established

- What Microsoft's driver does on event 1 beyond "no BIOS, INT 2Fh or
  INT 33h call, events keep coming": port 60h/64h accesses cannot be seen
  from MS3, and internal state cannot be seen at all.
- 386 Enhanced mode (VPOWERD) and APM's protected-mode interface: MS4
  offers real mode only and `[power.drv] UseRealMode=1` was set.
- Whether anything in Windows 3.1 other than POWER.DRV calls ordinal 7.
- Whether a nonzero dwParam is ever passed (POWER.DRV passed 0 each time).
