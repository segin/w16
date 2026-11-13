# w16 system driver (`SYSTEM.DRV`, module `SYSTEM`)

w16's own system driver for Windows 3.1: the timers every other part of
Windows runs on (USER's SetTimer, the display driver's cursor, COMM's
notifications, KERNEL's LRU sweep), the millisecond count behind
GetTickCount, the coprocessor's state, and the drive and timer
inquiries KERNEL makes. Like every w16 driver it is a genuine Windows 3.1
driver (ARCHITECTURE.md 6.7): the interface of Windows 3.1's
`SYSTEM\SYSTEM.DRV` and nothing w16-specific, and it replaces that
driver as it is (`SYSTEM.INI [boot] system.drv=system.drv`, as Setup
leaves it on a standard PC/AT). Standard mode; 386 Enhanced mode not
tried.

`system.asm` (NASM) and `system.lnk` (wlink); `make` builds
`build/dos/SYSTEM.DRV` (one fixed code segment, one fixed data segment,
expected Windows version 3.10) and the install tree's copy.

The 3.1 DDK does not document SYSTEM.DRV (Schulman et al., *Undocumented
Windows*, ch. 9). Sources: the NE tables of Windows 3.1's file (format
facts: names, ordinals; `tools/neinfo.py`, `docs/exports/SYSTEM.txt`),
Schulman ch. 9 and Pietrek, *Windows Internals*, ch. 1 (signatures,
who calls what), and black-box observation of Windows 3.1's driver with
the probes below. Microsoft's file was never disassembled and nothing
behind a pointer it returns was read.

## Interface

Who calls it (`tools/neimports.py --importers SYSTEM`; SYREC's record of
a boot, below): USER imports 1-6, COMM 2 and 6, MOUSE 6; KERNEL finds 1,
2, 5, 7 and 10 with GetProcAddress (Pietrek ch. 1); nothing imports 8, 9
and 20. The bytes each entry takes off the stack and the registers it
leaves were measured on Windows 3.1's driver (SY1, `regs` lines: every
entry called with marker values in all registers).

| Ord | Name | Pops | Returns, does |
|---|---|---|---|
| 1 | `InquireSystem(wWhat, wDrive)` | 4 | below |
| 2 | `CreateSystemTimer(wRate, lpProc)` | 6 | a handle (18h, 20h, ... 50h), 0 when all 8 are in use |
| 3 | `KillSystemTimer(hTimer)` | 2 | 0; hTimer when it is no timer in use |
| 4 | `EnableSystemTimers()` | 0 | takes INT 8; nothing when already enabled |
| 5 | `DisableSystemTimers()` | 0 | INT 8 back as Enable found it; nothing when not enabled |
| 6 | `GetSystemMsecCount()` | 0 | DX:AX, the msec count |
| 7 | `Get80x87SaveSize()` | 0 | 94 (5Eh) with a coprocessor, else 0 |
| 8 | `Save80x87State(lpSave)` | 4 | FNSAVE there (the coprocessor is initialised after) |
| 9 | `Restore80x87State(lpSave)` | 4 | FRSTOR from there |
| 10 | `WEP(bSystemExit)` | 2 | 1 (resident name) |
| 20 | `A20_Proc(w)` | 2 | 2, nothing done (Schulman; SY1: no vector, no BIOS data changed) |

Registers: Windows 3.1's driver keeps SI, DI, DS, BP and the direction
flag everywhere and returns with interrupts as they were; it changes BX
and CX in InquireSystem's drive inquiries, CreateSystemTimer and
KillSystemTimer, BX and ES in EnableSystemTimers (INT 21h AH=35h), and
leaves DX (and AX in the void ones) as its work left them (Enable and
Disable: 2508h in AX). w16's keeps every register but the results; a
caller can rely on nothing more than 3.1's keeps.

**InquireSystem** (Schulman's three-argument form is wrong for 3.1: it
takes two words; SY1):

- `wWhat` 0: DX:AX = 0:D68Dh, 54925, the timer's period in microseconds
  (`wDrive` not looked at).
- `wWhat` 1: drive `wDrive` (0 = A:): AX 0 no drive, 2 removable, 3
  fixed, 4 remote; DX 0, except 1 for B: when the machine has one floppy
  drive (DOS's one-drive logic: B: is A:). A: and B: come from the BIOS's
  equipment word: its bits 6-7 (drives - 1), so with bit 0 clear too
  (DOSBox without floppy images: 3.1's driver answers A: removable and
  B: 1:0); C: and up from DOS: IOCTL 4409h (remote: bit 12; an error: no
  drive), then 4408h (0 removable, else fixed), through KERNEL's
  NoHookDOSCall (Schulman lists the DOS calls 3.1's makes). KERNEL asks
  for drives 25 down to 0 at boot and GetDriveType asks this.
- `wWhat` 2: KERNEL's "one-drive logic" switch ((2, 0) at boot, (2, 1)
  when leaving for DOS: Pietrek ch. 1). 3.1's answered 0:0 and changed
  nothing SY1 could see (the real-mode interrupt table, the BIOS data
  area, protected-mode vectors 00h-7Fh, the drive answers): this driver
  keeps the value and answers 0:0.
- Any other `wWhat`: 0:0. A drive beyond Z: is not in the interface:
  3.1's driver answered 0 for 26, 27 and FFFFh but 3 for 0101h; this one
  answers 0 for all (SY1 logs them, not compared).

## Timers

All observed on Windows 3.1's driver with SY1 (part C), whose timer
procedures record each call in a fixed block:

- **Eight timers**, handles 18h, 20h, ... 50h, the first free one given
  out. At boot COMM.DRV takes 18h (100 ms), USER 20h (rate 0: the display
  driver's CheckCursor), KERNEL 28h (500 ms, LRU sweep), USER 30h (its
  SetTimer timer, 10000 ms first): a program gets four (38h-50h; SY1). A
  timer with a null procedure is accepted (3.1: the handle); this driver
  never calls a null procedure.
- **Rate**: a timer of R ms is called every `max(1, R * 1000 / 54925)`
  ticks (truncated): rates 0-109 every tick, 110-164 every 2nd, 165-274
  every 3rd, 275 every 5th, 500, 530 and 549 every 9th, 550 every 10th,
  1000 every 18th, 1100 every 20th, 2000 every 36th, 10000 every 182nd
  (SY1, 30 rates; 54.925 not 55: 10000 / 55 would be 181). The interval
  is constant (no carried remainder), and the first call comes that many
  ticks after the timer was made. (USER's caret blink of 530 ms becomes
  550 ms in USER, not here: SYSTEM calls a 530 ms timer every 494 ms.)
- **The tick** (INT 8, IRQ 0, taken in protected mode with INT 21h
  AH=25h; DOSX routes real-mode IRQ 0 to it): the handler that was there
  before (the BIOS: 0040:006C, end of interrupt, INT 1Ch) is called first
  (SY1: in every procedure call the BIOS tick count already counts that
  tick and the PIC's in-service bit is clear); then the msec count goes
  up; then, with interrupts enabled, each timer due is called in table
  order: AX its handle, DS the driver's data, ES and the stack the
  interrupted code's (SY1: SS the interrupted program's), interrupts
  enabled, IRQ 0 unmasked. The procedure returns with RETF.
- **No reentry**: a tick that comes while procedures are running (one
  that enables interrupts and waits, SY1's reentrancy test) goes to the
  BIOS and counts the msec, and no timer is counted down or called for
  it: after a procedure waited 3 ticks, every timer's calls are 3 ticks
  late and no call was missed twice (SY1: a 275 ms timer made at the
  same time was first called at tick 7, not 4).
- **KillSystemTimer** works from inside a timer procedure, its own timer
  too (SY1: killed on its 3rd call, no 4th).
- **The PIT is not reprogrammed** (Schulman): SY1 counts 40 INT 8
  interrupts in 40 BIOS ticks with a hook of its own in front, and the
  PIT's count reaches above F000h.

**The msec count** counts 54.925 ms a tick (54 and 925/1000, the
thousandths carried), from 0 when the driver loads, only while timers
are enabled: SY1 sees 109 after 2 ticks (USER's InitApp reads 6Dh) and
219 after 4; 54 or 55 between ticks, 54 every 13 or 14 ticks; 2196 or
2197 over 40 ticks; the same value in a timer procedure as right after
the tick; no change within a tick. While disabled it stands still, and
EnableSystemTimers does not catch up (SY1: 0 over 45 ticks disabled).

**Disable/Enable**: DisableSystemTimers puts back the INT 8 vector that
EnableSystemTimers found (3.1's does so even when another handler was
hooked in front of it since; so does this one) and the timers stop;
Enable takes INT 8 again. Twice in a row each does nothing the second
time (SY1, with 3.1's and with this one). WINOLDAP calls Disable before a
DOS program (DisableOEMLayer) and Enable after; USER at Windows' exit,
and KERNEL after WEP (SYREC). (In DOSBox, DOSX puts a real-mode stub in
the real-mode INT 8 vector while a protected-mode handler is installed
and the BIOS's back when DOSX's own is: seen in SY1's snapshots with
both drivers; that is DOSX's.)

## The coprocessor

`Get80x87SaveSize` is 94 when the BIOS's equipment word says there is a
coprocessor (bit 1; Schulman: INT 11h, and FNINIT at start-up), else 0;
KERNEL asks once at boot. Save is FNSAVE into the caller's 94 bytes
(3.1's: the coprocessor initialised afterwards, control word 037Fh, all
registers empty), Restore FRSTOR (SY1: a known state saved, the
coprocessor reset, restored: control, status, tags and ST(0) back).
Without a coprocessor both do nothing.

## How it was observed

- **SY1** (`tests/apps/sy1.c`, `sy1s.asm`), run as the shell by
  `tests/sysprobe.sh` on Microsoft's KRNL286, DOSX and USER, once with
  Windows 3.1's SYSTEM.DRV and once with w16's: part A the ordinals; B
  every entry's stack use and registers, InquireSystem over its inputs
  (with the machine's own DOS and BIOS answers logged beside),
  snapshots of the interrupt vectors and BIOS data around the calls, the
  coprocessor; C timers (capacity, handles, 30 rates, context, reentrancy,
  a timer killing itself); D the msec count against the BIOS tick count,
  INT 8 interrupts per tick, the PIT; E Disable/Enable; F WEP. Values
  that are the host's timing or the run's layout are logged with "~"
  and not compared.
- **SYREC** (`tests/apps/syrec.asm`): a recording SYSTEM.DRV in front of
  the driver under test (renamed SYSTGT by `tools/nerename.py`): every
  call through a whole boot and Windows' exit, with its caller, arguments,
  results and the bytes taken off, and each timer procedure call (decoded
  by `tools/syrec.py`). Only values are recorded, never memory behind a
  pointer. With either driver behind it, some of its own data words get
  written at boot by something else (pairs that look like AX = 1 and the
  driver's DS pushed: an interrupt frame on a stack in SYREC's segment;
  not seen with either driver loaded directly as SYSTEM, where a 4 KB pad
  at the start of w16's data stayed zero), so SYREC keeps a 4 KB pad in
  front of its records. Not understood.
- `sysprobe.sh fd`: part B again with DOSBox floppy images in A: and B:,
  then A: only (`tests/scripts/mkfloppy.py`).

## Verified

- `tests/sysprobe.sh` on the build host: SY1's logs identical (all but
  the "~" lines and fields), SYREC's call lists from boot to exit
  identical (98 calls: KERNEL's, USER's, COMM's and SY1's, results and
  bytes taken off), the floppy configurations identical.
- Boot-test cases `sysdrv` (kbtype, tabs, alttab, helpmenus, dragoff,
  pmkeys under real input on Microsoft's stack: every screen as with
  Microsoft's driver), `sysdrvall` (every w16 part), `sysdrvwoados` (the
  MS-DOS Prompt and back: timers disabled and enabled), `sysboot`
  (Microsoft's stack, w16's WIN.COM/DOSX/KRNL286, an emulated 80286),
  `sysclock` (Clock's scenarios), `sysprobe`.
- `tools/irqcheck.py --fixed`: every segment fixed (`make test`).

## Not verified / not done

- 386 Enhanced mode (VTD virtualises the timer there; not run).
- What 3.1's driver does for drive numbers beyond Z: (above), and for a
  KillSystemTimer handle that is inside the table but not an entry's
  (not tried on 3.1's: it could write into its data).
- Machines without a coprocessor in the BIOS's equipment word (DOSBox
  always reports one).
- Network and CD-ROM drives (remote: AX 4) and removable drives other
  than A:/B: (DOSBox offers neither under the harness).
- InquireSystem(2): nothing 3.1's does was seen.
