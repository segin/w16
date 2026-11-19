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
- [x] Layer 4: MCI (below)
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

Configuration sb2 (Windows 3.1's own SNDBLST2.DRV from
.tools/ref/win31/src, keys as Setup writes them: wave= midi=
sndblst2.drv) is the one used. Setup's midi1=msadlib.drv is left out:
MSADLIB.DRV finds DOSBox's OPL in some runs and not in others (DRV_ENABLE
1 or 0, DRVM_INIT 8 or 3, 1 or 0 devices), which moves the MIDI IDs; the
earlier runs with it showed a driver with 0 devices staying open. MIDI
out is SNDBLST2's port, and the MIDI Mapper's "Ad Lib" setup fails with
68 (MIDIERR_NODEVICE), as on 3.1. Creative's SB16 drivers: SB16SND.DRV wants ASP.SYS (mrun loads it
with DOSBox-X's DEVICE) and then stops at "Incorrect MIDIPORT setting in
the SYSTEM.INI file" even with [sndblst.drv] port/int/dma/hdma/midiport
set: not used. Pitfalls met: a probe call that hands the driver NULL or
0001:0000 (caps), or an error text into 0001:0000, left 3.1 unstable
later (the -W wait then saw a stalled log): removed; a NULL handle pointer
opens the device with no handle (done last in its section); C argument
lists are evaluated right to left; the -W wait needs 40 s (mrun's
default) on a loaded host.

Results (2026-11-18): mdscen.sh, all ten cases identical after the masks;
mprog.sh: every screen of the three programs 0 pixels off. Full run
(remote-test.sh, make test and the whole boot test): make test passes;
boot test 121 ok, 4 failures: ddeml (conn) and lzexpand (quirks), known
on master; taskman@tree (known to fail now and then); mmsystem@timer
(timer-shim: one timeKillEvent of a 500 ms one-shot event answered 97,
the event already fired, with w16's TIMER.DRV under load; the part
passed when run again alone: a timing flake of w16's TIMER.DRV, not
of layer 3, to look at).

Not done (23.5): a joystick driver,
QuickTime's sound (qtplay runs without a wave driver), w16's MMSOUND.DRV
handing MessageBeep to sndPlaySound as 3.1's does, Creative's drivers.

## Layer 4 (MCI): done

Run name `w16-mci` (`w16-mci2` for a second run beside it). Code:
`shell/mmsystem/mci.c` (all 16 exports), `tools/mkmcitab.py` (w16's core
and six type tables, generated into the build as `mcitab.rc`, included
by `mmsystem.rc`), the MCI texts in `mmsystem.rc`, MM_MCINOTIFY in
`snd.c`'s window (auto-close), DRV_EXITAPPLICATION in `core.c` (a task's
devices closed). ARCHITECTURE.md 23.2 has the facts, 23.4 the results,
23.5 what is UNVERIFIED. Tools (committed): MMP section Q (`tests/mmsystem/
mmci.c`, a script C:\MCI.TXT: `s` mciSendString, `c` mciSendCommand, `x`
mciExecute, `e`/`E` error strings, `y` yield procs, `d` helpers, `l`/`f`
tables, `t`/`T` MCT settings), MCT.DRV (`mct.c`, `mct.rc`: test MCI
driver, options from MCI_OPEN_DRIVER_PARMS.lpstrParams: type=N
table=NAME fail nofill), MDSC1/2.DRV (MCIWAVE/MCISEQ behind MMSYSLOG),
`tests/mmsystem/mcirun.sh OUT ms|w16 SCRIPT [shim]`, [mci] names in
`tests/mmsystem/mci/mct.ini`. Run: `tools/remote-run.sh -n w16-mci -g
build/mci "tests/mmsystem/mcirun.sh build/mci/X-ms ms tests/mmsystem/mci/
explore1.txt"`. Pitfalls: DRV_OPEN's lParam1 for an MCI driver is ""
(USER's); the options are in lpstrParams. MMSYSLOG takes a DWORD as a
string pointer only for LDT selectors.

Results (2026-11-18): `tests/mmsystem/mciscen.sh` (boot-test part
`mmsystem@mci`): explore1-11 identical (5,000+ lines; e3 alone 2025: every
documented device-type command for the eleven types), e5/e7 over 3.1's
MCIWAVE and MCISEQ identical but the DRV_EXITAPPLICATION records (compared
apart: w16's MMTASK task ends earlier; tried one and two OldYield before
its end, neither matched: left as is), qt (MCIQTW.DRV on
/mnt/build/w16-qtfiles/ref-installed/c, `-e W16_QTDIR=...`) identical on
the MCI side. `mprog.sh mplayer`: Media Player's nine screens 0 pixels
off. The fix that Media Player needed: mciSendCommand clears the high word
of the block's second DWORD after a MCI_RESOURCE_RETURNED answer. Full
run (remote-test.sh -n w16-mci -e W16_QTDIR=/mnt/build/w16-qtfiles, at
06f64f5): make test passes; boot test 122 ok, 3 failures, all known on
master: ddeml (conn), lzexpand (quirks), soundtime (sometimes). Every
mmsystem part (core mmio timer qt dev mci prog), timer, toolhelp and
quicktime passed. The mmsystem@timer flake (timeKillEvent 97) was not seen
in two runs of the part; an earlier partial run saw timer@s's shim
timing 102 ms off (a 100 ms limit) and toolhelp@ref386's Microsoft side
not finishing, both on Microsoft's MMSYSTEM and passing in the full run.

Observed on 3.1 (explore1-11; raw logs in build/mci, not kept):
- Driver protocol: OpenDriver (USER: message 0, DRV_LOAD, DRV_ENABLE for
  a first instance, DRV_OPEN with lParam2 = MCI_OPEN_DRIVER_PARMS {WORD
  wDeviceID (the new ID), LPCSTR lpstrParams (the [mci] line's options),
  WORD wCustomCommandTable FFFFh, WORD wType 0}; the driver fills table and
  type), then MCI_OPEN_DRIVER (0801h) with the open flags and block;
  every command by SendDriverMessage (from=USER) as (msg, flags, block);
  close: MCI_CLOSE_DRIVER (0802h, the close flags and block) then
  CloseDriver. Any message number from mciSendCommand is passed on
  (0900h, 1234h, even 07FFh). MCIWAVE loads its table at DRV_LOAD.
- Device IDs: lowest free from 1; aliases case-insensitive; default alias
  = the device name or element as typed; ALIAS flag always added (string
  path and command path, where the app's block is patched for the call:
  alias pointer = type pointer, then restored; TYPE_ID: type pointer = the
  [mci] key's name ("other", "other2" for HIWORD 2), no alias). Element
  only: [mci extensions] of WIN.INI by extension (unknown -> 263); "type
  X" keyword; "X!element"; "new" (element "", needs alias: 299). Opening
  an element twice without shareable is allowed (driver's business).
  Opening by a type name ("animation") picks the type's lowest [mci] key
  (animation1). sysinfo counts [mci] keys by name prefix = type name.
- Command tables: custom (driver's) -> type table by the wType the driver
  reported -> core. Type tables exist for videodisc, overlay, cdaudio,
  animation, waveaudio, sequencer (not vcr, dat, scanner, digitalvideo,
  other). Core: open close play record seek stop pause resume set status
  capability info sysinfo break sound save load (no media present, no
  info file). Keywords, flags and offsets of each: explore3/8 logs
  (summarised in ARCHITECTURE.md 23.2 when written).
- "open" with a device whose custom or type table has an "open" entry
  (MCIWAVE; animation, overlay, waveaudio types): the args are parsed
  again with that entry after DRV_OPEN into a fresh zeroed block:
  dwCallback = the window only with notify, wDeviceID 0, and every string
  parsed from the args points 4 bytes into a copy of it (alias
  "abcdefghij" arrives as "efghij", "ab" as "", type "waveaudio" as
  "audio"); the device name/element and default alias are unaffected. A
  parse error there closes the driver again (no MCI_OPEN_DRIVER).
- Parser: blanks and tabs separate; case-insensitive; quotes for strings
  (305 extra chars after, 294 no closing quote; quoted keywords are not
  keywords: 290); multi-word keywords match with any blanks between;
  integers: decimal, "-" allowed (signed range), no "+"/0x (270); a
  positive value wraps silently past 2^32 unless a digit would overflow
  429496729*10 (270); constants take integers too; "a:b:c:d" packs bytes
  (sequencer offset 1:2:3:4 = 04030201h); duplicate keyword 295; two
  values of one constant 284; unknown keyword 259; unknown verb 261;
  missing value: integer 270, constant 312; "" 267; no device 292.
- Returns: integer -> "%ld" signed, buffer must be >= 12 bytes (else 268);
  MCI_RESOURCE_RETURNED: string HIWORD(dwReturn) from MMSYSTEM (or the
  driver's with MCI_RESOURCE_DRIVER), unknown id -> ""; colonized 3/4:
  "%02d:%02d:%02d[:%02d]" of the bytes, both flags "01:02:03:"; precedence
  resource > colonized > integer_returned (deref of the slot as a far
  pointer) > plain. String returns: the caller's buffer and size go to the
  driver (NULL buffer: NULL, 0); no check after. Rect returns "%d %d %d
  %d". sysinfo quantity writes the DWORD into the buffer, then formats in
  place. Errors: LOWORD; >= 512 gets HIWORD = device ID; buffer cleared.
  mciSendString zeroes the buffer's first byte first.
- Notify: dwCallback = the hwnd argument always (string path), notify
  flag passed; drivers post via mciDriverNotify (PostMessage MM_MCINOTIFY,
  wParam status, lParam device ID; returns PostMessage's AX). MMSYSTEM
  notifies sysinfo itself (lParam 0); break with notify: nothing.
  Auto-open (verb on an unopened name): open with MMSYSTEM's window as
  callback, the command sent with notify to that window (closes the
  device when notified); notify by the app -> 300.
- Yield: default proc MMSYSTEM's with data 3 (VK_CANCEL); mciSetYieldProc
  NULL -> FALSE (unchanged); break on N: MMSYSTEM's proc, data N; break
  off: no visible change; break hwnd alone 273. mciDriverYield calls the
  proc (dev, data), returns its result.
- Helpers: creator task, driver data (set TRUE/get), invalid ID -> 0.
  mciLoadCommandResource: index lowest free from 1 (0 = core, not
  freeable), the same resource twice = two tables; missing -> FFFFh.
- Errors: mciGetErrorString shares MMSYSTEM's string table (0-69 wave
  texts, 257-349 MCI, 513-543 types/modes/formats, 1025-1029, 1152-1153,
  1216-1227); size 0 -> 0 untouched, else truncated, unknown -> 0 "";
  HIWORD = an open device's ID with LOWORD >= 512 -> the driver's string,
  not open -> 278's text and TRUE. 264's text has "\n\n" (bytes 0A 0A).
- mciExecute: mciSendString(cmd, NULL, 0, NULL); on error MessageBox
  caption = the device token, text = the error, MB_ICONHAND, OK (button ID
  2), no owner; returns TRUE/FALSE.

## QuickTime's player on the tree alone (2026-11-18)

A user closed QuickTime's Movie Player on w16's install tree alone
(w16's MMSYSTEM, MMTASK, TIMER.DRV and TOOLHELP, booted with
`tests/dropin.sh -T build/w16`, so KRNL286) and Windows ended with
"Standard Mode: Fault in MS-DOS Extender". Not this module's: with
Microsoft's MMSYSTEM and MMTASK, its TIMER.DRV, all three, or its
TOOLHELP put back the fault was the same; it was KERNEL's free of a
selector still in FS (`docs/notes/quicktime-handover.md`, "Closing the
player"; ARCHITECTURE.md 9.2). `tests/qtplay.sh` now closes the player
in every run (mmsystem@qt among them), and `-T 286|386` runs it on the
tree alone (boot-test parts quicktime@tree, quicktime@tree386). The
tree's default SYSTEM.INI no longer says MMSYSTEM.DLL is not w16's.

## Flakes of the boot-test case (2026-11-19)

Run names `w16-mmflake`, `w16-mmflake2` (two boot-test loops side by
side, to load the host as other agents' runs do) and `w16-mmflake3`
(scenario runs). Four causes: two from 3.1's TIMER.DRV, whose timeGetTime
often steps 55 ms back for one 55 ms period (ARCHITECTURE.md 21.2; MMP
polling it 3 s with no event: 0-7 such steps), one in MMSYSLOG (a test
tool), and one real w16 bug:

- `dev` (sb2w, sb2plain; about half the runs of the part): MMP closed a
  wave device while its 200 ms buffer played. SNDBLST2's WODM_CLOSE then
  waits for the buffer (369 ms in good runs); in about 1 close in 10 it
  gave up after 51-53 ms, left the buffer queued (flags 0012h, MMP's 3 s
  wait for WHDR_DONE timed out) and answered 3 to every open after that
  (wave, MIDI), so sb2plain lost everything after section w. Seen under
  Microsoft's MMSYSTEM as under w16's (32 closes each: 3 and 4). MMP now pauses the device, writes the buffer
  and closes: SNDBLST2 answers 33 at once in every run, and MMP resets,
  unprepares and closes as before.
- `timer` (timer-plain, timer-shim): TPR's waits and TPC's spinning
  callback (tests/timer) measured time as an unsigned difference, which
  a step back made huge; and under load, checks of numbers that follow
  the host's speed: see `docs/notes/timer-handover.md`.
- `dev` (sb2m) and `mci` (e7), on either side: a WOM_DONE record missing
  from one log. MMSYSLOG's MlDump cleared every record after writing the
  ones it had seen, so a driver's callback recorded at interrupt time in
  between was lost. It now keeps those for the next dump.
- `mci` (e7), w16 only: **a w16 bug.** "play c:\windows\chimes.wav wait"
  (auto-opened) never returned in 14 of 16 runs under load (0 of more
  than 50 on 3.1). MCIWAVE waits for its MMTASK task by calling
  mciDriverYield; w16's default yield procedure (and mmTaskYield) only
  called PeekMessage(PM_NOREMOVE), which returns at once without letting
  other tasks run when a message is waiting, so with any message in the
  application's queue the application spun and the task never ran.
  Found by elimination (the hang came at the same command each time;
  bigger records showed the application's last call to be a yield) and
  confirmed with a new probe, MCI script explore12 (`y 1 q`, `y 1 Y`):
  3.1's procedure and mmTaskYield let a counting task run during 20
  calls with a window message queued, and leave the message queued;
  w16's let it run only with nothing queued. Both now call Yield when
  PeekMessage finds a message (shell/mmsystem/mci.c mm_mciyield, core.c
  MMTASKYIELD); explore7 then passed 16 of 16 under the same load, and
  explore12 is identical. MMP's section k also signals a task right
  after mmTaskCreate now (both: the task's first mmTaskBlock gets it).
- `mci` (e5, e7), either side, under load: MMSYSTEM's start-up sound
  (2.5 s, played at boot) still played when a script began, and its
  WOM_DONE fell into a later step on one side. Section Q (mmci.c) now
  pumps messages for 4 s and drops the records first. After that,
  MCISEQ's mciDriverNotify answered 00F2 or 0108 on 3.1 from run to run
  (w16: 0108 throughout): PostMessage's AX, the message's place in USER's
  queue ring; mcmp.py now compares only whether it is 0.

Reliability after these changes (2026-11-19): `tools/remote-test.sh -b
mmsystem timer`, two such runs at a time (run names w16-mmflake and
w16-mmflake2, host load about 20-30 on 40 threads): mmsystem 10 of 10,
timer 10 of 10. Before them (master 4c5a53c, three runs of the case alone)
`dev` failed in 2 and `timer` in 1; the runs in
between, each with part of the changes, are in the commits' messages.
Full boot test (no make test), this branch and master 4c5a53c side by
side (so under load): both 118 ok; failures on both: taskman@tree,
ddeml (conn), lzexpand (quirks), quicktime (the movie's area, all three
parts); on master only: mmsystem@timer (tod), cplclick@co-tree, sound
(sn1); on the branch only: timer@qt, toolhelp@qt (the same qtplay
movie-area failure as quicktime's) and trails@timed, all three passing
when run again alone with quicktime.

## Next

All four layers are done. Left (ARCHITECTURE.md 23.5): MMTASK's end
(when 3.1's task really ends: record KERNEL's task switches around it, or
TOOLHELP's NotifyRegister in MMC.DLL, to see what 3.1's MMTASK does after
the function returns), a joystick driver, w16's MMSOUND.DRV handing
MessageBeep to sndPlaySound, Creative's drivers.
