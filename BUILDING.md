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
make -j"$(nproc)"          # everything below
```

`make` (the `all` target) builds:

| Output | What |
|---|---|
| `build/dos/` | every w16 module and program: `WIN.COM`, `DOSX.EXE`, `KRNL286.EXE`, `KRNL386.EXE`, `USER.EXE`, `GDI.EXE`, `SHELL.DLL`, `WINOLDAP.MOD`, `WINOA386.MOD`, `WIN87EM.DLL`, `VGA.DRV`, `MOUSE.DRV`, `PROGMAN.EXE`, `WINFILE.EXE`, `NOTEPAD.EXE`, `WRITE.EXE`, `REGEDIT.EXE`, `PIFEDIT.EXE`, `WINHELP.EXE`, `VMTASK.EXE`, the external VxDs `VSBD.386` and `VMTASK.386`, `SNAP.EXE`, …, plus the test probes |
| `build/fonts/` | w16's fonts: `VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON`, `SSERIFE.FON`, `COURE.FON`, `SERIFE.FON`, `SMALLE.FON`, `SYMBOLE.FON`, `DOSAPP.FON`, `EGA80WOA.FON`, `EGA40WOA.FON`, `CGA80WOA.FON`, `CGA40WOA.FON` (sources in `fonts/`; the second set's are written by `tools/fontgen.py`) |
| `build/w16/` | the **install tree**: a Windows 3.1-style directory holding only w16's files, each where Windows keeps its own (see below) |
| `build/win386/` | `WIN386.EXE` and its separate VxDs (`make win386` builds only these) |

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

`build/w16/` contains only w16's own files, and boots on its own: no
Microsoft files are needed. Copy it to a DOS drive as `C:\W16` and run `WIN`
from that directory in DOS or DOSBox: `WIN /S` for Standard mode, `WIN /3`
for 386 Enhanced mode. `build/w16/README.TXT` describes the tree and the
differences from Windows 3.1. Standard mode is the more complete of the
two: in 386 Enhanced mode, DOS boxes still lack some of 3.1's features.

The tree holds every w16 module that replaces part of Windows 3.1: WIN.COM,
DOSX and the task swappers, both KERNELs, USER, GDI, WIN386,
WINOLDAP and WINOA386, WIN87EM, the system DLLs (SHELL, COMMDLG, DDEML,
LZEXPAND, MMSYSTEM, OLECLI, OLESVR, TOOLHELP, VER), the drivers (system,
keyboard, mouse, comm, sound, timer, VGA display and grabbers, SB16), the
fonts, the Control Panel applets, the screen saver and the applets. It also
holds w16's default `SYSTEM.INI`, `WIN.INI`, `CONTROL.INI` and
`PROGMAN.INI` with three program groups (Main, Accessories, StartUp:
`assets/`). The tree is meant to be
installed as `C:\W16`; `PROGMAN.INI` names its groups there
(`C:\W16\MAIN.GRP`, ...), so a tree put elsewhere needs its `[Groups]`
lines changed. [TESTING.md](TESTING.md) §4 shows how to
assemble a complete all-w16 drive for DOSBox with `tests/dropin.sh`.

## 4. Layout of the source

| Directory | Module |
|---|---|
| `win/` | `WIN.COM` |
| `dosx/` | the Standard-mode DOS extender (`DOSX.EXE`) |
| `swap/` | the Standard-mode task swappers (`WSWAP.EXE`, `DSWAP.EXE`) |
| `kernel/` | KERNEL (`KRNL286.EXE` and `KRNL386.EXE`; `boot/` the real-mode start) |
| `win386/` | `WIN386.EXE` (386 Enhanced mode): `ldr/` the real-mode loader, `vmm/` the VMM and its built-in devices, `vxd/` the separate VxDs (VPICD, VKD, VMD, REBOOT, VSD, VMPOLL, VDMAD inside WIN386.EXE; VSBD.386 and VMTASK.386, external) |
| `user/`, `gdi/` | USER and GDI |
| `win87em/` | `WIN87EM.DLL`, the floating-point emulator |
| `winoldap/` | `WINOLDAP.MOD` (DOS programs in Standard mode) |
| `winoa386/` | `WINOA386.MOD` (DOS programs in 386 Enhanced mode) |
| `drivers/` | device drivers: `system/`, `keyboard/`, `mouse/`, `comm/`, `sound/`, `timer/` (`TIMER.DRV`), `vga/` (`VGA.DRV`), `grabber/` (`VGACOLOR.2GR`, `vga3gr/` `VGA.3GR`), `sb16/` (`SB16.DRV`) |
| `shell/` | the system DLLs and the applets, one directory each: `shelldll/` (`SHELL.DLL`), `commdlg/`, `ddeml/`, `lzexpand/`, `mmsystem/`, `toolhelp/`, `ver/`; `progman/`, `winfile/`, `taskman/`, `control/` and `cpl/` (`MAIN.CPL`, `CPWIN386.CPL`), `notepad/`, `write/`, `cardfile/`, `calc/`, `clock/`, `regedit/`, `pifedit/` (`PIFEDIT.EXE`), `winhelp/` (`WINHELP.EXE`), `vmtask/` (`VMTASK.EXE`, the VM Task Manager, a w16 extension), `scrnsave/` (the screen saver library and `SSSTARS.SCR`) |
| `ole/` | `OLECLI.DLL` and `OLESVR.DLL` |
| `fonts/` | w16's raster fonts, as text the build turns into `.FON` files |
| `assets/` | the install tree's own files: `SYSTEM.INI`, `WIN.INI`, `CONTROL.INI`, `README.TXT`, Program Manager's groups (`progman/`), the boot screen (`boot/`) |
| `include/` | shared headers (`w16/`) |
| `tools/` | build and analysis tools |
| `tests/` | test programs, probes and harness scripts |
| `docs/` | notes and handovers (`notes/`), the export database (`exportdb/`) and each module's export list (`exports/`), USER's import and export lists (`user/`) |

ARCHITECTURE.md describes the design, each module, and the clean-room rules
contributions must follow.
