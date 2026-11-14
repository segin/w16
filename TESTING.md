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

`make test` settings: `RUNTESTS_JOBS=N` probes at once (default: the
machine's threads, at most 16), `WINCOM_JOBS=N` WIN.COM scenarios at once
(default: half the threads, at most 12); `1` runs one at a time. Every probe
runs in a scratch copy of its directory, so runs never share files.

Boot-test settings:

- `BOOTTEST_JOBS=N`: cases run at once (default 3). On a busy machine use fewer; timing-sensitive cases can fail under heavy load. Cases made of independent sessions run as parts that are scheduled separately (`allw16nolock@1` ... `@6`, `pmdrop@pm`, `winfile@cmp`, `kbd@kbtype`, ...; a part can be run by name) and are reported as one line; the longest cases of the last run start first.
- `INPUTRUN_KEYDELAY=MS`: time between keys typed into DOSBox (default 50; with xdotool's 12 a loaded DOSBox lost keys).
- `BOOTTEST_REFRESH=1`: rerun the Windows 3.1 reference boots instead of using the cache in `build/refcache/` (the cache is keyed on everything a reference depends on, so you rarely need this).
- Results go to `build/boottest-HHMMSS/`: for a failing case, `CASE-*-diff.png` shows the differing pixels, and the case's `-ref` and `-w16` directories hold both sides' screens and logs. Retries are logged to `build/boottest-retries.log`. A test that fails only sometimes is a bug, not noise: report it.

Other test scripts, each documented at its top: `tests/npscenarios.sh`,
`tests/pmscenarios.sh`, `tests/calcscenarios.sh`, `tests/clockscenarios.sh`,
`tests/winoldap.sh`, `tests/swap.sh` (w16's WSWAP/DSWAP against Microsoft's),
`tests/mouseprobe.sh`, `tests/sysprobe.sh`, `tests/fonts.sh`,
`tests/dosxtest.sh`, `tests/win386.sh`.

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

386 Enhanced mode (`WIN /3`) needs DOSBox-X with `machine=svga_s3` and
Microsoft's KRNL386 (w16 has no KRNL386 yet); `tests/win386.sh` shows the
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
