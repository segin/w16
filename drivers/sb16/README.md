# w16 Sound Blaster 16 driver (`SB16.DRV`, module `SB16`)

w16's own driver for a Sound Blaster 16 (and the cards and emulators
compatible with it: DOSBox and DOSBox-X with `sbtype=sb16`): an
installable driver (ARCHITECTURE.md 10.7) that MMSYSTEM.DLL opens through
SYSTEM.INI `[drivers]` and that serves its wave, MIDI and auxiliary
devices. Like every w16 driver it is a genuine Windows 3.1 driver
(ARCHITECTURE.md 6.7): it runs under Microsoft's MMSYSTEM.DLL as under
w16's. ARCHITECTURE.md 24 has the interface and the results;
`docs/notes/sb16-handover.md` the state of the work.

It is not in the install tree's default SYSTEM.INI (a machine without
the card must start as before); `assets/README.TXT` says how to enable
it.

## Files

`sb16.h` (the definitions), `sb16a.asm` (NASM: the library entry, the
interrupt handler, the vector and the PIC, the huge-pointer copy),
`sb16.c` (DriverProc, the configuration, the DSP, mixer and DMA
controller, the work at interrupt time), `wave.c` (wodMessage,
widMessage), `midi.c` (modMessage, midMessage), `aux.c` (auxMessage, the
volumes), `sb16.lnk`, `sb16.rc` (the version resource from
`tools/mkver.py`). `make` builds `build/dos/SB16.DRV` (OpenWatcom large
model with SS != DS, one fixed code segment and one fixed data segment:
`tools/irqcheck.py --fixed` accepts it) and the install tree's
`SYSTEM\SB16.DRV`.

## Sources

- Windows 3.1 SDK: the Multimedia Programmer's Guide (the wave, MIDI and
  auxiliary functions, their structures, flags, error codes and callback
  messages, positions and loops), Programmer's Reference Vol. 1 ch. 25
  and Vol. 2 (installable drivers, GetPrivateProfileString,
  GetDOSEnvironment, GlobalDOSAlloc).
- What MMSYSTEM sends a device driver, and what Windows 3.1's own Sound
  Blaster driver SNDBLST2.DRV answers, observed black-box with the
  logging drivers of `tests/mmsystem` (MDS*.DRV and MMSYSLOG.DLL between
  Microsoft's MMSYSTEM and SNDBLST2: ARCHITECTURE.md 23.2, 24.1). Only its
  NE tables were read (its exports, module flags); nothing of its code.
- The card: Creative's *Sound Blaster Series Hardware Programming Guide*
  as a statement of hardware facts (the DSP's ports and commands, DSP
  4.xx's 16-bit and stereo auto-initialised transfers, the SB16 mixer's
  registers including 80h/81h/82h, the MPU-401 in UART mode, the BLASTER
  setting); the 8237 DMA controller's and the 8259 PIC's register maps;
  the MIDI 1.0 specification (message lengths, running status, All Notes
  Off). No driver's code was read or copied; Creative's own SB16 Windows
  drivers do not start under DOSBox-X and were not used at all.

## Interface

Exports (ordinals as SNDBLST2's for the five it has): DriverProc 1,
wodMessage 2, widMessage 3, modMessage 4, midMessage 5, auxMessage 6, WEP
7. Description `wave,midi,aux:w16 Sound Blaster 16`. Imports MMSYSTEM's
DriverCallback, StackEnter, StackLeave and timeGetTime.

| Device | Count | What |
|---|---|---|
| wave out 0 | 1 with a DSP 4.xx | 8/16 bits, mono/stereo, 5000-44100 Hz PCM; volume (mixer 32h/33h) |
| wave in 0 | 1 | the same formats |
| MIDI out 0 | 1 with an MPU-401 | the MPU-401 in UART mode (`MOD_MIDIPORT`) |
| MIDI in 0 | 1 | the MPU-401's input, time-stamped |
| aux 0-4 | 5 | master, wave, MIDI (FM), CD, line in (mixer 30h-39h) |

DriverProc: message 0 answers 0, DRV_LOAD 1, DRV_ENABLE 0 (SNDBLST2's
answer; the card is found and taken there), DRV_OPEN, DRV_CLOSE,
DRV_DISABLE, DRV_FREE 1, DRV_QUERYCONFIGURE 0 (no dialog), DRV_INSTALL
DRVCNF_RESTART, DRV_REMOVE DRVCNF_OK, the rest DefDriverProc. DRVM_INIT
answers 8 on every entry, as SNDBLST2's.

Configuration: SYSTEM.INI `[sb16.drv]` port (hex), int, dma, hdma, mpuport
(hex); else the BLASTER setting's A, I, D, H, P; else, for the IRQ and
DMA channels, the SB16 mixer's registers 80h and 81h; else 220h, 5, 1, 5,
330h. The DSP is reset and its version read (E1h); below 4.00 there are
no wave or aux devices. The MPU-401 is reset (FFh, twice) and put in UART
mode (3Fh); no acknowledgement, no MIDI devices.

## How it plays

One DMA buffer of 8 KB in conventional memory (GlobalDOSAlloc, the half of
a 16 KB block that does not cross a 64 KB boundary), auto-initialised, in
two halves of about 1/16 s (256-4096 bytes); the DSP interrupts after
each half (DSP 4.xx: B6h/C6h output, BEh/CEh input, 41h/42h the rate),
and the half just played is filled from the queued headers (silence
after the last one) while the other plays. A header is done when the
card has played past its last byte; loops (WHDR_BEGINLOOP/ENDLOOP,
dwLoops plays) go back to the loop's first header when its last is
copied; the card is stopped (DSP reset) after a half of silence with
nothing queued. Pause and restart are the DSP's D0h/D5h and D4h/D6h; the
position adds the DMA controller's count within the half under way.

The interrupt handler (sb16a.asm) reads the mixer's register 82h,
acknowledges the 8-bit (base+0Eh) or 16-bit (base+0Fh) interrupt, takes
the MPU-401's input bytes, sends the end of interrupt, then with
interrupts enabled and never reentered calls MMSYSTEM's StackEnter, the
C part (filling, emptying, the done headers' callbacks through
DriverCallback) and StackLeave: callbacks at interrupt time run on
MMSYSTEM's interrupt stack, those made inside a call (open, close,
reset, a MIDI long message) on the caller's, as SNDBLST2's.

## Verified

- MMP's device sections (`tests/mmsystem/mmdev.c`) over SB16.DRV in
  DOSBox-X: Microsoft's MMSYSTEM and w16's give identical logs, every
  message and callback (`tests/mmsystem/mdscen.sh`, sound `sb16w`).
- The same sections against SNDBLST2.DRV's under Microsoft's MMSYSTEM:
  identical but the expected differences (`tests/sb16/sbcmp.py`: the
  capabilities, the formats the SB16 takes, volume, the aux devices,
  TIME_MS kept, the driver's private header fields).
- The sound: eight WAV files (8/16 bits, mono/stereo, 8000-44100 Hz, four
  headers each, one looped three times, one at a lower wave volume)
  recorded in DOSBox-X correlate at 0.997 or better with the files, at
  the right length and level; seven notes through the MPU-401 and a MIDI
  file through MCISEQ come out at their pitch (`tests/sb16/sbaudio.sh`).
- Sound Recorder, Control Panel's Sound and Media Player over SB16.DRV:
  every screen as over SNDBLST2 (`tests/sb16/sbscen.sh prog`).

## Not done, not verified

- No OPL3 FM synthesiser device (it would need a patch set of w16's own:
  docs/notes/sb16-handover.md); the MIDI aux device sets the FM's volume only.
- 386 Enhanced mode: the driver offers no devices there. It would need a VxD that virtualises the
  card's ports, the DMA channels (VDMAD: the buffer's physical address)
  and the IRQ for the system VM, and the DMA buffer page-locked; without
  one the driver is not safe there.
- MIDI input with real data: DOSBox's MPU-401 has no input source, so
  only the buffer handling (open, add, start, stop, reset, close) is
  verified; the parsing (running status, system exclusive, real time) is
  not. The MPU-401's interrupt is taken on the card's IRQ (as on a real
  SB16); DOSBox raises the MPU's on IRQ 9, which is not hooked.
- Wave input records what DOSBox-X gives (silence); the data path is the
  output's in reverse but no real signal was recorded.
- Full duplex: one direction at a time (the second open answers 4).
- A configuration dialog (DRV_CONFIGURE).
