# SB16.DRV handover (w16's Sound Blaster 16 driver)

Task: w16's own Sound Blaster 16 driver for Windows 3.1's MMSYSTEM: wave
out and in, MIDI out and in through the MPU-401, aux volumes through the
mixer, Standard mode, behaving toward MMSYSTEM as Windows 3.1's own
SNDBLST2.DRV wherever the two overlap. ARCHITECTURE.md 24 has the facts
and the results; `drivers/sb16/README.md` the files and the sources. This
note is the working state, so that another agent can take over.

Rules followed: SNDBLST2.DRV observed from outside only (its NE tables,
`tools/neinfo.py`; the logging drivers of tests/mmsystem between it and
Microsoft's MMSYSTEM); nothing disassembled; no driver's code read or
copied (Linux, Wine and DOSBox sources not used at all). The card's
facts from Creative's Hardware Programming Guide as a statement of
hardware facts (not in .tools/docs: from the agent's knowledge of the
published guide; every one of them exercised against DOSBox-X's SB16).
All DOSBox work on installation-05, run name `w16-sb16`
(`tools/remote-run.sh -n w16-sb16`, `tools/remote-test.sh -n w16-sb16`).

## Status (2026-11-19)

- [x] The driver: DriverProc, wodMessage, widMessage, modMessage,
  midMessage, auxMessage (drivers/sb16). Built by `make`, installed as
  `SYSTEM\SB16.DRV`, not in the default SYSTEM.INI; `assets/README.TXT`
  says how to enable it. `irqcheck.py --fixed` accepts it (in `make test`).
- [x] MMP's device sections over it under both MMSYSTEMs: identical.
- [x] Against SNDBLST2's under Microsoft's MMSYSTEM (`tests/sb16/sbcmp.py`):
  only the expected differences.
- [x] The sound recorded and checked (`tests/sb16/sbaudio.sh`).
- [x] Sound Recorder, Sound, Media Player (`tests/sb16/sbscen.sh prog`).
- [x] Boot-test case `sb16` (parts dev, audio, prog).
- [ ] FM (OPL3) device: not done (below).
- [ ] 386 Enhanced mode: not done (no devices there; what it needs is in
  ARCHITECTURE.md 24.5).
- [ ] DRV_CONFIGURE dialog: not done (DRV_QUERYCONFIGURE answers 0).
- [ ] QuickTime's sound over SB16.DRV: not tried.

## How to run

- One scenario: `tools/remote-run.sh -n w16-sb16 -g build/X
  "tests/mmsystem/mdscen.sh -o build/X sbw:sb16w:1:w"` (MMP section w over
  SB16.DRV, both MMSYSTEMs, compared); `-m` for Microsoft's only.
- SNDBLST2's reference logs: `mdscen.sh -m sb2w:sb2:1:w` and so on;
  compare with `python3 tests/sb16/sbcmp.py SB2LOG SB16LOG`.
- The sound: `tools/remote-run.sh -n w16-sb16 -g build/X
  "tests/sb16/sbaudio.sh -o build/X [ms|w16]"`; the recording is
  `build/X/SIDE.cap/*.wav`, the analysis `SIDE.cmp`, SBP's log `SIDE.log`.
- Everything: `tools/remote-test.sh -n w16-sb16 -b sb16`.

## Findings worth knowing

- DOSBox-X's `mididevice=fluidsynth` needs a host audio driver and fails
  headless ("Can't create audio driver"); `mididevice=synth` with
  `midiconfig=FILE.sf2` renders into DOSBox-X's own mixer and is what the
  audio test uses. No SoundFont is installed on the host: tools/mksf2.py
  writes one (plain sine waves, every program), so pitches are exact.
- DX-CAPTURE /M writes a multi-track AVI, not WAVs: the test uses /A and
  plays wave and MIDI at different times.
- DOSBox-X records silence on the SB16's input (0x80 / 0x0000).
- MMSYSTEM's start-up sound (TADA.WAV) plays at boot under both
  MMSYSTEMs; Microsoft's also plays SystemExit (CHIMES.WAV) when Windows
  ends with SBP as the shell, w16's does not (an MMSYSTEM matter, left).
- MCISEQ's first note comes out 50-100 ms short under either MMSYSTEM.
- Sound Recorder enables Record for DING.WAV over SB16.DRV (22050 Hz
  input), not over SNDBLST2 (11025 Hz only): the prog part masks that
  button when it compares with SNDBLST2's screens.
- SNDBLST2's DRV_ENABLE answers 0 although it works; SB16.DRV does the
  same.

## Left, and where to start

- FM: an OPL3 MIDI synthesiser device (modMessage device 1,
  MOD_FMSYNTH): voice allocation over 18 two-operator (or 6 four-operator)
  voices, a patch set of w16's own (128 melodic + drums: operator
  parameters to be designed and checked by ear/spectrum, not taken from
  any existing bank), pitch bend, volume through mixer 34h/35h. The audio
  test could check it with `DX-CAPTURE /O` (DRO) or the recording.
- 386 Enhanced mode: a VxD (VSBD-like) or reliance on VDMAD/VPICD
  defaults; then remove the WF_ENHANCED check in sb16.c's enable().
- MIDI input: needs a MIDI input source in the emulator, or a test on real
  hardware.
- A configuration dialog for DRV_CONFIGURE/DRV_INSTALL (port, IRQ, DMA,
  HDMA, MPU port), writing `[sb16.drv]`.
- The MIDI Mapper: a setup for "Sound Blaster 16 MIDI", so that Media
  Player plays .MID through it without "set port".
