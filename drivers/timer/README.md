# w16 multimedia timer driver (`TIMER.DRV`, module `TIMER`)

w16's own timer driver for Windows 3.1: the installable driver
MMSYSTEM.DLL opens through `SYSTEM.INI [drivers] timer=timer.drv`, which
serves MMSYSTEM's time* functions (timeGetTime, timeGetSystemTime,
timeSetEvent, timeKillEvent, timeGetDevCaps, timeBeginPeriod,
timeEndPeriod). Like every w16 driver it is a genuine Windows 3.1 driver
(ARCHITECTURE.md 6.7): it replaces Windows 3.1's `SYSTEM\TIMER.DRV`
under Microsoft's MMSYSTEM.DLL as it is. ARCHITECTURE.md 21 has the
interface as observed and the results; docs/notes/timer-handover.md the
story and the data.

`timer.asm` (NASM), `timer.lnk` (wlink), `timer.rc` (the string table and
the version resource from `tools/mkver.py`); `make` builds
`build/dos/TIMER.DRV` (one fixed code segment, one fixed data segment,
module flags 8309h and 08h and expected Windows version 3.10 as 3.1's,
`tools/nemark.py`) and the install tree's copy.

Sources: the NE tables of Windows 3.1's file (format facts: module name,
description `timer:Timer`, WEP 1 and DriverProc 2, both resident, the
flags; its imports of MMSYSTEM's DriverCallback, StackEnter and
StackLeave; its string 1), the Windows 3.1 SDK (Multimedia Programmer's
Guide ch. 6, Programmer's Reference Vol. 2 and 3: the time* functions,
TIMECAPS, the error codes; Vol. 1 ch. 25: installable drivers), the
argument list of MMSYSTEM's DriverCallback from Wine's mmsystem.spec of
before 2002 (X11 licence), and black-box observation with the probes
below. Microsoft's TIMER.DRV and MMSYSTEM.DLL were never disassembled; the
logging driver TMS.DRV sits between them and records what passes.

## Interface

MMSYSTEM sends the time messages straight to DriverProc (identifier 0,
its handle for the driver), not through USER; timeGetTime jumps to
DriverProc with the application's return address. None of the messages
is documented; all were read off TMS.DRV's records:

| Message | Parameters | Result (Standard mode) |
|---|---|---|
| 0800h kill event | lParam1 the ID | 0; 97 (TIMERR_NOCANDO) for an ID not in use |
| 0804h set event | lParam1 -> {WORD delay, WORD resolution, DWORD callback, DWORD dwUser, WORD flags} | the ID; 0 for a delay of 0 or when 15 events are in use |
| 0808h time | | DX:AX milliseconds |
| 080Ch capabilities | lParam1 -> TIMECAPS, lParam2 its size | {1, 65535} and 0 for size 4; else 97, nothing written |
| 0810h begin period | lParam1 ms | 0 for 1-55 (counted); else 97 |
| 0814h end period | lParam1 ms | 0 when begun; else 97 |

The usual messages come through USER: message 0 0, DRV_LOAD 1, DRV_ENABLE 1, DRV_OPEN 1, DRV_INSTALL 2,
DRV_DISABLE 1, DRV_FREE 0, everything else 0 (DefDriverProc).

386 Enhanced mode as 3.1's answers there: timeGetDevCaps takes any size
from 4, timeBeginPeriod, timeEndPeriod and timeKillEvent answer 0 to
anything, 8 events at once, IDs (count << 3) | slot returned
sign-extended in DX:AX, flags bit 1 (not bit 15) marks an event that is
never called.

## How it keeps time

- Channel 0 of the 8254 in mode 2 at the shortest period anyone asked
  for: a timeBeginPeriod in force, or an event's resolution (its delay
  when the resolution is 0), 1-55 ms; else 65536 counts (the BIOS's
  54.9 ms). Written at once (control word and count) when that shortest
  period changes, as 3.1's does; the counts of the period under way are
  added to the clock first.
- Every interrupt adds the counts that passed to the millisecond clock
  (in 1/65536 ms) and toward the next BIOS tick; the INT 8 handler that
  was there before (SYSTEM.DRV's, then DOSX and the BIOS) is called once
  per 65536 counts, so the BIOS time of day, GetTickCount, SYSTEM.DRV's
  timers and WM_TIMER advance as without the driver; otherwise the
  driver sends the end of interrupt itself.
- timeGetTime: the clock plus the counts since the last interrupt (the
  8254's latched count; a count that started again before its interrupt
  was counted is caught through the PIC's IRR and by comparing with the
  last reading), never less than a value given out before.
- After the end of interrupt, with interrupts enabled, each event due
  (its time passed) is called: MMSYSTEM's StackEnter, DriverCallback
  (callback, DCB_FUNCTION, the ID, message 0, dwUser, 0, 0), StackLeave,
  so the callback runs on MMSYSTEM's interrupt stack with the driver's
  DS, as with 3.1's (same SP at entry). Never two callbacks at once: an
  event due while one runs waits for the next interrupt after it.
  Periodic events are due again a delay later; one-shot events are freed
  before their call.
- 386 Enhanced mode: the same, on VTD's virtual 8254 (3.1's driver uses
  VTDAPI.386 instead; not done here, see ARCHITECTURE.md 21.5). `POPF`
  does not restore the interrupt flag of Windows' code there, so every
  `cli` is undone with an explicit `sti`.

## Verified

`tests/timer/tscen.sh` (boot-test case `timer`, parts `s` and `3`):
TPR.EXE's log under Microsoft's MMSYSTEM with Microsoft's TIMER.DRV and
with w16's, plain and with TMS.DRV recording every message: every line
identical (225 plain, 1042 with TMS) in Standard mode after the
masks `tests/timer/tcmp.py` lists, the measurements within its limits;
in 386 Enhanced mode the same lines identical, the timings shown.
Part `qt`: QuickTime for Windows' sample movie on stock 3.1 with only
w16's TIMER.DRV plays as on 3.1 (the same frames, 12 s); the boot-test
case `quicktime` (w16's install tree over 3.1) runs it too.

## Not verified, not done

- 386 Enhanced mode timing: VTD's virtual 8254 makes one-shot events
  20-80 ms late and a 5 ms periodic event about 65 calls a second
  instead of 200 (3.1's, through VTDAPI.386, are exact); the callbacks
  run on MMSYSTEM's stack, 3.1's on VTDAPI's.
- 3.1's own timing quirks are not copied (ARCHITECTURE.md 21.2).
- Machines other than DOSBox's emulated PC/AT; an 80286 (the code is
  286 code, untried there); w16's own WIN386.
