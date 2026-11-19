# w16 sound driver (`MMSOUND.DRV`, module `SOUND`)

w16's own sound driver for Windows 3.1: the beep behind USER's
MessageBeep and the Windows 1.x/2.x SOUND API (voice queues of notes,
OpenSound ... StartSound), played on the PC speaker, the beep through
MMSYSTEM's sndPlaySound when MMSYSTEM is loaded. Like every w16
driver it is a genuine Windows 3.1 driver (ARCHITECTURE.md 6.7): the
interface of Windows 3.1's `SYSTEM\MMSOUND.DRV` (and of its
`SYSTEM\SOUND.DRV`, the same file byte for byte) and nothing
w16-specific, and it replaces that driver as it is (`SYSTEM.INI [boot]
sound.drv=mmsound.drv`, as Setup leaves it). Standard mode; 386
Enhanced mode not tried.

`sound.asm` (NASM), `sound.lnk` (wlink) and the note table from
`tools/sndtables.py`; `make` builds `build/dos/MMSOUND.DRV` (one fixed
code segment, one fixed data segment, expected Windows version 3.10) and
the install tree's copy. w16's default `SYSTEM.INI` (`assets/`) names it.

Sources: the NE tables of Windows 3.1's file (format facts: module name
SOUND, the 18 names and ordinals; `tools/neinfo.py`,
`docs/exports/SOUND.txt`; who imports what, `tools/neimports.py
--importers SOUND`), the Windows SDK's Sound functions (signatures,
the S_SER* error codes, S_QUEUEEMPTY/S_THRESHOLD/S_ALLTHRESHOLD), Pietrek,
*Windows Internals*, ch. 1-2 (KERNEL loads SOUND.DRV before USER; USER
asks it for "enable"/"disable" by name, which it does not have, and reads
WIN.INI's Beep), and black-box observation of Windows 3.1's driver with
the probes below. Microsoft's file was never disassembled, and nothing
behind a pointer it returns was read but the one word GetThresholdEvent
documents.

## Interface

Importers in the reference install: USER (DoBeep, 17), WINMINE (1, 2, 4,
5, 9, 10), DRWATSON (1, 2, 8, 9, 10). The bytes each entry takes off the
stack were measured with SN1 (every entry called with marker values in
all registers and 16 words on the stack); Windows 3.1's driver keeps SI,
DI, DS, BP and the direction flag everywhere and returns with interrupts
enabled; BX, CX, DX and ES it changes in some entries (w16's: AX, BX, CX,
DX, ES in some).

| Ord | Name | Pops | Returns, does |
|---|---|---|---|
| 1 | `OpenSound()` | 0 | 1 (voices); -1 (S_SERDVNA) when already open. A queue of 192 bytes |
| 2 | `CloseSound()` | 0 | 0; stops, frees the queue. When not open: a selector-like value (3.1's: 09D7h; w16's: its data selector; not compared) |
| 3 | `SetVoiceQueueSize(nVoice, nBytes)` | 4 | 0; -2 (S_SEROFM) for 1-5 bytes. 6 bytes a note: 192 bytes hold 32 notes, 12 hold 2, 1024 hold 170 |
| 4 | `SetVoiceNote(nVoice, nValue, nLength, nCdots)` | 8 | 0; -5 (S_SERBDNT) for a value outside 0-84; -4 (S_SERQFUL) when full; nLength a byte (0: nothing queued, AX = nValue) |
| 5 | `SetVoiceAccent(nVoice, nTempo, nVolume, nMode, nPitch)` | 10 | 0; -8 (S_SERDTP) tempo outside 32-255, -10 (S_SERDMD) mode outside 0-2, -12 (S_SERDPT) pitch outside 0-83 |
| 6 | `SetVoiceEnvelope(nVoice, nShape, nRepeat)` | 6 | 0, nothing (DX kept) |
| 7 | `SetSoundNoise(nSource, nDuration)` | 4 | 0, nothing (DX kept) |
| 8 | `SetVoiceSound(nVoice, lFrequency, nDuration)` | 8 | 0; -13 (S_SERDFQ) for HIWORD(lFrequency) 1-36 or 32768 and up; -4 when full |
| 9 | `StartSound()` | 0 | 0; the queued items play |
| 10 | `StopSound()` | 0 | 0; stops, empties the queue |
| 11 | `WaitSoundState(nState)` | 2 | 0 once the queue has played (0) or the count is down to the threshold (1, 2); -16 (S_SERDST) for other states |
| 12 | `SyncAllVoices()` | 0 | 0, nothing (DX kept) |
| 13 | `CountVoiceNotes(nVoice)` | 2 | the items not yet started while playing (1 while the last plays); 0 when not playing, even with notes queued |
| 14 | `GetThresholdEvent()` | 0 | DX:AX = a word of the driver's data (offset 84h, 0); 0 when not open |
| 15 | `GetThresholdStatus()` | 0 | bit 0: voice 1's count came down to its threshold; cleared by the call |
| 16 | `SetVoiceThreshold(nVoice, nNotes)` | 4 | 0 |
| 17 | `DoBeep(wType)` | 2 | the beep; AX 1 (sndPlaySound's answer) when a sound was handed to MMSYSTEM, else as the speaker loop leaves it (3.1's 0000, 0101, 0505), DX 500 |
| 18 | `WEP(bSystemExit)` | 2 | 1 (resident name; DX kept) |

One voice: the voice argument is not looked at (3.1's returned the same
for voices -1, 0-5, 255, 256 with CountVoiceNotes; writes to voices other
than 1 left the probe hanging later, so only voice 1 is written to).
Every entry but OpenSound and DoBeep does nothing and returns 0 when the
device is not open.

## The beep

All observed on Windows 3.1's driver with SN1's parts M, N, I, R, L, Y
and Z (`tests/soundprobe.sh beep`): the driver behind SNDREC, which
records USER's calls of it, and with its imports of KERNEL renamed to
KRNLOG (`tests/apps/krnlog.c`), which records every KERNEL function it
calls and, by handing out its own entries for them, every call of the
MMSYSTEM functions it gets from GetProcAddress, with the recording of
the run's sound (speaker and Sound Blaster) to hear what plays.

- **USER**: MessageBeep(type) calls DoBeep(type) (once per MessageBeep,
  every type, the device open or not), unless the beep is off:
  SystemParametersInfo(SPI_SETBEEP, FALSE) stops the calls; WIN.INI
  [windows] Beep=no written while Windows runs does not (USER read it at
  its start). w16's USER does the same (`user/misc.c`).
- **Finding MMSYSTEM**: at every DoBeep (but type FFFFh's) and every
  OpenSound until it is found, GetModuleHandle("MMSYSTEM"); never
  LoadLibrary, nothing at the driver's load. Found, GetProcAddress by
  ordinal: 2 (sndPlaySound), 602 (timeSetEvent), 603 (timeKillEvent),
  then timeKillEvent of the timer event it has (none: 0; MMSYSTEM answers
  97, or 6 when it was loaded by LoadLibrary and has no timer driver),
  and the handles are kept for good: not looked up again, also when
  MMSYSTEM goes away (a beep after that calls where sndPlaySound was:
  SN1 part L does not beep then). MMSYSTEM loaded by a program's
  LoadLibrary after the boot is found at the next beep.
- **The type**: FFFFh beeps on the speaker, MMSYSTEM or not. Any other
  type, once MMSYSTEM is found, goes to sndPlaySound(name, SND_ASYNC)
  (flags exactly 0001h) at once, in DoBeep, with interrupts on or off
  (SN1 part Z: no deferral), by bits 4-7 of the type alone: 1
  "SystemHand" (MB_ICONHAND 10h, also 11h, 14h, 110h, 1010h, 8010h), 2
  "SystemQuestion", 3 "SystemExclamation", 4 "SystemAsterisk", any other
  "SystemDefault" (0, 1-0Fh, 50h-0F0h, 100h, 8000h, FFF0h, FFFEh).
  MMSYSTEM looks the name up in WIN.INI [sounds] (ARCHITECTURE.md 23.2):
  a missing, empty or unplayable entry plays SystemDefault's sound.
- **The speaker**: when MMSYSTEM is not loaded or sndPlaySound answers
  FALSE (no wave device; no sound for the name and none for
  SystemDefault). DoBeep sets the 8254's channel 2 to divisor 1593 (749
  Hz, mode 3), turns the speaker on (port 61h bits 0 and 1), waits in a
  loop and turns it off. The loop is about 64,000 instructions: the beep
  lasts 21.4 ms at DOSBox's 3000 cycles a millisecond, 6.7 ms at 10000
  and about 2 ms at cycles=max (the recordings), so on a real machine its
  length follows the processor's speed.
- **Asynchrony**: MessageBeep returns at once (the sound plays after it);
  a new beep stops the sound playing (MMSYSTEM's sndPlaySound: each new
  sound stops the last, a looping one too); FFFFh's speaker beep plays
  over a wave sound.

w16's driver does all of this (`findmm`, `DOBEEP`).

## MMSYSTEM's timer

Once MMSYSTEM is found (by a beep or OpenSound), 3.1's driver plays the
SOUND API's notes on MMSYSTEM's timer: StartSound calls
timeSetEvent(5, 5, its callback, 0, TIME_PERIODIC) with interrupts off
(MMSYSTEM's timer driver then runs the 8254's channel 0 at 5 ms: the
"clock" below was its doing), the callback is one tick, the last tick
kills the event (timeKillEvent from the callback, interrupts off), and
StopSound and CloseSound kill it too (CloseSound with nothing playing:
timeKillEvent(0)). Without MMSYSTEM it plays from IRQ 0 in 5 ms ticks
too, but its channel 0 is then not at a steady 5966 (a count of CE4Ah
seen while playing; how it times them, UNVERIFIED); w16's runs channel 0
at 5966 then, as below. OpenSound allocates the queue with
GlobalAlloc(0, 192) (fixed). Every entry of the SOUND API also asks
KERNEL for the current task (GetCurrentTask; OpenSound, StartSound and
each note once, CloseSound twice); what for was not observed and w16's
does not (KRNLOG's records of it are not compared). WaitSoundState
yields (Yield) while it waits.

## Playing

All measured on Windows 3.1's driver from DOSBox-X's recordings of SN1's
parts H, T and U (onsets and lengths of every tone, to about 0.5 ms) and
the port state SN1 reads while notes play (parts D, G, P):

- **The clock**: StartSound turns the speaker on; ticks are 5 ms, on
  MMSYSTEM's timer event (above; these measurements were all made with
  MMSYSTEM loaded) or, without MMSYSTEM, in w16's driver from IRQ 0 with
  channel 0 of the 8254 at count 5966 (200 Hz, mode 2; the handler that
  was on IRQ 0 is still called 18.2 times a second, every 65536 counts;
  after the last item channel 0 goes back to 65536).
- **Start**: for the first 2 ticks channel 2 sounds whatever it had
  (heard as a 10 ms burst of the last beep's or marker's tone); then
  the quiet the previous playing left over (below); then the first item.
- **Notes**: value 1-84, 37 is C5 (523 Hz), a semitone apart (equal
  temperament; the divisor 1193182 / f, `tools/sndtables.py`; 3.1's
  measured within 0.2% of it over all 84), transposed by the accent's
  pitch, wrapping past 84 (pitch 83 + note 2 is note 1). A note of
  length L at tempo t (quarter notes a minute) is H = 96000 / (t L) half
  ticks (2.5 ms), truncated; its sounding part s = 7H/8 (normal) or 3H/4
  (staccato), truncated, or H (legato); it sounds (s + 1) / 2 ticks and
  is then quiet for (H - s + 1) / 2. This fits every one of 78
  combinations measured to the tick (parts T, U and V: fifteen tempos
  and twelve lengths legato, six tempos by three lengths normal and
  staccato, twenty-one tempos by two lengths normal); simpler rules
  (round(7T/8) and ceil(T/8) in whole ticks) were off by one tick for
  seven of them. Dots, the volume (0 too), envelopes and noise change
  nothing. A rest (value 0) is quiet for (H + 1) / 2 ticks and one more
  when something follows it.
- **Quiet** is channel 2 at divisor 16 (about 75 kHz) with the speaker on
  (port 61h bits stay set; SN1 part P reads the count).
- **SetVoiceSound**: HIWORD(lFrequency) Hz (the fraction is not looked
  at) for ceil(nDuration / 2) ticks, all of it sounding; frequency 0 is
  a rest of one tick whatever the duration.
- **The end**: when the queue is empty as an item's sound ends (or as a
  rest starts), the speaker goes off (channel 2 keeps the note's
  divisor) and playing ends; WaitSoundState(0) returns then. The item's
  quiet that was not played is carried to the next StartSound: it delays
  that start's first item (survives CloseSound and OpenSound; after
  StopSound or CloseSound mid-note, the note's quiet). StopSound and
  CloseSound also make channel 2 quiet (3.1's next start had no burst of
  the old tone).
- **Channel 2's count** is written alone, without a control word: 3.1's
  notes, recorded, all start with a first half-period about 125 us long
  and are even after it, as a count written alone gives in DOSBox-X (with
  a control word the first is even). On a short note that lowers the
  measured frequency by up to a few percent, the same on both drivers.
- Notes added while playing play after the others; StartSound while
  playing changes nothing; StopSound and CloseSound stop at once
  (speaker off).

## How it was observed

- **SN1** (`tests/apps/sn1.c`, `sn1s.asm`), run as the shell by
  `tests/soundprobe.sh` on Microsoft's KRNL286, DOSX and USER, once with
  Windows 3.1's MMSOUND.DRV and once with w16's, in DOSBox-X at a fixed
  10000 cycles a millisecond with the run's sound recorded
  (`tests/dropin.sh` DROPIN_CAPTURE, DOSBox-X's `DX-CAPTURE /A`). Parts:
  A the ordinals; B every entry's stack use and registers; C the results
  over the inputs; D playing, counts and port state against the BIOS
  ticks, StopSound; E DoBeep and MessageBeep with each type; F a melody
  at three tempos and modes; G channel 0 while playing; H every note,
  the lengths, dots, modes, tempos, transposition, volume,
  SetVoiceSound; S notes added while playing, StartSound twice,
  thresholds, CloseSound while playing; P the ports, sampled while notes
  play; T and U timing (above); Q the queue's capacity, in a second
  program (SNH.EXE, SN1 renamed) while SN1 waits. SN1 puts marker tones
  on the recording (2983 Hz, 110 ms, in runs of 1-4) between its groups.
- `tools/wavtones.py` finds the tones in a recording (the square wave's
  edges); `tools/tonecmp.py` compares two recordings segment by segment
  between the markers. Above about 6 kHz a half-period is 3-4 samples
  of the 48 kHz recording and `wavtones.py --split` breaks some notes
  into pieces, differently for each placement of the samples on the
  edges: DOSBox-X's recordings are the same run after run, but they
  change with things as remote from the driver as the length of the run
  directory's path (installation-05, master c94ba4e: the same recording
  in run directories `w16` and `s16`, another in `snd-c94b`, as at
  5cb41d9 and 3d4bc47). `tonecmp.py` therefore joins the pieces before
  it drops the short ones; dropped first, a note broken up in its first
  30 ms started that much late (the `sound` case's failure in directory
  `w16`: segment 10's note 81, gap 36.5 ms; after the join 1.6 ms).
- **SNDREC** (`tests/apps/sndrec.asm`): a recording SOUND in front of the
  driver under test (renamed SOUNT by `tools/nerename.py`): every call
  from the boot on with its caller, arguments, results and the bytes
  taken off; SN1 prints them at its end. Only values are recorded.
- **KRNLOG** (`tests/apps/krnlog.c`, `krnlogs.asm`): a recording KERNEL
  for a scratch copy of the driver under test whose imports of KERNEL
  are renamed to KRNLOG (`tools/nerename.py --import KERNEL=KRNLOG`, on
  the SOUNT copy): GlobalAlloc, GlobalFree, GlobalLock, Yield,
  GetCurrentTask, GetModuleHandle (with the name), GetProcAddress (with
  the name or ordinal) and __WINFLAGS (a label at offset 0415h of a
  segment of its own: a DLL linked by wlink cannot export a constant;
  SN1 checks the run's GetWinFlags against it). It hands the driver its
  own entries for MMSYSTEM's sndPlaySound, timeSetEvent and
  timeKillEvent and records their calls (the sound's name and flags) and
  the timer callback's. SN1's parts M N I R L Y Z print its records and
  SNDREC's after each step ("in": during the call, "after": in the 2.2 s
  after it); `tools/beepcmp.py` cuts the recordings at SN1's markers and
  compares the sound (speaker or wave: 10 ms RMS stretches) segment by
  segment. `tests/soundprobe.sh beep` runs six configurations
  (BEEP_CASES): Microsoft's MMSYSTEM with Windows 3.1's SNDBLST2.DRV
  (from its Setup files) and without a wave driver, w16's MMSYSTEM the
  same, no MMSYSTEM (not in `[boot] drivers=`; part L loads Microsoft's,
  or w16's, with LoadLibrary).

## Verified

- `tests/soundprobe.sh` on installation-05 (DOSBox-X, 10000 cycles a ms):
  sn1: SN1's logs identical with both drivers (563 lines: every entry's
  stack use and registers, the results over the inputs, counts,
  thresholds, the ports while playing); the recordings' tones the same
  within `tools/tonecmp.py`'s limits over 148 segments (onsets within 7
  ms of each other, frequencies within 2%); queue: identical (the
  capacity for 6 queue sizes, with and without StartSound and accents);
  rec: SNDREC's records identical (USER's DoBeep(type) for every
  MessageBeep); allw16: the same records and beeps with every w16 part
  (WIN.COM, DOSX, KRNL286, USER, GDI) as with all of Microsoft's.
- `tests/soundprobe.sh beep` (2026-11-19): SN1's beep logs identical
  with both drivers in all six configurations (384 lines with each
  MMSYSTEM and SNDBLST2, 384 and 313 without a wave driver, 293 and 28
  without MMSYSTEM: every KERNEL and MMSYSTEM call the driver makes,
  USER's calls of it), the recordings the same segment by segment (up to
  82 segments, 69 sounds: the wave sounds DING/CHORD/TADA, the speaker's
  beeps).
- Boot-test cases `sound`, `soundtime`, `soundrec`, `soundbeep`, `soundboot` (0
  pixels on Microsoft's stack and on w16's WIN.COM/DOSX/KRNL286) and
  `treealone` (build/w16 alone as C:\W16, no file of Microsoft's on the
  drive: boots to its Program Manager).
- `tools/irqcheck.py --fixed`: both segments fixed (`make test`).

## With a sound card (the Sound Blaster 16 variant)

`tests/sb16setup.sh` installs Creative's Sound Blaster 16 ASP software
into a scratch copy of the reference install (the user's disk images,
`.tools/sb16`; what it changes: the script's header) and
`soundprobe.sh sb16` runs SN1's beeps there, DOSBox-X as an SB16.
Creative's wave driver SB16SND.DRV stops Windows at "Installable
Drivers" in DOSBox-X, in Standard and in 386 Enhanced mode (its FM and
auxiliary drivers load), so the beep with a wave device was observed
with Windows 3.1's own SNDBLST2.DRV instead (`soundprobe.sh beep`,
above), where DING.WAV and CHORD.WAV play on the recording. (In the
SB16 variant 3.1's DoBeep gave a 10 ms click for type 0 and nothing for
10h-40h: the sound handed to MMSYSTEM, which did not play there.)

## Not verified / differences

- 3.1's SOUND API without MMSYSTEM (`[boot] drivers=` without
  mmsystem.dll): it plays in 5 ms ticks, but its channel 0 is not at a
  steady 5966; w16's runs channel 0 at 5966 then, and the notes' timing
  differs by tens of ms (SN1 part Y there: a note cut by StopSound
  sounded 280 ms on 3.1's, 190 on w16's); not compared (the `beep`
  probe's no-MMSYSTEM case runs parts M and L only).
- What 3.1's driver uses GetCurrentTask for (every SOUND API entry asks;
  a check that the caller is the task that opened it?); w16's does not
  ask.
- When MMSYSTEM was found but timeSetEvent fails (MMSYSTEM loaded by
  LoadLibrary, no timer driver): w16's plays from IRQ 0 then; 3.1's not
  observed.
- After a play on MMSYSTEM's timer ended, StartSound came about 110 ms
  later on 3.1's recording than on w16's although every call between took
  the same time by GetTickCount and the BIOS ticks on both (SN1 part Y,
  before its marker was added): 3.1's clock lost two ticks there. Not
  explained.
- The BIOS's ticks around StartSound: 3.1's driver gives the old IRQ 0
  handler one to three ticks more in the first ticks of playing than
  18.2 a second would (SN1 part W: 1 more before the first note, 3 more
  after a quiet of 101 ticks; the same over 15 s of notes); w16's keeps
  the 18.2 a second throughout. GetTickCount and the BIOS clock gain
  about 0.05-0.15 s a StartSound on 3.1. (Measured before w16's played
  on MMSYSTEM's timer, as 3.1's does when MMSYSTEM is loaded: probably
  the timer driver's doing; part W not run since.)
- 386 Enhanced mode (VTD virtualises the 8254; not run).
- Calls that never returned on Windows 3.1's driver, so not compared:
  WaitSoundState(S_THRESHOLD or S_ALLTHRESHOLD) with nothing playing
  (w16's returns 0), SetVoiceNote after SetVoiceQueueSize(0) (3.1's
  hung Windows within 20 notes; w16's returns S_SERQFUL), writes to
  voices other than 1 (w16's treats them as voice 1).
- SetVoiceQueueSize while playing (w16's: S_SERMACT; not tried on 3.1's).
- How 3.1's writes channel 2's count every tick on a real 8254 (with or
  without a control word: DOSBox-X restarts the count either way).
- Real machines: the beep's length follows the processor's speed on
  both drivers, with different instruction mixes.
