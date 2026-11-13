# w16 PS/2 mouse driver (`MOUSE.DRV`, module `MOUSE`)

w16's own mouse driver for Windows 3.1, for a mouse on the PS/2
auxiliary port of the 8042 keyboard controller (IRQ 12). Like every w16
driver it is a genuine Windows 3.1 driver (ARCHITECTURE.md 6.7): it
implements the mouse-driver interface of the Windows 3.1 DDK and nothing
w16-specific, and it replaces Windows 3.1's `SYSTEM\MOUSE.DRV` as it is
(`SYSTEM.INI` `[boot] mouse.drv=mouse.drv`, `[boot.description]
mouse.drv=Microsoft, or IBM PS/2`, both as Setup leaves them).

`mouse.asm` (NASM) and `mouse.lnk` (wlink); `make` builds
`build/dos/MOUSE.DRV` (one fixed code segment, one fixed data segment,
expected Windows version 3.10). Serial, bus and InPort mice, which
Windows 3.1's driver also handles, are not supported.

## Interface

Module name and ordinals are those of Windows 3.1's MOUSE.DRV (its NE
export table, `docs/exports/MOUSE.txt`). Only USER imports any of them
(1-3, `tools/neinfo.py` over every NE file of the reference install).

| Ord | Name | What it does here | Source |
|---|---|---|---|
| 1 | `Inquire(LPMOUSEINFO)` | fills MOUSEINFO; returns 16 (14 before Windows 3.1, without `msMouseCommPort`) | DDK; values MD1 |
| 2 | `Enable(lpEventProc)` | configures the device, takes IRQ 12, reports to the event procedure | DDK, Pietrek ch. 1 |
| 3 | `Disable()` | disables the device, gives IRQ 12 back | DDK |
| 4 | `MouseGetIntVect()` | 74h (-1 without a mouse) | DDK; MD1: 74h |
| 5 | `GetSetMouseData(LPVOID, DWORD)` | Microsoft Mouse 8.20's private settings call: every field (type, last motion, version 0820h, sensitivity, acceleration profiles, profile names, MOUSE.INI name), applied inside the driver; `RETF 8` | MD1, MD2A (Notes) |
| 6 | `CplApplet(hwnd, code, l1, l2)` | FALSE: Control Panel shows its own Mouse dialog | DDK; MD1 |
| 7 | `PowerEventProc(WORD wEvent, DWORD)` | POWER.DRV's call, WM_POWER codes: on 2 and 3 while enabled the device is set up again; returns 0; `RETF 6` | MD1, MD2B (Notes) |
| 8 | `WEP(bSystemExit)` | turns the mouse off if on; returns 1 (exported by name, resident) | DDK |

**MOUSEINFO** (DDK): `msExist` and `msRelative` (chars), then
`msNumButtons`, `msRate`, `msXThreshold`, `msYThreshold`, `msXRes`,
`msYRes`, `msMouseCommPort` (words). Filled as Windows 3.1's driver fills
it for a PS/2 mouse (MD1): FFh, FFh, 2, 30, 2, 2, 0, 0, 0. (`msRate` is 30
although the device is set to 40 reports a second; the DDK calls the
thresholds and resolutions reserved.) `msExist` is 0 when LibEntry found
no device.

**The event procedure** (USER's `Mouse_Event`, USER.299; DDK
"Mouse_Event", Schulman ch. 6) is called at interrupt time, once per
change: AX = status (`SF_MOVEMENT` 1, `SF_B1_DOWN` 2, `SF_B1_UP` 4,
`SF_B2_DOWN` 8, `SF_B2_UP` 10h; never `SF_ABSOLUTE`), BX/CX = relative
motion in mickeys (X right, Y down), DX = 2 (the number of buttons),
SI = DI = 0 (Windows for Pen Computing), interrupts enabled. All of that
is what Windows 3.1's driver does (MD1). Buttons are reported as they
are on the device: swapping them (`SwapMouseButtons`) is USER's work;
Windows 3.1's driver reports the same events with swapping on (MD1's
`swap` run). The middle button of a 3-button mouse is not reported
(neither does Windows 3.1's driver: MD1, middle clicks gave no events).

**Acceleration** (`MouseThreshold1`, `MouseThreshold2`, `MouseSpeed` in
WIN.INI) is USER's, not the driver's: Windows 3.1's driver gives the raw
mickeys (MD1: a 40-pixel move came as 40, a 300-pixel sweep unscaled),
and w16's USER needed acceleration of its own to put the pointer where
3.1's USER does on Windows 3.1's driver (commit d61c8a9). This driver
does none of that. Its own scaling, the sensitivity and acceleration
profiles that only Microsoft Mouse 8.20's programs set through
GetSetMouseData (Notes), is 1:1 until they change it, as 3.1's is.

## Hardware path, and why

Two ways to read a PS/2 mouse: the PS/2 BIOS's pointing-device services
(INT 15h AX=C200h-C207h: enable, reset, rate, resolution, type, init,
scaling, and C207h, a far-call handler the BIOS's IRQ 12 code gives each
3-byte packet to), or the 8042 directly (port 64h status, port 60h data,
IRQ 12 for every byte). This driver uses both, each for what it does best:

- **The BIOS for the device.** LibEntry finds the mouse with C205h
  (initialise, 3-byte packets), C201h (reset) and C204h (device ID);
  Enable sets it up as Windows 3.1's driver does, in its order: C205h,
  C201h, C202h (10/s), C204h, C203h (8 counts/mm), C207h (the packet
  handler), C206h (1:1 scaling), C202h (40 reports/s), C200h (enable);
  Disable does C200h (disable) and C202h (100/s) as 3.1's does, and
  C207h with 0:0, which 3.1's does not (w16's own: the BIOS should keep
  no handler address into the DOS extender after Windows ends). The BIOS
  knows its controller's quirks (command timing, the aux-port enable, the
  controller command byte), works on every machine with a PS/2 port, and
  the standard-mode DOS extenders handle these calls from protected mode
  (Microsoft's DOSX and w16's, ARCHITECTURE.md 9.6: C207h's handler is
  called in protected mode).
- **The 8042 for the packets.** IRQ 12 (INT 74h) is taken in protected
  mode (INT 21h AH=25h, so the DOS extender also routes IRQ 12 to it when
  it comes in real mode). The handler reads port 64h; with an auxiliary
  byte waiting (bits 0 and 5) it reads it from port 60h, ends the
  interrupt at both PICs and assembles the packet itself: no switch to
  real mode per byte. Byte 0 must have bit 3 set, and a byte after a
  pause of more than a timer tick starts a new packet (resynchronises
  after a lost byte). Without an auxiliary byte waiting, the previous
  handler (the BIOS) runs as an interrupt and returns to the driver; the
  BIOS's packet reaches the C207h handler, which keeps it the same way.
  Then, at the end of the IRQ, the packet goes to the event procedure
  (below). Decoding: buttons
  from bits 0-1 of byte 0, 9-bit X and Y with the sign in bits 4 and 5,
  Y negated for the screen (more below).

Windows 3.1's driver, observed black-box (MS2, `tests/dos/ms2.asm`: a
real-mode TSR loaded before WIN that logs INT 15h C2xxh, INT 33h, every
INT 74h the BIOS gets and every call of the C207h handler; TK49 writes
the log out), makes INT 33h AX=0024h (is there an MS-DOS mouse driver:
DOSBox's built-in one answers), then C205h, C201h, C202h, C204h twice,
C203h (BH 3), C207h, C206h (BH 1), C202h (BH 2), C200h (BH 1), and takes
INT 74h in a way that makes the DOS extender hook the real-mode IVT
entry too (DOSX's segment there afterwards). w16's driver gives the same
trace without the INT 33h call and the boot-time second detection pass
(MD2B: 3.1's set-up after Enable and after a resume is the order above); it sets
INT 74h with INT 21h AH=25h, which does the same (with DPMI 0205h,
Microsoft's DOSX left the IVT alone), and it does so before enabling the
device, so no packet can come before its handler is there (the gap that
made w16's DOSX drop such packets, ARCHITECTURE.md 9.6, does not arise).
Just before it enables the device (Enable, a resume) it also clears
what an earlier IRQ 12 may have left: bytes waiting in the 8042's
auxiliary output are read and dropped, and an IRQ 12 still marked in
service at the slave PIC gets a specific end of interrupt (then the
cascade, IRQ 2, at the master if nothing else is in service at the
slave). That is task time, where no IRQ 12 handler can be running, so
such a bit can only be a leftover; left, it stops every later IRQ 12
while the keyboard (IRQ 1) goes on: the stuck pointer below.

**The stuck pointer** (boot-test `input`, `tests/scripts/input/
bootnolock.txt`: after mouse motion during the boot the pointer never
moved again; keys still worked). Reproduced on installation-05
(`build/m6/rep.sh`, motion from the boot's first second): Microsoft's
USER, GDI, DOSX and MOUSE.DRV on w16's KRNL286 9 boots in 20, on Windows
3.1's KRNL286 none in 40. MS5 (`tests/dos/ms5.asm`) counted what the
idle loops send to real mode in 30 idle seconds: both KERNELs about
455,000 INT 28h, but w16's also an INT 2Fh AX=1689h (the 386 Enhanced
mode kernel idle call) on every pass, which 3.1's KRNL286 never makes.
Without it (kernel/task.c, idle) the same test gave 0 in 40. DOSBox
0.74 does nothing with 1689h (its source); the call doubled the idle
loop's switches to real mode and back through the DOS extender, and so
the chance that IRQ 12 comes during one.

**Where the interrupt is lost: Microsoft's DOSX with Microsoft's
MOUSE.DRV** (`tests/mousestress.sh`, MD3 `tests/apps/md3.c`, on
installation-05). MD3 runs as the shell for long runs: DOS file I/O and
idle phases keep Windows going to real mode, the pointer is moved
without a pause, and with `-r` MD3 calls the driver's Disable and Enable
every 200 ms (each Enable is a chance for a packet while the driver sets
itself up, as at boot); it logs every stall (no motion for 5 s) with the
PIC and 8042 registers. In steady motion with I/O alone nothing stalled
(16 runs of 10 minutes, either DOS extender, Microsoft's driver, with
the 1689h accelerator). With `-r 200` (5 minutes, 4 runs each):

| DOS extender | MOUSE.DRV | KRNL286 | runs stalled |
|---|---|---|---|
| Microsoft's | Microsoft's | Windows 3.1's | 4 of 4 |
| Microsoft's | Microsoft's | w16's | 3 of 4 |
| Microsoft's | w16's | w16's | 0 of 4 |
| Microsoft's | w16's, enabling before it takes INT 74h (MOUSELH) | w16's | 0 of 4 |
| w16's | Microsoft's | w16's | 0 of 4 |
| w16's | w16's (and MOUSELH) | w16's | 0 of 4 |

Every stall leaves IRQ 12 in service at the slave PIC (ISR 10h) and
nothing at the master: the IRQ was acknowledged at the master but never
ended at the slave, and the slave then holds back every later IRQ 12
while the keyboard (IRQ 1) goes on. It happens with Windows 3.1's own
KERNEL, USER, DOSX and MOUSE.DRV, so it is Windows 3.1's own behaviour
under motion during the driver's set-up, which w16's KERNEL had only
made likelier at boot; neither w16's DOS extender nor w16's driver gives
it, and w16's driver does not even with Microsoft's ordering on
Microsoft's DOSX. With MS2 loaded (`-L`, 8 runs of 5 minutes, Windows
3.1's own stack: 7 stalled with IRQ 12 in service), the last records
before such a stall were the start of a set-up: C205h, then C201h (the
reset), which returned normally; after it no BIOS call, no INT 74h and
no call of the C207h handler reached real mode again although MD3 went on
calling Disable and Enable: Microsoft's driver stopped in the middle of
its set-up for good. What w16's DOSX does differently is the drop: it does not call the
C207h handler for a packet the BIOS delivers while the client has not
taken INT 74h in protected mode (ARCHITECTURE.md 9.6; the BIOS still
ends that interrupt). Built without it (DOSXND.EXE, `-D nd`), w16's
DOSX with Microsoft's driver stalled in 3 of 4 runs the same way (slave
ISR 10h); with it, 0 of 4 in the same batch. So the end of IRQ 12 is
lost when the BIOS hands Microsoft's MOUSE.DRV a packet through its
C207h handler while the driver does not hold INT 74h: between its
Disable giving the vector back and its next Enable taking it again,
and between enabling the device (C200h) and hooking, which is the order
it uses. Called then, Microsoft's driver never ends that IRQ 12 at the
slave PIC and makes no BIOS call again. Microsoft's DOSX passes such
packets on (TK47); which instruction inside the driver then goes wrong
is not observable from outside. w16's DOSX sends no end of interrupt of
its own (the client's handler or the BIOS does).

Emulators: DOSBox 0.74's keyboard controller has no auxiliary port (its
source, read for facts: no A8h/D4h commands, status bit 5 never set);
its mouse is the PS/2 BIOS services alone, packets going to the C207h
handler from its INT 74h code. So in DOSBox every packet takes the
second path above, through Microsoft's or w16's DOSX, and the event
procedure is called with the interrupt state the BIOS gives (enabled,
like Windows 3.1's driver in the same place). DOSBox-X emulates the 8042
auxiliary port with IRQ 12 and the BIOS services: there the first path
is taken. Real hardware has both.

**Packets as Windows 3.1's driver treats them** (MD1; MD2C with MS3C
giving the C207h handler packets of its choosing, see Notes): the event
procedure is called at most once per IRQ 12, after the BIOS's INT 74h has
returned, with the latest packet of that IRQ (earlier ones overwritten,
not added); after each set-up the second IRQ that brings a packet reports
nothing and leaves its packet pending, to be reported at the end of the
next IRQ unless a newer packet replaces it first (with one packet per
IRQ, the usual case, that is the "lost second packet"); the overflow bits are
ignored, the 9-bit value reported as it is (DOSBox's 300-mickey step,
sent with X overflow, came as 44); a packet with motion and a button
change would be one event with both bits (the DDK allows it; DOSBox never
sends one, so UNVERIFIED against 3.1).

**Interrupt time.** In Standard mode a hardware interrupt reaches the
handler in protected mode on the interrupted stack (DOSX). Code and data
are fixed segments, so they are present and in place at any time. The
handler does no DOS or KERNEL calls. At the end of each IRQ 12 the
pending packet is decoded, interrupts are enabled and the event
procedure is called (3.1's calls it with interrupts enabled: MD1); an IRQ
12 during that call leaves its packet pending for the next IRQ (no
nesting into USER). On the 8042 path the per-IRQ rules above are applied
with "the IRQ that completed a packet": an extrapolation, not observed.

**386 Enhanced mode.** The DDK has the virtual mouse device (VMD,
`[386Enh] mouse=*vmd`) virtualise the mouse for DOS boxes and talk to an
MS-DOS mouse driver through INT 2Fh AX=1607h (DDK "Mouse Support for
Non-Windows Applications"); the Windows mouse driver itself is the same.
UNVERIFIED here: w16's driver under WIN386 (VKD traps ports 60h/64h).

## Verified

- `tests/mouseprobe.sh` (MD1, `tests/apps/md1.c`, as the shell, real
  xdotool input through DOSBox, `tests/scripts/input/mouseprobe.txt`):
  everything MD1 logs - entry points, MOUSEINFO and Inquire's result,
  MouseGetIntVect, CplApplet (NEWINQUIRE and INQUIRE), the bytes each
  entry takes off the stack, and every event procedure call with its
  registers and interrupt flag - is the same with w16's driver as with
  Windows 3.1's, and Windows 3.1's with `SwapMouseButtons=1` is the same
  again.
- `make boot-test` cases `mouse` (w16's MOUSE.DRV on Microsoft's
  KRNL286, DOSX, USER and GDI under the real-input scripts dragoff,
  pmitems, pmclose and helpmenus: drags, clicks, a double click, menus;
  every screen pixel-identical to the same script with Microsoft's
  MOUSE.DRV) and `mouseall` (every w16 part with w16's MOUSE.DRV against
  every w16 part with Microsoft's, the dragoff script: identical).
- DOSBox-X (`tests/dropin.sh -r -p 386`): Windows 3.1 boots with w16's
  driver to a screen pixel-identical to Microsoft's driver's.
- The install tree (`build/w16/SYSTEM/MOUSE.DRV`, boot-test `tree`).

## Not verified / not done

- 386 Enhanced mode (above).
- GetSetMouseData: the meaning of bits 2, 6 and 9 and of the fields
  behind 5 and 7; the sensitivity curve between the observed points; how
  3.1 picks a profile's factor (w16: the larger of |X| and |Y|) and in
  which order profile and sensitivity apply; limits on set values beyond
  the observed errors; reading or writing MOUSE.INI (w16 does neither).
  PowerEventProc beyond the set-up on a resume (Notes).
- Packets with motion and a button change together (above); why 3.1
  holds a packet back after a set-up; whether a button change in an
  earlier packet of a multi-packet IRQ survives (only motion was tested).
- The 8042 path with real input: DOSBox 0.74 has no aux port, and in
  DOSBox-X xdotool's motion does not reach the guest under the harness
  (with Microsoft's driver neither: the pointer stays put), so the
  real-input comparisons all run the BIOS path. The 8042 path is
  exercised only by booting in DOSBox-X.
- A mouse plugged in after boot, a missing mouse (Inquire
  then says `msExist` 0; Windows 3.1's values in that case are not
  observed).
- The BIOS's C207h handler cannot be read back, so Disable leaves none
  (a DOS mouse driver that used one sets it again itself).
- The INT 33h query Windows 3.1's driver makes: not needed for a PS/2
  mouse, not made.

## Notes: the open questions, observed (MD2)

Black-box probes of Windows 3.1's driver, headless DOSBox 0.74, Standard
mode, Microsoft's KRNL286, DOSX and USER (findings and logs in
`tests/apps/md2logs/a`, `b`, `c`, each with a `notes.md`):

- **GetSetMouseData** (MD2A, `tests/md2aprobe.sh`; MS3A.DRV records every
  call made to MOUSE): a private interface between Microsoft Mouse 8.20's
  own programs (POINT.EXE, POINTLIB.DLL, POINTER.EXE) and its driver. Not
  seen called by Windows 3.1 in Standard mode (USER calls only Inquire and
  Enable at boot); Enhanced mode, the Control Panel's Mouse applet and
  shutdown not covered. `GetSetMouseData(LPVOID lpData, DWORD dwFlags)`,
  `RETF 8`. dwFlags bit 0: set (else get); bits 1-14 each name a field of
  the buffer (bit 1 mouse type, 2 a byte (IRQ?), 3 the last packet's raw X
  and Y, 4 the version 0820h Mouse 8.20's tools require, 6 two bytes, 8 X
  and Y sensitivity, 9 a byte, 10 and 11 far pointers to acceleration
  tables and profile names, 12 the active profile, 13 = 10 and 11, 14 a far
  pointer to the MOUSE.INI path; 5 and 7 fail on PS/2). AX = 0 or error *
  256 + bit (02 not available, 03-05 bad value, 09 undefined bit: bits
  15-31, AX 0900h, DX = HIWORD(dwFlags), buffer untouched); one bad bit
  fails the whole call; settings take effect inside the driver at once
  (sensitivity and profiles scale what the event procedure gets). DX is
  2 after gets, 0 after sets; whether the result is a DWORD is not known.
  **w16 implements all the fields** (`mouse.asm`, GetSetMouseData): the
  values a get returns on PS/2 are the observed ones (type 4, byte 0, the
  version 0820h, 2 and 1, sensitivity 10 and 10, 0, profile 3), the
  checks and error codes are the observed ones, and the settings act
  inside the driver at interrupt time. What is w16's own: the four
  acceleration profiles' tables (chosen so that a 40-mickey step comes out
  2.5 times as large with Slow and 5 times with Fast, as with 3.1's), and
  the sensitivity curve, (s - 2) / 8 up to 10 and (s - 6) / 4 above, a
  step never below one mickey, fitted to 3.1's results at 1, 5, 15, 19
  and 20. The profile names are the ones Mouse 8.20's programs show; the
  MOUSE.INI name is the Windows directory's. Checked with MD2A against
  3.1's logs: the flag scan (everything written and returned, apart from
  scratch registers and table contents, which 3.1's log leaves out) and
  the sensitivity and profile runs give the same results.
- **PowerEventProc** (MD2B, `tests/md2bprobe.sh`; POWER.DRV from the
  install disks, MS4B.COM an APM BIOS of ours): `WORD
  PowerEventProc(WORD wEvent, DWORD dwParam)`, `RETF 6`, wEvent the first
  word pushed (`[bp+10]`). Called by POWER.DRV at task time after it sends
  WM_POWER, with WM_POWER's codes: suspend request (APM 0002h) gives
  WM_POWER(1) then PowerEventProc(1,0); normal resume (0003h) (2,0);
  critical resume (0004h) (3,0); a standby request gives nothing, its
  resume (2,0). On 2 or 3 while enabled 3.1's driver runs Enable's set-up
  again (the order above, without the boot-time detection pass) and the
  packet hold-back starts over; on other codes or while disabled it makes
  no recorded call. dwParam has no visible effect; it returns 0;
  returning -1 to event 1 did not stop POWER.DRV's suspend. w16 does the
  same. APM's protected-mode interface and Enhanced mode (VPOWERD) not
  run.
- **The packet after a set-up** (MD2C, `tests/md2cprobe.sh`,
  `tests/md2cview.py`; MS3C.COM gives the handler MD2C's packets, several
  per IRQ, held off or spaced): the rules under "Packets" above. 4 packets
  per IRQ gave events 4 and 8; 8 in one IRQ the single event 8; with 3 s
  between packets all 8 arrived, packet 2 one IRQ late, which a count of
  packets cannot give. Enable (from disabled) and a resume re-arm it; a
  GetSetMouseData set does not; a second Enable while enabled makes no
  BIOS calls (and w16 changes only the event procedure then). Not DOSBox,
  USER, the packets' content, timing or the DOS extender (the same under
  w16's DOSX). Purpose unknown; measured on the BIOS path in DOSBox only.
- **Disable:** 3.1's issues C200h BH 0, then C202h BH 5 (MD2A also saw INT
  33h AX=0020h; MD2C no C207h 0:0). w16 matches the first two and keeps
  its C207h 0:0 (above).
- **MD1's timer:** with no window, KillTimer(0, 1) does not stop the timer
  SetTimer chose (the id is SetTimer's return); harmless in MD1, which
  waits once.
