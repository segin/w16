# Testing w16

Everything below can be run by anyone with a clone of this repository and
their own copy of Windows 3.1. The project's agents run the same commands,
normally on a remote build host (see "Running on a remote host"); nothing
here depends on that host.

## 1. What you need

**Packages** (Arch Linux names; other distributions have equivalents):

| Package | Used for |
|---|---|
| `openwatcom-v2` (AUR; installed in `/opt/watcom`, or set `WATCOM`) | the C compiler, linker and resource compiler |
| `nasm` | the assembly sources |
| `python`, `python-pillow` | build tools, image comparisons |
| `noto-fonts` | the boot and About-box logos (`tools/mklogo.py`) |
| `dosbox` | most tests |
| `dosbox-x` (AUR) | WIN.COM, WINOLDAP, 286 and 386 Enhanced tests |
| `xorg-server-xvfb`, `xdotool`, `imagemagick` | headless screens, real keyboard and mouse input, pixel diffs |
| `mtools` | hard disk images (MS-DOS 4.00 boots, `tests/86box.sh`) |
| `86box`, `86box-roms` (AUR; optional) | the hardware-coprocessor test (`tests/86box-n4.sh`, section 3) |

On Arch: `yay -S openwatcom-v2 nasm python python-pillow noto-fonts dosbox dosbox-x xorg-server-xvfb xdotool imagemagick`.

Note that Wine also installs a program called `wrc`. The Makefile puts
OpenWatcom's `binl64` first on `PATH`, so this doesn't matter.

**The HDPMI16 DPMI host** (for `make test`): `tools/fetch-hdpmi.sh` downloads
the HX runtime into `.tools/hx/` and checks its checksum.

**A Windows 3.1 reference install** (for the boot tests and everything that
compares w16 with Windows 3.1). w16 cannot ship Microsoft's files: you supply
your own, and `.tools/` is git-ignored so they are never committed.

- `.tools/ref/win31/cdrive/` must be the C: drive of a Windows 3.1
  installation, with `WINDOWS/` and `WINDOWS/SYSTEM/` as Setup leaves them,
  configured for VGA, a PS/2 mouse and the US keyboard (the
  `SYSTEM.INI` the tests expect has `display.drv=vga.drv`,
  `mouse.drv=mouse.drv`, `keyboard.drv=keyboard.drv`, `sound.drv=mmsound.drv`).
  Install it in DOSBox from your own disks or images, then copy the drive's
  contents there.
- Optional, for some probes: `.tools/ref/w386/cdrive/` and
  `.tools/ref/wfw311a/cdrive/` (Windows for Workgroups 3.11). Tests that need
  them say so.
- Optional, for `tests/soundprobe.sh sb16`: `.tools/sb16/src/`, the
  Sound Blaster 16 ASP driver disk images (`Sound_Blaster_16ASP_(Disk_1).img`,
  `(Disk_2).img`); `tests/sb16setup.sh` installs them into a scratch copy
  of the reference install (its header says what Creative's installer
  changes).
- Optional, for work on 32-bit (Win32s) programs: `.tools/ref/win31s/cdrive/`
  and `.tools/ref/wfw311s/cdrive/`, the Windows 3.1 and WfW 3.11
  references with Microsoft Win32s 1.30c installed by its own Setup
  (`tests/dropin.sh` with `DROPIN_REF=$PWD/.tools/ref/win31s/cdrive` boots
  w16's parts on it). Win32s runs in 386 Enhanced mode only (`WIN /3`). To
  make them, put the Win32s disk images (`disk1.img`, `disk2.img`,
  `disk3.img`, 1.44 MB) anywhere below `.tools/win32s/src/` and run, in a
  directory of your choice,

  ```sh
  tests/win32ssetup.sh /tmp/w32s-31 win31    # from .tools/ref/win31/cdrive
  tests/win32ssetup.sh /tmp/w32s-wfw wfw311  # from .tools/ref/wfw311a/cdrive
  mkdir -p .tools/ref/win31s .tools/ref/wfw311s
  cp -a /tmp/w32s-31/c .tools/ref/win31s/cdrive
  cp -a /tmp/w32s-wfw/c .tools/ref/wfw311s/cdrive
  ```

  It runs Setup in DOSBox-X on a virtual display (about 10 minutes),
  swapping the disk images when Setup asks, then runs a 32-bit test
  program (`tests/win32s/w32t.c`, built with OpenWatcom's `wcc386`) and
  fails unless it ran. `OUTDIR/install.log` lists what Setup installed and
  changed and the test program's output; `OUTDIR/shot-*.png` show each
  step (the project's copies keep them beside `cdrive`, in `setup/`).
  The script's header says what Setup asks and what it installs.
- Optional: `.tools/docs/` (reference books as text, used by the
  documentation, not by tests).

The tests never modify `.tools/ref`: every run works in a scratch copy.

## 2. Building

```sh
make            # everything: build/dos (the w16 modules), fonts, the install tree build/w16
make win386     # w16's WIN386.EXE (386 Enhanced mode)
```

## 3. Running tests locally

All tests are headless: they unset `DISPLAY` and run DOSBox on a virtual X
display, so they never open windows on your desktop.

| Command | What it does | Time |
|---|---|---|
| `make test` | the default group files against their definitions and w16's icons (`grpcheck`; with a reference install also `tests/grpref.py`, `tools/mkgrp.py` against Setup's group files), `tools/castcheck.py`, then the probe programs under w16's KERNEL, on HDPMI16 and on w16's own DOS extender (`tests/run-tests.sh`); with a reference install also the WIN.COM scenarios (`tests/wincom.sh`) and the font checks (`tests/fonts.sh -q`); all of it in parallel | ~12 s on a 20-core, 40-thread server; ~1 min at 8 threads; ~2 min one at a time |
| `make boot-test` | boots real Windows 3.1 on w16's parts and compares every screen with the same boot on Microsoft's, pixel for pixel (`tests/boottest.sh`) | ~15 min at 3 jobs on a desktop; ~3.5 min at 12 or more jobs on a 20-core, 40-thread server, with `make test` beside it (`tools/remote-test.sh`) |
| `tests/boottest.sh CASE ...` | only those boot-test cases (names and descriptions at the top of `tests/boottest.sh`) | |
| `RUNTESTS_DPMI=dosx tests/run-tests.sh` | the probe programs on w16's own DOS extender only (`hdpmi` only HDPMI16; `make test` uses `"hdpmi dosx"`) | |

`make test` settings: `RUNTESTS_KERNEL=KRNL286` or `KRNL386` runs the
probes under one of w16's two kernels only (default both),
`RUNTESTS_JOBS=N` probes at once (default: the
machine's threads, at most 16), `WINCOM_JOBS=N` WIN.COM scenarios at once
(default: half the threads, at most 12); `1` runs one at a time. Every probe
runs in a scratch copy of its directory, so runs never share files.

Boot-test settings:

- `BOOTTEST_JOBS=N`: cases run at once (default 3). On a busy machine use fewer; timing-sensitive cases can fail under heavy load. Cases made of independent sessions run as parts that are scheduled separately (`allw16nolock@1` ... `@6`, `pmdrop@pm`, `winfile@cmp`, `kbd@kbtype`, ...; a part can be run by name) and are reported as one line; the longest cases of the last run start first.
- `INPUTRUN_KEYDELAY=MS`: time between keys typed into DOSBox (default 50; with xdotool's 12 a loaded DOSBox lost keys).
- `INPUTRUN_HDD=1`: `tests/inputrun.sh` boots the scratch drive as a hard disk image with MS-DOS 4.00 (`.tools/msdos`, `tools/msdos-build.sh`) and the install's HIMEM.SYS, in DOSBox-X (dropin's `-p 386`); needed for 386 Enhanced mode DOS boxes, which do not run on DOSBox's own DOS. DOSBox-X loses typed letters at the default delay: `INPUTRUN_KEYDELAY=150`.
- `BOOTTEST_REFRESH=1`: rerun the Windows 3.1 reference boots instead of using the cache in `build/refcache/` (the cache is keyed on everything a reference depends on, so you rarely need this).
- Results go to `build/boottest-HHMMSS/`: for a failing case, `CASE-*-diff.png` shows the differing pixels, and the case's `-ref` and `-w16` directories hold both sides' screens and logs. Retries are logged to `build/boottest-retries.log`. A test that fails only sometimes is a bug, not noise: report it.

Other test scripts, each documented at its top: `tests/npscenarios.sh`,
`tests/wfscenarios.sh` and `tests/wfhelp.sh` (w16's File Manager against
Microsoft's; `tests/wfobs.sh` for observing the original; scripts with
`; fmx:` load w16's File Manager extension probe FMX.DLL, `tests/apps/fmx.c`),
`tests/cplscenarios.sh` (w16's Control Panel and applets against Microsoft's:
CONTROL.INI, WIN.INI, SYSTEM.INI byte for byte and every screenshot within
`tests/scripts/cpl.baseline`; `fn*` Fonts, `pt*` Ports, `en*` 386 Enhanced
in `WIN /3`, `en*w` with w16's WIN386 as well),
`tests/rescenarios.sh` and `tests/rehelp.sh` (w16's Registration Info Editor against Microsoft's: screens, REG.DAT, WIN.INI and saved .REG files; its New Style scenarios NS* against w16's approved screens in `tests/scripts/regedit/ns-ref/`, `RE_NSREF=update` to approve; `tests/regobs.sh` for observing the original, `REGOBS_TRACE=1` logging its SHELL calls through XHELL.DLL),
`tests/whscenarios.sh` (w16's Windows Help against Microsoft's over the
reference install's own .HLP files: screens within
`tests/scripts/winhelp.baseline`, WIN.INI and WINHELP.BMK byte for byte,
`WH_BASELINE=update` to rewrite; `tests/whtree.sh OUTDIR [286|386]`:
on w16's install tree alone, with three of the reference install's help
files as input; `tests/whobs.sh` for observing the original; `make build/hlpdump` builds the .HLP reader for the host,
`tests/winhelp/hlpdump.c`),
`tests/pmscenarios.sh`, `tests/calcscenarios.sh`, `tests/clockscenarios.sh`,
`tests/ssscenarios.sh` (w16's SSSTARS.SCR against Microsoft's: dialogs,
INI files, the running saver, USER starting it; `tests/sstree.sh`: on
w16's install tree alone; the boot-test case `scrnsave`),
`tests/winoldap.sh`, `tests/swap.sh` (w16's WSWAP/DSWAP against Microsoft's),
`tests/grabber.sh` (w16's VGACOLOR.2GR against Microsoft's),
`tests/grabber3cmp.sh` (w16's VGA.3GR against Microsoft's, 386 Enhanced mode),
`tests/lzexpand/lzscen.sh` (w16's LZEXPAND.DLL against Microsoft's, the
probe's log section by section; the boot-test case `lzexpand` runs it),
`tests/ver/vscen.sh` (w16's VER.DLL against Microsoft's, the probe's log
section by section; `tests/ver/vtree.sh`, `vmods.sh` and `vfonts.sh` for
the install tree, w16's modules' versions and Control Panel's Fonts
applet; the boot-test case `ver` runs them all),
`tests/toolhelp/thscen.sh` (w16's TOOLHELP.DLL against Microsoft's: the
probe THP in groups of sections on 3.1 under both kernels and in 386
Enhanced mode and on w16's tree, the logs compared by
`tests/toolhelp/thcmp.py`; `thwatson.sh` Dr. Watson with each; the
boot-test case `toolhelp` runs them through `tests/toolhelp/thcase.sh`),
`tests/drv/drun.sh` (USER's installable drivers: the probe DRP and its
driver DRT on w16's USER against Microsoft's; the boot-test case
`drivers`), `tests/timer/tscen.sh [/S|/3]` (w16's TIMER.DRV against
Microsoft's under Microsoft's MMSYSTEM: the probe TPR's log, plain and
with the logging driver TMS.DRV between them, compared by
`tests/timer/tcmp.py`; one run alone: `tests/timer/trun.sh`; the
boot-test case `timer`), `tests/mmsystem/mscen.sh [core|mmio|timer]`
(w16's MMSYSTEM.DLL against Microsoft's on the reference 3.1: the probe
MMP's core and MMIO sections compared by `tests/mmsystem/mcmp.py`, and
TPR with w16's MMSYSTEM and TIMER.DRV against Microsoft's two; one run
alone: `tests/mmsystem/mrun.sh OUT ms|w16` with `MRUN_ARG` the sections;
the boot-test case `mmsystem`, whose part `qt` plays QuickTime's movie
with w16's MMSYSTEM, MMTASK and TIMER.DRV),
`tests/mmsystem/mdscen.sh [-m] [NAME:SOUND:SHIM:SECTIONS...]` (layer 3:
MMP's device sections over Windows 3.1's own Sound Blaster and MIDI
Mapper drivers, expanded from the reference's Setup files, with the
logging drivers MDS*.DRV between them and MMSYSTEM, both MMSYSTEMs
compared; `-m` Microsoft's only; `mrun.sh`'s `MRUN_SND` and `MRUN_SHIM`
for one run; part `dev` of the boot-test case `mmsystem`; the probes that
compare against 3.1's timer must not trust timeGetTime as an unsigned
difference: Microsoft's TIMER.DRV often steps 55 ms back for one 55 ms
period, which made `dev` and `timer` fail now and then until
2026-11-19, ARCHITECTURE.md 21.2 and 21.4),
`tests/mmsystem/mprog.sh OUT ms|w16 soundrec|sound|midimap|mplayer [-e REFDIR]`
(Sound Recorder, Control Panel's Sound and MIDI Mapper applets and Media
Player with real keys over the same drivers; part `prog`),
`tests/mmsystem/mciscen.sh [-m] [NAME:SCRIPT:KIND...]` (layer 4, MCI:
MMP's MCI scripts, `tests/mmsystem/mci/*.txt`, over the test MCI driver
MCT.DRV and over 3.1's MCIWAVE and MCISEQ behind the logging drivers
MDSC*.DRV, both MMSYSTEMs compared; with `$W16_QTDIR/ref-installed/c`, a
QuickTime-installed drive, also QuickTime's MCIQTW.DRV; one run alone:
`tests/mmsystem/mcirun.sh OUT ms|w16 SCRIPT [plain|shim|qt]`; part
`mci`), `tests/qtplay.sh` (QuickTime for Windows 2.1.2 installed and
its sample movie played on w16's tree laid over 3.1, against stock 3.1,
then the player closed, reopened, its movie's window dragged while it
plays, and ended with File > Exit and Alt+F4 and Windows ended (the close
sequence); `-T 286` or `-T 386`: on w16's
install tree alone as C:\W16 instead, under its KRNL286 or KRNL386,
where the tree's SB16.DRV gives QuickTime a sound device and the stock
3.1 run gets Windows 3.1's SNDBLST2.DRV to match; w16 runs with
`[w16] hangwatch=120` (`$QTPLAY_HANGWATCH`), and prints KERNEL's longest
stretch without progress (the installer's unpacking: 15-21 s idle,
43-46 s at load average 55);
the installer, Apple's, comes from `$W16_QTDIR`, default
`~/Downloads/qtw16`, which must hold `QTINSTAL.EXE`; skipped without it;
`-m FILE` puts Microsoft's file back for bisection, `-t FILE` lays only that file of the tree over 3.1; the boot-test case
`quicktime`, parts `over`, `tree` and `tree386`),
`tests/sb16/sbscen.sh [dev] [audio] [prog]` (w16's SB16.DRV in DOSBox-X
as a Sound Blaster 16: MMP's device sections over it under both
MMSYSTEMs and against Windows 3.1's SNDBLST2.DRV, `tests/sb16/sbcmp.py`;
`tests/sb16/sbaudio.sh`: the probe SBP.EXE plays WAV files of every
format, MIDI notes and a MIDI file with the sound recorded, checked by
`tests/sb16/sbaudio.py` against the material; the MIDI goes through
DOSBox-X's own synthesiser, `mididevice=synth`, which needs a DOSBox-X
built with FluidSynth, and a sine SoundFont from `tools/mksf2.py`; Sound
Recorder, Sound and Media Player over it; the boot-test case `sb16`),
`tests/mouseprobe.sh`, `tests/sysprobe.sh`, `tests/soundprobe.sh`, `tests/fonts.sh`,
`tests/dosxtest.sh`, `tests/win386.sh`, `tests/us27run.sh` (probe US27 on
Windows 3.1, booting again past the cases that end the task; makes the
logs `tools/us27ref.py` and `tools/obsref.py` turn into
`tests/apps/us27ref.h`).

**A hardware coprocessor (optional): 86Box.** DOSBox's and DOSBox-X's
coprocessors raise no exceptions (no IRQ 13), so what WIN87EM does with
an unmasked exception on a real coprocessor shows only on an emulator
that wires the 387 as an AT does. `tests/86box.sh PROGRAM.EXE [FILE...]`
runs a probe under the reference Windows 3.1 on 86Box: a 386DX with a 387
(the AMI BIOS of "asus386", its CMOS written by `tools/atcmos.py` so it
boots without stopping in setup), MS-DOS 4.00 from `.tools/msdos` with
HIMEM.SYS, the probe started from WIN.INI's `run=`, all headless on its
own Xvfb display; its TK.LOG is read back from the disk image with
mtools and printed. `B86_WIN87EM=build/dos/WIN87EM.DLL` puts w16's
WIN87EM in place of Microsoft's, `B86_MODE=/3` runs 386 Enhanced mode,
`B86_SHOT=DIR` saves a screenshot every 10 seconds (the rest at the top
of the script). `tests/86box-n4.sh` runs TK67P13 (`__fpMath 7`, FISTP and
a zero divide with exceptions unmasked) with both WIN87EMs in both modes
and compares the logs (about two minutes). Needs 86Box and its ROMs
(Arch: the AUR packages `86box` and `86box-roms`), Xvfb, mtools. Not part
of `make test`. 86Box's 387 is not exact either: an out-of-range FISTP
with invalid unmasked stores the integer indefinite, sets no flag and
does not pop (Intel: nothing stored, the exception raised); a zero divide
does raise IRQ 13 (ARCHITECTURE.md 9.2).

Run the boot test from a copy if you will edit the scripts while it runs:
`cp tests/boottest.sh build/boottest-snapshot.sh && sh build/boottest-snapshot.sh`.

## 4. Trying w16 interactively

`tests/dropin.sh` prepares a scratch C: drive (a copy of the reference
install with w16's files dropped in) and can boot it headless; `-o DIR`
keeps the drive so you can boot it yourself in DOSBox. For example, every
w16 part in Standard mode:

```sh
B=$PWD/build/dos F=$PWD/build/fonts
tests/dropin.sh -w -x \
  -f $B/USER.EXE=WINDOWS/SYSTEM/USER.EXE -f $B/GDI.EXE=WINDOWS/SYSTEM/GDI.EXE \
  -f $B/SHELL.DLL=WINDOWS/SYSTEM/SHELL.DLL -f $B/PROGMAN.EXE=WINDOWS/PROGMAN.EXE \
  -f $B/NOTEPAD.EXE=WINDOWS/NOTEPAD.EXE -f $B/CLOCK.EXE=WINDOWS/CLOCK.EXE \
  -f $B/CALC.EXE=WINDOWS/CALC.EXE -f $B/WINOLDAP.MOD=WINDOWS/SYSTEM/WINOLDAP.MOD \
  -f $B/WIN87EM.DLL=WINDOWS/SYSTEM/WIN87EM.DLL -f $B/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV \
  -f $F/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON -f $F/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON \
  -f $F/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON -f $F/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON \
  -i w16:watchdog=0 -i w16:exitmsg=0 -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP' \
  -c ver -t 3 -o build/try
```

then boot it with a DOSBox configuration of your own (`memsize=16`,
`cputype=386`, `cycles=max`; `mount c build/try/c`, `cd \WINDOWS`,
`SET TEMP=C:\WINDOWS\TEMP`, `WIN /S`). `-w` is w16's WIN.COM, `-x` w16's
DOS extender; leave out a `-f` to use Microsoft's file instead; `-r` uses
Microsoft's KERNEL. `[w16] watchdog=0` stops w16's KERNEL from ending a
boot that seems stuck (it is on in tests). `tests/dropin.sh` with no
`-o` prints its full option list.

386 Enhanced mode (`WIN /3`) runs KRNL386.EXE: Microsoft's, or w16's with
`tests/dropin.sh -3 -c 'WIN /3 /B'` (ARCHITECTURE.md 9.10); `tests/win386.sh` shows the
setup. w16's WIN386 writes its trace to COM1 (`serial1=file file:com1.txt`).
Real mouse motion does not reach the guest in DOSBox-X, so the boot-test
case `win386input` (mouse, keys, Exit Windows) runs in DOSBox through
`tests/inputrun.sh` (`-c "WIN /3 /B"`, with KRNL386.EXE put back by `-f`:
see the case in `tests/boottest.sh`). `tests/vxpprobe.sh ms|w16` loads the
probe VxD VXP.386 (`make vxprobes`) and prints the control messages and
service results it saw (ARCHITECTURE.md 12.7).

## 5. Running on a remote host

`tools/remote-test.sh` runs the same suites on another Linux machine you
control, which helps when your own is busy or small:

```sh
W16_REMOTE_HOST=myhost tools/remote-test.sh            # build + make test + full boot test
W16_REMOTE_HOST=myhost tools/remote-test.sh -b write   # one boot-test case
tools/remote-test.sh -t                                 # make test only
tools/remote-test.sh -b -e DROPIN_INI=w16:ExtendedUserHeap=1 user write   # with a setting
```

- The host needs the packages from section 1, passwordless `ssh`, and its own
  copy of the reference install at `$W16_REMOTE_TOOLS` (default
  `/mnt/build/w16/.tools`). It holds Microsoft's files, so it must be private:
  never a shared or public CI runner.
- Each checkout or worktree gets its own directory there,
  `$W16_REMOTE_DIR/NAME` (default `/mnt/build/w16-runs/NAME`); the reference
  cache is shared between them.
- `-j N` sets the boot test's jobs (default: a third of the host's threads,
  3 to 16; 13 on installation-05); at most `W16_REMOTE_SLOTS` runs (default 3)
  use the host at once. With both suites asked for, `make test` runs beside
  the boot test (at 8 probes at once) instead of before it.
- The summaries and the failing cases' diffs come back to
  `build/remote-NAME/`; the host keeps its last 3 result directories.

The project's own runs use `installation-05`, the script's default host.

## 6. What a passing run looks like

- `make test` ends with exit status 0; the log has no `FAIL` lines.
- `make boot-test` ends with `boottest: 0 failure(s)`. Cases marked `xfail`
  are known, expected failures (each states why).
- After every merge into master, both are run; see ARCHITECTURE.md §9.2 for
  how the tests are built and what each kind of check means.
