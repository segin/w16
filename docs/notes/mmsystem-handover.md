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
- [ ] Layer 3: wave/MIDI/aux/joystick, sndPlaySound (in progress, below)
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

## Layer 3 (wave, MIDI, aux, joystick, sndPlaySound): in progress

Run name `w16-mmsys3`. Tools (committed): MMSYSLOG.DLL + MDS*.DRV
(tests/mmsystem/mdl.c, mds.c: logging pass-through device drivers; a
scratch copy of the real driver gets its imports of MMSYSTEM renamed to
MMSYSLOG, so DriverCallback and MIDIMAP's midiOut* calls are seen too),
MMP's device sections (mmdev.c: n e w i m j a y x b; l lists the open
drivers, z is 3 s of messages; the first device section waits 4 s and
logs the boot), `mrun.sh` with MRUN_SND=none|sb2|multi|sb16 and
MRUN_SHIM=1, `mdscen.sh` (cases NAME:SOUND:SHIM:SECTIONS, `-m`
Microsoft's only). Configuration sb2 (Windows 3.1's own SNDBLST2.DRV and
MSADLIB.DRV from .tools/ref/win31/src, keys as Setup writes them) is the
main one. Creative's SB16 drivers: SB16SND.DRV wants ASP.SYS (mrun loads
it with DOSBox-X's DEVICE) and then stops at "Incorrect MIDIPORT setting
in the SYSTEM.INI file" even with [sndblst.drv] port/int/dma/hdma/
midiport set: not used. MSADLIB finds no Ad Lib in DOSBox (DRVM_INIT 3,
0 devices). Pitfalls: a probe call that handed the driver 0001:0000 left
SNDBLST2 broken later (removed); a NULL handle pointer opens the device
with no handle (done last in its section); C argument lists are
evaluated right to left (GetID lines fixed); the -W wait needs 40 s
(mrun's default now) on a loaded host.

Observed so far (Microsoft's MMSYSTEM over SNDBLST2/MSADLIB/MIDIMAP):
- Boot: drivers opened at boot (by USER: message 0, DRV_LOAD,
  DRV_ENABLE, DRV_OPEN with options ""), one OpenDriver per key and
  direction: "wave" for wodMessage (DRVM_INIT 100, then WODM_GETNUMDEVS
  3), "wave" again for widMessage (100, WIDM_GETNUMDEVS 50), "midi" for
  mod (100, 1) and mid (100, 53), "midi1" likewise, then "midimapper"
  (mod 100, 1; its mid open closed again). An open whose entry is
  missing is closed at once. The mapper is opened only when there are
  MIDI output devices. Then MMSYSTEM plays SystemStart asynchronously.
- sndPlaySound: MMSYSTEM's hidden window (class "#42", an integer atom)
  gets the callbacks: WODM_OPEN with CALLBACK_WINDOW|WAVE_ALLOWSYNC
  (00010002), instance 0, the WAVEFORMAT inside the file image read into
  one global block (the header in the same block; its rec/user/next/res
  left as they were: masked), PREPARE (driver 8: MMSYSTEM sets
  WHDR_PREPARED itself), WRITE; on WOM_DONE (the window, dispatched in
  the caller's message loop) UNPREPARE and CLOSE. SND_SYNC waits for the
  end. NULL or "" stops (WODM_RESET, then the same clean-up), answer 1.
  Names: the [sounds] key first (case-insensitive; the file before the
  comma, blanks trimmed; no comma: the whole value), else a file (OpenFile
  search: "chimes.wav" works, "tada" does not); missing or not a wave:
  SystemDefault unless SND_NODEFAULT (then 0 and no driver call).
  SND_NOSTOP while one plays: 0. SND_LOOP needs SND_ASYNC (with SND_SYNC:
  0); loops are WHDR_BEGINLOOP|ENDLOOP, dwLoops FFFFFFFF. Unknown flags
  (FF00): 0. SND_MEMORY plays the caller's image in place (header in a
  small block of MMSYSTEM's); not RIFF: 0 even without NODEFAULT. A new
  sound stops the one playing first.
- MessageBeep goes through MMSOUND.DRV to sndPlaySound, asynchronously:
  0 SystemDefault, 10h SystemHand, 20h SystemQuestion, 30h
  SystemExclamation, 40h SystemAsterisk, 50h the default, FFFFh nothing
  through MMSYSTEM (the speaker); each stops the sound before.
- Error texts (by calling 3.1's functions; probe section e): 0-11 and
  32-35 for wave, the MMSYSERR texts and 64-69 for MIDI; other numbers 9
  (MMSYSERR_BADERRNUM) with "" written; size 0: 0, nothing written; NULL
  buffer: 9; truncated to size-1 characters.
- waveOutOpen: flags other than QUERY|ALLOWSYNC: 10 (handle untouched);
  callback type > 3, a window that is not one, a function that is not
  code: 11 (handle untouched); then *phwo = 0, bad ID 2, then the driver:
  WODM_OPEN(p1 -> WAVEOPENDESC{hWave, lpFormat (the caller's), dwCallback,
  dwInstance}, p2 = the caller's flags, user -> a DWORD on the stack); a
  query has hWave 0 and no handle; a NULL format goes to the driver (32).
  A NULL handle pointer opens the device. WAVE_MAPPER for a query asks
  device 0.
- Wave in: prepare/unprepare go to the driver (8: MMSYSTEM sets/clears
  WHDR_PREPARED); AddBuffer of an unprepared header 34 without the
  driver; unprepare of a queued (INQUEUE) header 33 without the driver;
  GetPosition passes size and pointer through; Close with buffers: the
  driver's 33; Message passes any number; handles are typed (an output
  handle to waveIn*: 5); stale handles 5.
- MIDI: midiOutOpen sets *ph = 0 before checking flags (10 for flag 1;
  callback type 4: 11); MODM_OPEN user -> the handle's own DWORD; the
  mapper (FFFF) goes to the midimapper driver (MIDIMAP looks for its
  setup's devices with midiOutGetNumDevs/GetDevCaps: 68 when absent).
- aux, joystick without drivers: 2 (bad ID) / 6 (FFFF; no joystick
  driver); joySetCapture with a bad window: 165 first.

Next steps: the wave/MIDI logs of build/m3 (probe fixes), then w16's
implementation (shell/mmsystem/dev.c, snd.c), mdscen.sh comparisons,
real programs (Sound Recorder, Media Player, SND.CPL, MIDI Mapper).

Layer 4 (MCI) after that: see the task's list (core command table from
the Programmer's Guide, mciLoadCommandResource, [mci] and [mci
extensions], MCIWAVE/MCISEQ/MCIQTW). MCIQTW.DRV imports DriverCallback,
mciLoadCommandResource, mciSet/GetDriverData, mciDriverYield,
mciDriverNotify and mciFreeCommandResource.
