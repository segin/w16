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
| `p7zip` (optional) | CD images (`tests/staroff.sh`) |
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

Other test scripts, each documented at its top: `tests/gd16.sh` (the font-substitution probe GD16 as the shell, `-r` on Microsoft's GDI, on the reference install with w16's Liberation fonts and `[FontSubstitutes]` lines, `-m` keeping Microsoft's Arial, Times New Roman and Courier New; boot-test case `gdisubst`), `tests/gd13.sh` (the TrueType probe GD13 as the shell, `-r` on Microsoft's GDI, with the free test font installed through CreateScalableFontResource; `tools/gd13.py REF LOG` compares two logs, `tools/gd11.py tt REF LOG` GD11's TrueType answers), `tests/ttview.sh` (TTVIEW.EXE, the TrueType viewer that rasterises with w16's fixed-point `gdi/ttf/ttfx.c`, on Windows/386 2.11, 3.1 Standard and 386 Enhanced, w16 and an emulated 80286: its page, hinted by w16's autohinter `gdi/ttf/ttfhint.c` and unhinted, against the host's drawing bit for bit; `-2` the 8-24 pixel specimen, `-u` shows the unhinted page, `-g` the specimen as 3.1 draws it with its own rasteriser; `tests/ttf/pg2png.py` makes images of the pages; `make test` runs `tests/ttf/ttfxtest.c`, the rasteriser against floating-point stb_truetype and the hinted bitmaps against the scan-conversion rules on the hinted outline), `tests/npscenarios.sh` (`NP_W16FILES="$PWD/build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE"` runs w16's Notepad on w16's edit control too), `tests/npqes.sh` (Notepad when Windows ends: whole-screen captures of its system-modal boxes),
`tests/wfscenarios.sh` and `tests/wfhelp.sh` (w16's File Manager against
Microsoft's; `tests/wfobs.sh` for observing the original; scripts with
`; fmx:` load w16's File Manager extension probe FMX.DLL, `tests/apps/fmx.c`),
`tests/cplscenarios.sh` (w16's Control Panel and applets against Microsoft's:
CONTROL.INI, WIN.INI, SYSTEM.INI byte for byte and every screenshot within
`tests/scripts/cpl.baseline`; `fn*` Fonts, `pt*` Ports, `en*` 386 Enhanced
in `WIN /3`, `en*w` with w16's WIN386 as well; C:\CMP.LOG compared too:
broadcasts in `wnchange`, blink counts in `dkblk*`, keyboard SetSpeed
calls in `kbspeed*`; `;! survive` scenarios where 3.1's MAIN.CPL dies),
`tests/rescenarios.sh` and `tests/rehelp.sh` (w16's Registration Info Editor against Microsoft's: screens, REG.DAT, WIN.INI and saved .REG files; its New Style scenarios NS* against w16's approved screens in `tests/scripts/regedit/ns-ref/`, `RE_NSREF=update` to approve; `tests/regobs.sh` for observing the original, `REGOBS_TRACE=1` logging its SHELL calls through XHELL.DLL),
`tests/pifscenarios.sh` and `tests/pifhelp.sh` (w16's PIF Editor against
Microsoft's: screens within `tests/scripts/pifedit.baseline`, every saved
.PIF and WIN.INI byte for byte, `PIF_BASELINE=update` to rewrite; the
WinHelp requests; `tests/pifobs.sh` for observing the original, `; mode:
/3` for 386 Enhanced mode, `; var:` for variants of the reference
install's PIFs made at test time),
`tests/whscenarios.sh` (w16's Windows Help against Microsoft's over the
reference install's own .HLP files: screens within
`tests/scripts/winhelp.baseline`, WIN.INI and WINHELP.BMK byte for byte,
`WH_BASELINE=update` to rewrite; scenarios with `; hlp: w16test` build
w16's own test help file with OpenWatcom's `whc` and those with `; owhlp:`
use OpenWatcom's own help files, both skipped without OpenWatcom;
`make test` runs `tests/winhelp/hlpcheck.sh`, the reader over every help
file at hand; `tests/whtree.sh OUTDIR [286|386]`:
on w16's install tree alone, with three of the reference install's help
files as input; `tests/whobs.sh` for observing the original; `make build/hlpdump` builds the .HLP reader for the host,
`tests/winhelp/hlpdump.c`),
`tests/pmscenarios.sh` (`;ini SECTION|KEY|VALUE` and `;grp NAME` lines set
up PROGMAN.INI and `tools/mkbadgrp.py`'s group files),
`tests/ddescenario.sh` (Program Manager's DDE interface, TDDE scripts in
`tests/scripts/dde/`; the boot-test case `ddescen`), `tests/calcscenarios.sh`, `tests/clockscenarios.sh`,
`tests/ssscenarios.sh` (w16's SSSTARS.SCR against Microsoft's: dialogs,
INI files, the running saver, USER starting it; `tests/sstree.sh`: on
w16's install tree alone; the boot-test case `scrnsave`),
`tests/winoldap.sh`, `tests/swap.sh` (w16's WSWAP/DSWAP against Microsoft's),
`tests/pifread.sh -c -m std|386` (how w16's WINOLDAP and WINOA386 read
PIFs against Microsoft's: file reads, SHELL's block, the switcher's
record, what the program got; fixtures `tests/scripts/pifread/mkpifs.py`),
`tests/grabber.sh` (w16's VGACOLOR.2GR against Microsoft's),
`tests/grabber3cmp.sh` (w16's VGA.3GR against Microsoft's, 386 Enhanced mode),
`tests/installers.sh OUTDIR [NAME...]` (third-party Windows 3.x
installers on w16's install tree in 386 Enhanced mode, driven by keys
from `tests/scripts/installers/NAME.txt`, the files each installs
compared with the same run on Windows 3.1; the installers are never in
the repository: put each in its own directory under `$W16_INSTDIR`,
default `~/Downloads/w16-installers` (WEP1, WEP2, WEP3: each pack's disks
merged; BW/DISK1; W4/DISK1, W4/DISK2; JOHNNY; WP3/WP3V23B5.EXE;
MP52/MP52BETA.EXE), missing
ones are skipped; the boot-test case `installers`, one part per
installer; ARCHITECTURE.md 9.12; `[w16] int21log=1` logs every INT 21h
call and the traced imports into the debuglog),
`tests/win32sinst.sh [-o OUTDIR] [k386|tree]` (Microsoft Win32s 1.30c's
Setup from its disk images below `$W16_WIN32S_SRC`, default
`.tools/win32s/src`, laid out as DISK1-DISK3: on 3.1 with w16's KRNL386 it
must install and restart Windows, on w16's tree reach Setup's screen;
skipped without the images; the boot-test case `win32s`),
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
`tests/staroff.sh` (StarOffice 3.1's CD image, `$W16_STAROFF_ISO`, default
`~/Downloads/staroffice/STAROFF.ISO`, mounted as a CD-ROM drive D: with
`tests/dropin.sh`'s `DROPIN_CDROM`; Microsoft's OLE 2 libraries from it
(ZIP archives, read with `7z` and Python) under TK70 on 3.1 and on w16's
tree; skipped without the image; the boot-test case `staroff`; nothing of
it is committed; `DROPIN_COVER=DIR` lays an installed application's files
over the scratch C: drive for hand runs),
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
see the case in `tests/boottest.sh`). `tests/vsdprobe.sh ms|w16 [fg bg two hung cadd kbd ...]` runs the Virtual Sound Device's probes with the sound recorded (ARCHITECTURE.md 12.8 VSD). `tests/vdmadprobe.sh [-R] ms|w16` runs the Virtual DMA Device's: SBD.COM's Sound Blaster DMA and VDS calls in a box, the probe VxD VXM.386 (`-R`: VXMR, which reserves buffer space; `VDMADPROBE_INI` adds SYSTEM.INI settings), the sound recorded; `tools/vdmadcmp.py REF W16` compares two runs (the boot-test case vdmad). `tests/msvcvxd.sh [-o DIR] [vxc boot cvw]` runs the probe VxD VXC.386 (and VXCU.386, a missing VMM service) under both WIN386s, and, with Visual C++ 1.52 installed in `$W16_MSVC_DIR` (its C:\MSVC; skipped without), boots w16's tree with its four devices, starts CodeView for Windows, and runs WXSRVR.EXE (VMB.386's client) with Program Manager's menus opened, against Windows 3.1 (boot-test cases `win386vxc` and `msvc`, ARCHITECTURE.md 12.7). `tests/vxpprobe.sh ms|w16` loads the
probe VxD VXP.386 (`make vxprobes`) and prints the control messages and
service results it saw (ARCHITECTURE.md 12.7). The DPMI host's answers
are probed by VX11 (`tests/apps/vx11.c`, run as the shell with
`tests/refrun.sh VX11.EXE /3`, under w16's WIN386 with
`REFRUN_FILES=build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE`); the
boot-test case `win386audit` compares it with Windows 3.1's
(the audit's triage file, docs/notes/win386-audit-triage.md, is in git
history), and `win386hdd` does the same on
MS-DOS 4.00 (`INPUTRUN_HDD=1`), where DOS calls take interrupts while they
run (DOSBox's own DOS runs none of its INT 21h code in the VM). How much of the processor Windows
gets beside DOS boxes: `tests/vmpollrun.sh OUTDIR WHAT [w16]` (VX13 and
VX14, `make vxprobes`; WHAT is what each box runs: `cmd`, or a VX14 mode
such as `p` for a program polling the keyboard or `b` for a busy one)
prints Windows' busy-loop and message counts with 0, 1, 2 and 4 boxes;
`VMPOLL_VXT=1` adds the scheduling probe VxD VXT.386 (DOSBox-X, its log in
OUTDIR/com1.txt). The boot-test case `win386idle` runs it under w16's and
Microsoft's WIN386 (ARCHITECTURE.md 12.12). Where 386 Enhanced mode's
time goes: `tests/vx16run.sh OUTDIR CONF` runs VX16 (`make vxprobes`) as
WIN.INI run= and prints its counts by the CMOS clock (a busy loop, the
clocks, timer events, trapped I/O, reflections, file reads, drawing,
messages; text, fills, lines, scrolls, stretches, GetDC, a sorted list
box, typing in an edit control, SendMessage to another task, GlobalAlloc,
profile reads: how w16's own drivers and modules perform); CONF `ms`, `w16` or `std` (the reference install under
Microsoft's or w16's WIN386, or Standard mode), `tree`, `treems` or
`treestd` (w16's install tree the same ways); `VX16_ONLY` picks phases,
`DROPIN_CYCLES="fixed 30000"` counts instructions instead of host time
(ARCHITECTURE.md 12.14). KERNEL's idle loop: `tests/kidlerun.sh OUTDIR
CONF [BOX]` runs VX17 (`make vxprobes`) as WIN.INI run=; it counts the
idle calls a waiting Windows makes (protected-mode INT 28h and INT 2Fh
1689h, DPMI 0300h calls), a 55 ms timer's WM_TIMERs, a busy loop and
message round trips, with no DOS box and then one running BOX (a VX14
mode, default `b`), and in 386 Enhanced mode VXT.386 reports each VM's
run time, V86 INT 28h, Release_Time_Slice and the VMM's idle callbacks
per phase (DOSBox-X, `tools/kidlesum.py` sums them). CONF: `ms`, `msk`,
`w16`, `wk`, `tree` (386 Enhanced: Microsoft's or w16's KRNL386 under
Microsoft's or w16's WIN386, the tree), `std`, `std286`, `stdk`,
`stdk286`, `treestd`, `tree286` (Standard mode); `KIDLE_MS5=1` loads
MS5.COM first, which counts what reaches real mode; `KIDLE_PIF` sets the
box's PIF (e.g. `background=0`) (ARCHITECTURE.md 12.15).
The VM Task Manager (ARCHITECTURE.md 15.12): `tests/vmtaskrun.sh OUTDIR
CONF` runs VMT1 (`make vxprobes`) as WIN.INI run= with VMTASK.386
loaded; it calls the device's API with no DOS box and after starting a
busy, a polling and a full-screen polling box, prints each VM's share
and checks them (the run times add up to the system time, the busy box
gets at least half, the polling ones at most a tenth, the windowed
flag) and exits 1 on a failed check; CONF `ms` (Microsoft's WIN386 and
KRNL386), `w16` (Microsoft's KRNL386 under w16's WIN386) or `tree`;
`VMTASK_DOSBOX=x` runs DOSBox-X. `tests/vmtaskshot.sh OUTDIR CONF [x]`
takes VMTASK.EXE's screen with 0, 1 and 2 boxes (OUTDIR/vmt0.png to
vmt2.png; CONF `tree`, `ms`, or `std` and `novxd` for one shot without
the device; `x` for DOSBox-X). With `VMTASK_END=N VMTASK_METHOD=0|1`
`vmtaskrun.sh` also ends the N-th box through the API (Close_VM or
Nuke_VM; 1 by default) and checks that the System VM cannot be ended.
`tests/vmtaskend.sh OUTDIR tree|ms [x]` drives VMTASK.EXE with keys:
End VM on a busy box (3.1's Terminate warning), End Task on VMTH.EXE
(a program that ignores WM_CLOSE: the offer to end it by force),
Shift+F10's shortcut menu, the IDs columns; VMT1 logs the tasks' and
VMs' IDs for comparison and checks at the end that VMTH and the box are
gone (shots e0-e8 in OUTDIR).
`tests/qtplay.sh` takes `QTPLAY_W16OPTS` and
`tests/sb16/sbaudio.sh` `SBAUDIO_OPTS`/`SBAUDIO_CONF` (more dropin.sh
options, a COM1 for the VMM's trace) for the w16 side. XMS and EMS in a DOS box:
`tests/v86mmrun.sh OUTDIR [w16]` runs VX15 (`make vxprobes`) in a box and
prints its log of XMS and INT 67h results (`V86MM_PIF` the PIF's limits,
`V86MM_INI` SYSTEM.INI keys such as `386Enh:NoEMMDriver=on`); the case
`win386xms` compares it with Microsoft's WIN386 for three PIFs
(ARCHITECTURE.md 12.13). `tests/memloadrun.sh OUTDIR MODE [w16]` runs
MEMLOAD.EXE's workloads (x XMS, e EMS, 3 XMS 3.0 and UMBs, c three boxes
at once) and prints their logs; the case `win386memload` compares them
with Microsoft's WIN386. The DOS memory arena in a box:
`tests/mcbrun.sh OUTDIR MODE [x|dosbox]` runs MCBCHK.EXE (the chain
walked, the MCB that links the UMBs, UMBs linked, high- and low-first
allocations) without Windows (`plain`), under Microsoft's WIN386 (`ms`),
w16's (`w16`) or on w16's tree alone (`tree`), in DOSBox-X or plain DOSBox,
and prints its log and DOSBox-X's MCB messages; `MCBRUN_PROG=DIR:PROG`
runs a program of your own in the box instead. The case `win386mcb`
compares the logs with Microsoft's WIN386.

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
- A case that passes or fails with where code lands is a bug, not a flake.
  `make build/kpad/N/KRNL386.EXE` links KRNL386 with N bytes of padding
  after `thunk.o` (kernel/kpad.asm), shifting the rest of its code; swap it
  into a tree to try a case at other layouts. Boot-test case `win386pad`
  does that for `win386tree` (sizes 8 and 96, Makefile `KPADS`).
