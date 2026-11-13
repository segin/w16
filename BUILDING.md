# Building w16

w16 builds on Linux with OpenWatcom v2 (16-bit C, the linker and the
resource compiler), nasm and Python. Building needs no Microsoft files;
only the tests do (see [TESTING.md](TESTING.md)).

## 1. Prerequisites

| Package (Arch name) | Used for |
|---|---|
| `openwatcom-v2` (AUR) | `wcc`/`wcc386`, `wlink`, `wrc`. Expected in `/opt/watcom`; set `WATCOM=/path` otherwise |
| `nasm` | the assembly sources (loaders, drivers, DOSX, thunks) |
| `python`, `python-pillow` | the build tools in `tools/` (resources, fonts, icons, images) |
| `noto-fonts` | the boot logo and the About-box logo (`tools/mklogo.py`) |
| a host C compiler (`gcc` or `clang`, as `cc`) | a few host-side helpers (`HOSTCC` to override) |

On Arch: `yay -S openwatcom-v2 nasm python python-pillow noto-fonts base-devel`.

Other distributions: install OpenWatcom v2 from its releases
(https://github.com/open-watcom/open-watcom-v2) and point `WATCOM` at it.
The Makefile puts `$WATCOM/binl64` first on `PATH`; Wine's unrelated `wrc`,
if installed, is not used.

## 2. Building

```sh
git clone https://github.com/segin/w16.git
cd w16
make -j"$(nproc)"          # everything below except WIN386
make win386                # w16's WIN386.EXE (386 Enhanced mode)
```

`make` (the `all` target) builds:

| Output | What |
|---|---|
| `build/dos/` | every w16 module and program: `WIN.COM`, `DOSX.EXE`, `KRNL286.EXE`, `USER.EXE`, `GDI.EXE`, `SHELL.DLL`, `WINOLDAP.MOD`, `WIN87EM.DLL`, `MOUSE.DRV`, `PROGMAN.EXE`, `NOTEPAD.EXE`, `CLOCK.EXE`, `CALC.EXE`, `SNAP.EXE`, …, plus the test probes |
| `build/fonts/` | w16's fonts: `VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON`, `SSERIFE.FON` |
| `build/w16/` | the **install tree**: a Windows 3.1-style directory holding only w16's files, each where Windows keeps its own (see below) |
| `build/win386/` | `WIN386.EXE` (with `make win386`) |

Other targets:

| Target | What |
|---|---|
| `make dos` | only `build/dos/` |
| `make shell` | only the applets (Program Manager, Notepad, Clock, Calculator, …) |
| `make install-tree` | only `build/w16/` |
| `make groups` | Program Manager's default group files again (`assets/progman/*.GRP`, by `tools/mkgrp.py` from the `*.txt` definitions and the icons of w16's programs); `make test` checks that the committed ones are up to date |
| `make vgaart` | the VGA display driver's graphics (`drivers/vga/art`) into `build/vga/` |
| `make clean` | removes `build/` entirely |
| `make test`, `make boot-test` | the test suites ([TESTING.md](TESTING.md)) |

Builds are reproducible apart from build timestamps embedded in some
executables.

**Version.** w16's version is the one line of the file `VERSION` (its only
source). The Makefile writes it to `build/w16ver.h` with the build: the
source tree's commit, `git rev-parse --short=7 HEAD`, or `W16_BUILD` from
the environment (e.g. `make W16_BUILD=mybuild`), or `unknown` outside a git
checkout. The file is rewritten only when either changes. USER shows both
on the desktop (ARCHITECTURE.md §6.8, `[w16] DesktopVersion`). Module
version resources and `GetVersion` keep Windows 3.1's 3.10.

## 3. Using the install tree

`build/w16/` contains only w16's own files. w16 does not yet replace every
part of Windows 3.1, so to boot it you add the remaining files from your
own Windows 3.1 installation: `build/w16/README.TXT` lists exactly which
files, and where they go. Copy them in without overwriting w16's, then run
`WIN /S` (Standard mode) from that directory in DOS or DOSBox.

The tree holds every w16 module that replaces part of Windows 3.1: WIN.COM,
DOSX, KERNEL, USER, GDI, SHELL, COMMDLG, WINOLDAP, WIN87EM, the mouse,
keyboard and comm drivers, the fonts and the applets. It also holds w16's
default `SYSTEM.INI`, `WIN.INI` and `PROGMAN.INI` with three program
groups (Main, Accessories, StartUp: `assets/`). The tree is meant to be
installed as `C:\W16`; `PROGMAN.INI` names its groups there
(`C:\W16\MAIN.GRP`, ...), so a tree put elsewhere needs its `[Groups]`
lines changed. `WIN386.EXE` (386
Enhanced mode) is not in it yet. [TESTING.md](TESTING.md) §4 shows how to
assemble a complete all-w16 drive for DOSBox with `tests/dropin.sh`.

## 4. Layout of the source

| Directory | Module |
|---|---|
| `kernel/` | KERNEL (`KRNL286.EXE`) |
| `dosx/` | the Standard-mode DOS extender (`DOSX.EXE`) |
| `win/` | `WIN.COM` |
| `win386/` | `WIN386.EXE`: the real-mode loader and the VMM |
| `user/`, `gdi/` | USER and GDI |
| `shell/` | `SHELL.DLL` and the applets (Program Manager, Notepad, Clock, Calculator) |
| `winoldap/` | `WINOLDAP.MOD` (DOS programs under Windows) |
| `drivers/` | device drivers (`mouse/`; `vga/` holds the display driver's graphics) |
| `tools/` | build and analysis tools |
| `tests/` | test programs, probes and harness scripts |

ARCHITECTURE.md describes the design, each module, and the clean-room rules
contributions must follow.
