# MMSYSTEM handover (w16's MMSYSTEM.DLL)

Task: w16's own MMSYSTEM.DLL (module MMSYSTEM, 136 exports, Windows
3.1's multimedia system library) in layers, each committed working and
tested before the next: 1 skeleton and core, 2 MMIO, 3 wave/MIDI/aux/
joystick and sndPlaySound, 4 MCI. ARCHITECTURE.md 23 has the facts in
order; this note is the working state, kept current after every layer so
that another agent can take over.

Rules followed: Microsoft's MMSYSTEM.DLL, MMTASK.TSK and drivers observed
from outside only: NE tables (tools/neinfo.py, tools/neimports.py),
probes, logging shims. Nothing disassembled. Constants and structure
layouts from the Multimedia Programmer's Guide, OpenWatcom's
h/win/mmsystem.h (test programs include it; w16's code has its own
definitions) and Wine's mmsystem.h of before 2002 (X11 licence; the
DCB_* flags, mmTask* arguments). The error-text strings must come from
calling 3.1's *GetErrorText functions, never from its resources (one
look at MMSYSTEM's string table happened by accident, through
tools/neres.py; nothing from it was used: do not run neres/resdump on
MMSYSTEM.DLL). All DOSBox work on installation-05, run name `w16-mmsys`
(tools/remote-run.sh, tools/remote-test.sh `-n w16-mmsys`).

## Layout

- `shell/mmsystem/`: `mmfix.asm` (fixed code: entry, StackEnter/Leave,
  DriverCallback, the time* functions, mmTaskSignal, the Drv* jumps,
  OutputDebugStr), `core.c` (DriverProc, mmsystemGetVersion, the
  interrupt stack, mmTaskCreate/Block/Yield), `mmio.c` (layer 2: every
  mmio* function, the DOS and MEM procedures), `stubs.c` (every export
  of the later layers: right argument sizes, "no device" answers),
  `mmsys.h`, `mmsystem.lnk` (all 136 exports, 3.1's ordinals and
  residency; DRIVERPROC = MMDRIVERPROC since windows.h has a DRIVERPROC
  type), `mmsystem.rc` (version 3.10.0.103), `mmtask.asm`/`mmtask.lnk`
  (MMTASK.TSK). Makefile: `make mmsystem`; flags 8309h/08h, 030Ah/08h.
  Installed as SYSTEM\MMSYSTEM.DLL and SYSTEM\MMTASK.TSK (since layer
  2: with layer 1 alone QuickTime's player said "Could not open movie
  sample.mov"; QTIM.DLL reads movies through mmio).
- `tests/mmsystem/`: MMP.EXE (`mmp.c`, the probe, sections in its
  header; `mmio.c` its MMIO sections F D M B E), MMC.DLL (`mmc.c`,
  `mmcs.asm`: callbacks, the mmTask function, StackEnter with markers,
  INT 21h/41h hooks, a pop counter, the logging "TST " I/O procedure),
  `mrun.sh` (one run, ms|w16), `mcmp.py` (masks and known differences,
  each explained), `mscen.sh` (parts core, mmio, timer: everything at
  once and compared). `tests/timer/trun.sh` takes TRUN_MM=w16.
- Boot-test case `mmsystem` (parts core, mmio, timer, qt).
## Status

- [x] Layer 1: skeleton, DriverProc/boot driver, StackEnter/StackLeave,
  DriverCallback, timer API over TIMER.DRV, mmsystemGetVersion,
  OutputDebugStr, Drv*, mmTask* and MMTASK.TSK. mscen.sh: core identical
  but the known differences, prestack identical, TPR plain and shim pass
  tcmp.py (w16's MMSYSTEM over w16's TIMER against Microsoft's two), and
  with Microsoft's TIMER under both MMSYSTEMs every exact TPR/TMS line is
  identical (the timings differ by 3.1's driver's own faults). Boot-test
  run (remote, w16-mmsys): mmsystem core and timer, timer, drivers,
  treealone ok.
- [x] Layer 2: MMIO (1210-1226). mscen.sh mmio: 270 lines identical;
  QuickTime's movie plays with w16's MMSYSTEM as on 3.1 (mmsystem@qt).
- [x] Layer 3: wave/MIDI/aux/joystick, sndPlaySound (below)
- [ ] Layer 4: MCI
- [x] ARCHITECTURE.md 23 (23.2 has the observations in order),
  TESTING.md, assets/README.TXT, the install tree

## Layer 1 observations (MMP on 3.1, Microsoft's KRNL386, Standard mode)

- mmsystemGetVersion 0101h. Drivers open after boot: TIMER (handle 2,
  alias "timer") and MMSYSTEM (1, "mmsystem.dll"); midimapper is not
  opened at boot. MMSYSTEM's DriverProc through USER answers 1 to
  DRV_INSTALL (9) and DRV_EXITAPPLICATION (0Ch), 0 to 0, 8, 0Dh, 0Fh,
  10h, 0800h, 0801h, 0808h, 0BFFh, 0C00h, 4000h, 4001h, 7FFFh. Drv* are
  USER's functions (same answers as OpenDriver etc.).
- DriverCallback(dwCallback, uFlags, hDevice, uMessage, dwUser, dw1,
  dw2), 22 bytes: type = uFlags & 7. 1 window: PostMessage(LOWORD,
  uMessage, hDevice, LOWORD dw1.. i.e. lParam dw1), returns PostMessage's
  AX (3.1's USER answers a queue offset, 110, 132 ...), 0 for window 0;
  2 task: PostAppMessage, 0 for task 0; 3 function: 1, 0 when LAR of its
  selector fails or is not code; 0 for types 0, 4-7. The function gets
  AX = LAR's access rights (FB00h), BX = hDevice, CX = ES = the caller's
  SS, the caller's DX SI DI DS, flags 3206h (PF), return address in
  MMSYSTEM. Unless DCB_NOSWITCH (8) it calls StackEnter: on the
  interrupt stack when there is one and it is free (callback SP 05E6h),
  else on the caller's stack. Stack use: push bp, [StackEnter], one word,
  the five arguments (the callback from TIMER at SP 05C2h, as TPR saw).
- StackEnter: no stack yet (timeSetEvent makes it) or stack in use: no
  switch, one word pushed, CF set (flags ZF PF CF); otherwise SS = the
  600h block, SP 05FCh with the old SS:SP above, flags 3202h (AF set in
  some runs: masked). StackLeave pops the word (ZF PF) or switches back
  (PF). Every other register kept. The block's owner as GetExePtr shows
  it: TIMER or MMSYSTEM depending on who first called (not compared).
- timeSetEvent: callback NULL, a data pointer or 0001:0000 -> 000Bh
  (MMSYSERR_INVALPARAM) without the driver. Its frame: push bp, 14 bytes
  of locals holding the arguments in the driver's order (TMS sees the
  address and the saved BP after them). timeGetTime, timeBeginPeriod,
  timeEndPeriod and timeKillEvent jump to the driver (TMS: from=the
  application); timeSetEvent, timeGetSystemTime (size < 8 -> 129, else
  wType 1 and the time) and timeGetDevCaps (lParam1 the caller's
  pointer, lParam2 its size) call it.
- OutputDebugStr: no INT 21h, no INT 41h (KERNEL's OutputDebugString
  does one INT 21h there): nothing observable.
- mmTaskSignal(h): PostAppMessage(h, WM_USER, 0, 0), its AX; 0 for 0
  and for 1234h. mmTaskBlock(h): GetMessage loop, window messages
  translated and dispatched, returns the first task message's number
  (0408h for a WM_USER+8 posted before the signal). mmGetCurrentTask =
  GetCurrentTask. mmTaskYield returns.
- mmTaskCreate(fn, &h, inst): 0, h valid, returns before fn runs; fn is
  called by MMTASK (return address in MMTASK) with DX:AX = inst and inst
  on the stack, SI 0, DI the instance, ES 0, DS MMTASK's, BP 141Ah over a
  0 at the stack's top 141Ch; the command tail is empty; the task's data
  has fn at 10h and inst at 14h (w16 puts them there too); its queue
  holds 64; the task ends when fn returns. NULL handle pointer: works.
- Undocumented exports' argument sizes (MMP section pops): MMDRVINSTALL
  (34) pops 8 and answers 0 to zeros; JOYSETCALIBRATION (109) pops 26
  and answers 6; MCIPARSECOMMAND (704) faults on 3.1 with zeros
  (UNVERIFIED: w16 guesses 16).

## Known differences left (mcmp.py KNOWN)

- The mmTask function's BX, CX, DI: CX 05EEh on 3.1 (left over from
  its start-up calls), DI the instance handle and BX = DI - 0Fh in both,
  but handle values follow the heap; the function's selector in the
  task's data likewise.

## Layer 2 observations

ARCHITECTURE.md 23.2 (MMIO) has them; the raw logs are MMP's sections
F D M B E (`MRUN_ARG=FDMBE tests/mmsystem/mrun.sh OUT ms`). Things that
took a second run to see: the probe's C argument lists are evaluated
right to left (a "then at" query beside a call runs before it); an
unbuffered file's place is MMSYSTEM's (seeks send nothing until the
next read or write); 3.1 takes mmioSeek(-1, SEEK_SET) on an unbuffered
file as the place -1. Masks added: GetTempFileName's number, the
interrupt stack's owner.

## Layer 3 (wave, MIDI, aux, joystick, sndPlaySound): done

Run name `w16-mmsys3`. ARCHITECTURE.md 23.2 has the facts in order
(device drivers, MMSYSTEM's own checks, sndPlaySound, MessageBeep,
joystick), 23.4 the results, 23.5 what is UNVERIFIED.

Code: `shell/mmsystem/dev.c` (the drivers opened at DRV_LOAD, every wave,
MIDI and aux function, the error texts from `mmsystem.rc`'s string
table), `snd.c` (sndPlaySound, MMSYSTEM's window "#42", SystemStart at
boot), `stubs.c` (MCI; joystick without a driver), `mmfix.asm`
(mm_iscode, mm_hinst set at load).

Tools: MMSYSLOG.DLL + MDS*.DRV (tests/mmsystem/mdl.c, mds.c: logging
pass-through device drivers; a scratch copy of the real driver has its
imports of MMSYSTEM renamed to MMSYSLOG, so DriverCallback and MIDIMAP's
midiOut* calls are recorded too), MMP's device sections (mmdev.c: n e w i
m j a y x b; l lists the open drivers, z is 3 s of messages; the first
device section waits 4 s and logs the boot; MMSYSTEM's window's messages
are logged as "to MMSYSTEM's window"), `mrun.sh` (MRUN_SND=none|sb2|
multi|sb16, MRUN_SHIM=1), `mdscen.sh` (cases NAME:SOUND:SHIM:SECTIONS,
`-m` Microsoft's only), `mprog.sh` + tests/scripts/input/mm*.txt (Sound
Recorder, SND.CPL, MIDI Mapper), boot-test parts `mmsystem@dev` and
`mmsystem@prog`. `mcmp.py` masks: each one says why.

Configuration sb2 (Windows 3.1's own SNDBLST2.DRV and MSADLIB.DRV from
.tools/ref/win31/src, keys as Setup writes them: wave= midi=
sndblst2.drv, midi1=msadlib.drv) is the one used. MSADLIB finds no Ad
Lib in DOSBox (DRVM_INIT 3, 0 devices), so MIDI out is SNDBLST2's port
and the MIDI Mapper's "Ad Lib" setup fails with 68 (MIDIERR_NODEVICE), as
on 3.1. Creative's SB16 drivers: SB16SND.DRV wants ASP.SYS (mrun loads it
with DOSBox-X's DEVICE) and then stops at "Incorrect MIDIPORT setting in
the SYSTEM.INI file" even with [sndblst.drv] port/int/dma/hdma/midiport
set: not used. Pitfalls met: a probe call that hands the driver NULL or
0001:0000 (caps), or an error text into 0001:0000, left 3.1 unstable
later (the -W wait then saw a stalled log): removed; a NULL handle pointer
opens the device with no handle (done last in its section); C argument
lists are evaluated right to left; the -W wait needs 40 s (mrun's
default) on a loaded host.

Results (2026-11-18): mdscen.sh, all ten cases identical after the masks;
mprog.sh: every screen of the three programs 0 pixels off.

Not done (23.5): Media Player (needs MCI: layer 4), a joystick driver,
QuickTime's sound (qtplay runs without a wave driver), w16's MMSOUND.DRV
handing MessageBeep to sndPlaySound as 3.1's does, Creative's drivers.

## Next: layer 4 (MCI)

Where to start: the stubs in `stubs.c` (mci*). The task's list: the core
command table from the Programmer's Guide, mciLoadCommandResource, [mci]
and [mci extensions], MCIWAVE/MCISEQ/MCIQTW. The layer 3 tools carry over:
MCIWAVE.DRV and MCISEQ.DRV (in .tools/ref/win31/src, and installed by
Setup in the reference) are installable drivers that call the wave and
MIDI functions, so MDS*.DRV under them shows what MCI makes of a command;
mprog.sh can take Media Player (MPLAYER.EXE) next. MCIQTW.DRV imports DriverCallback,
mciLoadCommandResource, mciSet/GetDriverData, mciDriverYield,
mciDriverNotify and mciFreeCommandResource.
