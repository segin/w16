# w16 sound driver (`MMSOUND.DRV`, module `SOUND`)

w16's own sound driver for Windows 3.1: the beep behind USER's
MessageBeep and the Windows 1.x/2.x SOUND API (voice queues of notes,
OpenSound ... StartSound), played on the PC speaker. Like every w16
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
| 17 | `DoBeep(wType)` | 2 | the beep; AX as its loop leaves it (3.1's 0000, 0101, 0505), DX 500 |
| 18 | `WEP(bSystemExit)` | 2 | 1 (resident name; DX kept) |

One voice: the voice argument is not looked at (3.1's returned the same
for voices -1, 0-5, 255, 256 with CountVoiceNotes; writes to voices other
than 1 left the probe hanging later, so only voice 1 is written to).
Every entry but OpenSound and DoBeep does nothing and returns 0 when the
device is not open.

## The beep

USER's MessageBeep(type) calls DoBeep(type) (SNDREC's record: once per
MessageBeep, every type, the device open or not); w16's USER does the
same (`user/misc.c`), unless WIN.INI [windows] Beep is not "yes".
DoBeep sets the 8254's channel 2 to divisor 1593 (749 Hz, mode 3), turns
the speaker on (port 61h bits 0 and 1), waits in a loop and turns it
off. The loop is about 64,000 instructions: the beep lasts 21.4 ms at
DOSBox's 3000 cycles a millisecond, 6.7 ms at 10000 and about 2 ms at
cycles=max (the recordings), so on a real machine its length follows the
processor's speed. The type is not looked at.

## Playing

All measured on Windows 3.1's driver from DOSBox-X's recordings of SN1's
parts H, T and U (onsets and lengths of every tone, to about 0.5 ms) and
the port state SN1 reads while notes play (parts D, G, P):

- **The clock**: StartSound turns the speaker on and sets channel 0 of
  the 8254 to count 5966 (200 Hz: 5 ms ticks, mode 2; the handler that
  was on IRQ 0 is still called 18.2 times a second, every 65536 counts);
  after the last item, channel 0 goes back to 65536.
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
  between the markers.
- **SNDREC** (`tests/apps/sndrec.asm`): a recording SOUND in front of the
  driver under test (renamed SOUNT by `tools/nerename.py`): every call
  from the boot on with its caller, arguments, results and the bytes
  taken off; SN1 prints them at its end. Only values are recorded.

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
- Boot-test cases `sound`, `soundtime`, `soundrec`, `soundboot` (0
  pixels on Microsoft's stack and on w16's WIN.COM/DOSX/KRNL286) and
  `treealone` (build/w16 alone as C:\W16, no file of Microsoft's on the
  drive: boots to its Program Manager).
- `tools/irqcheck.py --fixed`: both segments fixed (`make test`).

## With a sound card (the Sound Blaster 16 variant)

`tests/sb16setup.sh` installs Creative's Sound Blaster 16 ASP software
into a scratch copy of the reference install (the user's disk images,
`.tools/sb16`; what it changes: the script's header) and
`soundprobe.sh sb16` runs SN1's beeps there, DOSBox-X as an SB16.
Results so far, on Microsoft's driver:

- Creative's wave driver SB16SND.DRV stops Windows at "Installable
  Drivers" in DOSBox-X, in Standard and in 386 Enhanced mode (its FM and
  auxiliary drivers load); Windows 3.1's own SNDBLST2.DRV (expanded from
  the reference's Setup files, [sndblst.drv] port=220, int=5) loads.
- With SNDBLST2.DRV as the wave device (MMSYSTEM loaded, WIN.INI
  [sounds] as Setup leaves it), Windows 3.1's DoBeep no longer beeps on
  the speaker: DoBeep(0) (and MessageBeep(0)) gives only a 10 ms click
  on the recording, types 10h-40h give nothing at all, and FFFFh gives a
  20 ms speaker beep (6.7 ms without a wave device). So 3.1's driver
  hands the beep to MMSYSTEM when there is a wave device (its imports of
  GetModuleHandle and GetProcAddress fit that), and the sound itself did
  not play in this emulation (a click, no DING.WAV). UNVERIFIED: which
  call it makes and with which sound; w16's driver does not do this: it
  always beeps on the speaker (`soundprobe.sh sb16` fails there).

## Not verified / differences

- The beep with a wave device (above).
- The BIOS's ticks around StartSound: 3.1's driver gives the old IRQ 0
  handler one to three ticks more in the first ticks of playing than
  18.2 a second would (SN1 part W: 1 more before the first note, 3 more
  after a quiet of 101 ticks; the same over 15 s of notes); w16's keeps
  the 18.2 a second throughout. GetTickCount and the BIOS clock gain
  about 0.05-0.15 s a StartSound on 3.1.
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
