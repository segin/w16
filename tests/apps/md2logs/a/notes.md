# MD2: MOUSE.DRV's GetSetMouseData (5), PowerEventProc (7) and the packet lost after Enable

Black-box observation of Windows 3.1's `SYSTEM\MOUSE.DRV` ("Microsoft
Mouse - Version 8.20"), headless in DOSBox, Microsoft's KRNL286, DOSX
and USER (`tests/dropin.sh -r` via `tests/inputrun.sh`). No Microsoft
code was disassembled or copied: the driver was only called and its BIOS
calls and results were recorded. (MS3's first version also dumped the 32
bytes behind any argument that looked like a far pointer, which recorded
bytes of Microsoft code and data, e.g. USER's event procedure passed to
Enable. That dump was removed from MS3 and from the logs before this was
committed to master, and no conclusion here used it.) The files of the retail
Microsoft Mouse 8.20 package (`POINT.EXE`, `POINTLIB.DLL`,
`POINTER.EXE`, `POINTER.DLL`) and Windows 3.1's `POWER.DRV` (from
`win31/src/POWER.DR_`) were also run as black boxes. They are not part
of the repository.

## Tools

| File | What it is |
|---|---|
| `tests/apps/md2a.c`, `md2ax.c`, `md2as.asm` | MD2A.EXE, run as the shell. It calls MOUSE's entries directly, runs another program (`[w16] md2run`) and writes out the MS3, MS2 and MS4 logs. Modes: 1 GetSetMouseData flag scan, 2 PowerEventProc arguments, 4 settings and then input, 8 the lost packet, 16 PowerEventProc while disabled. |
| `tests/apps/ms3a.asm`, `ms3a.lnk` | MS3A.DRV, a recording MOUSE module. It stands in front of `MOUSX.DLL`, a scratch copy of the real driver whose module name was changed in the NE header (`tools/nerename.py`). Every call from any module is recorded with the caller, the registers, the 8 stack words, 32 bytes at each readable far pointer before and after, the results and the number of bytes popped. |
| `tests/dos/ms4a.asm` | MS4A.COM, a pretend APM 1.0 BIOS (INT 15h AH=53h) that records its callers. `MS4 N`: the Nth "get PM event" poll returns a suspend request, and the poll after "set power state" returns a normal resume. |
| `tests/dos/ms2.asm` | MS2.COM (existing): the INT 15h C2xxh, INT 33h and C207h handler call log. |
| `tests/md2aprobe.sh` | The runner. MS3 is in place by default; `-M` uses the real driver alone, `-w` / `-d FILE` use another driver. |
| `tests/md2adrop.py` | Condenses a mode 8 log into packets given to the handler against events reported. |
| `tests/scripts/input/md2a.txt`, `md2adrop.txt` | The input scripts. |
| `tests/apps/md2logs/a/` | The logs cited below (TK.LOG copies). In `scan3-ms.log` the bytes of the driver's built-in tables are left out. |

## GetSetMouseData (MOUSE.5)

**Who calls it.** In every boot with MS3 in place, Windows itself makes
no call to ordinal 5 or 7; only USER calls 1 (Inquire) and 2 (Enable)
(`md2logs/a/r3.log`, `apm2.log`, records C #1 and C #2). The callers are
Microsoft Mouse 8.20's programs:

- `POINTLIB` segment 1 (the Mouse/BallPoint Control Panel DLL).
- `POINT` segment 1.
- `GROWSTUB` (`POINTER.EXE`, the pointer-growth program). It has no
  import record for MOUSE, so it binds at run time.

(Calls C #3 to C #12 in `md2logs/a/r3.log`, and `build/md2/r2/md2.log`
for GROWSTUB.)

**Signature** (from the callers' stack words, `r3.log`, and confirmed by
direct calls in `scan3-ms.log`):

    DWORD FAR PASCAL GetSetMouseData(LPVOID lpData, DWORD dwFlags);   /* RETF 8 */

On the stack, `w0:w1` is `dwFlags` (low word nearest) and `w3:w2` is
`lpData`. The callers passed flags 0x12, 0x4000, 0x02, 0x1F52, 0x10,
0x1001 and 0x0101.

**dwFlags** (mode 1, `scan3-ms.log`; one bit per call, with the buffer
filled with CCh):

- Bit 0 means **set**: the fields named by the other bits are written
  from `*lpData`. Without it they are read into `*lpData`.
  - POINTLIB's acceleration radio button sent 0x1001 with +24=2.
  - Its sensitivity scroll bar sent 0x0101 with +13/+14 going 0B, 0C, 0D
    (`r3.log` C #9 to C #12).
  - A set followed by a get reads the new value back (`beh11.log`,
    `beh12.log`).

| Bit | Field in `*lpData` | Value on this PS/2 setup | Evidence |
|---|---|---|---|
| 1 (0x2) | +0 byte | 04 (PS/2, like INT 33h fn 24h CH) | scan3 |
| 2 (0x4) | +1 byte | 00 (IRQ? as INT 33h fn 24h CL) | scan3 |
| 3 (0x8) | +2, +4 words | the raw X, Y of the last packet (D8 FF EC FF after a -40,-20 move even with Y sensitivity 0) | beh11, beh12 "state:" lines |
| 4 (0x10) | +6 word | 0820h = driver version 8.20. POINTLIB checks it: with w16's driver, which leaves the buffer alone, it says "This version of Mouse Control Panel requires MOUSE.DRV version 8.20 or later." | scan3; `pt-w16.log`, `build/md2/pt-w16/run/p1.png` |
| 5 (0x20) | (+8?) | get fails: AX=0205 | scan3 |
| 6 (0x40) | +10, +11 bytes | 02 01. Setting +11 to 0 gives AX=0406; +11=2 or +10=3 gives AX=0506; setting the same values succeeds. | scan3, beh1 (`build/md2/beh1`), beh12 |
| 7 (0x80) | (+12?) | get fails: AX=0207 | scan3 |
| 8 (0x100) | +13 horizontal and +14 vertical sensitivity | 0A each. 10 is 1:1; observed factors: 0 → 0, 1 → 1/40, 5 → 0.375, 15 → 2.25, 19 → 3.25, 20 → 3.5 | beh11, beh12 |
| 9 (0x200) | +15 byte | 00. 1 is accepted with no visible effect on motion or clicks; 5Ah gives AX=0309 | beh1, beh12 |
| 10 (0x400) | through the far pointer at +16: the acceleration profiles, 260 bytes (4 counts, then 4×32 movement thresholds, then 4×32 factors in 1/16) | the driver's built-in defaults (not reproduced here) | scan3 (aux1, 4 bytes into aux2) |
| 11 (0x800) | through the far pointer at +20: 4 profile names, 16 chars each, space-padded | "Slow", "Moderate", "Fast", "Unaccelerated" | scan3 |
| 12 (0x1000) | +24 byte, the active profile (0-based) | 03 (Unaccelerated). Set to 2 (Fast), a 40,20 move came as 200,100; 0 (Slow) gave 100,50; 3 gave 40,20 | beh12 |
| 13 (0x2000) | bits 10 and 11 together | | scan3 |
| 14 (0x4000) | through the unaligned far pointer at +25: the MOUSE.INI path, with a length byte first | 15h "C:\WINDOWS\MOUSE.INI" 00 | scan3 (aux3); POINTLIB and GROWSTUB set +25 before calling it (`r3.log` C #4, `r2` C #8) |
| 15-31 | none | the call fails: AX=0900, DX=HIWORD(dwFlags), nothing filled | scan3 |

**Results.**

- AX is a status: 0 means done. Otherwise AX = (code << 8) | bit number.
  - Code 02: the field is not available (get of bits 5 and 7).
  - Codes 03, 04, 05: bad values in a set.
  - Code 09: undefined flag bits (low byte 00).
- One bad bit makes the whole call fail. 0xFFFFFFFE filled nothing
  (scan3). Set 0x1041 with a bad +11 also did not apply the +24 change
  (beh1, step 4).
- DX was 0002 after every successful get, whatever DX held on entry (the
  `R` lines in scan3). It was 0000 after sets (`r3.log` C #9 onwards).
  Its meaning is unresolved.
- The settings take effect at once on what the driver reports. Unlike
  `MouseSpeed` and the thresholds, which USER applies, sensitivity and the
  acceleration profiles are applied inside MOUSE.DRV (beh11, beh12; the
  default profile is Unaccelerated with sensitivity 10, so by default the
  driver passes raw mickeys on, as drivers/mouse/README.md says).

**Hypotheses proven wrong.**

- Research had suggested (WORD code, WORD index, LPVOID) or a USER
  ControlPanelInfo-like shape. Neither fits: the far pointer is the
  first-pushed argument and the DWORD is a bit mask (scan3; the
  CCh-filled buffer with bit 10 ended MD2 at `build/md2/scan-ms`, because
  the driver followed the pointer at +16).
- "Control Panel uses it": no. CONTROL.EXE and MAIN.CPL import nothing
  from MOUSE, and no boot made any call.

## PowerEventProc (MOUSE.7)

    WORD FAR PASCAL PowerEventProc(WORD wEvent, DWORD dwParam);   /* RETF 6 */

**Caller.** Windows 3.1's APM driver `POWER.DRV` (installed as `[boot]
drivers=mmsystem.dll power.drv`), with MS4 as its APM BIOS (`apm2.log`):

1. MS4 handed it event 0002h (system suspend request).
2. POWER (segment 1) called MOUSE.7 with stack words (w2 w1 w0) = (0001
   0000 0000), that is wEvent = 1 and dwParam = 0.
3. POWER called 5307h BX=0001 CX=0002 (suspend all).
4. On the next poll MS4 gave 0003h (normal resume), and POWER called
   MOUSE.7 with wEvent = 2.

The values match the SDK's PWR_SUSPENDREQUEST (1) and PWR_SUSPENDRESUME
(2). The order of steps 2 and 3 is inferred from the event order: MS3
and MS4 keep separate sequence numbers.

**What it does** (direct calls with MS2's BIOS log: `pow1.log`,
`pow2.log`, `apm2.log`):

- **wEvent 2 or 3, driver enabled.** The driver sets the PS/2 device up
  again with the same BIOS calls as Enable: C205h (BH 3), C201h, C202h
  (BH 0), C204h, C203h (BH 3), C207h (its handler), C206h (BH 1), C202h
  (BH 2), C200h (BH 1). dwParam = 0x00010001 makes no difference.
- **wEvent 0, 1, 4, 5, 8002h, FFFFh.** No BIOS activity.
- **Driver disabled** (after Disable). Events 2, 3 and 1 do nothing at
  all (`pow2.log`, mode 16).
- **Return value.** Always AX=0000 and DX=0000. AX is not PWR_OK (1).
  Six bytes are taken off. The input still works afterwards.

Falsification test for "it is a resume hook": had event 2 made no BIOS
calls, or had it made them while disabled, the hypothesis would have
needed changing. It made them only when enabled.

## The packet lost after Enable

Mode 8 (`drop1.log`; summary `drop1.summary`; input `md2adrop.txt` moves
1, 2, ... 9 mickeys in turn, about 0.9 s apart). MS2 records every
packet the BIOS hands to the C207h handler, and MD1's procedure records
every event:

| Phase | Packets given to the handler (x) | Events (x) |
|---|---|---|
| A: Disable, Enable | 0, 1, 2, 3, 4, 5, 6 | 2, 3, 4, 5, 6 |
| B: PowerEventProc(2) | 7, **8**, 9, 1, ... | 7, 9, 1, ... |
| C: Disable, Enable | 8, **9**, 1, ... | 8, 1, ... |
| D: GetSetMouseData set (0x101, same values) | 8, 9, 1, ... | all |
| E: Disable, Enable, PowerEventProc(1) | 8, **9**, 1, ... | 8, 1, ... |

(In A the first packet, x=0 with no button change, gives no event anyway,
and the second, x=1, is lost.)

- **Which packet is lost.** The second packet the handler receives after
  each set-up of the device is lost, whether that set-up comes from
  Enable or from PowerEventProc's resume. A packet with no change counts
  as the first. A first packet with motion is reported and the second is
  dropped (B, C, E). Packets 0.9 s apart are dropped, so this is not a
  short time window after the set-up.
- **Where it is lost.** It is lost inside Microsoft's MOUSE.DRV, not in
  DOSBox's BIOS or in DOSX. The BIOS did call the handler for the lost
  packets (MS2 kind 5). With w16's driver patched to drop nothing
  (`build/md2/nodrop`, `drop-nodrop.summary`), the same harness and DOSX
  delivered every packet, including the second after Enable.
- **Why the driver discards it.** Not observable from outside.

## Side observations

- Microsoft's Disable makes C200h BH 0 (disable), C202h BH 5 (100
  reports a second) and INT 33h AX=0020h (enable the DOS mouse driver)
  (`drop1.log` and `pow2.log`, after "disabled").
- In MD1 and MD2 a timer set with `SetTimer(0, 1, ...)` gets an ID
  chosen by USER, so `KillTimer(0, 1)` does not kill it. MD2's `wait()`
  keeps the returned ID. MD1 calls `wait()` only once, so it is not
  affected.
