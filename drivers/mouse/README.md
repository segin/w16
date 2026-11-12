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
| 5 | `GetSetMouseData` | returns DX:AX = 0002:0000 and takes 8 bytes of parameters off, as 3.1's did for zero parameters | MD1 (purpose UNVERIFIED) |
| 6 | `CplApplet(hwnd, code, l1, l2)` | FALSE: Control Panel shows its own Mouse dialog | DDK; MD1 |
| 7 | `PowerEventProc` | returns 0 and takes 6 bytes off, as 3.1's did | MD1 (purpose UNVERIFIED) |
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
does none.

## Hardware path, and why

Two ways to read a PS/2 mouse: the PS/2 BIOS's pointing-device services
(INT 15h AX=C200h-C207h: enable, reset, rate, resolution, type, init,
scaling, and C207h, a far-call handler the BIOS's IRQ 12 code gives each
3-byte packet to), or the 8042 directly (port 64h status, port 60h data,
IRQ 12 for every byte). This driver uses both, each for what it does best:

- **The BIOS for the device.** LibEntry finds the mouse with C205h
  (initialise, 3-byte packets), C201h (reset) and C204h (device ID);
  Enable sets it up with C205h, C201h, C203h (8 counts/mm), C206h (1:1
  scaling), C202h (40 reports/s), C207h (the packet handler) and C200h
  (enable); Disable does C200h (disable) and C207h with 0:0. The BIOS
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
  after a lost byte). Without an auxiliary byte waiting, the interrupt
  goes on to the previous handler: the BIOS, whose packet then reaches
  the C207h handler, which decodes it the same way. Decoding: buttons
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
trace without the INT 33h call and the second detection pass; it sets
INT 74h with INT 21h AH=25h, which does the same (with DPMI 0205h,
Microsoft's DOSX left the IVT alone), and it does so before enabling the
device, so no packet can come before its handler is there (the gap that
made w16's DOSX drop such packets, ARCHITECTURE.md 9.6, does not arise).

Emulators: DOSBox 0.74's keyboard controller has no auxiliary port (its
source, read for facts: no A8h/D4h commands, status bit 5 never set);
its mouse is the PS/2 BIOS services alone, packets going to the C207h
handler from its INT 74h code. So in DOSBox every packet takes the
second path above, through Microsoft's or w16's DOSX, and the event
procedure is called with the interrupt state the BIOS gives (enabled,
like Windows 3.1's driver in the same place). DOSBox-X emulates the 8042
auxiliary port with IRQ 12 and the BIOS services: there the first path
is taken. Real hardware has both.

**Packets as Windows 3.1's driver treats them** (MD1, with MS2 loaded
for the BIOS's packets): the second packet after each Enable never
reaches the event procedure (packets 0,0 / 5,0 / 40,0 came as 0,0 and
40,0; 0,140 / 3,0 / 5,0 / 40,0 as 0,140, 5,0, 40,0; why is UNVERIFIED,
w16's driver drops it the same way); the overflow bits are
ignored, the 9-bit value reported as it is (DOSBox's 300-mickey step,
sent with X overflow, came as 44); a packet with motion and a button
change would be one event with both bits (the DDK allows it; DOSBox never
sends one, so UNVERIFIED against 3.1).

**Interrupt time.** In Standard mode a hardware interrupt reaches the
handler in protected mode on the interrupted stack (DOSX). Code and data
are fixed segments, so they are present and in place at any time. The
handler does no DOS or KERNEL calls. Events go through a ring of 8: the
handler queues the decoded packet, ends the interrupt, enables
interrupts and calls the event procedure; an IRQ 12 during that call
only queues, and the outer call's loop delivers it (no nesting into
USER). The BIOS path calls the event procedure with the BIOS's interrupt
state (disabled).

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
- `GetSetMouseData` and `PowerEventProc`: what they do; only their stack
  use and their results for zero parameters are known.
- Packets with motion and a button change together (above); why the
  second packet after Enable is lost, and whether it is when it carries
  a button change (it is, here).
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
