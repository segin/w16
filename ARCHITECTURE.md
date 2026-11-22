# w16 Architecture

w16 is a clean-room reimplementation of 16-bit Microsoft Windows that aims for 100% binary compatibility with Windows 3.1.

This document records the project's goals, toolchain, source and clean-room policy, historical findings, target definition, module inventory, design notes and roadmap.

## 1. Project Overview

- **Goal:** unmodified Win16 applications and drivers run on w16 exactly as they do on Windows 3.1.
- **Method:** clean-room reimplementation from public documentation, black-box observation and explicitly open-licensed Microsoft material (see §3).
- **Repository:** Git, initialized 2026-11-07.

## 2. Toolchain

| Tool | Details |
|---|---|
| Compiler and linker | Open Watcom C/C++ v2 (`openwatcom-v2` from the AUR, build 2.0 2026-06-01), installed at `/opt/watcom` |
| Environment | Run `source /opt/watcom/owsetenv.sh` before using `wcc`/`wcl`/`wlink` |
| Win16 code generation | e.g. `wcc -ml -0 -zW file.c` (large model, 8086 instructions, Windows prolog/epilog). On a Linux host, objects are named `.o`. |
| Document OCR | `tesseract` + `tesseract-data-eng`, `pdftoppm`/`pdftotext` (poppler), used for the scanned MT-DOS specs |

## 3. Source and Clean-Room Policy

w16 is dedicated to the public domain under CC0 1.0 (`LICENSE`): the copyright status of machine-generated code varies between countries, so w16 waives any rights worldwide instead. Third-party code it includes keeps its own permissive licence and notice (the exceptions `LICENSE` lists); nothing may be included that would stop the rest being public domain, which is why copyleft code below is reference only.

Every piece of knowledge in w16 must have a recorded source. When a fact, structure layout, ordinal or behaviour comes from a document, cite it in the code or spec (e.g. `/* Source: MT-DOS Memory Management spec, 1984-10-30, MIT */`).

| Source | Status | How it may be used |
|---|---|---|
| Public SDK documentation (Windows 3.1 SDK reference, guides) | **Allowed** | Facts: APIs, messages, structures, ordinals. Rewrite in our own words. |
| Published books (Petzold; Schulman/Maxey/Pietrek *Undocumented Windows*; Pietrek *Windows Internals*; etc.) | **Allowed** | Facts and behaviour. No copying of code listings. |
| Black-box observation of real Windows 3.1 | **Allowed** | Run real apps and drivers, compare behaviour and output. |
| MS-DOS 1.25 / 2.0 / 4.00 source, `microsoft/MS-DOS` on GitHub | **Allowed (MIT)** | Study, adapt or include. Keep Microsoft's copyright notice and the MIT text with any derived code. |
| Multitasking MS-DOS 4.00 beta binaries and specs (Ray Ozzie archive, `v4.0-ozzie/` in the same repo) | **Allowed (MIT)** | Study, disassemble, adapt or include. Same notice requirement. Dirty-room work is permitted. |
| Windows 1.0 Developer Release 5 notes | **Facts only** | Not MIT. Facts such as "Windows uses the MS-DOS memory manager" may be relied on; never copy the text. |
| Windows 3.1 DDK **documentation** | **Facts only** | Interface specs, DDI references, VMM service lists and structure layouts, rewritten in our own words. |
| Windows 3.1 DDK **headers** (`.INC`, `.H`) | **Do not copy** | Rewrite needed definitions from the documentation. |
| Windows 3.1 DDK **sample source** | **Excluded** | Public availability removes trade-secret concerns but not copyright. Do not copy, adapt or redistribute it, and whoever writes a w16 module should not read the matching sample. |
| Windows binaries (any version) | **Forbidden** | Never disassemble or decompile. In particular, never check MT-DOS-derived code against Windows 1.x binaries. |
| Leaked Windows source | **Forbidden** | Never. |
| Third-party permissive code (MIT, BSD, zlib), e.g. the user's LOSE.COM (MIT) | **Allowed** | May be included. Keep its copyright notice and licence text. |
| Wine before its March 2002 move to the LGPL (X11 licence), and the ReWind fork that continued that tree | **Allowed (X11, MIT-equivalent)** | Study, adapt or include. Keep Wine's copyright notice and licence text with anything derived. Most useful for export ordinal tables (`.spec` files) and 16-bit structure layouts. Some early Wine knowledge may have come from disassembly by others; like the books above, such facts are acceptable, but w16 contributors still never disassemble Windows themselves. |
| TransGaming WineX/Cedega additions (Aladdin Free Public License) | **Reference only** | Not open source; never copy. Take X11-era Wine code from Wine or ReWind instead. |
| Third-party copyleft code (GPL, LGPL), e.g. VBADOS/VBMOUSE.DRV (GPL-2.0), Wine's 16-bit DLLs (LGPL) | **Reference only** | Read for facts and approach; never copy or adapt code, which would stop w16 being public domain. |
| Test-only tools (DOSBox, HDPMI16, the user-supplied reference Windows copies) | **Not shipped** | Used to build and test only; kept out of the repository (`.tools/`). |
| Trademarks ("Microsoft", "Windows", "MS-DOS") | — | Not covered by any licence. Use only to describe compatibility, never as the product's name. |

**References.** The documentation w16 relies on, under the "facts only" rules above (no sample code, headers or macro bodies; a fact taken from them is confirmed by a probe wherever it can be). Cite them by author, title and chapter or page:

- Microsoft Corporation. *Microsoft Windows Programmer's Reference, Volume 1: Overview.* Windows 3.1 SDK. Microsoft Press, 1992. ISBN 1-55615-453-4. Doc. PC28915-0492.
- Microsoft Corporation. *Microsoft Windows Programmer's Reference, Volume 2: Functions.* Windows 3.1 SDK. Microsoft Press, 1992. ISBN 1-55615-463-1. Doc. PC28916-0492.
- Microsoft Corporation. *Microsoft Windows Programmer's Reference, Volume 3: Messages, Structures, and Macros.* Windows 3.1 SDK. Microsoft Press, 1992. ISBN 1-55615-464-X. Doc. PC28917-0492. (Its macros are documentation of C helper macros; Volume 4's assembly-language macros, ch. 14, are Microsoft code and excluded.)
- Microsoft Corporation. *Microsoft Windows Programmer's Reference, Volume 4: Resources.* Windows 3.1 SDK. Microsoft Press, 1992. ISBN 1-55615-494-1. Doc. PC30211-0492. File formats: bitmaps, icons and cursors, clipboard, metafile, fonts, Program Manager groups, NE executables and resources, Write, Calendar, object modules and libraries, symbol files, resource scripts, Help.
- Microsoft Corporation. *Microsoft Windows Multimedia Programmer's Guide.* Windows 3.1 SDK, 1992. Doc. PC30253-0492.
- Microsoft Corporation. *The Windows Interface: An Application Design Guide.* Windows 3.1 SDK, 1992. Doc. PC28921-0692.
- Microsoft Corporation. *Microsoft Windows 3.1 Device Driver Kit: Device Driver Adaptation Guide* and *Virtual Device Adaptation Guide.* 1992.
- Microsoft Corporation. *Microsoft Windows 3.1 Device Driver Kit: Installation.* 1992. Doc. PC29131-0392.
- Schulman, Andrew, David Maxey and Matt Pietrek. *Undocumented Windows.* Addison-Wesley, 1992.
- Pietrek, Matt. *Windows Internals: The Implementation of the Windows Operating Environment.* Addison-Wesley, 1993.

**Format facts versus data (the user's rule, 2026-11-09).** Windows' file formats may be parsed: NE, LE and W3 headers, import and export tables, resource tables, and the structure of resources. What a compatible replacement needs to look and behave identically may be reproduced in w16's own source files. Wine follows the same practice. This covers:
- dialog-template layouts: control positions, IDs, classes and styles;
- menu structures and command IDs;
- accelerator tables;
- user-visible UI text: labels, menu items, messages and error texts;
- icon, cursor and bitmap dimensions;
- export names and ordinals;
- the observed contents of INI files.

These are written in w16's own `.rc` and source files.

What may **not** be copied out of Microsoft files:
- **Code, of any kind.**
- **Artwork:** bitmaps, icons, cursors and font glyphs. w16 draws its own.
- **Data tables and constants beyond the interface,** even when they are only numbers. Example: Clock's circle table. Such values are computed, then corrected by black-box observation (screen pixels, GDI call logs), with the evidence recorded. Any copied data is removed from git history before merging.
- **Microsoft branding:** product names, copyright lines and logos. These are never presented as w16's own. A Microsoft branding string in a UI is used only with the user's explicit decision for that string.

**Test-only combinations with Microsoft's files (the user's decision, 2026-11-13).** For 1:1 comparison testing, a test may build a transient binary that combines w16's code with data taken from the reference install's own files, for example `tests/vgamsres.sh`, which links Microsoft's VGA.DRV resources into a scratch copy of w16's driver so that a screen comparison measures w16's drawing code and not its art. The user's reasoning: this is intermediate copying for verifying compatibility, as in *Sega v. Accolade* (9th Cir. 1992). Conditions:
- the result is made in a scratch or build directory at test time, from the user's own reference install, and is never committed, put in the install tree, distributed or published;
- it is only run and compared by the tests, never inspected to reproduce what it contains (no viewing of Microsoft's art, no copying of its data into w16);
- the script says so in its header.
w16's shipped files stay entirely w16's own.

Notes:
- The MIT licence on the MS-DOS repository has no explicit patent grant. Any relevant patents from the 1980s and early 1990s have expired.
- MS-DOS 4.0 was co-developed with IBM, and IBM copyright notices appear in its sources. Microsoft published it under MIT, and the project relies on that in good faith.

**Branding: user-facing "Windows" is "w16" (the user's decision, 2026-11-10).** Wherever w16's own programs and modules show the product's name to a user (resource strings, menus, dialog captions and texts, message boxes, console messages), it says "w16" where Windows 3.1 says "Windows": Program Manager's Help > w16 Tutorial and File > Exit w16..., the Exit w16 dialog ("This will end your w16 session."), Notepad's can't-quit message, WINOLDAP's messages, WIN.COM's console messages and `/?` help, DOSX's and WIN386's fatal messages, and the install tree's README. Everything else in that text stays as observed. Unchanged:
- functional identifiers other software matches: INI sections and keys (`[windows]`, `[NonWindowsApp]`, `windir`), class and module names, file and directory names (`WINDOWS\`, `WINDOWS\SYSTEM`), DDE and help-file names and topics;
- the term "non-Windows application" (a DOS program, as in `[NonWindowsApp]` and PIF files) in WINOLDAP's messages;
- text that names Microsoft's product or another version: Program Manager's Windows 2.x build's About box ("Windows 2.x", the system it runs on), the README's "your own Windows 3.1 installation" and "Windows directory";
- the linker's DOS stub text ("This is a Windows executable") and module descriptions (`.def` DESCRIPTION);
- comments, documentation, citations, developer tools' output and test probes that log or reproduce the original.

Tests that compare with Microsoft's output read "w16" as "Windows" (`tests/wincom.sh`) or mask the rebranded text, so everything else is still compared.

## 4. Historical Findings: Multitasking MS-DOS 4.00 and Windows

Source: `microsoft/MS-DOS`, directory `v4.0-ozzie/`, containing scanned product specs and beta disk images.

- **Documents present:** Overview, Intro, Memory Management, Dynamic Linking, System Calls, Session Manager, Device Drivers, 286 Compatibility, Command Guide, Release Notes, and the beta-site and OEM welcome letters. Most are scanned images without a text layer.
- **Release Notes** are titled "Multi-Tasking MS-DOS Beta Test Release 1.00". They state that this build is based on MS-DOS 2 sources.
- **Memory Management spec** (Microsoft, 1984-10-30). Its abstract says: *"This memo discusses in detail the extended memory management facilities of MS-DOS 4.0. Many of these facilities are also available in the MS-Windows environment under MS-DOS 2 & 3."*
- **Windows 1.0 Developer Release 5 notes** (reported by the project owner; not in the MS-DOS repo) state that Windows uses the MS-DOS memory manager.
- **Conclusion:** Windows 1.x's real-mode memory manager is the MT-DOS 4.00 memory manager or very close to it. Shared design and API are confirmed; shared code is likely but unproven, and must not be verified against Windows binaries.
- **Relevance to w16:** the MT-DOS Memory Management and Dynamic Linking specs (MIT) are authoritative sources for the *semantics* of handle-based memory: movable and discardable segments, lock counts, compaction, and NE-style dynamic linking. Windows 3.1 has no real mode, so the implementation is selector-based (§5), but the API behaviour carries over.

## 5. Target

**Windows 3.1 (US English, build 3.10.103), 386 Enhanced mode, with Standard mode as the first milestone.**

| Decision | Choice | Rationale |
|---|---|---|
| Version | 3.1 | Superset of the 3.0 API; the version almost all Win16 software targets (TrueType, COMMDLG, OLE 1, DDEML, MMSYSTEM, TOOLHELP). Apps marked 3.0 run on it. |
| Not 3.0 | — | Lacks APIs that many 3.1 apps import, so they would fail to load. |
| Not WfW 3.11 | Later extension | Adds networking and 32-bit file access, and drops Standard mode. These can be layered on afterwards. |
| CPU mode | 386 Enhanced (final), Standard (milestone 1) | Many apps check `WinFlags`, call VxD entry points (INT 2Fh AX=1684h), or use `WINMEM32`, so 100% compatibility requires Enhanced mode. Standard mode needs only a DPMI host plus 16-bit protected-mode KERNEL, which makes it the fastest route to running apps. |
| Real mode | Not supported | Windows 3.1 itself dropped real mode. |
| Host OS | MS-DOS 3.1+ compatible DOS | Same as the original. |

### What "100% binary compatible" means here

1. **Applications:** any unmodified Win16 `.EXE`/`.DLL` that runs on Windows 3.1 runs on w16.
   - Identical **module names** and **export ordinals**. Win16 imports bind by ordinal, so ordinals are ABI.
   - Identical **calling conventions** (Pascal, far, prolog/epilog expectations, `DS` handling via `MakeProcInstance`/fixups).
   - The **NE loader** behaves the same way: segment flags, relocations, self-loading apps, resources, `RT_*` types.
   - The same **undocumented internals** that real apps rely on: task database (TDB), module database (NE header in memory), PDB/PSP, `__0000H`/`__A000H`/`__WINFLAGS` style exported selectors, the KERNEL "THHOOK" data, USER `WND` and GDI object layouts that tools read, and TOOLHELP results.
2. **Drivers:** unmodified third-party 3.1 drivers load and work. That means display/printer drivers through the GDI DDI, keyboard/mouse/comm/sound through their driver interfaces, multimedia drivers through `DriverProc`, and Enhanced-mode `.386` VxDs through the VMM services interface.
3. **DOS interface:** INT 21h, INT 2Fh Windows broadcasts (1605h/1606h), DPMI 0.9 (INT 31h) for apps, and `WINOLDAP` behaviour for DOS boxes.

---

## 6. Module Inventory

Each **module name** (as named in `NE` headers and import tables) must exist with the correct ordinals. File names are what Windows 3.1 setup installs. Priorities: **P0** = required for any app to run; **P1** = required by many apps; **P2** = needed for full compatibility; **P3** = user-facing completeness.

### 6.1 Boot / Mode Layer

| File | Module | Role | Mode | Pri |
|---|---|---|---|---|
| `WIN.COM` | — | Loader: checks CPU/memory, picks mode, shows logo, starts DOSX or WIN386 | both | P0 |
| `DOSX.EXE` | — | Standard-mode DOS extender / DPMI host (16-bit) | Std | P0 |
| `WSWAP.EXE`, `DSWAP.EXE` | — | Standard-mode DOS app task swappers | Std | P2 |
| `WIN386.EXE` | — | VMM and built-in VxDs (32-bit ring 0; see 2.6) | Enh | P1 |
| `WINOLDAP.MOD` | WINOLDAP | DOS-app support in Standard mode | Std | P2 |
| `WINOA386.MOD` | WINOLDAP | DOS-app support in Enhanced mode | Enh | P2 |
| `*.2GR` / `*.3GR` (e.g. `VGA.3GR`) | — | Display "grabbers" that save and restore DOS-box screens | both | P2 |
| `SYSTEM.INI`, `WIN.INI` | — | Config file formats and semantics are part of the ABI | both | P0 |

### 6.2 Core Modules

| File | Module | Role | Pri |
|---|---|---|---|
| `KRNL286.EXE` / `KRNL386.EXE` | **KERNEL** | Memory (global/local heap, selectors), tasks and scheduler, NE loader, resources, atoms, profile (INI) APIs, file I/O, DOS3Call, error handling, TOOLHELP hooks | P0 |
| `USER.EXE` | **USER** | Windows, messages, input, dialogs, menus, controls (button, edit, listbox, combobox, scrollbar, static, MDI), clipboard, DDE plumbing, hooks, timers, COMM API forwarding | P0 |
| `GDI.EXE` | **GDI** | Device contexts, drawing, bitmaps/DIBs, fonts (raster, vector, TrueType rasterizer), metafiles, palettes, regions, printing (Escape and spooler interface) | P0 |

### 6.3 System Drivers (16-bit, loaded from `[boot]` in SYSTEM.INI)

| File | Module | SYSTEM.INI key | Role | Pri |
|---|---|---|---|---|
| `SYSTEM.DRV` | SYSTEM | `system.drv` | Timer, system and hardware info (w16's: `drivers/system/`, §6.7) | P0 |
| `KEYBOARD.DRV` | KEYBOARD | `keyboard.drv` | Keyboard input, `ToAscii`, OEM/ANSI conversion (w16's: US, code page 437, `drivers/keyboard/`, §6.7) | P0 |
| `KBD*.DLL` (e.g. `KBDUS.DLL`) | KBD* | `[keyboard] keyboard.dll` | Keyboard layout tables | P1 |
| `MOUSE.DRV` | MOUSE | `mouse.drv` | Mouse input (w16's: PS/2, `drivers/mouse/`, §6.7) | P0 |
| `VGA.DRV` | DISPLAY | `display.drv` | GDI DDI display driver, 640×480×16 (other display drivers deferred, see §6.10) | P0 |
| `VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON` | — | `fonts.fon`, `fixedfon.fon`, `oemfonts.fon` | System, fixed and OEM fonts | P0 |
| `MMSOUND.DRV` (3.1's `SOUND.DRV` is the same file) | SOUND | `sound.drv` | MessageBeep's beep and the legacy `OpenSound` API on the speaker (w16's: `drivers/sound/`, §6.7) | P1 |
| `COMM.DRV` | COMM | `comm.drv` | Serial and parallel ports (w16's: `drivers/comm/`, §6.7) | P1 |
| `TIMER.DRV` | TIMER | `[drivers] timer` | Multimedia high-resolution timer, MMSYSTEM's (w16's: `drivers/timer/`, §21) | P1 |
| `MIDIMAP.DRV` | MIDIMAP | `[drivers] midimapper` | MIDI mapper | P2 |
| `POWER.DRV` | POWER | `[drivers]` | APM | P3 |
| `LANGxxx.DLL` | LANGxxx | `language.dll` | Language and collation (default is built-in) | P2 |
| `*.DRV` (network, e.g. `LANMAN21.DRV`) | (varies) | `network.drv` | WNet* API provider | P2 |

### 6.4 System DLLs (redistributable APIs that apps import)

| File | Module | Role | Pri |
|---|---|---|---|
| `COMMDLG.DLL` | COMMDLG | Common dialogs (Open/Save, Color, Font, Print, Find/Replace) | P0 |
| `SHELL.DLL` | SHELL | Drag-drop, `ShellExecute`, registration database (`Reg*`), `ExtractIcon`. w16: `shell/shelldll/` (§13) | P0 |
| `WIN87EM.DLL` | WIN87EM | x87 emulator / FPU support. Most C runtimes import it. w16: `win87em/` (§9.2, floating-point findings) | P0 |
| `TOOLHELP.DLL` | TOOLHELP | Task, module, heap and global walkers; notifications (w16: `shell/toolhelp/`, §22) | P1 |
| `VER.DLL` | VER | Version resources, `VerInstallFile` (w16: `shell/ver/`, §20) | P1 |
| `LZEXPAND.DLL` | LZEXPAND | LZ decompression (`COMPRESS.EXE` format; w16: `shell/lzexpand/`, §19) | P1 |
| `DDEML.DLL` | DDEML | DDE Management Library (w16: `shell/ddeml/`, §18) | P1 |
| `OLECLI.DLL` | OLECLI | OLE 1.0 client | P1 |
| `OLESVR.DLL` | OLESVR | OLE 1.0 server | P1 |
| `MMSYSTEM.DLL` | MMSYSTEM | Multimedia: wave, MIDI, aux, joystick, timer, MCI, mmio (w16: `shell/mmsystem/`, §23; core and mmio done) | P1 |
| `MCI*.DRV` (`MCIWAVE.DRV`, `MCISEQ.DRV`, `MCICDA.DRV`) | MCI* | MCI device drivers | P2 |
| `MSADLIB.DRV`, `SNDBLST.DRV`, etc. | (varies) | Hardware audio drivers (`DriverProc`) | P2 |
| `WINMEM32.DLL` | WINMEM32 | 32-bit memory access for 16-bit apps | P2 |
| `PENWIN.DLL` | PENWIN | Pen extensions (Windows for Pen Computing) | P3 |
| `DISPDIB.DLL` | DISPDIB | Full-screen DIB display (multimedia/games) | P2 |
| `WINHELP.EXE` | WINHELP | Started by `USER.WinHelp`, so it is part of the app ABI. `.HLP` format (w16: `shell/winhelp/`, §15.10) | P1 |
| `PRINTMAN.EXE` | PRINTMAN | Spooler, driven by GDI print jobs | P2 |
| `UNIDRV.DLL` (+ minidrivers like `HPPCL.DRV`) | UNIDRV | Universal printer driver | P2 |
| `PSCRIPT.DRV` | PSCRIPT | PostScript printer driver | P3 |

Commonly found on 3.1 systems but **not** shipped with base 3.1 (apps install them): OLE 2.0 (`COMPOBJ`, `STORAGE`, `OLE2`, `OLE2DISP`, `OLE2NLS`, `TYPELIB`), `WINSOCK.DLL`, Video for Windows (`MSVIDEO`, `AVIFILE`, `MCIAVI.DRV`), Win32s. These are **out of scope** for the base target, but the base must be able to host the originals.

### 6.5 Shell and Applets (not needed for ABI, needed to be usable)

| File | Pri | Notes |
|---|---|---|
| `PROGMAN.EXE` | P1 | Default `shell=`; DDE interface used by installers (ABI-relevant) |
| `CONTROL.EXE` + `MAIN.CPL`, `DRIVERS.CPL`, `SND.CPL` | P2 | The `.CPL` (`CPlApplet`) interface is ABI-relevant |
| `TASKMAN.EXE` | P2 | Ctrl+Esc task list: done (`shell/taskman/`, 15.5) |
| `WINFILE.EXE` | P2 | File Manager. Its extension DLL interface is ABI-relevant |
| `CLIPBRD.EXE`, `SYSEDIT.EXE` | P3 | |
| `REGEDIT.EXE` | P3 | Registration Info Editor: done (`shell/regedit/`, 15.9), with a New Style view (`[w16] RegeditNewStyle`) |
| `PIFEDIT.EXE` | P3 | PIF Editor: done (`shell/pifedit/`, 15.11), both modes, .PIF files byte for byte as 3.1's |
| `NOTEPAD`, `WRITE`, `PBRUSH`, `CALC`, `CARDFILE`, `CALENDAR`, `CLOCK`, `TERMINAL`, `RECORDER`, `CHARMAP`, `SOUNDREC`, `MPLAYER`, `SOL`, `WINMINE` | P3 | Optional; original designs only |
| `SETUP.EXE` | P3 | |

### 6.6 Enhanced-Mode Layer (32-bit, ring 0, inside `WIN386.EXE` plus external `.386`)

Third-party `.386` VxDs call VMM services by service ID, so the **VxD IDs and service tables are ABI**.

| VxD | Role | Pri |
|---|---|---|
| VMM | Virtual machine manager, scheduler, memory/paging, V86 and PM hosting, DPMI | P1 |
| VPICD | Virtual interrupt controller | P1 |
| VTD | Virtual timer | P1 |
| VDMAD | Virtual DMA | P1 |
| V86MMGR | V86 memory mapping, XMS/EMS emulation | P1 |
| DOSMGR | DOS integration (instancing, InDOS) | P1 |
| PAGESWAP, PAGEFILE | Paging to the swap file | P1 |
| BLOCKDEV, INT13, WDCTRL (FastDisk) | Block device / 32-bit disk access | P2 |
| VKD | Virtual keyboard | P1 |
| VDD (e.g. `VDDVGA`) | Virtual display | P1 |
| VMD / VMOUSE | Virtual mouse | P1 |
| VCD | Virtual COM | P2 |
| VFD | Virtual floppy | P2 |
| SHELL | Shell VxD (system modal messages, `SHELL_Event`) | P1 |
| VSD | Virtual sound (beep) | P2 |
| EBIOS, BIOSXLAT, VMPOLL, COMBUFF, VNETBIOS, DOSNET, LPT, REBOOT, PARITY | Support VxDs | P2 |

---

### 6.7 Design Note: Drivers Target Windows

Because w16 aims for 100% binary compatibility, **every w16 driver is a genuine Windows 3.1 driver**:

- w16 drivers (`.DRV`, `.386`, `.CPL`, MCI/`DriverProc` drivers, grabbers) implement the standard Windows 3.1 driver interfaces (GDI DDI, keyboard/mouse/comm/sound driver interfaces, VMM/VxD services) and nothing w16-specific.
- Each w16 driver must also load and work on genuine Windows 3.1. This is a test requirement: drivers are validated on real Windows 3.1 as well as on w16.
- Conversely, unmodified third-party Windows 3.1 drivers must work on w16.
- No private w16-only driver interfaces. If w16 internals need something, it goes through the documented Windows mechanism (exports, `Escape`, `DriverProc` messages, VxD services).

w16's drivers live under `drivers/`, one directory each with a README (what it is, the interface, what is verified). **`drivers/mouse/`: w16's `MOUSE.DRV`** (done; Standard mode, and 386 Enhanced mode as tested by the user), a PS/2-mouse driver in NASM (`make` builds `build/dos/MOUSE.DRV`): module MOUSE with Windows 3.1's eight ordinals (Inquire, Enable, Disable, MouseGetIntVect, GetSetMouseData, CplApplet, PowerEventProc, WEP). The PS/2 BIOS (INT 15h C2xxh) finds and configures the device as Windows 3.1's driver does (black-box trace, `tests/dos/ms2.asm`); IRQ 12 is taken in protected mode and the bytes read from the 8042 when it has them, the BIOS's C207h packet handler otherwise (DOSBox 0.74 emulates no auxiliary port: there every packet comes that way). `tests/mouseprobe.sh` (MD1, `tests/apps/md1.c`) compares Inquire's MOUSEINFO, the entry points' stack use and every event-procedure call (registers, interrupt flag) with Windows 3.1's driver under the same real input: identical. Acceleration and button swapping are USER's. Boot-test cases `mouse` and `mouseall`. GetSetMouseData (Microsoft Mouse 8.20's private settings call, not used by Windows), PowerEventProc (POWER.DRV's resume hook) and the per-IRQ packet hold-back after each set-up were observed black-box with the MD2 probes (`tests/apps/md2logs/`). 

**`drivers/comm/`: w16's `COMM.DRV`** (Standard mode), the communications driver in NASM (`make` builds `build/dos/COMM.DRV`): module COMM with Windows 3.1's 22 entries (inicom ... getdcb 1-15, WEP 16, Suspend/ReactivateOpenCommPorts 17-18, CommWriteString 19, ReadCommString 20, EnableNotification 100, an unnamed 101), each popping the bytes Windows 3.1's pops. 8250/16450/16550 UARTs on COM1-COM4 (BIOS data area, else SYSTEM.INI [386Enh] COMnBase/COMnIrq, COM3 3E8h), shared IRQs, the 16550 FIFO from 4800 baud, XON/XOFF and RTS/DTR flow control both ways, CTS/DSR output handshaking and the CTS/DSR/RLSD time-outs, events and WM_COMMNOTIFY, LPT1-LPT3 through their ports. Everything was measured black-box on Windows 3.1's driver with CM1 (`tests/apps/cm1.c`, `tests/commprobe.sh`): CM1 runs as the shell, drives COM1 and COM2 (DOSBox `nullmodem` ports whose far ends are `tests/commhost.py`, a TCP server on the host that records what arrives and sends bytes, line changes and breaks on CM1's in-band commands) directly and through USER, and logs only the driver's results, the UART's registers and the far end's record; the two drivers' logs must be identical (670 observations and every byte on the cable; LPT output in DOSBox-X with `commprobe.sh -x`). Boot-test case `comm`. Not verified: 386 Enhanced mode (VCD), carrier detect and ring (DOSBox's nullmodem cannot signal them), parity/framing errors other than a break, real printers (`drivers/comm/README.md`).
**`drivers/keyboard/`: w16's `KEYBOARD.DRV`** (done, Standard mode), in NASM (`make` builds `build/dos/KEYBOARD.DRV`): module KEYBOARD with Windows 3.1's 21 ordinals (Inquire, Enable, Disable, ToAscii, AnsiToOem, OemToAnsi, SetSpeed, WEP, ScreenSwitchEnable, GetTableSeg, NewTable, OemKeyScan, VkKeyScan, GetKeyboardType, MapVirtualKey, GetKBCodePage, GetKeyNameText, AnsiToOemBuff, OemToAnsiBuff, EnableKBSysReq, GetBIOSKeyProc), each taking the stack bytes 3.1's takes (measured). The US layout and code page 437 are built in (`[keyboard] keyboard.dll=` and `oemansi.bin=` empty; language libraries are not supported). INT 9 is taken in protected mode and keys go to USER's event procedure without the BIOS, as 3.1's driver does. The ANSI/OEM tables are generated from the public cp1252/cp437 definitions plus 3.1's observed best-fit choices (`tools/kbtables.py`); no table comes from Microsoft's file. `tests/kbprobe.sh`: KB1 (`tests/apps/kb1.c`: every export over all inputs, stack use, registers) identical to 3.1's driver; KB2 (`tests/apps/kb2.c`, real key input with the recording driver KBREC.DRV in front and the real-mode TSR KB4.COM: scan codes, event-procedure calls, calls into the driver, INT 16h calls, messages) identical; typematic repeats within the host's timing. Boot-test cases `kbd`, `kbdall`, `kbdwrite`, `kbdwoados`. Not verified: 386 Enhanced mode, Print Screen and Ctrl+Break (DOSBox sends neither under the harness) (`drivers/keyboard/README.md`).

**`drivers/system/`: w16's `SYSTEM.DRV`** (Standard mode), in NASM (`make` builds `build/dos/SYSTEM.DRV`): module SYSTEM with Windows 3.1's eleven ordinals (InquireSystem 1, CreateSystemTimer 2, KillSystemTimer 3, EnableSystemTimers 4, DisableSystemTimers 5, GetSystemMsecCount 6, Get80x87SaveSize 7, Save80x87State 8, Restore80x87State 9, WEP 10, A20_Proc 20), each popping what 3.1's pops (4, 6, 2, 0, 0, 0, 0, 4, 4, 2, 2 bytes). Undocumented in the DDK; facts from the NE tables, Schulman ch. 9, Pietrek ch. 1 and black-box observation. Eight timers with 3.1's handles (18h-50h; COMM, USER's cursor timer, KERNEL's LRU sweep and USER's SetTimer timer take four at boot), each called every max(1, rate * 1000 / 54925) ticks; INT 8 chained to the BIOS first, then the msec count (+54.925 ms a tick, only while enabled), then the timers due with interrupts on, AX the handle, DS the driver's, no reentry (a tick during the procedures counts the msec and nothing else); the PIT is not reprogrammed. InquireSystem: 54925 for the timer resolution; drive types from the BIOS's floppy count (B: 1:0 on a one-floppy machine) and DOS IOCTL 4409h/4408h. `tests/sysprobe.sh`: SY1 (`tests/apps/sy1.c`: every export, registers, 30 timer rates, a timer procedure's context, reentrancy, the msec count against the BIOS ticks, Disable/Enable) and SYREC (`tests/apps/syrec.asm`, a recording SYSTEM in front of the driver under test: every call from boot to exit) identical with 3.1's driver and w16's. Boot-test cases `sysdrv`, `sysdrvall`, `sysdrvwoados`, `sysboot`, `sysclock`, `sysprobe`. Not verified: 386 Enhanced mode, machines without a coprocessor, remote and CD-ROM drives (`drivers/system/README.md`).

**`drivers/sound/`: w16's `MMSOUND.DRV`** (Standard mode), the sound driver in NASM (`make` builds `build/dos/MMSOUND.DRV`; w16's SYSTEM.INI names it, `sound.drv=mmsound.drv`): module SOUND with Windows 3.1's 18 ordinals (OpenSound ... SetVoiceThreshold 1-16, DoBeep 17, WEP 18; 3.1's MMSOUND.DRV and SOUND.DRV are the same file), each popping the bytes 3.1's pops (0, 0, 4, 8, 10, 6, 4, 8, 0, 0, 2, 0, 2, 0, 0, 4, 2, 2). Observed black-box with SN1 (`tests/apps/sn1.c`) in DOSBox-X with the run's sound recorded (`tests/dropin.sh` DROPIN_CAPTURE: DOSBox-X's `DX-CAPTURE /A`; `tools/wavtones.py` reads the speaker's tones back, `tools/tonecmp.py` compares two recordings) and SNDREC (`tests/apps/sndrec.asm`, a recording SOUND in front of the driver). What 3.1's driver does: USER's MessageBeep calls DoBeep(type) (any type, unless SPI_SETBEEP turned the beep off); DoBeep finds MMSYSTEM with GetModuleHandle (at each DoBeep and OpenSound until it is loaded; then sndPlaySound, timeSetEvent and timeKillEvent by ordinal, kept) and hands every type but FFFFh to sndPlaySound(name, SND_ASYNC) at once, the name by bits 4-7 of the type (1 SystemHand, 2 SystemQuestion, 3 SystemExclamation, 4 SystemAsterisk, else SystemDefault); FFFFh, no MMSYSTEM or a FALSE answer sound 749 Hz (divisor 1593) on the speaker for a CPU loop of about 64,000 instructions (6.7 ms at 10000 DOSBox cycles a ms); the SOUND API is a real one-voice queue (6 bytes a note, 192 by default) played on the speaker in 5 ms ticks, on a 5 ms periodic timeSetEvent once MMSYSTEM is found (else w16's from its own IRQ 0 handler with the 8254's channel 0 at 200 Hz, the old handler still getting 18.2 ticks a second), notes 1-84 (37 is C5) in equal temperament, lengths, tempos and the normal/legato/staccato modes timed to the 5 ms tick by rules measured over 78 combinations (`drivers/sound/README.md`); dots, volume, envelopes, noise and SyncAllVoices do nothing. w16's USER now calls DoBeep from MessageBeep too (`user/misc.c`). `tests/soundprobe.sh` (sn1, queue, rec, allw16, beep) compares both drivers' logs (identical) and their recordings (tones within `tonecmp.py`'s limits; `beep`: KRNLOG.DLL, `tests/apps/krnlog.c`, records every KERNEL and MMSYSTEM call the driver makes, in six configurations of MMSYSTEM and wave driver, sound compared by `tools/beepcmp.py`); boot-test cases `sound`, `soundtime`, `soundrec`, `soundbeep`, `soundboot`, `treealone` (build/w16 alone, no file of Microsoft's). Not verified: 386 Enhanced mode; calls that never returned on 3.1's (WaitSoundState(S_THRESHOLD) with nothing playing, notes after SetVoiceQueueSize(0)) return on w16's.

**`drivers/timer/`: w16's `TIMER.DRV`**, MMSYSTEM's multimedia timer driver in NASM (`make` builds `build/dos/TIMER.DRV`; w16's SYSTEM.INI names it, `[drivers] timer=timer.drv`, with `[boot] drivers=mmsystem.dll`): module TIMER with 3.1's WEP 1 and DriverProc 2 and its six undocumented messages (kill 0800h, set 0804h, time 0808h, capabilities 080Ch, begin and end period 0810h, 0814h), found black-box with the logging driver TMS.DRV between Microsoft's MMSYSTEM and Microsoft's TIMER.DRV; 15 events (8 in 386 Enhanced mode) with 3.1's IDs, the 8254 at the shortest period asked for (1-55 ms), the BIOS tick, GetTickCount and SYSTEM.DRV's timers kept at their rate, callbacks through MMSYSTEM's StackEnter and DriverCallback as 3.1's. `tests/timer/tscen.sh` and the boot-test case `timer` compare the probe TPR's log and TMS's record with Microsoft's driver and with w16's, and play QuickTime's movie with w16's driver. 3.1's own timing faults are not copied; in 386 Enhanced mode the timing runs on VTD's virtual 8254, not VTDAPI.386 (§21).

**`drivers/sb16/`: w16's `SB16.DRV`**, a Sound Blaster 16 driver (C and NASM; `make` builds `build/dos/SB16.DRV`, installed as `SYSTEM\SB16.DRV` but not named in the default SYSTEM.INI: `assets/README.TXT` says how to enable it): an installable driver with DriverProc, wodMessage, widMessage, modMessage, midMessage and auxMessage for MMSYSTEM: wave output and input (8/16 bits, mono/stereo, 5000-44100 Hz, auto-initialised DMA in two halves), MIDI out and in through the MPU-401 in UART mode, five aux devices on the mixer. Its behaviour toward MMSYSTEM follows Windows 3.1's own SNDBLST2.DRV as observed with the logging drivers (answers, error codes, callback order and timing class). Standard mode only. §24.

### 6.8 Design Note: UI Fidelity

Like ReactOS, w16 user interfaces match the original Windows 3.1 UIs 1:1 wherever possible.

- **Match exactly:** layout, metrics and spacing, colours, the system colour scheme, window frames and captions, 3D bevels, controls (buttons, check boxes, scroll bars, list boxes, combo boxes, edit controls), menus, dialog templates (control positions in dialog units), keyboard behaviour, and the wording of standard UI text (button labels, menu items, common-dialog strings).
- **Reference method:** black-box observation of real Windows 3.1, such as screenshots and pixel comparison at the same resolution and colour depth. This is allowed by §3. Functional elements drawn by code (frames, bevels, scroll arrows, check marks, radio buttons, size boxes) are recreated by measurement.
- **Pixel tests:** UI work is validated by screenshot comparison against real Windows 3.1 at 640×480×16.
- **Exceptions (redrawn originally, matching size and style only):**
  - Artwork: icons, cursors, bitmaps that are pictures rather than functional glyphs, wallpapers, and the startup logo.
  - Microsoft logos and branding are never reproduced.
  - Games (`SOL`, `WINMINE`, etc.) get original designs, not recreations.
- **Fonts:** see §6.9.
- **Enhancements toggled by configuration (the user's rule, 2026-11-13):** "Extending the system in ways that enhance while remaining compatible are still 1:1 if they can be toggled to stock behavior by mere configuration." An extension beyond 3.1 is allowed when a `[w16]` key in SYSTEM.INI (read at the module's start-up) turns it off, and off must be exactly 3.1: no visible difference, no message or call an application could see, tested against Windows 3.1 with the key set. Compatible enhancements may be on by default; ones that can break programs are off by default. The settings:

  | Key (`[w16]`, SYSTEM.INI) | Default | Stock 3.1 | What it does |
  |---|---|---|---|
  | `ScrollBarMenu` | 1 (on) | 0 | the right-click scroll-bar menu of Windows 2000 and later (§10.3) |
  | `DesktopVersion` | 1 (on) | 0 | the version stamp in the desktop's bottom right corner, in the style of Windows 3.0's debug pre-release builds (§10.3) |
  | `ExtendedUserHeap` | 0 (off) | 0 | windows outside USER's DGROUP; can break programs that read USER's structures (§10.3) |
  | `ExtendedGdiHeap` | 0 (off) | 0 | GDI objects outside GDI's DGROUP; can break programs that read GDI's structures (§11.7) |
  | `NotepadAccelerators` | 1 (on) | 0 | w16's Notepad: later Windows' Notepad keys Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+P, Ctrl+A, Ctrl+F, shown in its menus with F1 (§15.1) |
  | `PEError` | 1 (on) | 0 | KERNEL: WinExec of a 32-bit program (a PE file) while Win32s is not installed returns 21 and starts nothing, and w16's Program Manager says so ("Cannot run ... This 32-bit program requires Win32s, which is not installed.", in its "Application Execution Error" box); 0: as 3.1, WINOLDAP runs the program's MS-DOS stub (§9.8) |
  | `PifFieldFix` | 1 (on) | 0 | w16's PIF Editor: reading a PIF writes nothing outside its text fields; 0: as 3.1, a NUL after each field that is filled to its end (§15.11) |
  | `DebugToAux` | 0 (off) | 1 | 1: OutputDebugString's text to DOS handle 3 (AUX) always, as 3.1's retail KERNEL (in DOSBox AUX is the console, and the text is painted over Windows' screen); 0: there only when handle 3 is not the console (IOCTL 4400h), else only to `[w16] debuglog` (§9.4, debugging aids) |

  `tests/dropin.sh` (and so every boot test) writes `DesktopVersion=0`, `NotepadAccelerators=0` and `PEError=0` (as does `tests/refrun.sh`, so `tests/npscenarios.sh` compares stock menus) unless a `-i` or `DROPIN_INI` setting says otherwise: every screen it compares with Windows 3.1 is taken with the stamp off.   `DROPIN_INI=w16:KEY=VALUE` runs any boot test with a setting (`tests/remote-test.sh -e DROPIN_INI=...`). The debugging keys of §9.4 (`trace`, `heapcheck`, `userlog` ...) are not enhancements and change nothing an application sees.

### 6.9 Fonts (`.FON`)

w16 needs its own raster and vector font set, drawn from scratch. Face names, file names and **metrics must match exactly**, because USER layout depends on them. For example, dialog base units come from the System font's average character width and height, so a one-pixel difference moves every dialog control. Glyph shapes are original, following the same approach as Wine's own `.FON` files.

**File format:** `.FON` is a resource-only NE DLL containing `RT_FONTDIR` and `RT_FONT` resources. Each `RT_FONT` is a Windows `.FNT` file, version 2.0 (raster) or 3.0. A host tool, `tools/fontc`, compiles text glyph sources into `.FNT` and packages them into `.FON`. Metrics are recorded per size from black-box measurement (`GetTextMetrics`, `GetCharWidth`) on real Windows 3.1.

| Set | Files | Faces | Pri |
|---|---|---|---|
| VGA (96 dpi) system | `VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON` | System, Fixedsys, Terminal | P0 |
| VGA (96 dpi) "E" set | `SSERIFE.FON`, `SERIFE.FON`, `COURE.FON`, `SYMBOLE.FON`, `SMALLE.FON` | MS Sans Serif, MS Serif, Courier, Symbol, Small Fonts | P1 (done) |
| Vector | `MODERN.FON`, `ROMAN.FON`, `SCRIPT.FON` | Modern, Roman, Script | P2 |
| DOS box | `DOSAPP.FON`, `EGA80WOA.FON`, `EGA40WOA.FON`, `CGA80WOA.FON`, `CGA40WOA.FON` | Terminal (windowed DOS apps) | P2 (done) |
| 8514 (120 dpi) | `8514SYS.FON`, `8514FIX.FON`, `8514OEM.FON`, `SSERIFF.FON`, `SERIFF.FON`, `COURF.FON`, `SYMBOLF.FON`, `SMALLF.FON` | as above | Deferred |
| EGA/CGA | `EGASYS.FON`, `EGAFIX.FON`, `EGAOEM.FON`, `CGASYS.FON`, `CGAFIX.FON`, `CGAOEM.FON`, the `*B.FON`/`*A.FON` sets | as above | Deferred |

The TrueType core fonts (Arial, Times New Roman, Courier New, Symbol, Wingdings) are not `.FON` files. They are covered later by metric-compatible, freely licensed TrueType fonts (e.g. Liberation) installed under the same face names.

#### 6.9.1 w16's VGA fonts (done: `VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON`, `SSERIFE.FON`)

`make fonts` builds them into `build/fonts/` from hand-drawn glyph sources in `fonts/` (one file per font size: `vgasys.txt`, `vgafix.txt`, `vgaoem.txt`, `sserife8.txt` ... `sserife24.txt`). Every glyph is original text art (`#` ink, `.` blank); accented letters are composed from the base letter and accent pieces drawn once per font. No Microsoft font file enters the repository or `build/fonts/`.

**Where the metrics come from.** Black-box: the probe FN1 (`tests/apps/fn1.c`) records everything a program can observe about the fonts — every `TEXTMETRIC` field, `GetTextFace`, `GetCharWidth` for codes 0-255, `GetTextExtent` of test strings, `GetAspectRatioFilter`, the `LOGFONT` of the stock fonts, `EnumFonts` and `EnumFontFamilies` (LOGFONT and TEXTMETRIC of every font), 871 `CreateFont` requests (heights -40..40, widths, weights, character sets, pitch and family, 13 face names) and what they map to, `GetDialogBaseUnits`, `SM_CYCAPTION`, `SM_CYMENU`. Its log on Windows 3.1 with its own fonts is `tests/fonts/fn1ref.txt` (identical in Standard and 386 Enhanced mode). The `.FNT` header fields of the reference files were read with `tools/fonhdr.py`, which stops at the header: it never reads glyph offsets or bitmaps, and `tools/fontspec.py`, which does draw glyphs, refuses any font whose `dfCopyright` does not start with "w16".

**Observed metrics** (96 dpi; all `.FNT` version 2.0 (0x200), `dfType` 0, `dfVertRes`/`dfHorizRes` 96, external leading 0, not italic/underlined/struck out; height, ascent, internal leading in pixels; `tm*Char` as GetTextMetrics reports them, i.e. absolute codes; the file stores default and break characters relative to `dfFirstChar`):

| File (module, resource ID) | Face | pt | Height | Ascent | Int. leading | AvgWidth | MaxWidth | Weight | CharSet | Pitch/family (file) | First-last | Default | Break |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `VGASYS.FON` (FONTS, 31) | System | 10 | 16 | 13 | 3 | 7 | 14 | 700 | 0 | 0x21 variable, swiss | 32-255 | 128 | 32 |
| `VGAFIX.FON` (FIXFONTS, 31) | Fixedsys | 12 | 15 | 12 | 3 | 8 | 8 | 400 | 0 | 0x30 fixed, modern | 32-255 | 128 | 32 |
| `VGAOEM.FON` (OEMFONTS, 30) | Terminal | 12 | 12 | 10 | 0 | 8 | 8 | 400 | 255 (OEM) | 0x30 | 1-254 | 32 | 32 |
| `SSERIFE.FON` (SSERIFE, 84) | MS Sans Serif | 8 | 13 | 11 | 2 | 5 | 11 | 400 | 0 | 0x21 | 32-255 | 128 | 32 |
| (85) | | 10 | 16 | 13 | 3 | 7 | 14 | | | | | | |
| (86) | | 12 | 20 | 16 | 4 | 8 | 16 | | | | | | |
| (87) | | 14 | 24 | 19 | 6 | 9 | 19 | | | | | | |
| (88) | | 18 | 29 | 23 | 5 | 12 | 24 | | | | | | |
| (89) | | 24 | 37 | 29 | 5 | 16 | 32 | | | | | | |

The advance width of every character is in the `widths` lines of each source (from `GetCharWidth`) and fontc checks each glyph against them. Codes outside first-last, and 0x7F-0x9F in the ANSI fonts, have the default character's width, except 0x91/0x92 (single quotation marks) in MS Sans Serif 10 pt and up. Other observations:
- **Dialog base units are not `tmAveCharWidth`.** `GetDialogBaseUnits` gives 8 x 16 while the System font's average width is 7: USER takes the horizontal unit from the text extent of "A-Z a-z" (429 pixels / 52 = 8.25). So every letter's width matters for dialog layout, not just the header's average.
- Stock objects: `SYSTEM_FONT` System, `SYSTEM_FIXED_FONT` Fixedsys, `OEM_FIXED_FONT` Terminal, `ANSI_FIXED_FONT` Courier 13 px, `ANSI_VAR_FONT` "Helv" (MS Sans Serif 13 px through `[FontSubstitutes]`; `GetTextFace` returns "Helv"), `DEVICE_DEFAULT_FONT` Courier 16 px. A request for height 0 with no face gets MS Sans Serif 20 px; an unknown face name likewise.
- `EnumFontFamilies` leaves `elfFullName`/`elfStyle` unset for raster fonts (whatever memory held; it differs between runs), so FN1 prints them only for TrueType fonts.
- `dfWidthBytes` (version 2.0) is the sum over all character-table entries of the glyph's width in bytes, `ceil(width / 8)`, including the extra entry after the last character; the bitmaps follow the table without gaps (`dfFace - dfBitsOffset = dfWidthBytes * dfPixHeight` in every reference font). The extra entry's width cannot be observed through the API; the byte count fixes it at 1 byte in all the VGA fonts, and w16 makes it as wide as the space (at most 8). With that, w16's `dfSize`, `dfWidthBytes`, `dfBitsOffset` and `dfFace` equal Windows 3.1's.

**`.FON` packaging** (observed from the reference files' NE headers): a DOS stub, an NE header with flags 0x8300 (library, Windows API), expected Windows version 3.10, target OS 2, no segments, no entry points, alignment shift 4; a resource table with `RT_FONTDIR` named "FONTDIR" (flags 0x0C50: moveable, preload), the `RT_FONT` resources by ID (0x1C30: moveable, pure, discardable) and an `RT_VERSION` (0x0C30); the module name in the resident-name table and in the non-resident table the description "FONTRES 100,96,96 : <name>" (aspect 100, 96 x 96 dpi — format facts; the text after the colon is free, except for SSERIFE.FON, whose "MS Sans Serif 8,10,12,14,18,24 (VGA res)" is the `[fonts]` key in WIN.INI and is kept). The font directory is a count, then per font its resource ID, the first 113 bytes of its `.FNT` header (to `dfFace`, plus a reserved dword) and its device and face names. Version: 3.10, `VFT_FONT`/`VFT2_FONT_RASTER`, `VOS_DOS_WINDOWS16`, with w16's own strings. w16's DOS stub and copyright texts are its own.

**fontc** (`tools/fontc.py`; source format in its docstring): `fontc.py fnt SRC OUT.FNT`, `fontc.py fon OUT.FON --module NAME --desc TEXT [--info KEY=VALUE] ID=SRC...`, `fontc.py check SRC...`. Header lines (`face`, `points`, `height`, `ascent`, `internal-leading`, `weight`, `charset`, `pitch-family`, `avgwidth`, `first`, `last`, `default`, `break`, `copyright`, `version` 2 or 3, ...), expected `widths`, then `char CODE` with a grid of exactly `height` rows (the row length is the advance width), `piece NAME` grids, and composed glyphs `char 0xC0 = 0x41 + grave_cap@dx,dy`. It checks row counts, widths against the observed ones, `maxwidth`, the character range, and the format's limits (ZOI, no others): codes 0-255, a version 2.0 font within 64 KB (16-bit glyph offsets; version 3.0 has 32-bit ones), a 60-byte copyright, 255-byte module name and description, resource IDs 1-32767, NE resource offsets/lengths in 16 bits of 2^shift units (fontc raises the shift as needed). UNVERIFIED: version 3.0 output follows the published layout but has not been tried on Windows 3.1 (the VGA set is 2.0).

**Verification** (`tests/fonts.sh`; `-q` runs in `make test`): in a scratch copy of the reference install the four files are replaced by w16's; FN1's log must equal `tests/fonts/fn1ref.txt` in Standard and 386 Enhanced mode. With `-s DIR`, SNAP takes the same screens with Microsoft's fonts and with w16's (Program Manager, its File menu, File Run, File Properties, Notepad with text, Notepad's About box, Control Panel and its Fonts dialog): the window dumps (every window's class, text, style, rectangle) must be identical and `tools/layoutdiff.py` checks that the pixels that differ form text-shaped regions no taller than a font cell; FN2 (`tests/apps/fn2.c`) shows every character of w16's fonts as Windows draws them (only w16's: Microsoft's glyphs are not rendered for viewing). Specimen sheets: `tools/fontspec.py OUT.png SOURCE|FILE.FON [--scale N]`.

**Results** (2026-11-08): with all four files replaced, FN1's log is identical to Windows 3.1's in Standard and 386 Enhanced mode (30 fonts dumped, 871 mappings, 66 enumerated fonts: every metric, width and extent, the enumeration order and the mapping of every request), and every `.FNT` header field equals the reference file's except the copyright text. In Standard mode the eight screens' window dumps (155 windows: rectangles, styles, texts) are identical, and the differing pixels (900-4800 per screen) all lie in regions no taller than a text cell. Not yet checked: screenshots in 386 Enhanced mode. The rest of the VGA set followed on 2026-11-21 (6.9.2).

**The designs** (all original, drawn as text in `fonts/`, accents composed): System, a bold square-shouldered sans with 2-pixel stems and 1-pixel horizontals; Fixedsys, its fixed-pitch companion (0 with a centre bar, serifed I and l); Terminal, the same weight in 8x12 with the full code page 437 set, box drawing on fixed lines (single: column 3, row 5; double: columns 2 and 4, rows 4 and 6) so lines join across cells, shades tiling with a 4x2 period; MS Sans Serif, a neutral grotesque with 1-pixel strokes at 8-14 pt, 2 at 18 pt and 3 (vertical) at 24 pt.

#### 6.9.2 The rest of the VGA set (done: `COURE.FON`, `SERIFE.FON`, `SMALLE.FON`, `SYMBOLE.FON`, `DOSAPP.FON`, `EGA80WOA.FON`, `EGA40WOA.FON`, `CGA80WOA.FON`, `CGA40WOA.FON`)

With these the install tree has every raster font 3.1's Setup installs for VGA; only the three vector (plotter) fonts are left (below).

**Observation: probe FN3** (`tests/apps/fn3.c`, API only; nothing reads Microsoft's font files, not even their headers). Per face (Courier, MS Serif, Small Fonts, Symbol) every enumerated size realised again from its LOGFONT and dumped as FN1 does (TEXTMETRIC, GetCharWidth 0-255, extents), plus a CreateFont mapping grid; per WIN.INI file the fonts that go when it is removed (RemoveFontResource, then added back); per DOS-box file the fonts AddFontResource adds (every face enumerated before and after) and the Terminal requests WINOLDAP makes by cell; per file its module name (TOOLHELP ModuleFirst/Next after LoadLibrary), its RT_FONT IDs (FindResource 1-1023) and SizeofResource. Its log on 3.1 (`tests/fonts/fn3ref.txt`) is identical in Standard and 386 Enhanced mode. What it shows:

| File (module) | IDs | Fonts (height x average width, px) | Notes |
|---|---|---|---|
| `COURE.FON` (COURE) | 90-92 | Courier 13x8, 16x9, 20x12 | fixed (pitch/family 0x30), il 0 |
| `SERIFE.FON` (SERIFE) | 78-83 | MS Serif 13x5, 16x6, 19x8, 21x9, 27x12, 35x16 | 0x11; tmMaxCharWidth of 13 and 16 px is one more than the widest GetCharWidth |
| `SMALLE.FON` (SMALLE) | 1-8 | Small Fonts 3x1, 5x3, 6x3, 8x4, 10x4, 11x5; MS Serif 10x4, 11x5 | resources aligned to 512 bytes; Small Fonts has zero-width characters and the Windows 0x80-0x9F set |
| `SYMBOLE.FON` (SYMBOLE) | 1-6 | Symbol 13x5 ... 35x15 | charset 2, codes 32-254; GetCharWidth(255) shows the extra table entry, 8 |
| `DOSAPP.FON` (DOSAPP) | 1-6 | Terminal 6x4, 12x5, 8x6 (weight 700), 12x7, 18x10, 16x12 | OEM, codes 0-255; default/break 96/0 or 31/31 |
| `EGA80WOA.FON` ... `CGA40WOA.FON` | 131, 130, 101, 100 | Terminal 12x8, 12x16, 8x8, 8x16 | OEM, 1-254; own aspect values (72x96, 72x192, 48x80, 48x160 as tmDigitizedAspectX/Y) |

The mapper gives a font of the requested cell when one is loaded; EGA80WOA's 12x8 has VGAOEM's metrics, so a request for it realises VGAOEM's (FN3 shows its own metrics in the FILE section's enumeration). 3.1 reports dfVertRes as tmDigitizedAspectX and dfHorizRes as tmDigitizedAspectY (a w16 font written with 96/72 shows ax=96 ay=72); fontgen writes the fields so that the API shows 3.1's values. Packaging as in 6.9.1; the WIN.INI files' descriptions are their `[fonts]` keys ("Courier 10,12,15 (VGA res)", "MS Serif 8,10,12,14,18,24 (VGA res)", "Symbol 8,10,12,14,18,24 (VGA res)", "Small Fonts (VGA res)"), the DOS-box files' are w16's own. SizeofResource equals 3.1's for every font: the extra table entry is one byte wide (8 pixels or less) in all of them (the sizes fix it), SMALLE.FON is built with `fontc --shift 9`, and DOSAPP.FON's 4x6 and 12x16 resources are padded to 3.1's lengths (`--pad`; their `.FNT` content is complete without it). Every `.FON` file (the first four too) is padded to the size of 3.1's (`fontc --file-size`): Control Panel's Fonts dialog shows it ("The size of the font on the disk is: 64 KB" for SSERIFE.FON; w16's was 63 KB).

**The designs** are drawn by `tools/fontgen.py` from `fonts/skeleton.txt`: w16's own stroke skeletons (lines, elliptical arcs, dots, polygons, serif marks, sub-glyphs), one per Unicode character, on a unit box. fontgen fits each into the cell GetCharWidth gives the character, hinted per size (a table of cap height, x-height, ascender, descender, stem width, serif length; stems are 1 pixel up to 14 pt, 2-3 at 18 and 24 pt), composes accented letters (capitals shortened where the accent does not fit), makes code page 437's box drawing, blocks and shades by rule so they join across cells (single lines on the cell's centre column/row, doubles one pixel either side, 2 pixels thick in 12-wide cells), and uses `fonts/tiny.txt`'s hand-drawn pixel glyphs for cells under 7 rows. Styles: Courier a light slab serif (serifs on every stem end, slab I, i, l, 1); MS Serif and Symbol a bracketless serif (no serifs on small letters below a 7-pixel x-height); Small Fonts and the DOS-box sizes sans serif. EGA80WOA is VGAOEM's Terminal design (6.9.1, the same 8x12 cell), EGA40WOA and CGA40WOA are EGA80WOA and CGA80WOA doubled in width (as the 40-column modes double each pixel). The sources fontgen writes (`fonts/coure10.txt` ... `fonts/cga40woa.txt`) are committed; `make fonts` builds them with fontc, fontgen runs only when a design changes (`tools/fontgen.py --png DIR` writes specimen sheets).

**Verification:** `tests/fonts.sh` replaces all 13 files and runs FN1 and FN3: on 2026-11-21 both logs equal 3.1's in Standard and 386 Enhanced mode (FN3: 34 fonts, 294 mappings, 13 files' module names, IDs and resource sizes; FN3's version-resource sizes are not compared). With Microsoft's WIN386 and WINOA386 on 3.1 and w16's DOSAPP and grabber fonts in place of Microsoft's, a DOS window starts, its Fonts dialog lists the same sizes (4x6 ... 16x8) and the window in 8x12 and in 12x16 has the same layout as with Microsoft's files (`tests/scripts/input/dosbox386fonts.txt`). w16's WINOA386 (`load_fonts`) now finds the grabber's four files in the tree instead of falling back to the OEM font.

**Vector fonts (`MODERN.FON`, `ROMAN.FON`, `SCRIPT.FON`): not done.** They are a different format: `.FNT` version 1.0/2.0 with the vector bit in dfType, each glyph a list of pen moves rather than a bitmap; GDI scales and strokes them itself (ENUM type 0, any height, OEM charset 255, FN3's enumeration shows them as Roman 32x19, Script 37x17, Modern 32x17). Supporting them would take: (1) fontc: a vector glyph source format (polylines per character, pen-up markers) and the vector `.FNT` layout (character table of offsets/widths into the stroke data, dfType bit 0); fontgen's skeletons are already stroke lists and could be emitted as vector glyphs; (2) GDI: w16's GDI would have to realise a vector font (scale the strokes to the request, emulate bold/italic by offsetting/shearing) and draw text with them as polylines through the driver's Output (no ExtTextOut path), and GetCharWidth/GetTextExtent from scaled advance widths; the mapper (`gdi/font.c`) would have to rank vector fonts against raster ones as 3.1 does (FN1's MAP lines for "Roman"/"Script"/"Modern" and FF_ROMAN/FF_SCRIPT/FF_MODERN requests are the reference); (3) observation: FN3-style dumps at many heights, since a vector font's metrics depend on the request. That is GDI work (in files another agent is changing) and was left out.

### 6.10 Scope: VGA First

For now, w16 targets **VGA (640×480, 16 colours, 96 dpi) only**: `VGA.DRV`, the VGA grabber (`VGA.3GR`/`VGA.2GR`) and the VGA font sets. SuperVGA, 8514/A (120 dpi), EGA and CGA display drivers and their font sets are deferred. Nothing in the design should assume VGA in a way that would prevent adding them later; for example, metrics come from the display driver and are never hard-coded.

**w16's VGA driver** lives in `drivers/vga/` (its README has the driver, its status, the probes and tests; mouse trails there too). Its graphics: the display driver carries the bitmaps, icons and cursors USER draws the interface with (`OBM_*` frame pieces, `OIC_*` message-box icons, `OCR_*` cursors). `drivers/vga/resources.md` lists every resource of the reference driver with its ID, size, format, cell layout and hot spot (format facts, `tools/vgainv.py`, headers only); `drivers/vga/art/` holds w16's own art for each, drawn as text and built by `make vgaart` (`tools/vgaart.py`) into `.BMP`/`.ICO`/`.CUR` files under `build/vga/`.

### 6.11 Design Note: Undocumented Layouts Are Part of the Contract

Programs from the period depend on some internal data layouts that the SDK never documented. When w16's own programs or observed applications rely on one, w16's modules must reproduce it exactly. Known so far:

| Layout | Who relies on it | Evidence |
|---|---|---|
| **HICON memory block** (USER): `GlobalLock(hIcon)` yields a 12-byte header (hotspot x, y, width, height, bytes per row, planes, bits per pixel), then the AND mask, then the XOR bits in the display driver's format. | Program Manager (ours and the original) copies these bytes straight into `.GRP` files (`shell/progman/group.c`, `icon_bits`). Other applications are believed to read it too. | Described in published books on Windows internals. Confirmed black-box: a `.GRP` file written by w16's Program Manager from this block on Windows 3.1 is byte-identical to the one the original Program Manager writes. |
| **`.GRP` file format** | Program Manager; installers and tools that read group files | Observed on Windows 3.1 (`shell/progman/group.c` header comment, `tools/grpdump.py`; written by `tools/mkgrp.py`, checked against Setup's files by `tests/grpref.py`, 9.6). |

So w16's USER must store icons (and probably cursors, which share the format) in exactly this layout. Add rows here as more such layouts turn up.

## 7. Proposed Milestones

1. **M1:** NE loader plus KERNEL running on an existing DPMI host. A console-less test app calls KERNEL exports.
2. **M2:** DOSX equivalent, WIN.COM, SYSTEM/KEYBOARD/MOUSE/DISPLAY (VGA), USER/GDI, `tools/fontc`, and the P0 VGA system fonts (§6.9). A "Hello World" Win16 app built with OpenWatcom runs (Standard mode).
3. **M3:** COMMDLG, SHELL, WIN87EM, TOOLHELP, VER, LZEXPAND, DDEML, OLECLI/OLESVR, MMSYSTEM, WINHELP, PROGMAN. Common 3.1 software runs.
4. **M4:** WIN386 (VMM and the core VxDs), WINOA386, DOS boxes.
5. **M5:** Printing, multimedia drivers, the remaining P2/P3 items.

## 8. Open Items

- **Ordinal tables (largely resolved):** `docs/exports/*.txt` lists, for every Windows 3.1 system module, which ordinals exist, observed black-box with `tests/apps/texp.c` (`GetProcAddress` by ordinal; Standard and 386 Enhanced mode agree), plus the same for Windows/386 2.11. Names come from OpenWatcom's import libraries and Wine's X11-licensed spec files (wine-20020228); `tools/exports.py` regenerates them. Still open: names for some OLE, SHELL and COMMDLG ordinals, and each export's exact semantics. Observed along the way: `GetProcAddress` ignores bit 15 of an ordinal (32769 finds ordinal 1).
- **Windows 2.x reference:** Windows/386 2.11 runs headless (`tests/w386run.sh`): MS-DOS 4.00 built from Microsoft's MIT source (`tools/msdos-build.sh`) boots from an image (`tools/mkhdd.py`) in DOSBox-X. Observed: it has no system MDI client class, no private-profile calls, no `WinExec`, no SHELL.DLL (see `build/ref/tw2-w386.txt` and `docs/exports`). The 2.x builds of w16 programs must work around each.
- **MT-DOS relevance:** 3.1 has no real mode, so the MT-DOS memory manager informs the *semantics* of the handle-based Global/Local APIs (movable, discardable, lock counts, compaction) rather than the selector-based implementation.

## 9. Implementation Notes

### 9.1 KRNL286.EXE layout and boot (M1)

`KRNL286.EXE` has the same shape as the original: an MZ stub followed by the NE module `KERNEL`.

1. **Bootstrap** (the MZ stub, `kernel/boot/`, real mode, small model). It finds its own path from the DOS environment and reads the resident NE tables, every KERNEL segment and their relocation records into conventional memory. It then enters 16-bit protected mode through DPMI (INT 2Fh AX=1687h), creates a descriptor per segment, applies KERNEL's relocations, and far-calls the NE entry point with ES:BX pointing to a `bootinfo` block (`kernel/bootinfo.h`, internal to w16). KERNEL may not import anything.
2. **KERNEL** (`kernel/`, large model, SS ≠ DS, no C runtime). Exported APIs are `far pascal __loadds`. Its startup:
   - registers itself as module 0;
   - hooks protected-mode INT 21h, so AH=4Ch from a task ends that task (and AH=51h/62h give the task's PDB);
   - loads the application named on the command line through the NE loader;
   - makes it the first task (its command tail is the rest of KERNEL's command line) and runs the scheduler from its own stack; when the last task ends, KERNEL exits to DOS with that task's exit code (Windows calls ExitWindows then, as Pietrek describes; w16 has no USER yet).
3. **DOS services** come through INT 21h from protected mode, translated by the DOS extender, exactly as the real KERNEL relies on DOSX/WIN386. The M1 test host is HDPMI16; w16's own DOSX replaces it in M2.
4. **Constant exports** (`__AHINCR`, `__0040H`, `__WINFLAGS`, …). `wlink` cannot export absolute symbols, so these labels are placeholders in `BOOT_TEXT` and KERNEL resolves those ordinals to runtime values before it looks at its entry table. KERNEL has Windows 3.1's four segments (TK62 "kseg"): `_TEXT` fixed, `BOOT_TEXT` and `HEAP_TEXT` moveable and discardable (GlobalFlags 0900h; never discarded), DGROUP. At start-up each becomes a fixed block of the global heap owned by KERNEL's module (`gadopt_kernel`: copied there and its selector moved with `seg_move`), as 3.1's are (KERNEL audit, medium 33; its triage file is in git history).
5. **Unimplemented imports** bind to 8-byte thunks. A thunk reports `w16: call to unimplemented MODULE.ordinal` and ends the task with exit code 0FFh. Loading never fails just because an API is missing. Imports from Windows modules w16 does not provide yet (USER, GDI, KEYBOARD, SOUND, SYSTEM, DISPLAY, MOUSE, COMM: `pending_modules` in `kernel/loader.c`) bind to these thunks too; any other missing module fails the load with 2, as on Windows.
6. **Modules** (`kernel/module.c`, `kernel/loader.c`, `kernel/mdb.c`): the hModule is the selector of the module's in-memory module table in the Windows 3.1 layout (§9.2); beside it, w16 keeps one record per loaded module in its own global block, in a list that starts with KERNEL. Loading a module loads the modules it imports from first (recursively, each found once and shared, with usage counts), relocates it, patches the prologues of its exported procedures and runs a DLL's initialisation routine; freeing the last use of a DLL calls its `WEP` and releases what it imports. Usage counts follow the import closure (§9.2, tasks). `MakeProcInstance` thunks live in the task's TDB (`kernel/procinst.c`).
8. **Tasks** (`kernel/task.c`; layout and behaviour in §9.2): each task has a TDB in DOS memory in the Windows 3.1 layout, with its PDB (a child PSP of KERNEL's, made with DOS 55h) at offset 100h. The scheduler is non-preemptive: a task switch (`task_switch` in `kentry.asm`) saves the registers on the task's own stack and its SS:SP in the TDB, and switches the DOS PSP, the current drive and directory and the FPU control word with it. A task's end runs on a stack of KERNEL's (`exit_stack`), frees what the task owned and runs the next task with an event. `LoadModule` and `WinExec` (`kernel/loader.c`) start programs; another instance of a loaded program gets a copy of its DGROUP as it was loaded.
7. **Directories:** the system directory is the one `KRNL286.EXE` is in. The Windows directory is the `windir` environment variable, which WIN.COM (or LOSE.COM) must pass, as on Windows 3.1 (§9.2). Without it, KERNEL falls back to the parent of the system directory if that is called `SYSTEM`, else the system directory itself (w16's own convention).

M1 limitations, in order of when they should be fixed:
- **Local heap:** implemented (`kernel/local.c`) with Windows 3.1's observed layout. The notify procedure and LocalShrink as TK54 (`tests/apps/tk54.c`, in make test) observed them on 3.1 under both KERNELs: a request that does not fit after compacting and discarding calls the procedure with LN_OUTOFMEM (wArg = the arena bytes needed); TRUE retries, FALSE fails; KERNEL's own procedure grows the heap's segment, so with a program's procedure that answers FALSE the heap never grows. Every discardable block is offered with LN_DISCARD (wArg = its flags byte, 0Fh) and kept when the answer is FALSE (LocalCompact(FFFFh) included); a block compaction moved is reported with LN_MOVE (wArg = its old address). LocalShrink compacts and ends the heap at first + max(cb, the minimum size) + 2 (the last sentinel as LocalInit places it, never below a block in use), reallocates the segment to that and returns last − first + 0Ah. UNVERIFIED: LocalHandleDelta's limits, hMem for LN_OUTOFMEM.
- **Global heap:** implemented (`kernel/global.c`) in Windows 3.1 KRNL286's layout as observed (§9.2, TK19): arenas, Burgermaster, selector table, free list, LRU ring, compaction (moving blocks, also the running task's stack, through `seg_move` in `kentry.asm`), discarding with GlobalNotify. Not yet: KRNL286's placement details (3.1 moves moveable blocks up out of a fixed block's way, keeps discardable code above a fence at the top), THHOOK, reallocating a discarded block to more tiles than it had (GlobalReAlloc returns 0 there). Deliberately different: the heap grows by DPMI chunks on demand instead of taking all memory at start-up, and GlobalCompact/GetFreeSpace count what DPMI can still give.
- **Selectors** (`kernel/selector.c`, `kernel/dpmi.c`): they come from the DPMI host (INT 31h 0000h) and are changed through DPMI (0007h-000Ch). A freed selector goes where 3.1 puts it (§9.2, "Freed selectors"): back to the host at once under KRNL286, on KERNEL's own free list in the LDT under KRNL386, which hands those out first. Selector values differ (KRNL386 does not claim the LDT's free descriptors at start-up nor take 256 at a time in Enhanced mode, Pietrek ch. 1-2), and a fresh `AllocSelector(0)` is a not-present data segment instead of Windows' unusable system-type descriptor. GlobalPageLock does not page-lock (DOSX does not page).
- **Loading:** DLLs load with their dependencies (§9.2 lists what was observed). Floating-point OS fixups are applied as observed (§9.2). Not yet: self-loading modules, demand loading and discarding, network directories in the search. The global heap's block records and the unimplemented-import thunks grow in chunks as needed (no fixed tables).
- **Module and task layout:** module tables and TDBs have the Windows 3.1 layout (some segment flag bits differ; resources are loaded through them, `kernel/resource.c`, but resources larger than 64 KB cannot be reloaded after discarding yet). TDB fields w16 does not fill yet: the signal procedures (24h-2Dh, USER's), the per-task interrupt handlers (32h-4Dh), compatibility flags (4Eh), A8h.
- **Tasks:** without USER there are no message queues (the queue slot and Get/SetTaskQueue are there), so Yield is OldYield and nothing posts events but the tasks themselves; a task waiting with no task holding an event waits forever, as on Windows. Not yet: SetPriority, LockCurrentTask, the lazy PSP switch (w16 switches it with every task), TOOLHELP notifications, terminating tasks from outside, DOS programs (WINOLDAP).

### 9.2 Testing and verification

- `make test` runs each test program under `KRNL286.EXE` in headless DOSBox with HDPMI16 (`tests/dosrun.sh`, `tests/run-tests.sh`); the floating-point probe TK6 runs in DOSBox-X, with a coprocessor and without one.
- `make boot-test` (`tests/boottest.sh`) boots real Windows 3.1 on w16's parts in headless DOSBox and compares every screen with the same boot or the same real input on Microsoft's (`tests/dropin.sh` for a boot and its screenshot, `tests/inputrun.sh` for xdotool input on a virtual display; `make test` boots no Windows). Its harness, described in full at the top of `tests/boottest.sh`:
  - *Reference cache:* the all-Microsoft runs are kept in `build/refcache/NAME/KEY/` and reused while nothing that makes them changes. KEY hashes the run's command line, the contents of every file it names (input scripts, PIFs, WIN.INIs, w16 probes such as WC4.COM used on the reference side, and the build/dos WIN.COM/DOSX/KRNL286 that dropin.sh's -w, -x or a missing -r would take), `DROPIN_*`/`INPUTRUN_*` settings, the reference install's file list (path, size, time), the emulator and tools, the harness scripts, the reference-run code of boottest.sh and the text of the case. Only complete results whose case passed are stored (a completed boot, a script run to its end, no blank screen; a reference taken at a bad moment must not fail every later run); `key.txt` in each entry lists what the key was made from. `BOOTTEST_REFRESH=1` runs every reference again. The w16 side is never cached, nor is a "reference" made mostly of w16 parts (`mouseall`).
  - *Waits on state:* a screenshot is taken when the screen equals the reference's (`dropin.sh -e`, `inputrun.sh -e` for the screen an input script starts from, the reference's `boot.png`), or, with nothing to compare with, once BOOTLOG.TXT has reached Final USER init and the screen has held still 5 s (`-s`; for an input run also not a blank capture and with an active caption: Program Manager up, not just a still screen); the old time is the limit, and a screen that never matches is taken then. Input sessions end when their script ends; `waitexit` waits for WIN to return. Scripts wait for windows with `settle`, `waitpixel`, `waitmoved` rather than fixed sleeps where a loaded host made the sleeps too short, and a shot of a screen without a blinking caret is preceded by `match NAME MAX STILL`: the reference's run waits until the screen has held still for STILL seconds (the old sleep), a w16 run (`inputrun.sh -E` the reference's directory, `-M` what the case leaves out) until its screen has equalled the reference's shot for 1 s, at most MAX, so a repaint under way or a busy program holding the hourglass still is not taken for the state; shots with a caret keep their fixed delay after the last key, which sets the caret's phase. A blank (one-colour) capture is never a settled screen and is taken again. `DROPIN_FIXED=1` and `INPUTRUN_FULL=1` restore the fixed timing for debugging.
  - *Job pool:* `BOOTTEST_JOBS` cases at a time (default 3), results printed in case order with each case's time and a summary. Every DOSBox gets its own Xvfb display (`tests/xvfb.sh`, `Xvfb -displayfd`: `xvfb-run -a` chose display numbers racily, so two runs could share a display and capture each other's DOSBox window); shared references are made once under a lock; retries are appended to `build/boottest-retries.log` under a lock; the cases that took longest in the last run (`build/boottest-times.txt`) start first. Cases made of independent sessions are split into parts scheduled like cases (`allw16nolock@1..6`, each keeping its deliberate boot timing; `pmdrop`, `winfile`, `kbd`, `mouse` per session or script), each case still reported in one line. An emulator that ends at once with "Can't init SDL" (DOSBox-X, 1 in 8 started together, UNVERIFIED why) is started again, at most twice, and logged as a retry. Keys go to DOSBox 50 ms apart (`INPUTRUN_KEYDELAY`; at 12 ms a loaded DOSBox lost keys on both sides). Every boot starts on 15 June 1992 at 10:00 (`tests/dropin.sh` runs `CLOCKSET.COM`, `tests/dos/clockset.asm`, from a drive mounted only for it): a screen never shows the host's date or time, so a cached reference stays valid across midnight (Clock's minimised title showed the date).
  - *Times:* on installation-05 (20 cores, 40 threads) master a5e2a73 took 305-338 s for the boot test at 12 jobs plus 93 s for `make test` after it; now `tools/remote-test.sh` (sync, build, `make test` beside the boot test, 54 cases at 13 jobs) takes 224-227 s in all. The boot test alone: 217-226 s at 12 jobs, about 200 s at 20, 30 and 40 (one 173 s): the longest cases (`kbdall`, `comm`, `woados`, `kbdwoados`, each about 190-210 s, one session after another inside) set the wall time, so more jobs no longer help. `tools/remote-test.sh` picks a third of the host's threads (3 to 16; 13 there). `comm` failed at every job count from 12 up (CM1's out-queue count one off, or a byte of a command missing in the far end's record: the probe's timing against the emulated UART), also with the other cases at nice 10; it passed on a quiet host.
  - *Remote host:* `tools/remote-test.sh [-t|-b] [-j JOBS] [-e VAR=VALUE ...] [CASE ...]` runs the same suites on a build host (default `installation-05`, 20 cores / 40 threads; full boot test about 3.5 minutes): it rsyncs the checkout (a worktree too) to its own directory `/mnt/build/w16-runs/NAME`, links `.tools` to the host's one copy (`/mnt/build/w16/.tools`) and `build/refcache` to a cache shared by all runs (`/mnt/build/w16-refcache`), builds, runs `make test` and/or the boot test, keeps the last 3 result directories there, and copies the summaries and the failing cases' diffs back to `build/remote-NAME/`. At most 3 runs use the host at once (`W16_REMOTE_SLOTS`). The host holds Microsoft's reference files: it must stay private (never a shared or public runner). It needs the same packages as a local build (`openwatcom-v2`, `nasm`, `dosbox`, `dosbox-x`, `xorg-server-xvfb`, `xdotool`, `imagemagick`, `noto-fonts` for the logos, `python-pillow`).
- `make test` runs its probes on HDPMI16 and on w16's DOSX (`RUNTESTS_DPMI="hdpmi dosx"`), each probe in a scratch copy of its directory, `RUNTESTS_JOBS` at a time, with `tests/wincom.sh` (its scenarios `WINCOM_JOBS` at a time) and `tests/fonts.sh` beside them; the output keeps the old order. On installation-05 (20 cores, 40 threads): 92 s for the HDPMI16 probes plus 93 s for the DOSX ones, one at a time, before; 12 s for both now at 16 jobs (10 s at 32; 130 s at 1).
- Test programs print `PASS`/`FAIL` lines for behaviour we are certain of, and `OBS name=value` lines for raw observations to compare between w16 and Windows.
- Code that relies on a recalled-but-unchecked detail is marked `UNVERIFIED:`. `grep -rn UNVERIFIED kernel` lists them.
- A probe can turn its observations into checks: `tests/apps/tk3.c` looks up every `OBS` name in `tk3ref.h`, generated by `tools/obsref.py` from the `TK.LOG` of reference runs in both modes (names whose values differ between the runs are left out; a name only some runs report is compared among those). Values that depend on the program's layout, such as addresses, are printed as `OBS ~name` and checked relative to each other instead.
- Test programs write their results to `TK.LOG` in the current directory with plain INT 21h. On real Windows a task's stdout and `OutputDebugString` are not visible, so a log file is the one channel that works on both systems.
- **Reference systems:** the user supplied disk images of Windows 3.1 (3.10.103) and Windows for Workgroups 3.11a. Both are installed with an unattended Setup (`SETUP /H:` with a settings file) into `.tools/ref/win31/cdrive` and `.tools/ref/wfw311a/cdrive`. `.tools/` is gitignored, so no Microsoft files enter the repository.
- **KRNL286 and KRNL386:** on a 386 or later Windows 3.1 runs **KRNL386.EXE in Standard mode too** (TK19 printed `GetModuleFileName(KERNEL)` = `C:\WINDOWS\SYSTEM\KRNL386.EXE` under `WIN /S`); KRNL286.EXE runs only on an 80286. So every reference run before TK19 observed KRNL386, whose global heap differs from KRNL286's (block sizes, fixed handles, the arena layout; its local heap differs too). `tests/refrun.sh` observes KRNL286 with `REFRUN_KRNL286=1` (copies KRNL286.EXE over KRNL386.EXE in the scratch copy: KRNL286 then runs on the 386, reports the real processor in GetWinFlags, and TOOLHELP still reads its heap) or `REFRUN_CPU=286` (DOSBox-X emulating an 80286; slower). w16's KRNL286.EXE follows KRNL286 where the two differ in the global heap; w16's KRNL386.EXE, built from the same sources, follows KRNL386 (§9.10). A probe whose plain observations differ under KRNL286 gets a second table, `TK_REF286` (`tools/obsref.py --k286 K286LOG [-x REGEX] tkNref.h`), which `tklog.h` consults first when the global heap behaves like KRNL286's (`k286()`: GlobalAlloc(GMEM_FIXED, 1) gives 10h); names given with `-x` (KRNL286 differs where w16 does not follow it) or `-X` (KRNL286 varies between runs) are not checked then. TK3 (global heap values only), TK12, TK15 and TK16 have one; TK1's own checks follow the kernel.
- `tests/refrun.sh PROGRAM.EXE [/S|/3] [ref-dir]` runs a test program on the reference system in headless DOSBox. It makes the program the shell (`shell=` in a scratch copy of SYSTEM.INI), so Windows exits when the program does, then prints `TK.LOG`. This is black-box observation (§3): only the program's own observations are used, and no Windows binary is inspected.
- Windows 3.1 is the authority. WfW 3.11 runs only in 386 Enhanced mode, so where it differs from 3.1 the difference is investigated rather than copied.
- Behaviour confirmed on Windows 3.1 becomes a `PASS`/`FAIL` check. Values that depend on the environment, such as selector numbers and the command tail WIN.COM builds, remain `OBS`.
- First findings, the same in Standard and 386 Enhanced mode unless noted. Several replaced recalled guesses that were wrong:
  - **Initial stack:** the automatic data segment is laid out as static data, then the stack, then the local heap. Initial SP = data size + stack size.
  - **InitTask:** CX and instance word 0Ah equal SP − stack size + 96h. Words 0Ch and 0Eh equal the entry SP. The command tail is NUL-terminated (the CR is replaced), and BX points past its leading blanks.
  - **hInstance:** the handle of the automatic data segment, which is DS with bit 0 clear.
  - **Global handles:** a moveable block's handle is its selector with bit 0 clear, a fixed block's its selector. Under KRNL386 a program cannot have a fixed block once Windows is up: GlobalAlloc(GMEM_FIXED) from a program's code gives a moveable one (even handle, GlobalFlags 0), from a DLL's a fixed one (odd), as Pietrek (ch. 2, GbTop) describes and DRP's `galloc` section shows (10.7); KRNL286 gives programs fixed blocks too. (Until 2026-11-17 w16's KRNL386 gave every block an even handle, a rule taken from TK58, which allocates from a program; MMSYSTEM loads its fixed block's handle into SS for its interrupt stack, so every multimedia timer callback faulted: QuickTime, `docs/notes/quicktime-handover.md`.)
  - **GlobalSize:** rounds up to 32 bytes (KRNL386; KRNL286 rounds arena and block together to 32, GlobalSize being an odd multiple of 10h: see the global heap findings).
  - **Lock counts:** kept only for discardable blocks. GlobalFlags of a locked fixed or moveable block is 0, and of a locked discardable block 0101h.
  - **GlobalAlloc(GMEM_MOVEABLE, 0):** returns a block that is already discarded (GlobalFlags 4000h, size 0, GlobalLock returns NULL). `GlobalAlloc(GMEM_FIXED, 0)` succeeds too (KRNL386; KRNL286 returns 0).
  - **GlobalFree of a non-handle:** returned 0.
  - **GetWinFlags:** 0419h in Standard mode and 0C29h in 386 Enhanced mode, on a 486 with an FPU. That includes WF_80x87, and in Enhanced mode also WF_PAGING.
- Local heap findings (`tests/apps/tk3.c`; Standard and 386 Enhanced mode identical; `kernel/local.c` describes the layout in full):
  - **Task heap:** InitTask makes the heap in the last CX (heap size) bytes of DGROUP, i.e. as `LocalInit(DS, 0, CX)`. Instance word 06h is pHeap, the heap information block; word 08h (the atom table in published descriptions) stays 0.
  - **LocalInit(seg, start, end):** start is rounded up to 4; with start 0 the heap is the last `end` bytes of the segment. A 0Ah-byte busy sentinel arena (padded to 0Ch) comes first, then the information block as the data of a busy fixed arena, 28h bytes under KRNL286 (30h under KRNL386) (pHeap = start + 10h), one free arena, and a last sentinel at (end − 0Ah) & ~3. Returns 1. LocalInit(sel, 16, 40) crashed the program, so tiny heaps are not probed.
  - **Information block** (offsets from pHeap), KRNL286's, which w16 keeps (TK53: TOOLHELP's GlobalEntry sees a local heap by the "LH" at 22h, and only by it): 04h arena count, 06h first sentinel, 08h last sentinel, 0Ah/0Bh two bytes LocalCompact sets (00h/10h, or FFh/00h when it had to try discarding), 0Eh newest handle table, 10h first free handle, 12h handles per table (20h), 14h a near pointer into KERNEL (w16: 0), 18h far pointer into KERNEL (the LocalNotify procedure), 1Eh 0200h (extra bytes when growing), 20h last − first + 0Ah, 22h 484Ch ("LH"); 24h bytes. KRNL386's is 2Ah bytes (the sentinels and the discard total dwords): 0Ah last sentinel, 0Eh/0Fh, 14h table, 16h free handle, 18h delta, 1Eh notify, 24h, 26h, 28h "LH"; w16 had that layout until TK53, and TOOLHELP then found no local heap on w16's KERNEL (TK3 and TK13 check either layout by the global heap's granularity, tklog.h k286()).
  - **Arenas:** 4-aligned, header = previous arena | flags (bit 0 busy, bit 1 moveable), next arena. Free arenas add size, previous free, next free (address-ordered list closed by the sentinels). Arena size = max(0Ch, round4(n + 4)) for fixed, max(0Ch, round4(n + 6)) for moveable blocks; LocalSize is that less 4 or 6. A free arena is split only when at least 14h bytes remain. Fixed blocks come from the lowest free arena that fits, moveable blocks from the top of the highest. Freed blocks merge with free neighbours.
  - **Handles:** a fixed block's handle is its near pointer (a multiple of 4). A moveable handle (2 mod 4) points into a handle table: a count word, 20h four-byte entries (data address, flags byte, lock count byte; a free entry holds the next free entry and FFFFh), and a link to the previous table. LocalHandle(data) gives the handle, LocalHandle of anything else 0.
  - **LocalAlloc:** fixed size 0 fails (returns 0); moveable size 0 gives a discarded block (LocalFlags 4000h). The handle (and a new table) is allocated before the block. Without LMEM_ZEROINIT a block keeps old contents.
  - **Flags and locks:** LocalFlags is the flags byte (LMEM_DISCARDABLE bits >> 8, 40h discarded) << 8 | lock count; 0 for fixed blocks, whose LocalLock returns the handle and LocalUnlock 0. LocalUnlock returns the new lock count. The count stops at FFh and then never changes.
  - **LocalReAlloc:** grows in place into a following free arena; otherwise a moveable block moves (even when locked, and without LMEM_MOVEABLE), and a fixed block moves only with LMEM_MOVEABLE (else 0). Size 0 with LMEM_MOVEABLE discards an unlocked moveable block (discardable or not; locked: 0) and frees a fixed block (returns 0); size 0 without it returns 0. LMEM_MODIFY sets the discardable bits of a moveable block and is ignored for a fixed one. LMEM_ZEROINIT clears the grown part.
  - **LocalFree:** returns 0 always, also for invalid and locked handles (a locked block is freed).
  - **LocalCompact(n):** moves unlocked moveable blocks up into free space above them; discards unlocked discardable blocks only if the largest free arena is still smaller than n; returns the largest free arena less 6. It does not grow the segment.
  - **Growing:** a request that does not fit reallocates the segment (fixed segments too) to round32(GlobalSize + arena size + 200h + 18h); the new space joins the end of the heap. LocalAlloc of FFF0h fails.
- Module and string findings (`tests/apps/tk4.c`; both modes identical):
  - **hModule** is the selector (RPL bits set, so odd) of the module's NE header in memory: word 0 reads "NE". It differs from hInstance.
  - **GetModuleHandle:** names match regardless of case; "TK4.EXE" and "C:\TK4.EXE" find module TK4, "TK4 " does not; `MAKELP(0, h)` with the hModule, hInstance or hInstance | 1 returns the module; NULL and unknown names give 0. How a name is matched: TK60 below.
  - **GetModuleFileName:** the full upper-case path ("C:\TK4.EXE" for a program started from C:\), returning the number of characters copied; at most n − 1 characters plus NUL; n = 0 writes nothing; an unknown handle returns 0 with an empty string.
  - **GetProcAddress:** the name is upper-cased (a-z, and E0h-FEh but F7h; not 9Ah, 9Ch, FFh as Reserved3 does) and must then equal an export's name byte for byte, so an export whose name is in mixed or lower case is found by no spelling (TK65, below); "#3" means ordinal 3 ("#" and decimal digits only, in 16 bits; "#1x", "# 1", "#" give 0), bit 15 of an ordinal is ignored; ordinal 0, gaps, unknown ordinals and names, and the module-name entry give 0; a constant export such as `__AHINCR` comes back as FFFF:value. Until 2026-11-20 w16 ignored case on both sides.
  - **lstrcpy/lstrcat** return the destination. A NULL far pointer does not fault in them: it acts as a small scratch string (lstrlen(NULL) = 2, `lstrcpy(NULL, "x")` returns NULL but later reads of NULL see "x"). w16 treats NULL as an empty string instead (`kernel/string.c`).
- DLL loading findings (`tests/apps/tk5.c` with the test DLLs `tkdll.c`, `tkdlb.c`, `tkid.c`, `tknod.asm`; run on Windows 3.1 by `tests/tk5ref.sh`; both modes identical):
  - **Search order** for a name without a path (a copy in each directory, removed one by one): current directory, Windows directory, system directory, the directory of the running task's program, the PATH directories. No extension is added (`LoadLibrary("TKNOD")` did not find TKNOD.DLL). While Windows loads its shell there is no running task: DLLs the shell imports were not found in the shell's own directory (Windows then waits forever), so `tests/tk5ref.sh` puts the DLLs in the Windows directory.
  - **Loaded modules** are matched by name before any file is opened, whatever the path (`C:\TKNODIR\TKDLB.DLL`, a directory that does not exist, returned the loaded TKDLB), and by `MAKELP(0, hModule)`; the usage count goes up and the initialisation does not run again. How a name is matched: TK60 below.
  - **Results:** the hInstance (DGROUP handle = DS & ~1) of a DLL with data, the hModule of a DLL without (odd), an odd selector other than the hModule for KERNEL. Errors: 2 file not found (also for `""`, and when a module the DLL imports is missing), 3 path not found, 20 (14h) not an NE file (a text file, an MZ file without NE header), 0 when the initialisation returns 0. After a failure nothing of the DLL stays loaded.
  - **Usage counts:** 1 for a DLL loaded once, implicitly or not; each LoadLibrary/LoadModule adds 1, each FreeLibrary/FreeModule takes 1; at 0 the DLL's `WEP` runs with 0 and the module is gone. `GetModuleUsage` takes the hModule or hInstance. `LoadModule` of a DLL behaves like LoadLibrary; FreeModule's AX afterwards is not meaningful (F300h).
  - **Implicit dependencies** load before the program; a dependency's initialisation runs before (and nested deeper on the stack than) that of the DLL importing it.
  - **Initialisation routine (the NE entry point of a DLL):** AX = 1, CX = heap size, DX = DS = the DGROUP selector, DI = hInstance, SI = 0, ES = 0 (no command line), DS:0000–000F all zero and no local heap: the DLL makes it itself (LocalInit). For a DLL loaded by LoadLibrary it runs on the caller's stack; for the shell's implicit DLLs on another stack. BX and BP held leftover values. A DLL without a data segment gets DS = DX = DI = its hModule, CX = 0. When the routine returns 0, the DLL's WEP still runs before it is freed.
  - **Imported modules** are looked for as NAME.DLL only: a DLL importing TKEXEM, which existed only as TKEXEM.EXE, failed with 2.
  - **Exports:** imports and GetProcAddress find names in the non-resident table too. GetProcAddress on a program module (by hModule or hInstance, by name or ordinal) returns 0; with hInstance 0 it looks in the current task's own module and finds its exports (TK65; Pietrek ch. 8).
  - **Names, TK65** (`tests/apps/tk65.c`, TK65D.DLL with exports UPPERRES, MixedRes, lowerres resident and UPPERNR, MixedNr, lowernr not, plus names with C4h, 8Ah, 9Fh, D7h; TK65I1-9.DLL each import one of them by a name; in `make test`, reference `tests/tk64ref.sh`; KRNL286 and KRNL386, both modes alike): GetProcAddress finds UPPERRES and UPPERNR by any spelling, never MixedRes, lowerres, MixedNr or lowernr; E4h finds C4h; 9Ah, FFh, F7h find nothing. Imports by name follow the same rule ("upperres" and "uppernr" resolve, "MixedRes", "MIXEDRES", "lowerres" do not, and the DLL still loads, the import going to KERNEL's undefined-dynalink code) and an import named "#4" is ordinal 4. Every module of the 3.1 and WfW installs, and every w16 module, exports and imports upper-case names only (a scan of the NE name tables; the one exception, the test DLL OSSPY's `_OsPrintf`, is imported by ordinal), so the stricter rule changes nothing for them. `kernel/module.c` mod_find_name.
  - **Prologues of exported procedures:** in a DLL `8C D8 90` (mov ax,ds; nop) and `1E 58 90` (push ds; pop ax; nop) become `B8` + the DGROUP selector; in a program, three NOPs.
  - **MakeProcInstance (protected mode):** called with a program's DS it returns a new 8-byte thunk every time, `B8 <caller's DS> EA <procedure>`, in the task's TDB (see the task findings below); the hInstance argument is ignored (hInstance, hInstance | 1, 0 and a DLL's hInstance gave the same thunk). Thunks are 8 bytes apart; FreeProcInstance makes a thunk the next one handed out. Called with a DLL's DS it returns the procedure itself; with the DS of a global block of no module, a thunk loading that DS.
  - **GetInstanceData** copies from the instance's DGROUP to the same offset in the caller's and returns the count, also for a DLL's instance.
  - `LoadLibrary("")` and `LoadModule("")` return 2 and write the word 2 at the string's offset 2 (bytes 2 and 3, past its NUL; as OpenFile sets an OFSTRUCT's nErrCode): TK60 with a buffer of AAh bytes, KRNL286 and KRNL386. That is what changed the probes' other string literals (in TK60's first run the hex digits its log is written with). GetModuleHandle("") writes nothing. w16 does the same (`kernel/loader.c`, `empty_name`).
  - **Implicit DLLs of a program started later** (TK5 from WIN.INI's `load=` with Program Manager as shell, `REFRUN_SHELL=PROGMAN.EXE tests/tk5ref.sh`): their initialisation runs on the new task's stack, one after the other rather than nested — inside InitTask, as Pietrek's *Windows Internals* describes; only the shell's are initialised during loading. Confirmed for a program started by LoadModule (TK15, below).
  - **Environment:** every task's environment holds lowercase `windir=C:\WINDOWS` (no `WINDIR`), equal to GetWindowsDirectory.
- Finding loaded modules by name (`tests/apps/tk60.c`, in `make test`; `tests/tk5ref.sh OUT TK60`, and `REFRUN_KRNL286=1 tests/refrun.sh` with TKID.TPL for KRNL286: KRNL286, KRNL386 in Standard and in 386 Enhanced mode give the same log). TK60 writes copies of TKID.TPL whose module name differs from the file's (module TKFB in TKFA.DLL, TKFC in TKFB.DLL and in TKFD.DLL, TKFM in a file TKFN without extension, TKFY in TKFX.EXE, TKFN in TKFO.DLL; TK60.EXE is module TKSIXTY) and asks for them in some 200 ways. A module's *file name* is its path's last component; a name's last component is what follows its last `\`, `/` or `:`. A file name *matches* a name when it begins the name's last component, regardless of case (whatever follows is ignored):
  - **GetModuleHandle** compares the whole string with the module names first, regardless of case, then with the file names: "TKFB", "tkfb" find module TKFB; "TKFB.", "TKFB ", "TKFBX", "C:\TKMN\TKFB", "C:TKFC", "TKFC.DLL", "KERNEL.EXE" and the kernel's file name less its extension find nothing; "TKFA.DLL", "tkfa.dll", "C:\NOWHERE\TKFA.DLL", "D:\TKFA.DLL", "C:TKFA.DLL", "C:/TKMN/TKFA.DLL", "\TKFA.DLL", "C:\A.B\TKFA.DLL", "TKFA.DLLX", "TKFA.DLL.", "TKFE.DLL " and "TKFA.DLLXXXXXXXXXXXXXX" find module TKFB (file TKFA.DLL), "TKFB.DLL" module TKFC (not TKFB), "KRNL386.EXE" (or KRNL286.EXE) KERNEL, "TK60.EXE" the program; "TKFA", "TKFA.", "TKFA.DL", "TKFA.EXE", "TKFA.DLL\X" and "TKFA.DLL/" find nothing; for the file TKFN, "TKFN", "TKFN.", "TKFNX", "TKFN.DLL" and "C:\TKMN\TKFN" find it. With a module named TKFN loaded as well, "TKFN" finds that one (module names first, though it was loaded later). Two modules of one name: the first in the list. Handles: an hModule, an hInstance, hInstance | 1, the hModule with RPL 0 and a code selector find the module; an hTask and NULL give 0. The result is the hModule (odd, "NE" at offset 0, GetExePtr's).
  - **LoadLibrary and LoadModule** (and so WinExec) compare the file names first, then the base name (the last component up to its first dot) with the module names (Pietrek, LMAlreadyLoaded: FindExeFile, then FindExeInfo): with only TKFA.DLL loaded, "C:\TKMN\TKFB.DLL", "TKFB.DLL", "TKFB", "TKFB.EXE", "TKFB.XYZ" and "C:\NOWHERE\TKFB" give module TKFB, and TKFB.DLL is not read; "TKFA", "TKFA." and "TKFA.EXE" load nothing (2); "TKFA.DLLX" finds TKFA.DLL's module; "TKFX.DLL" gives 2 (base TKFX is no module name, TKFX.EXE is not a prefix); "TKSIXTY.DLL" gives 5 (the program found); "KERNEL.DLL" gives KERNEL; with a module TKFN and the file TKFN both loaded, "TKFN" and "TKFN.DLL" find the file TKFN's module.
  - **A file read is a module of its own** even when a module of its name is loaded: TKFD.DLL (module TKFC) loaded while TKFB.DLL's module TKFC is gives a second module TKFC, each used once; GetModuleHandle("TKFC") and LoadLibrary("TKFC.DLL") then find the first. The names are compared only before a file is opened.
  - w16 before (until 2026-11-18): GetModuleHandle and LoadLibrary took the base name of any name as a module name, and a library whose module name was loaded gave that module. Now `kernel/module.c` (`module_find_file`, GetModuleHandle) and `kernel/loader.c` (`find_loaded`) do as above. Imports are module names (NAME.DLL is read when none is loaded), as before.
  - `tests/run-tests.sh` runs TK5, TK11 and TK60 in the same layout as the reference runs (the DLLs in `C:\WINDOWS`, `C:\TKPATH` on the PATH via `DOSRUN_PRE`; `tests/refrun.sh` has `REFRUN_PRE` for the same), with KERNEL started through the test tool `WINENV.COM` (`tests/dos/winenv.asm`), which passes `windir` as WIN.COM does. KERNEL is in `C:\W16SYS` there, so the Windows directory can only come from `windir`.
- Module table findings (`tests/apps/tk11.c`, run by `tests/tk5ref.sh OUTDIR TK11`; both modes identical). The hModule's segment holds the layout published in Pietrek's *Windows Internals* (1993), Table 3-1, which the probe confirmed field by field; `kernel/mdb.c` builds it:
  - 02h usage count (= GetModuleUsage); 06h next module table (a list from KERNEL that holds every module; a module's table is linked before those of the modules it imports); 08h pointer to DGROUP's segment table entry; 0Ah pointer to the load file information (length byte = 8 + length of the name, fixed-media byte 1, the full path as GetModuleFileName gives it).
  - 0Ch flags: the file's, plus 0040h on every module (meaning unknown) and 4000h on DLLs (WEP to be called).
  - Segment table right after the 40h-byte header, 10-byte entries ending in the segment's global handle. The segment flags change too: w16 sets the documented "allocated", "loaded" and moveable bits; 3.1 also changed bits 0400h and F000h and cleared 0100h of a data segment, which is not understood (UNVERIFIED, not checked).
  - 28h module references hold the hModules of the imported modules; 38h and 3Ah repeat 2Ah (not 3Ah for KERNEL).
  - Entry table in memory: bundles {first ordinal − 1, last ordinal, next bundle} followed by 5-byte entries {FFh or segment number, flags, segment, offset}; ordinals that are not exported (gaps) have no bundle.
  - 18h of a program's table (initial SS:SP; 0 in the file) held the initial SP (2E10h) for TK11 in both modes, but 2434h and 2444h in two earlier runs of the probe (as TK6) whose initial SP was 2DF4h. Not understood; w16 leaves the file's value (UNVERIFIED).

- Resource findings (`tests/apps/tk12.c` with `tk12.rc`, run by `tests/refrun.sh`; Standard and 386 Enhanced mode identical; implemented in `kernel/resource.c`). The resource table layout matches the one published in Schulman et al., *Undocumented Windows* (1992), chapter 5:
  - **Table:** the file's resource table, in the module table right after the segment table. Each type's third and fourth words hold a far pointer to its handler; after loading every type has the same one (KERNEL's default handler). Each resource's entry is {offset, length, flags, id, handle, usage}; flag 0004h means the data is in memory.
  - **FindResource:** returns the offset of the resource's entry in the module table, or 0 (missing name or type, id 0, `""`, a NULL hInstance). hInstance or hModule both work. `"#n"` is the integer n, read as decimal digits up to the first other character, into 16 bits (`"#003"` = 3, `"#1x"` = 1, `"#65537"` = 1; `"#"` finds nothing); `"1"` without `#` is a name. Names and named types: the query is upper-cased and compared exactly with the table's string, so a lower-case name in the table never matches.
  - **PRELOAD** resources are read while loading: they have a handle with usage 0 and flag 0004h clear.
  - **LoadResource:** if the entry has a handle: usage + 1, flag 0004h set unless the block is discarded, and the same handle (a discarded block stays discarded). Otherwise a discardable resource gets a discarded block (GlobalFlags 4100h, size 0) without the handler being called, and any other resource is read by calling the type's handler with (0, the hInstance passed, hRsrc); if that returns 0 so does LoadResource and nothing changes. hRsrc 0 gives 0.
  - **LockResource:** GlobalLock, but a discarded resource is first reloaded by its type's handler, called with (handle, hModule, hRsrc) into the same handle; if the handler returns 0, LockResource returns NULL. Lock counts are those of the global heap (discardable blocks only). `LockResource(0)` is NULL.
  - **FreeResource:** usage − 1; while it stays above 0 the result is the handle, else 0. At 0 a resource that is not discardable is freed (handle 0, flag 0004h cleared); a discardable one stays in memory until FreeResource is called again at usage 0. A handle that belongs to no resource (AllocResource, GlobalAlloc) is freed, returning 0.
  - **Memory:** a discardable resource's block has GlobalFlags 0100h (4100h when discarded), any other 0; handles are even; sizes round to 32 as for GlobalAlloc. `GlobalReAlloc(h, 0, GMEM_MOVEABLE)` (GlobalDiscard) discards an unlocked discardable resource and returns h.
  - **SizeofResource:** the table's length shifted by the resource shift, so rounded up to the alignment (a 6-byte resource with shift 4 gives 10h); 0 for hRsrc 0.
  - **AccessResource:** a new DOS handle of the module's file each call, at the resource's offset, read-only (writing gives error 5).
  - **AllocResource(h, hRsrc, size):** a new block of `size` bytes, or the resource's size when size is 0 (10 bytes for a 1008-byte resource gave 20h); discardable when the resource is; the table is not changed.
  - **SetResourceHandler:** installs the handler for a type (named or integer, as FindResource takes them) and returns the previous one; 0 for a type the module does not have.
  - **Default handler:** called directly with hMem 0 for a resource whose entry holds a (discarded) handle, it read the data into that handle and returned it, leaving usage and flag 0004h alone.
  - Not observed (UNVERIFIED in the code): the file's sharing mode, AccessResource's failure value, non-ASCII names, handlers for KERNEL's own module, discarding blocks that are moveable but not discardable, and DirectResAlloc (KERNEL.168, which *Undocumented Windows* describes; not implemented).
- Floating-point findings (`tests/apps/tk6.c` with `tk6fp.asm`, `tk6n.c`, the arithmetic probes `tk7.c` and `tk8.c` (with `TK8T1`-`TK8T5`), `tk9.c` (exports), `tk10.c` (fixup records) and `tk67.c` (the WIN87EM audit's cases, in parts `TK67P1`-`TK67P12`, with the helper task `tk67h.c`; `TK67P13` on 86Box, below); run by `tests/tk6ref.sh` in DOSBox-X with a coprocessor and without one (`fpu=false`), both modes; Standard and 386 Enhanced mode identical; implemented in `kernel/loader.c` (`os_fixup`), `kernel/kentry.asm` (`int3d_handler`) and `win87em/`):
  - **Reference environment:** DOSBox's FPU keeps no exception flags in the status word and cannot be switched off, so these probes run in DOSBox-X (`REFRUN_X`, `DOSRUN_X`). Neither emulator raises floating-point exceptions (IRQ 13; checked with a DOS program), so how Windows 3.1 delivers an unmasked exception *with a coprocessor* is not observable there; 86Box is used for that (`tests/86box.sh`, `tests/86box-n4.sh`, `TK67P13`; "IRQ 13" below). With a coprocessor DOSBox-X reads control word bit 6 as 1; the probe ignores that bit.
  - **OS fixups:** OpenWatcom (`-fpi`) references `FIxRQQ` symbols at each floating-point instruction; wlink turns them into additive NE relocations of type 3 (OSFIXUP) with t1 = 1 (`FIARQQ`/`FJARQQ`, DS prefix), 2 (SS), 3 (CS), 4 (`FIERQQ`, ES), 5 (`FIDRQQ`, FWAIT + escape), 6 (`FIWRQQ`, NOP FWAIT), one record per instruction at its FWAIT. The symbols' values come from OpenWatcom's `emu87.lib`; `tests/apps/fixrqq.asm` defines them for programs without a run-time library.
  - **With a coprocessor** the loader turns the FWAIT of types 1-5 into a NOP (`9B D9 C0` -> `90 D9 C0`) and type 6 into INT 3Dh (`90 9B` -> `CD 3D`). INT 3Dh works in a program that does not import WIN87EM (WIN87EM is not loaded for it), so KERNEL handles it (an FWAIT).
  - **Without one** the loader adds the FIxRQQ value at the FWAIT and, for types 1-3, the FJxRQQ value one byte further: `CD 34`-`CD 3B` (escape D8h-DFh) followed by the ModR/M byte, `CD 3C` + the escape byte with the segment in its top two bits (DS 00, SS 01, CS 10, ES 11), `CD 3D` for a lone FWAIT. WIN87EM's emulator handles those; a program that does not import WIN87EM computed a wrong result and Windows hung at its exit. No code is patched back after it runs, in either case.
  - **Before any `__fpMath` call** a task's control word is 037Fh with a coprocessor and 1332h with the emulator.
  - **`__fpMath`** (register interface; BX = function; SI, DI, DS, SP kept; BX, CX, DX otherwise changed): 0 initialises (FINIT, control word 1332h, the coprocessor gets 1330h; AX = 0, CF = 0; the handler is not changed); 1 and 2 do the same with CF = 1 (CF = 0 with the emulator); 3 sets the calling task's exception handler DX:AX (one per task, see "Two tasks" below; AX = 253Eh afterwards); 4 sets the control word AX (the coprocessor gets AX & FF3Ch, i.e. invalid and denormal unmasked; the emulator all of AX) and returns AX & FF3Ch; 5 returns the control word last given to 4 (with the emulator: its control word); 6 rounds ST to an integer with the rounding control AX & 0C00h (AX = that control word; with the emulator 1, 0 for a zero, and for a NaN or an infinity that control word & FF00h (`TK67P12`); a NaN, even a signaling one, raises nothing; with precision unmasked a non-integer calls the handler with 86h, `TK67P7`); 7 pops ST into DX:AX as a 32-bit integer rounded with AX & 0C00h (out of range 8000:0000h; the 3.1 emulator also changes SI and DI, w16's does not; with the emulator and invalid unmasked an out-of-range value or a NaN calls the handler with 81h, `TK67P2`, `P6`, `P8`; an empty stack gives 0:0, no handler call, nothing popped, `TK67P9`, after which the 3.1 program hung). A handler called from 6 or 7 gets WIN87EM's DS both in DS and in the word after its return address (an instruction's exception has the program's DS there), and in every such case the 3.1 program ended after the handler returned; w16 passes WIN87EM's DS too and returns from `__fpMath`; 8 returns the exception flags (status & 3Fh with a coprocessor; with the emulator every flag raised since the last 9 or 0, including bits 6 and 7); 9 clears the exceptions (AX = 0, CF = 1 with a coprocessor); 10 returns the number of registers in use; 11 returns 1 with a coprocessor, 0 without, DX kept; 12 keeps AX (meaning unknown); see below for 13 and up. The caller's direction flag is kept (functions 5 and 10 called with DF set returned with it set, `TK67P5`); w16's entries clear DF for their own code and put the caller's back (`__Win87EmSave` called with DF set ended the 3.1 program; an emulated FLD and FSTP with DF set worked).
  - **`__Win87EmInfo(buf, n)`:** n < 12 returns FFFFh and writes nothing; otherwise 12 bytes: 0600h, the save area size 01CDh, WIN87EM's data and code selectors, 1 with a coprocessor (0 without), 0; returns 0. **`__Win87EmSave` / `__Win87EmRestore(buf, n)`:** n < 01CDh returns FFFFh and does nothing; otherwise 0. With a coprocessor the area starts with an FSAVE image (which reinitialises the coprocessor); with the emulator its first 94 bytes are not written; WIN87EM's own state follows (w16 uses its own layout there).
  - **Emulator status word:** TOP always reads 0; a stack underflow sets IE only (and calls the handler with 8Bh, see below); ES is never set except for the square root of a negative number (81h). FINIT is not emulated: it sets bit 6 (0040h), leaves the control word, and calls the handler with 87h whatever the masks. Nor are FSTENV, FLDENV, FSAVE and FRSTOR (`TK67P1`, with one to three values on the stack and with images of TOP 5 or 6): each does the same, writes nothing to its image and takes nothing from it, and the values on the stack stay as they were. (w16 emulated them until the WIN87EM audit, wrongly for any TOP but 0.)
  - **Exceptions (emulator):** an unmasked exception calls the handler set by function 3 after the instruction, with AL = the code (83h zero divide, 87h unemulated, 88h square root of a negative number, which is reported even when masked), AH varying between runs, WIN87EM's DS, and on the stack the far return address and then the program's DS (OpenWatcom's handler reads its DS there). The handler returns with RETF and the program continues. **NaNs and integer stores** (`TK67P1`): a quiet NaN operand of add, subtract, multiply, divide (memory or register) or compare raises IE (a coprocessor raises it only for compare); FSQRT of a quiet NaN raises IE and calls the handler with 88h masked or not, the NaN the result; FRNDINT, FST/FSTP to memory and FCHS of one raise nothing; a NaN keeps its sign through FSUB and FSUBR, as the subtrahend too; FLD m32/m64 of a signaling NaN raises nothing, masked or not, and leaves it signaling (a coprocessor would raise IE and quiet it). FIST/FISTP with invalid unmasked stores the integer indefinite, pops (FISTP), and calls the handler with 81h; a valid FIST afterwards stores normally though IE stays set. w16 does all of these the same way.
  - **Arithmetic** (`tk7.c`: add, subtract, multiply, divide both ways, compare, square root, FRNDINT, FIST to 16 and 32 bits, FST to 32 and 64 bits, on twelve operand pairs including overflow, underflow, a denormal, zeros and a division by zero; the four rounding controls and three precisions; constants and integer loads; all exceptions masked): the 3.1 emulator gives the same 80-bit results and flags as the coprocessor except that -0.0 loads as +0, 0/0 sets ZE as well as IE, FRNDINT of 0 sets PE, the condition codes read 0 after FLDCW + FCLEX, and its square root of 1e308 is one unit in the last place above the correctly rounded result. w16's emulator matches the 3.1 emulator in all of these except that square root (it rounds correctly).
  - **Unusual fixup records** (`tk10.c`, rewritten after linking by `tools/nechain.py`): the additive flag of an OS fixup record does not matter and records are never followed as chains. A record without the flag whose site held the offset of another site patched only its own site, as a number (04BCh became 04B1h with a coprocessor, 60EEh without); the other site was left alone. With a coprocessor the loader lowers the first byte by 0Bh (9Bh -> 90h) rather than storing 90h.
  - **`__fpMath` above 12** (`tk9.c`, 13-24): AX = DX = FFFFh, CF = 0, BX and CX kept, nothing else changed, with and without a coprocessor.
  - **Ordinal 2** (`tk9.c`): takes one word argument (it pops two bytes; 0, 1 and 200h behaved the same) and reinitialises like function 0 (register stack emptied, control word 1330h); AX = 0, CX = 0, and BX, SI, DI came back as 01A6h in every run (an internal value, it seems; w16 leaves them). Called with nothing pushed and all registers 0 it ended the program. Without a coprocessor the program ended at its next floating-point instruction afterwards; w16 resets its emulator instead (UNVERIFIED: what the word means).
  - **Instructions the 3.1 emulator does not emulate** (`tk8.c`): FBLD, FBSTP and FXTRACT, like FINIT (handler called with 87h, nothing else changes). It does emulate F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN (in a fresh state; `TK8T1`-`TK8T5` run one each) with 8087 semantics: results within a few units in the last place of the exact value (up to 6 for FYL2XP1), FPTAN leaves an unnormalised pair Y, X (Y / X = tan) instead of tan and 1.0, and FPATAN gives nonsense outside the 8087's range 0 <= Y < X. PE is set unless the result is exact. Special operands (`TK67P3`): FYL2X of +-0 gives a finite K (C00D 800D 00CD 7E83 6B27h, about -16390) times Y with PE only, no ZE or IE and no handler call even unmasked (+0 for Y = +-0); FYL2X of an unnormal is taken by its value (the unnormal 0.5 gave about -1 with PE: not taken as exact); FYL2XP1 of a denormal gives 0 with UE and PE, of the smallest normal 0 with PE; FPATAN with X = +-0 gives the indefinite with IE and ZE, with Y = +-infinity the indefinite with IE, else with Y = +-0 or X = +-infinity +0 and no flag (where the 387 gives the angles). w16 does these the same way. A coprocessor (DOSBox-X) gives correctly rounded results and the 387's FPTAN. w16 emulates these within about two units, FPTAN in the 387's form, FPATAN with the 387's quadrants; the flags match, the last bits do not.
  - **FPREM** (emulator): the remainder is complete in one go whatever the exponents (C2 never set; the low quotient bits in C0, C3, C1); a zero divisor gives +0 with no exception. **FSCALE** (emulator): no exception flags at all; the scale is the top 16 bits of ST(1)'s significand shifted right by (15 - its unbiased exponent) & 15, negated if ST(1) is negative (so 0.9 scales by E666h = -6554). w16 does both the same way.
  - **Square root** (emulator): of 24 doubles and the integers 2-61, 12 results are one unit in the last place above or below the correctly rounded one (and PE was missing for two inexact results). They are not explained by the exact value's position between its neighbours (the wrong ones lie 0.26-0.67 of a unit above the lower one, but others there are right). A Newton iteration y = (y + x/y) / 2 with every step rounded to nearest at 64 bits reproduces 80 of the 83 results; the square roots of 51, 61 and 1e308 still differ, and no variant tried (other precisions, truncation, rounding up, mixed) fits all. So no rule was found: w16 rounds correctly, and these values are shown, not checked. The square root of +infinity is invalid (the handler gets 88h).
  - **Stack faults** (emulator): an FSTP to memory from an empty stack stores the register's stale contents (0 with exponent 3FFFh for a register not used since function 0), sets no flag, and calls the handler with 8Bh although IE is masked; the reference program ended soon after. w16 reports 8Bh for every stack underflow whatever the masks (but `__fpMath` 7's, above).
  - **Deep stack** (emulator): the register stack holds more than eight values; twenty pushed came back intact, with no flag set; the handler was called once with 8Ah (stack overflow) when the depth first passed 14 (at the fifteenth push). With a coprocessor the ninth push sets IE and leaves eight. `__Win87EmSave` keeps the registers below ST(7) as they are (`TK67P1`: twelve saved, all popped, twelve others pushed and popped, restored: the first twelve came back). w16 spills the registers below ST(7) to a block of WIN87EM's own (GMEM_SHARE) that grows as needed, and its save area keeps the 19 nearest ST(7) (words 106-255 and 260-299, their number at 300; w16's layout).
  - **Two tasks** (`TK67P4`, `P10`, `P11`, each starting `TK67H.EXE`): the emulator's state is one for all tasks: a second task saw the first's control word (137Fh) and its two values before its own `__fpMath` 0, which reset them for the first too, and after the second ended (with `__fpMath` 2 or without) the first had the second's control word and no values; with a coprocessor KERNEL's per-task control word (TDB 14h) gave the first its own hardware control word back (133Ch) while function 5 returned what the second set last. The handler of function 3 is the task's own: the first task's exception reached its handler while the second, with another handler, was running and after it ended, with `__fpMath` 2 or without, and the second's exception reached the second's. Nine values pushed after the other task had pushed nine and ended all came back. w16: one emulator state; the handlers in a table of {task, its PDB, handler} (a block of WIN87EM's, growing as needed), an entry freed by `__fpMath` 2 or once its task is gone or its handle belongs to a task with another PDB. Until the audit w16 had one handler for all, which outlived its task (the next exception of another task far-called freed code), and the spill block belonged to the task that first spilled and went with it, leaving a dead selector (the audit's N1).
  - **Save area** (`tk8.c`): after the 94-byte FSAVE part, words 94/96 hold the handler (emulator only), 98 1 with a coprocessor, 100 the control word of function 4, 104 that & FF3Ch, 256 function 12's word; the rest held selectors, addresses and values of unknown meaning and was zero from 304 on. w16 writes the known words there and keeps its own state from 304.
  - **IRQ 13** (with a coprocessor): not raised by DOSBox or DOSBox-X. 86Box raises it (`tests/86box.sh`: an AMI 386DX board with a 387, MS-DOS 4.00, the reference install; `TK67P13`, `tests/86box-n4.sh`, Standard and 386 Enhanced mode alike). With Microsoft's WIN87EM, 1/0 with zero divide unmasked (the 1330h of function 0) then an FWAIT'd instruction: the handler of function 3 was not called and the program wrote nothing more; no message, Windows ran on (whether the task ended or hangs is not yet told apart). w16 implements the documented AT path: WIN87EM hooks INT 75h, clears the coprocessor's BUSY latch (port F0h), sends EOI to both PICs, takes the highest-priority unmasked exception from the status word (81h invalid, 8Ah/8Bh stack fault, 82h-86h the others), clears the flags and calls the task's handler as the emulator does; on 86Box it called it with 83h, the program's DS on its stack, and the program went on (a difference from 3.1, kept until what 3.1 does is known). **The audit's N4** (`__fpMath` 7 on a coprocessor, `fpm_pop32`, whose FISTP runs with invalid unmasked): on 86Box Microsoft's and w16's WIN87EM gave the same, an out-of-range value or a NaN returning 8000:0000h with the value left on the stack, no flag and no handler call. 86Box's 387 is not exact there, though: a plain FISTP of 5e9 with invalid unmasked stored the integer indefinite, set no flag and did not pop (Intel: nothing stored, the exception raised), so whether a real 387 returns the previous call's DX:AX instead stays UNVERIFIED; w16 is unchanged there.
  - **Still open:** the 3.1 emulator's own rounding of square roots and transcendentals (not reproduced bit for bit; see above); what 3.1 does after a coprocessor exception's handler returns, and an invalid FISTP on a real 387 (86Box's is not exact, above); what ordinal 2's argument means; w16 also exports `WEP` as ordinal 6 (UNVERIFIED that Windows has none); the 387-only FPREM1, FSIN, FCOS, FSINCOS are treated as not emulated (UNVERIFIED).

- Atom findings (`tests/apps/tk13.c`; both modes identical; implemented in `kernel/atom.c`):
  - **Table:** word 08h of the caller's DS points to it; the first AddAtom *or FindAtom* makes it. A fixed local block: the bucket count, then that many bucket heads (LocalSize 2 + 2n). Default 37 buckets (also `InitAtomTable(0)`). InitAtomTable returns the table's near pointer (nonzero); with a table already there it returns that one and changes nothing. The size is computed in 16 bits: `InitAtomTable(-1)` failed (0); `InitAtomTable(8000h)` made a 2-byte table with count 8000h and then wrote past it (w16 fails instead).
  - **Entries:** fixed local blocks of 6 + length bytes: next entry in the bucket, usage count, length byte, the name (as first added) and a NUL. A string atom is C000h | (entry >> 2); GetAtomHandle(atom) = atom << 2 without any check (a deleted atom gives its old handle), 0 for integer atoms. New entries go to the end of their chain. Names longer than 255 characters and `""` give 0.
  - **Hash:** h = rol(h, 1) + h + ror(h, 1) + c over the case-folded characters (16 bits, from 0); bucket = h mod n. Fitted to 62 names in tables of 37, 8191 and 8192 buckets, all reproduced. Comparisons and the hash fold a-z and E0h-FEh (but F7h) down by 20h.
  - **Counts:** AddAtom of an existing name adds 1, FindAtom does not; DeleteAtom returns 0 and frees the entry when the count reaches 0. DeleteAtom of an integer atom returns 0; of a string atom that is not one, an unrelated nonzero value (04EFh in Standard, 0297h in Enhanced mode), but 0 for C000h and FFFFh; w16 returns the atom there (UNVERIFIED that anything depends on the value).
  - **Integer atoms:** `MAKEINTATOM(n)` gives n below C000h, else 0. `"#"` followed only by digits (none at all included) is the number mod 10000h, and 0 if that is C000h or more (`"#70000"` = 1170h, `"#65536"` = 0, `"#"` = 0); anything else after `#` (`"#12a"`, `"# 12"`, `"#-1"`, `"#1 "`) is a string atom.
  - **GetAtomName(atom, buf, n):** copies at most n − 1 characters and a NUL and returns the number copied; n = 0 writes nothing. Integer atoms read as `"#123"`; atom 0 and invalid string atoms give `""` and 0.
- Profile findings (`tests/apps/tk14.c`, run by `tests/tk14ref.sh`; both modes identical; implemented in `kernel/profile.c`):
  - **Files:** a name without a path is the Windows directory only (not the current or system directory); a relative path is from the current directory; any write creates the file (also a delete in a file that did not exist: an empty file). A directory that does not exist makes the write return 0.
  - **Caching:** WIN.INI and one private file (the last used) are cached. Each call looks for the file (a deleted file reads as empty), but a cached file is not read again when it changes on disk, even with a new size or date, until another private file is used or `WritePrivateProfileString(NULL, NULL, NULL, file)` / `WriteProfileString(NULL, NULL, NULL)` (both return 0) drops it. Writes go to the file at once (the whole file is rewritten).
  - **The cached copy is normalised on reading:** blanks (space, tab) at the start of each line are removed, and on lines not starting with `[` the blanks around the first `=`; a Ctrl-Z at the very end goes. Writes insert their text into the copy unnormalised, so `" b4"` written as a key cannot be read back until the file is read again; the copy, normalisation included, is what is written back.
  - **Lines:** LF ends a line (CR LF too); a lone CR does not. A NUL ends a value but not the line. 1500-character values come back whole.
  - **Sections:** a line starting with `[` and containing `]`; the name ends at the first `]`, text after it is ignored, a line without `]` is no header. Names are not trimmed in the file (`[ Spaced ]` is not `Spaced`); queries are trimmed both sides (`" Sect1 "` finds `[Sect1]`); case does not matter; only the first of two sections of one name is used (reading and writing).
  - **Keys:** the text before the first `=` (`;key9=x` is found as key `;key9`, but not listed; `=v` is key `""`); lines without `=` are ignored.
  - **Values:** trailing blanks removed, then one pair of enclosing `"` or `'`; the default goes through the same (`"  def  "` gives `"  def"`, `"\"def\""` gives `def`). n − 1 characters and a NUL are copied, the count returned; n = 0 means no limit.
  - **Key lists** (key NULL): each name with its NUL, then a NUL; the return value counts all but the last NUL. If n < total + 2, the first n − 2 bytes are copied, bytes n − 3 and n − 2 are set to NUL and n − 2 is returned. A missing section gives the default. A NULL section gives the default (3.1 does not list sections).
  - **GetProfileInt:** missing key: the default; otherwise blanks, an optional `-`, an optional `0x` or `x` (hexadecimal), another optional `-`, digits to the first other character, 16 bits: `-5` FFFBh, `0x1F` 1Fh, `x5` 5, `0x-5` FFFBh, `-0x10` FFF0h, `012` 12, `  42abc` 42, `99999` 869Fh, `+7`, `abc`, `"5"` and `""` 0.
  - **Writing:** the value ends at its first control character and loses trailing blanks; a key or section name loses trailing blanks only. An existing key keeps its spelling, only the value is replaced. A new key goes after the section's last non-blank line (comments count); a new section after the file's last non-blank line, with a blank line before it (at the start of a file of blank lines). A file not ending in CR LF still does not afterwards. NULL value deletes the key's line; NULL key deletes the section up to the next header. The writer assumes CR LF: in a file with bare LFs it inserts at wrong places (w16 does not copy that). A NULL section with a key wrote a garbage section name on 3.1; w16 returns 0.
- File findings (`tests/apps/tk14.c`; both modes identical; implemented in `kernel/file.c`):
  - **OpenFile search** for a name without a path (a copy in each directory, removed one by one): current directory, Windows directory, system directory, the program's directory, PATH. A relative path with a directory is not searched (error 3). `_lopen` does not search. OF_DELETE searches too.
  - **OFSTRUCT:** cBytes = 8 + the length of szPathName (not the structure size); fFixedDisk 1 for C:; reserved = the file's DOS date, then time; szPathName the full upper-case path. On failure only nErrCode (2 not found, also after a search; 3 no path) and szPathName (the name in the current directory) are written.
  - **OF_PARSE** returns 0: drive, current directory, `.` and `..` resolved, `/` as `\`, upper case, wildcards kept. Bad names (more than 8 + 3 characters, blanks, no name, a drive that is not ready) return −1 with nErrCode 2 and a partial path that w16 does not reproduce exactly. OF_EXIST and OF_DELETE return the handle number used (closed); OF_CREATE truncates and gives a read/write handle. OF_REOPEN|OF_VERIFY fails (−1) when the date and time in the structure differ from the file's.
  - **_l functions:** HFILE_ERROR (FFFFh) for errors and bad handles; `_lclose` 0; `_lread` at the end 0; `_lwrite` of 0 bytes truncates; `_llseek` with origin 3 gives FFFFFFFFh. `_hread`/`_hwrite` move more than 64K at once. Sharing, attributes and seeks before the start are left to DOS.
  - **GetTempFileName:** `TEMP\~PPPXXXX.TMP` (prefix: first 3 characters, case kept; XXXX the number in upper-case hex); with TF_FORCEDRIVE `C:~PPPXXXX.TMP`; a nonzero number makes no file; 0 picks a number (it varied between runs) and creates the file. TEMP was used only when its directory existed when Windows started, else the Windows directory. **GetTempDrive** returned `'C'` with AH = `':'`.
  - **GetDriveType** (in DOSBox): A: 2, B: 0, C: 3, Z: 3, others 0. **SetHandleCount:** at least 20, at most 255, never shrinks; returns the count.
- Task findings (`tests/apps/tk15.c` with its helper `tk15a.c`, and `tk16.c` with `tk16a.c` (TK16A, TK16B, TK16C); run by `tests/tk5ref.sh OUTDIR TK15` / `TK16`, which also copies a probe's helpers; Standard and 386 Enhanced mode identical; implemented in `kernel/task.c`, `kernel/loader.c`, `kernel/procinst.c`). The TDB layout is the one published in Pietrek's *Windows Internals* (1993), Table 3-2, and the scheduler behaves as his chapter 6 describes; the probes checked each field and rule used:
  - **TDB:** a 200h-byte block below 1 MB (paragraph aligned); hTask is its selector, and `GlobalHandle(hTask)` is hTask itself (odd, unlike other handles). 00h next task (0 ends the list), 02h SS:SP of a switched-out task, 06h event count, 08h priority (0), 0Ch the TDB's own selector, 14h FPU control word (037Fh), 16h/18h 0, 1Ah expected Windows version (0300h, the NE header's), 1Ch hInstance, 1Eh hModule, 20h message queue (0 without USER), 22h the parent task (for the shell a selector that is not a task), 60h PDB selector, 62h DTA (PDB:0080h), 66h 80h + drive (82h for C:), 67h the directory as `\WINDOWS`, AAh DirectedYield's task, B0h a code alias of the TDB, B2h-F1h MakeProcInstance thunks, F2h the module name (8 bytes, no NUL when full), FAh `TD`. The PDB is at offset 100h (selector base = TDB base + 100h); a task's PDB has KERNEL's PSP as parent (16h), which GetCurrentPDB returns in DX; the handle table is in the PDB (20 handles); FCB 1 holds 2 and nCmdShow.
  - **Task APIs:** GetCurrentTask returns the first task of the list in DX. GetNumTasks counts tasks (1 for the shell alone). IsTask returns its argument when that is a task's selector (also with other RPL bits), else 0 (hInstance, hModule, PDB, 0). GetTaskDS is the hInstance. GetTaskQueue(0)/(hTask) read 20h; SetTaskQueue returns the old value. GetExePtr gives the hModule for an hInstance (also a second instance's) or hModule, 0 for an hTask or 0. INT 21h AH=51h and 62h return the task's PDB. Every task shares KERNEL's environment selector (GetDOSEnvironment, PDB 2Ch); a LoadModule parameter block with an environment gives the task a copy (a new 20h-byte block for 10 bytes), which was still allocated after the task had ended.
  - **Events and scheduling:** PostEvent(0 or hTask) adds an event (AX afterwards = hTask); WaitEvent(0) takes one and returns 0, and while there is none it lets other tasks run. Yield (no queue) and OldYield add an event to the caller while the scheduler looks, so a task with no other task holding an event continues at once. The scheduler takes DirectedYield's task if it has an event, else the first task in the list with an event; the task switched to moves behind the other tasks of its priority, a new task goes in front of them. The traces of TK15's modes A-C (`tk15ref.h`) follow these rules exactly, e.g. the second instance in a Yield loop ran before the first and the parent ran last; a task's end hands over the same way. A new task starts with 1 event; a parent waiting in LoadModule has 2.
  - **Starting a program:** LoadModule (and WinExec) return the hInstance after the new task has run until it yielded or waited (or ended). The new task's registers: AX = 0, BX = stack size, CX = heap size, DX = 0, SI = hPrevInstance, DI = hInstance, BP = 0, ES = PDB, DS = SS = DGROUP, SP as for the shell. InitTask returns AX = the PDB (not 1), BX past the tail's leading blanks, CX the stack limit, DX = nCmdShow (LoadModule's or WinExec's), SI, DI, ES = PDB. The tail is LoadModule's as given (leading blank and all); WinExec drops one blank after the program name (three gave two), adds `.EXE` to a name without extension and searches as LoadLibrary (the caller's program directory included). A task starts in the current directory of the one that started it, and its directory stays its own: a task's `chdir` did not change the other's, and the TDB of a switched-out task holds its own.
  - **Implicit DLLs of a program started later** are not initialised before InitTask; InitTask initialises them, dependencies first, one after the other at the same SP on the task's stack (AX = 1, CX, DX = DS, DI = hInstance, ES = 0 as for LoadLibrary). When one fails (TKBAD), the task ends inside InitTask (what follows InitTask never runs), LoadModule/WinExec still returned an hInstance, the DLL's WEP runs and the program and the DLL are freed. (w16 ends the task with exit code F0h, as Pietrek describes; UNVERIFIED.)
  - **A second instance** of a loaded program shares the code segments, gets its own DGROUP, PDB and TDB, and hPrevInstance (SI, also from InitTask) = the first instance's hInstance; GetInstanceData from it works. Its DLLs are not initialised again. GetModuleUsage: 2 for the program and for every DLL in its import closure (TKDLL and TKDLB both, although TKDLB is imported by the program and by TKDLL; with one instance both were 1): each module is counted once per load, which w16 follows for every load and free. A second instance of a program with two writable data segments fails with 16.
  - **Errors:** LoadLibrary of a program (loaded or not) and LoadModule with the parameter block -1 give 5; a missing file 2, a missing directory 3.
  - **A task's end** (INT 21h AH=4Ch): the blocks it allocated (GlobalAlloc, also in a DLL it called) and its DGROUP are freed, its module with the last instance (GetModuleHandle 0), its implicitly loaded DLLs when no one else uses them (WEP); a DLL it loaded with LoadLibrary stays loaded (usage 1). w16 also closes the files of its PDB (UNVERIFIED on Windows).
  - **MakeProcInstance thunks** are in the TDB, through its code alias (B0h): B2h next thunk segment, B4h `PT`, B6h 0, B8h the first free slot, then seven 8-byte thunks from BAh. A slot is a thunk's offset + 6; a free slot's word holds the next (a new TDB: C0h → C8h → … → F0h → 0). The eighth thunk goes to a new 40h-byte segment (its selector, also the thunks', at B2h) of the same format from offset 0 (thunks from 8). FreeProcInstance zeroes the thunk's first 6 bytes and puts its slot first, so it is the next one handed out; after freeing all ten, the next thunk was the last freed (EAh). UNVERIFIED: where a third segment goes (w16: the end of the chain).
  - Not observed (UNVERIFIED in the code): the registers Windows keeps across a task switch (w16 keeps BP, DS, ES, SI, DI, BX, CX, DX), WaitEvent's result after waiting, SetPriority, the stack depth at which InitTask calls the DLLs (3.1: entry SP − 3Ch at the DLL's entry; w16 differs), which instance becomes hPrevInstance with three, and what happens when the shell ends while other tasks run (on Windows the system exits). A difference of 20h in a task's heap position noted while testing TK13 was not reproduced: DGROUP size, initial SP and pHeap matched Windows 3.1 for every layout tried (programs with up to 512 data relocations and 4A4h bytes of BSS); a probe's own size changes when its reference table is regenerated, so such values are only compared relative to each other.
- Global heap findings (`tests/apps/tk19.c` with `tk19n.asm`, run with `REFRUN_KRNL286=1` and `REFRUN_CPU=286`, identical but for tile limits on the 286 (low word FFFFh); implemented in `kernel/global.c`, which describes the layout in full). The structures are the KRNL286 ones published in Schulman et al., *Undocumented Windows* (1992), ch. 5 and Pietrek, *Windows Internals* (1993), ch. 2; the probe checked every field it uses, and on Windows checks TOOLHELP's view against the arenas:
  - **Arena:** 10h bytes right below the block, with a selector of its own (limit 0Fh): 00h 'M' (fixed and free blocks, sentinels) or the lock count (moveable blocks), 01h owner (PDB, module, the TDB itself for a TDB; 0 free, FFFFh sentinel, FFFDh Burgermaster, FFFCh "not there"), 03h data size in paragraphs, 05h flags (04h DGROUP, 08h discardable code, 40h GMEM_NOTIFY), 06h/08h previous/next arena, 0Ah handle, 0Ch/0Eh the free list (free blocks) or the LRU ring (discardable blocks). Arena and block together are an even number of paragraphs: GlobalAlloc(1) gives GlobalSize 10h, (100) 70h, (1000h) 1010h, (10000h) 10010h.
  - **Burgermaster** (GlobalMasterHandle: AX = DX, a fixed block): 04h arena count, 06h/08h first/last sentinel, 12h 20h, 1Ah newest discardable block, 1Ch LRU count, 22h free count, 24h/26h FFFFh; the selector table at 72h (THHOOK 16h/18h: 3FFCh, 72h; one word per LDT entry: the arena of a block's first selector, the owner of a discarded block, else 0). It is the first block, after the first sentinel (owner FFFFh, size 1, previous = itself, 0Ch FFFFh); the last sentinel has 'Z', next = itself, 0Eh FFFFh. Pieces of linear memory are bounded by "not there" arenas of size 0.
  - **Handles:** fixed blocks' handle = selector (odd), moveable = selector − 1. A program's fixed segments and every resource are moveable. A discarded block keeps its handle: descriptor not present (access 73h), limit = owner, selector table = owner; GlobalHandle 0. A block over 64K has one arena and a selector per 64K; tile i's limit is size − i·10000h − 1 (on a 386); only the first is in the selector table and known to GlobalHandle.
  - **GlobalFlags** = flags << 8 (0400h DGROUP, 0800h discardable code; not GMEM_NOTIFY) | 0100h discardable | 4000h discarded | a moveable block's count. Discardable blocks are those in the LRU ring, GMEM_FIXED | GMEM_DISCARDABLE ones too (which GlobalReAlloc(h, 0, GMEM_MOVEABLE) does not discard).
  - **Counts:** GlobalLock/Unlock count only for moveable discardable blocks (GlobalUnlock returns the new count); GlobalFix/Unfix for every moveable block, not fixed ones; GlobalWire = GlobalFix + moving the block lower; GlobalUnwire returns FFFFh when the count reaches 0, else 0. GlobalFree frees locked blocks too and returns 0 always.
  - **LRU:** a new or MODIFY-made discardable block becomes the head (newest); GlobalLRUOldest/Newest move a block to the tail/head. Discarding takes the oldest first; when memory ran out, all three unlocked discardable blocks were discarded at once, the locked one stayed.
  - **GlobalNotify:** stored at TDB 2Eh (a pointer with a bad selector is ignored); called with the handle and DS = ES = SS before discarding a GMEM_NOTIFY block of the task, also for GlobalReAlloc(h, 0, GMEM_MOVEABLE); 0 keeps the block (GlobalReAlloc then returns 0). A discarded block reallocated loses GMEM_NOTIFY.
  - **GlobalReAlloc:** a fixed block blocked by its neighbour does not grow (0) unless GMEM_MOVEABLE, then it moves keeping its selector; a moveable block grown in front of another moveable block stayed in place (the other moved); a block grows in place into free space after it.
  - **GlobalCompact(n):** moves moveable blocks down into free space; returns the largest free block's size less 30h (not counting one next to the discardable-code fence); (−1) discards every unlocked discardable block. GetFreeSpace(0): free and discardable blocks (arenas included) less the paragraphs reserved for discardable code.
  - **TOOLHELP** (which reads these structures): GlobalInfo.wcItems = Burgermaster count + 1, wcItemsFree and wcItemsLRU its counts; GlobalFirst/Next give dwAddress = the arena's address, dwBlockSize = GlobalSize, wcLock = arena byte 0 ('M' = 4Dh for fixed blocks), wFlags = arena flags | 0100h (GF_PDB_OWNER) for a PDB's block, dwNext = the next arena's selector (0 after the last); GlobalEntryHandle's hOwner is the task for a PDB's block.
  - Not observed (UNVERIFIED in the code): what KRNL286's GlobalCompact(n) discards for other n, GetFreeSpace's flag, LockSegment, the meaning of Burgermaster 0Ah-0Ch, 1Eh, 20h and 28h-71h, GMEM_SHARE blocks of a DLL, a DLL's fixed segments.

- Selector findings (`tests/apps/tk17.c`, built twice: TK17, and TK17A with its huge-block part, which Windows could not finish after TK17's other steps; run with `tests/refrun.sh` under KRNL386 in both modes and under KRNL286 (`REFRUN_KRNL286=1`; TK17A also with `REFRUN_CPU=286`); tables `tk17ref.h`/`tk17ref286.h`, `tk17aref.h`/`tk17aref286.h`; the same everywhere where not noted; implemented in `kernel/selector.c`, huge blocks in `kernel/global.c`; w16 passes both under HDPMI16 (`make test`) and under DOSX as the shell of the drop-in boot, `tests/dropin.sh -f build/dos/TK17.EXE=TK17.EXE -i 'boot:shell=C:\TK17.EXE' -o DIR`, TK.LOG then in DIR's WINDOWS):
  - **Constants:** `__0000H`, `__0040H`, `__A000H`, `__B000H`, `__B800H`, `__C000H`, `__D000H`, `__E000H`, `__F000H` are LDT selectors (RPL 3) of present, writeable, not yet accessed data segments (F2h) with base = paragraph × 16 and limit FFFFh (`__0040H` too: base 400h, limit FFFFh, not 2FFh). `__ROMBIOS` is the same selector as `__F000H`. `__AHINCR` = 8, `__AHSHIFT` = 3. `__WINFLAGS` = GetWinFlags(). GetProcAddress gives them as FFFF:value. GlobalHandle of a constant selector is 0. Selector 40h itself is valid (its limit is the DOS extender's: FFFFh under DOSX, 2FFh under WIN386).
  - **Rights:** a program's code segment is FBh, its data and every global block F3h (accessed set); SelectorAccessRights reads 1Ah and 12h.
  - **AllocSelector(0)** gives a selector the CPU rejects until something gives it rights: Windows marks it in use with type 0Fh at DPL 0 (its limit word is a free-list link). SetSelectorBase/Limit do not change that. DPMI cannot make such a descriptor; w16 gives a not-present data segment, and the probe does not compare what the CPU says about such selectors.
  - **AllocSelector(sel)** copies sel into (limit >> 16) + 1 selectors 8 apart; tile i gets base + 64K·i and limit − 64K·i (e.g. a 31230h-byte block under KRNL286: 3122Fh, 2122Fh, 1122Fh, 122Fh). A copy is not a block (GlobalHandle 0) and writes through it are seen through sel. A freed selector is the next one AllocSelector hands out.
  - **AllocSelectorArray(n):** n present data selectors (F3h), base 0; limit 0 under KRNL286 (KRNL386: left over from its free list).
  - **FreeSelector** returns 0. It frees (limit >> 16) + 1 selectors of a valid selector (only the first of an AllocSelectorArray with small limits; all three after SetSelectorLimit(2FFFFh)). The selector of a GlobalAlloc block frees the block (GlobalSize 0 afterwards). Freeing a free selector also returned 0 but put it on Windows' free list twice (the next two AllocSelector(0) gave the same selector), and Pietrek describes a not-present segment being freed as a run counted by its base (UNVERIFIED, not probed); w16 does neither.
  - **Get/SetSelectorBase** work on the LDT slot sel & ~7 whatever the TI and RPL bits (GetSelectorBase(40h) is that of 47h); SetSelectorBase stores all 32 bits and returns sel. **SetSelectorLimit** stores the low 20 bits as they are (FFFFFFFFh gives FFFFFh, 100000h gives 0) without touching the granularity bit, and returns 0. **GetSelectorLimit:** KRNL286 reads the descriptor (a free selector gave its free-list word); KRNL386 is LSL's 32-bit limit, 0 when LSL fails. w16 reads the descriptor.
  - **SelectorAccessRights(sel, 0, x)** = (byte 6 & D0h) << 8 | (byte 5 & 1Eh), 0 when LAR fails. Any other op (1, and 2 too) sets byte 5's bits 1Eh and byte 6's bits D0h from x, keeping the present bit, DPL and the accessed bit, and returns 0.
  - **AllocDStoCSAlias(sel or handle):** a new selector with sel's base and limit as code (F3h → FBh); the block gets a GlobalFix lock (GlobalFlags 1 for a moveable block, still 1 after the alias is freed; a fixed block shows 0 under KRNL286, 1 under KRNL386). The alias is not a block (GlobalHandle 0). AllocDStoCSAlias(0) is 0; of a free selector it still returned a selector. **AllocCStoDSAlias** and **AllocAlias** make a writeable data copy (FBh → F3h); they are different entry points.
  - **PrestoChangoSelector(src, dst)** copies src into dst with the code bit toggled and returns dst as passed (KRNL386: dst | 1); with src 0 it returns 0 and leaves dst.
  - **LongPtrAdd(sel:off, n)** adds n + (off & FFF0h) to the base of sel only and returns sel:(off & 0Fh) (sel:1234h + 10h: base + 1240h, offset 4; sel:5 + 3: base + 3, offset 5). KRNL286 returns 0 and changes nothing when n is 1 MB or more (FFFFFh worked, 100000h did not), so for "negative" n too; KRNL386 took up to FF0000h and failed for 7FFF0000h, FFFFFFF0h. UNVERIFIED: whether n or n + (off & FFF0h) is limited. LongPtrAdd of a tiled copy stopped Windows; not probed.
  - **GlobalDOSAlloc(n)** returns paragraph:selector; the selector is also the handle (odd, GlobalHandle gives it in both words); base = paragraph × 16 below A0000h; GlobalSize as for GlobalAlloc; F3h, limit GlobalSize − 1; not cleared. Over 64K it is tiled like GlobalAlloc's blocks. 0 and 1 MB give 0. **GlobalDOSFree** returns 0; it also frees a GlobalAlloc block, and GlobalFree frees a GlobalDOSAlloc block. Calling GlobalDOSFree(0) and on a freed selector made Windows stop soon after (not probed further).
  - **Lock calls:** GlobalFix and GlobalUnfix leave AX = the handle passed. GlobalWire returns (handle | 1):0; GlobalUnWire returned FFFFh. LockSegment/UnlockSegment return the handle and count only for discardable blocks (GlobalFlags of a moveable block stays 0). **GlobalPageLock** under KRNL286 adds a GlobalFix lock and returns 1 every time (0 for 0), **GlobalPageUnlock** takes it off and returns 0; under KRNL386 they return a page-lock count (1, 2, then 1, 0) and GlobalFix also counts for fixed blocks. GlobalHandleNoRIP = GlobalHandle.
  - **FS, GS and ES on the way out (TK61 "gfree", "live"):** KRNL386's GlobalFree returns FS = GS = 0, whatever they held (the freed block, or DS; also DRP's `galloc` section, 10.7); KRNL286 leaves them, also when they hold the freed block. KRNL386 also returns FS = 0 (GS kept) from GlobalSize, GlobalFlags, LockSegment, UnlockSegment, GlobalFix, GlobalUnfix, GlobalPageLock and GlobalPageUnlock, not from GlobalLock, GlobalUnlock, GlobalHandle, GetSelectorBase or IsTask; both kernels return ES = 0 from LockSegment to GlobalPageUnlock in that list, and IsTask keeps ES. FreeSelector keeps FS and GS on both (ES = 0). w16: `kentry.asm` AFTER_FREE, `api_fs0`/`api_es0` (`kernel/kernel.h`) in those functions, IsTask a KAPI_ES function.
  - **Freed selectors (TK61, `tests/apps/tk61.c`; Microsoft's KRNL286 in Standard mode, KRNL386 in Standard and 386 Enhanced mode; w16's DOSXT trace and the probe's own view of the host's free chain, `C:\TK61DIAG.FLG`):**
    - *KRNL286* gives every freed selector back to DOSX at once (INT 31h 0001h): the next 0000h hands it out again, and its descriptor is DOSX's free one (access byte 0, byte 6 0, the base kept, limit 0-15 the link of DOSX's chain). It keeps no free list of its own. The order of the frees, which DOSX's last-in-first-out chain turns into the reuse order: GlobalAlloc takes the selector of the rest's arena first, then the block's; GlobalFree gives back the arena of a free block merged after it, then its own arena (merged into a free one before it), then the block's selectors, tile 0 first; a GlobalReAlloc that shrinks a block frees the tiles it no longer needs before it makes the rest's arena (which gets the last of them). w16 did the block's selectors first and made the arena before freeing the tiles (`kernel/global.c` free_block, GlobalReAlloc).
    - *KRNL386* keeps them on its own list (Pietrek ch. 2, Get_Sel and Free_Sel) and never gives them back to the host: the descriptor is access byte 0, byte 6 0Fh (not free to the DPMI host either: WIN386's free ones are all zero, DOSX's have byte 6 0), base 0, and limit 0-15 the LDT offset of the next free one, FFFFh at the end (in Standard mode the list was 6 long at the probe's start, in Enhanced mode 1F2h). The last one freed is the first handed out, to GlobalAlloc and AllocSelector(0) alike; a huge block's tiles are freed tile 0 first, so they come back in reverse; AllocSelectorArray(n) takes the first one on the list that starts a run of n free ones going up (UNVERIFIED in Standard mode, where 3.1 took others for 3). GlobalCompact gives none back. w16 (`kernel/dpmi.c` list_take/list_put): the same, written through the LDT's own selector (INT 2Fh 168Ah "MS-DOS", 0100h; `kernel/k386c.c` k386_init); without it (a host without the vendor API) freed selectors go to the host, Pietrek's rule when KERNEL has no LDT selector. Freeing a selector already on the list does nothing on w16 (3.1 put it on twice, TK17).
    - *Both:* after any free (GlobalFree of fixed, moveable and huge blocks, FreeSelector, a GlobalReAlloc that drops tiles, freeing a discarded block, FreeLibrary, a task's end) LAR, LSL, VERR and VERW fail on the selector. A discarded block's selector stays: not present (73h), the AVL bit set (byte 6 10h; w16 now too), limit = the owner.
    - *A freed selector left in FS or GS:* Microsoft's DOSX faults reloading it at the next timer tick and at the next INT 21h ("Standard Mode: Fault in MS-DOS Extender", 005B:0B58, the error code the selector), under both kernels; INT 31h leaves FS as it was. WIN386 loads 0 instead. So 3.1's KRNL286 ends Windows when a task ends with one of its blocks in FS (TK61's helper task; the probe skips that step under KRNL286), while KRNL386 survives: it zeroes FS and GS before the task's blocks go (as its GlobalFree; w16 `kernel/task.c` task_exit_c). That is what ended Windows when QuickTime's Movie Player closed on w16's KRNL286 (`docs/notes/quicktime-handover.md`, "Closing the player"; QuickTime itself does not run on 3.1's KRNL286). w16's KERNEL now does what 3.1's does (the earlier stopgap, `dpmi_free_desc` zeroing FS or GS, is gone), and w16's DOS extenders do not end Windows there: w16's DOSX (`dosx/ring0.inc` fsgs_restore) leaves such a selector in the register when nothing changed it (INT 31h, as Microsoft's DOSX) and loads 0 otherwise (as WIN386; Microsoft's DOSX faults there), and w16's WIN386 loads 0 (`win386/vmm/vmmasm.asm` fixsel). Deliberate difference: Microsoft's DOSX ends Windows where w16's goes on.
  - **Huge blocks:** GlobalAlloc over 64K gives tiles 8 apart; GlobalHandle and GlobalSize of tiles after the first give 0. **Tile limits:** on a 386 (KRNL286 and KRNL386) tile i's limit is GlobalSize − 64K·i − 1, so each reaches the end of the block (2A350h bytes under KRNL286: 2A34Fh, 1A34Fh, A34Fh); on an emulated 80286 (`REFRUN_CPU=286`) every tile but the last has FFFFh, the most a 286 descriptor holds. GlobalReAlloc to more tiles always moves the block to a new selector run, so the handle changes, and keeps the data (the new memory is not cleared); a fixed block grows that way only with GMEM_MOVEABLE under KRNL286 (KRNL386 also without). Shrinking to fewer tiles keeps the handle and frees the tiles beyond.
  - **The KERNEL audit (TK62, `tests/apps/tk62.c`, in make test; every finding's verdict is in git history: `docs/notes/kernel-audit-triage.md`, removed once the audit was complete):** an outside review's findings were checked against 3.1 before w16 changed behaviour, and its crashes became regressions. Observed on 3.1 (both modes, KRNL286 where it differs): IsBadReadPtr/IsBadWritePtr of 0 bytes are 0, NULL too; an expand-down segment is good at every offset; IsBadStringPtr wants the NUL within the first max bytes ("abc" bad with 3, good with 4, max 0 always bad) except that max FFFFh sets no bound. AllocSelector(0) given data rights by SelectorAccessRights stays unusable under KRNL386 and becomes readable under KRNL286 (w16's KRNL286 does that). OpenFile takes only a letter as a drive ("1:", "@:" fail with 2) and no ':' in a name ("COM1:", "FOO:"). LockSegment(-1)/UnlockSegment(-1) work on the caller's DS (`kentry.asm` SEGLOCKAPI passes it). KERNEL.79/80 (AnsiUpper/Lower) also pair 8Ah/9Ah, 8Ch/9Ch, 9Fh/FFh. Kept as 3.1 has them: AddAtom's count and the TDB's event count are words that wrap (10001h adds or posts leave 1); SetTaskSignalProc and GetAppCompatFlags read and write any segment as a TDB; WinExec("") returns 2; profile defaults lose their quotes; a value written with leading blanks keeps them until the file is read again; a line "=v" gives an empty key. Not matched yet: GetCodeHandle of KERNEL's code (3.1: its segment's handle; w16 0, as GlobalHandle of it: KERNEL's segments are not blocks of w16's heap) and Catch's layout (3.1's last word written is the ninth). Where 3.1 itself fails, w16 survives: a module whose entry table does not fit a 64K module table fails to load (3.1's KRNL286 ended Windows; TKENT.DLL, `tools/mkne.py --entries`), lstrlen/lstrcpy/lstrcat return 0 for a string with no NUL before its segment's end or a copy over its own source (3.1 faults and recovers to 0), and a task's end that empties a piece of the heap, a block of FFFD0h bytes, GlobalReAlloc with the LDT exhausted, DLL import chains 32 deep and profile files that cannot be opened (other than missing) no longer corrupt or lose anything.

**SNAP.EXE, interactive** (`tests/apps/snap.c`, header comment): every scenario script runs as before (C:\SNAP.TXT, or a script named on SNAP's command line). Started with no script (from Program Manager, File > Run, WIN.INI run=), the Windows 3.1 build opens a window of standard controls (title "SNAP", icon `tools/snapicon.py`, our own art): a command line run through the same dispatcher as script lines, a target (the window that was active before SNAP's, or one picked with Pick) that commands acting on "the active window" activate first, buttons for Snap (SNAPnnnn.BMP in the current directory, never overwriting; SNAP hides while it takes the screen), Dump, Info and Run..., a log pane with what each command logs (the TK.LOG lines; the whole log is kept and File > Save Log writes it), File > Open Script (a script with its progress in the log; Stop ends it), and Help > Commands and About (ShellAbout). COMMDLG and SHELL are loaded only when used, so script mode imports neither. SNAP2 (Windows 2.x) stays script-only and carries the icon. Checked with real input (`tests/inputrun.sh`) on the reference 3.1 and on w16's USER and GDI.

### 9.3 Additional permitted sources and tools

| Item | Status |
|---|---|
| OpenWatcom headers and import libraries (`h/win`, `lib286/win/windows.lib`) | Allowed (open source, Sybase OWPL): API names, ordinals, constants and structure layouts as facts. Used directly only to build test programs. |
| HDPMI16 (HX DOS extender, freeware, "may be used for any purpose") | Test-only DPMI host, fetched into `.tools/` by `tools/fetch-hdpmi.sh`; never committed or redistributed. |
| DOSBox | Test-only emulator. |

### 9.4 Drop-in KERNEL: booting Windows 3.1 with w16's KRNL286.EXE

Goal: copy `build/dos/KRNL286.EXE` over `WINDOWS\SYSTEM\KRNL286.EXE` of a Windows 3.1 install, run `WIN /S` (Microsoft's WIN.COM, WSWAP and DOSX), and have Windows run on it with Microsoft's drivers, GDI, USER and shell. `tests/dropin.sh` does this in a scratch copy of the reference install and reports how far the boot gets (`-r` runs the reference KERNEL for comparison, `-s` takes a screenshot, `-i` edits SYSTEM.INI, `-f` adds files).

**The DOSX → KRNL286 contract** (observed with `tests/dos/tk18.asm`, TK18, in place of `SYSTEM\KRNL286.EXE`; Pietrek ch. 1 describes the same):
- `WIN /S` runs `SYSTEM\WSWAP.EXE`, which runs `SYSTEM\DOSX.EXE` with the tail `krnl286    ` plus what is left of WIN's own command line; DOSX starts KERNEL with INT 21h 4B00h as an **ordinary real-mode DOS program** (MSW = FFF0h, PE clear). There is no special entry: the registers are what DOS gives any program. KERNEL itself switches to protected mode through DPMI (INT 2Fh 1687h), exactly as w16's MZ stub already did, so the same KRNL286.EXE runs under HDPMI16 and DOSX.
- On a 386, DOSX starts `KRNL386.EXE` if it exists and `KRNL286.EXE` only if not (observed: with only KRNL286 replaced, Windows came up on Microsoft's KRNL386). `tests/dropin.sh` therefore deletes KRNL386.EXE in its scratch copy.
- KERNEL's command tail is WIN's, with the switches WIN.COM handled blanked out: `WIN /S /B C:\FOO.EXE arg1  arg2` gives `     /B C:\FOO.EXE arg1  arg2`. /B is KERNEL's (BOOTLOG.TXT); the rest becomes the shell's command tail. Every PSP in the chain has `windir=C:\WINDOWS` in its environment (lowercase name).
- PSP chain: KRNL286 (parent DOSX) ← DOSX (parent WSWAP) ← WSWAP (parent WIN.COM) ← WIN.COM ← COMMAND.COM. DOSX keeps 1Dh KB of DOS memory; KERNEL gets the rest (530 KB free in DOSBox with memsize=16).
- DPMI: INT 2Fh 1687h gives AX=0, BX=0 (16-bit clients only), CL=4 (DOSBox's CPU), DX=005Ah (version 0.90), SI=0 (no host data), entry 0A09:0930 (inside DOSX). After the switch: CPL 3, LDT selectors (CS=02E7, DS=SS=027F, ES=PSP selector 02DF with limit 100h, environment selector in PSP:2Ch), LDT selector 0038h in the GDT, TR=0088h. INT 31h 0400h: AX=005A, BX=000A, CL=4, DX=0870 (PIC bases 08h/70h); 0003h: 8.
- INT 31h functions that work: 0000-0009, 000A, 000B, 000C, 0200-0205, 0300, 0400, 0500 (largest free block 15 MB with 16 MB), 0501, 0502, 0600, 0601, 0604, 0702, 0703, 0800, 0900-0902. **Not:** 0100/0101 (DOS memory: CF set, AX=8, BX=0 although DOS has 530 KB free), 0305/0306 (no raw mode switch), 0A00 (vendor API through INT 31h). 0507 and 0E00 make DOSX fail with "Insufficient extended memory to run Windows in standard mode" on the screen. INT 21h AH=48h from protected mode returns a selector to *extended* memory (base above 1 MB). Real-mode INT 21h through 0300h works. w16 therefore emulates 0100h-0102h with real-mode 48h/49h/4Ah when the host refuses 0100h (`kernel/dosx.c`).
- The Microsoft vendor extension: INT 2Fh 168Ah with DS:SI="MS-DOS" returns AL=0 and an entry point (0053:1CCD); calling it with AX=0100h returns the LDT's own selector (009Bh, limit FFEFh: 8190 descriptors) with CF clear. DOSX marks free LDT descriptors with access byte 80h and chains them through the limit field.
- Exit: DOSX and WIN.COM print nothing when KERNEL exits with INT 21h 4Ch from protected mode, whatever the exit code (0, 1, FFh tried).
- INT 2Fh 1600h returns AL=0 under DOSX (no Enhanced mode), INT 2Fh 4300h AL=80h (XMS present). DOSX hooks real-mode INT 22h, 24h, 2Fh; WSWAP INT 1Ch, 21h, 2Ah, 5Ch.

**The boot sequence** (`kernel/winboot.c`; the reference `BOOTLOG.TXT` of `WIN /S /B`, TK22 as the shell, and Pietrek ch. 1, BootStrap/SlowBoot/LoadNewExe, for the order):
- w16 boots Windows when the Windows directory's SYSTEM.INI has `[boot] system.drv=`; otherwise KERNEL runs the program on its command line as before (§9.1).
- Order: `[boot]` system.drv, keyboard.drv, mouse.drv, display.drv, sound.drv, comm.drv, fonts.fon, oemfonts.fon, then GDI.EXE and USER.EXE, then `shell=` (default PROGMAN.EXE). GDI's initialisation loads FONTS.FON (found as the loaded module FONTS), the fixedfon.fon file and OEMFONTS.FON; USER's loads DISPLAY.drv (the loaded DISPLAY) and the WIN.INI [fonts]; the shell's implicit imports (SHELL.DLL) are loaded with it.
- `BOOTLOG.TXT` (with /B): `[boot]`, then `LoadStart = NAME` / `LoadSuccess = NAME` around every load while booting, under the name as given (the [boot] value, `GDI.EXE`, `NAME.DLL` for an implicit import); a failure is `LoadFail = nosuch.drv Failure code is 02` and the screen shows `Error loading nosuch.drv`, and Windows exits. USER writes its `INIT=…`/`INITDONE=…` lines through DiagOutput (KERNEL.340), which appends its string as it is (TK22: no CR/LF added). Loads after the boot are not logged (TK22's LoadLibrary calls).
- The shell is loaded but not run before KERNEL calls USER's FinalUserInit (USER.400): TK22's DiagOutput lines come after `INITDONE=Final USER`. The remaining INIT lines (Installable Drivers, TSRQuery) come from USER after that.
- A boot task (Pietrek: a TDB with KERNEL's PDB, one event, module -1) is current while the boot modules initialise; without it USER's message queue (`SetTaskQueue(0, …)`) had no task and USER spun at boot. It is deleted before the scheduler starts.
- TK22 on 3.1 at shell time: modules loaded KERNEL, SYSTEM, KEYBOARD, MOUSE, DISPLAY (VGA.DRV), SOUND (MMSOUND.DRV), COMM, FONTS, OEMFONTS, FIXFONTS, GDI, USER; not MMSYSTEM (loaded later, at Installable Drivers), not SHELL; usage counts KERNEL 11, SYSTEM 4, KEYBOARD 4, MOUSE 2, DISPLAY 3, SOUND 2, COMM 2, FONTS 2, OEMFONTS 2, FIXFONTS 1, GDI 2, USER 1; file names in upper case; GetWinFlags 0419h. w16 matches all of it except KERNEL's own usage count (1).

**Which slot.** On a 386 (DOSBox), `WIN /S` runs Microsoft's KRNL386.EXE; KRNL286.EXE runs only on a 286 or when KRNL386.EXE is missing (Pietrek; observed with TK18 and by the heap work). `tests/dropin.sh` deletes KRNL386.EXE in its scratch copy, so DOSX falls back to the KRNL286.EXE slot, which holds w16's KERNEL; `-r` deletes it too, so the reference is Microsoft's KRNL286 on the same machine. `-x 286` runs DOSBox-X emulating an 80286 (and checking segment limits): there too the boot screen is pixel-identical to Microsoft's KRNL286's.

**Other differences found while booting** (each observed, each fixed): INT 31h 0007h on the selector in SS: HDPMI16 returns through the SS it has cached, DOSX does not, so `seg_move` runs on a stack of KERNEL's (TK19 as the shell returned through zeros before). USER's GetMessage reaches WaitEvent with interrupts disabled, so KERNEL's idle loop enables them. Standing in front of SYSTEM.DRV's INT 08h handler (the first watchdog) stopped SYSTEM's timer procedures once a task had called InitApp (TK23), so the watchdog samples the RTC instead. GetTaskQueueDS/ES keep every other register and the carry flag (TK24; returning the queue in AX made PostQuitMessage ineffective). GetDOSEnvironment keeps BX and ES (TK24; MOUSE.DRV uses ES afterwards, which faults where segment limits are checked). GetCodeInfo and the loaded module table's segment flags and DGROUP allocation (TK25; USER's SetWindowsHook, from SHELL's RegisterShellHook in PROGMAN's WM_CREATE, read them and then sent a message to a bad window and waited for ever). Cursors and icons are not preloaded (TK27, TK28; 3.1 hands USER the display driver's PRELOAD cursors in device format, made by the handler USER installs later).

**Probes for the boot** (not in `make test`; run them as the shell with `tests/dropin.sh -f build/dos/TKnn.EXE=TKnn.EXE -i 'boot:shell=C:\TKnn.EXE'`, a command tail with `-c 'WIN /S /B ARGS'`, and with `-r` for 3.1): TK20, a minimal windowed program (InitApp, RegisterClass, CreateWindow, painting, SetTimer, the message loop, DestroyWindow/PostQuitMessage/PostMessage endings) runs through identically now; TK21, CreateSystemTimer and GetSystemMsecCount; TK22, the boot probe (modules, usage counts, DiagOutput); TK23, which step of a program's set-up affects the system timer; TK24, the registers GetTaskQueueDS/ES and GetDOSEnvironment leave; TK25, GetCodeInfo; TK26, icons from a program's resources, drawn and minimised; TK27, the cursor block LoadCursor gives; TK28, PRELOAD and LOADONCALL cursors of a program. USER calls, at boot, SYSTEM.EnableSystemTimers, CreateSystemTimer(0, DISPLAY's cursor procedure), and in InitApp CreateSystemTimer(10000, its idle timer), which SetTimer later re-creates at the timer's rate; COMM makes a 100 ms one.

**Where it stands** (`tests/dropin.sh -s shot.png -i w16:watchdog=0`, in DOSBox, in DOSBox-X as a 386 and as an 80286): Windows 3.1 boots on w16's KRNL286.EXE under Microsoft's DOSX with Microsoft's drivers, fonts, GDI, USER, SHELL and PROGMAN, and the screen is **pixel-identical** to the same boot on Microsoft's KRNL286; `BOOTLOG.TXT` is byte-for-byte the same. With PROGMAN as the shell and SNAP (tests/apps/snap.c) started from WIN.INI's load= line, these screens also match 3.1 pixel for pixel: Notepad, Control Panel (with its applets' icons), Write with README.WRI, File Manager, Calculator after Notepad was closed, Notepad after Control Panel and PIF Editor were closed; Clock differs only in the time it shows. Starting a DOS program through Microsoft's WINOLDAP.MOD works as on 3.1: the MS-DOS Prompt goes full screen with the same output (its banner, then COMMAND.COM), and `COMMAND.COM /C ECHO HI` from Program Manager returns to Windows with a screen identical to 3.1's, Notepad started afterwards too (0 pixels). WINOLDAP calls WinOldApCall(1) and GetWinOldApHooks on w16.

**More findings from the desktop** (each observed on 3.1 with the probe named, and fixed):
- A module's path is built without DOS's TRUENAME (through DOSX on DOSBox-X it returned garbage for some names, and GDI then failed to read FONTS.FON): a full path as given, else the current drive and directory; in upper case (TK22).
- Every DLL load adds a use to KERNEL too (TK29: 11 at the shell); KERNEL is never freed.
- Modules loaded after USER get USER's DIB handlers (USER.357/356) for RT_ICON/RT_CURSOR, and their PRELOAD icons and cursors are read through them (TK30: MAIN.CPL's icons; TK28); the drivers' icons and cursors wait for their first LoadResource (TK27).
- Registers: 3.1 keeps BX, CX and ES in 20 of the 39 exports TK31 measures (KAPI_ES in kernel.h; the list is in tools/kapiprag.py's output); not ES in GlobalFree and the like. Write needs GetVersion to keep them, and LocalHandleDelta (KERNEL.310), which it asks for with GetProcAddress.
- Each task has its own DTA (TDB 62h): KERNEL records INT 21h AH=1Ah and sets the task's DTA again on every switch, since DOSX keeps one; File Manager listed another task's buffer before.
- A DOS program starts WINOLDAP.MOD from the system directory with the tail "ARGS", NUL, "PROGRAM" (TK33).
- Reports (faults, the watchdog, "Error loading") switch to text mode first (INT 10h AX=0003h), as Windows does.

**The KERNEL ↔ USER contract** (what KERNEL does for USER; our own USER must implement the other side). Observed on 3.1 unless marked:
- *Boot:* KERNEL loads USER.EXE with LoadLibrary after GDI (USER's initialisation runs then, with a boot task current that has KERNEL's PDB and gets USER's first message queue through SetTaskQueue(0, …)); loads the shell without running it; calls USER.400 (FinalUserInit, Pietrek) with no arguments; deletes the boot task; starts the scheduler. USER writes its `INIT=`/`INITDONE=` lines with DiagOutput (KERNEL.340), strings appended verbatim.
- *Task start:* the program's start-up code calls InitTask, WaitEvent(0) and USER's InitApp(hInstance); InitApp makes the queue (SetTaskQueue(0, hQueue)), sets the task's signal procedure to USER's SignalProc (SetTaskSignalProc(0, USER.314); TK32 saw USER.314 there) and creates a 10 s system timer the first time (SYSTEM.CreateSystemTimer). USER installs its DIB handlers for RT_ICON/RT_CURSOR on the program (SetResourceHandler; Pietrek); KERNEL gives every module loaded after USER the same handlers (USER.357/356) and reads PRELOAD icons and cursors through them (TK30, TK28).
- *Task end:* before freeing anything, on the task's own stack, KERNEL calls the TDB's signal procedure (TDB 2Ah) once, as a Pascal far call with five words pushed in this order: hTask, wCode = 20h, the exit call's AX (4Cxxh; also in DI), the task's hInstance, its hQueue; BX = 20h (TK32, all five values and both registers observed). USER's SignalProc then cleans up the task (Pietrek: AppExit: windows, queue, hooks, capture, classes). Only then does KERNEL free the task's blocks, unlink it, close its files, free its module and DLLs (each freed DLL's WEP, then USER.314(hModule, 80h)) and its PDB and TDB, and schedule. When the last task ends, KERNEL exits Windows (3.1 calls USER's ExitWindows; TK32 as the shell got no signal).
- *DLL load:* when a DLL is loaded while USER is there, KERNEL calls USER.314 (SignalProc) directly with (hModule, 40h, 0, 0, 0), BX = 40h, before the DLL's initialisation (Schulman, Pietrek; UNVERIFIED: the order and the zero arguments).
- *Waiting:* USER's GetMessage calls WaitEvent(hTask) with interrupts disabled; KERNEL's idle loop enables them. USER's timer and input interrupt code wakes a task with PostEvent(hTask). PeekMessage yields through OldYield. Yield of a task with a message queue is USER's UserYield (USER.332), which answers waiting inter-task SendMessages around OldYield (Pietrek ch. 6); LoadModule starts a new task with that Yield (Pietrek ch. 3, CloseApplEnv). With a plain OldYield there, Program Manager, waiting in LoadModule, did not answer Notepad's activation messages and, back in its own code, ended the Run dialog by activating itself, so Notepad opened behind it with Microsoft's USER (boot-test `runfrontk`; on 3.1 Notepad reaches GetMessage before Program Manager's EndDialog). GetTaskQueueDS/ES change only DS/ES (TK24). USER calls K327 (HandleParamError) with the error code in BX on a bad parameter and continues.
- *Registers:* USER and other modules rely on KERNEL's exports keeping BX, CX and ES where 3.1 keeps them (TK24, TK31).

**Debugging aids** (w16 only, while booting Windows; `[w16]` keys in SYSTEM.INI, set with `tests/dropin.sh -i w16:KEY=VALUE`): `debuglog=FILE` (dropin.sh sets `C:\W16DBG.LOG` and prints it) receives w16's own diagnostics: notes like K327's, WinOldApCall, calls to unimplemented imports, `schedlog`, `modmap`, and a copy of OutputDebugString's text. Without it they go to KERNEL's standard output only while no Windows screen is up (make test, the boot before USER), else nowhere: written to handle 1 they were painted over Windows' screen in a session without a redirect (KERNEL audit, finding 9). Reports that end Windows (faults, the watchdog, heapcheck, "Error loading") switch to text mode and still print to standard output. **OutputDebugString** with no debugger: Windows 3.1's retail KRNL286 writes the text to DOS handle 3 (AUX) (TK56 with E10: INT 21h AH=40h BX=3); in DOSBox AUX is the console, so 3.1 paints it over its own screen. w16 does that with `[w16] DebugToAux=1` (`tests/dropin.sh` sets it); by default (§6.8) it writes to handle 3 only when that is not the console (IOCTL 4400h: a character device without the console bits), and the text goes to `debuglog` either way (boot-test `odstring`: stock against 3.1, default against the screen without the calls). `trace=MOD.ORD,...` binds those imports to thunks that record each call (caller, AX-DX, SI, DI, arguments; the callee gets the caller's registers) for the reports, `tracefrom=MODULE` only the calls from MODULE's code (they are printed when Windows ends too: a register that changes between two calls of the module that should keep it shows who changed it, §20.6); `modmap=1` prints every module's selectors once the shell is loaded; exceptions 0, 6, 0Bh-0Eh are reported with module:segment (base, limit), registers, the last 16 INT 21h calls and the trace, instead of DOSX's "Fault outside of MS-DOS Extender"; `watchdog=SECONDS` (dropin.sh sets 15; 0 for screenshots) samples the RTC's periodic interrupt and, after SECONDS without a boot-log line, prints where the processor has been and exits; K327 (HandleParamError) notes the first parameter errors. `exitmsg=1` (set by `tests/dropin.sh`) prints `w16: back to DOS, code N` when Windows ends (3.1 prints nothing; without it w16's KERNEL does not either). `schedlog=1` prints every task switch with what asked for it (start, Yield, OldYield, DirectedYield, WaitEvent). `heapcheck=1` (usable under both kernels since the KERNEL audit fixed KRNL386's false "bad free list" alarm on a free arena right after the first sentinel) checks the heaps as Windows runs: the whole global heap (arena links both ways, each arena's size reaching the next arena, every block's selector table entry and selector base, each of its 64K selector tiles' base and limit against the block's size (the limit's low word; FFFFh where a 286 host can give no more), a DGROUP on more than one tile, the free list in address order) at each GlobalAlloc, GlobalReAlloc, GlobalFree and GlobalLock and at each task switch, and the caller's local heap (arena links, moveable arenas against their handle-table entries, the free list with sizes and back links) at each LocalAlloc, LocalReAlloc and LocalFree; at the first inconsistency it prints what is wrong, the call or event and its caller's CS:IP (`kentry.asm` notes the return address of the Global/Local thunks), and the arena with its neighbours (or the local arena's bytes), and ends Windows (code FEh). It is slow (the global walk asks DPMI for each arena's base): boot tests with it need longer waits, and `watchdog=0`.

**The import surface** (`tools/kimports.py WINDOWS_DIR`): the boot modules and PROGMAN import 151 KERNEL entry points; all are exported. Observed on 3.1 so far: DiagOutput (TK22), GetTaskQueueDS/ES and GetDOSEnvironment registers (TK24), GetCodeInfo (TK25); the selector services and the heap's exports are `kernel/selector.c`'s and `kernel/global.c`'s, observed by that work. Not yet observed and therefore guesses (`kernel/kmisc.c`, `kernel/local.c`, `kernel/kentry.asm`): SetErrorMode's storage, GetExeVersion while booting, LocalCountFree/LocalHeapSize/LocalShrink, GetHeapSpaces, the IsBad*Ptr family, Catch/Throw's buffer layout, K327 (returns, keeping every register), DoSignal (does nothing), GlobalPageLock/Unlock, Reserved1/2/5 (AnsiNext, AnsiPrev, lstrcmp in byte order), and which registers the C-written exports keep (TK24 found two that must keep more than C does; others may too).

**Remaining gaps:**
1. DOS programs: what TK34 (`tests/apps/tk34.c`, tail `ORD N` calls any KERNEL ordinal) observed on 3.1 in Standard mode, and w16 does: **WinOldApCall** (151) pops one word; 1 and 2 return AX = the argument and DX = a selector of KERNEL's PSP (base the PSP, limit 100h); 0 returns DX = a selector with base PSP+100h (KERNEL's image), limit FFFFh, and AX = one with base PSP+1100h, limit 0Fh, meaning unknown (w16: 0, logged). **RegisterWinOldApHook** (343) pops three words and returns AX=1; **GetWinOldApHooks** (344) takes none and returns DX:AX = 0 until 343 ran, afterwards a far pointer from 343's words (with all three words 1234h: 1234:1234; with all 0: F000:1060, so 0 may select a default; a call with three different words hung 3.1). w16 stores 343's last two words and logs both. Still open: 343's meaning, the TDB's WINOLDAP flag (Pietrek: TDB 16h bit 0) and IsWinOldApTask, a DOS program that waits for keys (scripts cannot type into the full-screen session yet).
2. The carry flag 3.1 keeps in some exports (TK31). BX and CX are now kept by every export (tools/kapiprag.py) except where 3.1 leaves a value its callers use: bisecting the earlier failure (GDI did not start when every export kept BX and CX) found GlobalLock and LocalAlloc, and TK43 (`tests/apps/tk43.c`) measured on 3.1 that GlobalLock returns the selector in CX as well as DX (ES = 0, BX kept), and LocalAlloc, LocalReAlloc, LocalLock, LocalUnlock, LocalSize and LocalFlags return CX = AX (LocalFree CX = 1; ES kept); GlobalUnlock CX = 0, GlobalFlags CX = the flags and DX = the selector, GlobalFree CX = DX = 0, GlobalReAlloc CX = the handle, and those and GlobalSize ES = 0. kentry.asm's export thunks (GHEAPAPI, LOCALAPI) set these; with them Windows boots pixel-identically. Other registers 3.1 changes to values nobody was seen to use (BX after GlobalSize and the like) are kept. **Segment moves** (kentry.asm seg_move) run with interrupts off from the copy until the new base is set, on a 1 KB stack: before, an interrupt during a move ran on a 256-byte stack and could write into the segment being copied (a suspect for the boot stopping when the mouse moved). UNVERIFIED how 3.1 moves segments.
3. The per-task interrupt vectors (Pietrek ch. 1, BootStrap: INT 00h, 02h, 04h, 06h, 07h, 3Eh, 75h in TDB 32h-4Dh). TK42 (3.1): a task's seven dwords are what INT 21h AH=35h returns, and AH=25h for INT 00h or 06h changes both (still so after other tasks ran). Not done in w16 yet: still to observe what a new task gets on 3.1 when another task changed one, and whether the coprocessor vectors (07h, 3Eh, 75h, which WIN87EM or an emulator sets from one task for all) are switched too. GetProcAddress on constant entries returning ES:BX (Pietrek). **`__GP` recovery** (Schulman ch. 5, `_GP` and HasGPHandler; TK38, TK40 on 3.1) is done: a module that validates by letting a load fault exports `__GP` (resident name), a table of {code selector, low, high, recovery offset} ending with selector 0. TK38: KERNEL (35 entries, also KERNEL.342), USER (121) and GDI (69) have one, DISPLAY none; HasGPHandler(sel:off) gives the recovery offset for low <= off < high, else 0. TK40 (a stand-in DLL named PENWIN, `tests/apps/tk40d.asm`, faulting inside its own table's range; with `__GP` only in the non-resident names 3.1 did not find it): the recovery code is entered at sel:recovery with every general register, DS, ES and SS as at the fault, SP 4 lower with [SP] = the faulting IP and [SP+2] = the error code, flags 3202h (the arithmetic flags clear). w16 does the same for exceptions 0Bh-0Dh (`kernel/fault.c` gp_handler, `kentry.asm` fault_exit; a DS or ES that is not valid becomes 0); its own KERNEL exports an empty `__GP` and checks pointers instead: lstrlen/lstrcpy/lstrcat of a freed selector return 0 and change nothing, as 3.1's (TK39). **K327** (HandleParamError) with a warning code (bit 15 set) returns with every register kept (TK41); with any other code (0001h, 4001h, 6040h, 7007h) called from a function of the usual frame, from an application or from inside a `__GP` range, 3.1 never came back to the caller nor to the function's caller and the task ended, silently. GDI's recovery for TextOut with a freed selector calls K327 with 7007h: on 3.1 TextOut returns 0, on w16 K327 returns and GDI goes on to the display driver, which faults (TK39). So the retail K327 unwinds in a way not yet observed: still open. USER (US27, 10.3): 3.1's USER has the same kind of table and w16's now exports its own (USER.484, `user/gp.asm`), covering the code that touches a caller's pointer; its recovery code returns to its C caller, which returns 3.1's value. US27 shows no USER recovery that ends the task the way K327 did in TK41: every recovered fault came back to the program with a value, and every task end seen was an Application Error (the boot stayed up until the run's time was up), none a silent end. One result fits K327 unwinding past the function that called KERNEL: SetProp with a name running past its segment's end returns TRUE on 3.1 though KERNEL's atom code faulted (recovered by KERNEL's table) and no atom can have been made; w16 returns TRUE there without asking KERNEL. UNVERIFIED which. **SetSigHandler** (KERNEL.140; Schulman ch. 5, DoSignal; TK38): SetSigHandler(proc, lpOldProc, lpOldType, type, 1) returns 0, stores the old handler and type, and sets TDB 24h (type) and 26h (handler); a task starts with type 0 and KERNEL's default handler (a RETF); a NULL handler puts the default back. DoSignal calls nothing when the task has no focus window (TK38's case).
4. A discarded system icon (IDI_APPLICATION after GlobalCompact(-1)) draws nothing on 3.1 and a black square on w16 (TK26).
5. The meaning of the module-table segment flag bits 0400h, 4000h, 8000h (TK25 reproduces them for the cases seen).
6. TK19 (the heap probe) as the shell with Microsoft's USER: FAILs on GlobalNotify's TDB 2Eh and block adjacency (USER's and GDI's own blocks are in the heap there), then TOOLHELP faults (GP, error code 0070h) walking the heap.
7. The hang users saw when closing a program with the mouse is not reproduced by scripted closes (SC_CLOSE); the task-exit signal to USER (above) is what was missing on that path.

### 9.5 WIN.COM

w16's WIN.COM is `win/win.asm` (NASM, an 8086 `.COM`). It began as LOSE.COM, the user's MIT-licensed Suite16 loader (https://github.com/Suite16OSE/lose, imported unchanged in its own commit; notice and AUTHORS in `win/LICENSE.lose`): from it come the DOS version check, saving the INT 23h/2Fh vectors, the /R /S /2 /3 /? switches blanked in the tail for KERNEL, the INT 2Fh check for a running Windows and the SYSTEM\ program names. Everything else below was observed black-box on the reference 3.1 install with the WC probes (`tests/dos/wc1.asm`-`wc8.asm`, `tests/apps/wc9.c`, built by `make wcprobes`), DOSBox-X's own INT 21h and file logs (`-log-int21 -log-fileio`), and Pietrek ch. 1 for orientation (each fact used was confirmed by observation). Microsoft's WIN.COM, WIN.CNF, .LGO, WSWAP, DOSX and WIN386 were never disassembled.

`build/dos/WIN.CNF` is the program alone; `build/dos/WIN.COM` is WIN.CNF with w16's logo (`assets/boot/W16LOGOR.BMP`, made by `tools/mklogo.py`) appended, the way 3.1's Setup builds WIN.COM. `tests/wincom.sh` runs 25 scenarios with Microsoft's WIN.COM and ours in scratch copies of the reference install (headless DOSBox-X) and compares screen text, exit codes, video modes, redirected output, what the started programs receive (WC1 standing in for WSWAP/DOSX/WIN386/KRNL386), and the switcher block (WC4); all are equal. It runs at the end of `make test` when the reference install is there. `tests/dropin.sh -w` boots with our WIN.COM. The logo (mode 12h) leaves the BIOS's row count (0040:0084h) as it was before it, as 3.1's WIN.COM does (WCB, a BIOS data area dump in WSWAP's place: 18h from 80x25, 31h from 50 lines; mode 12h itself sets 1Dh): Windows' display driver reads it when it starts and restores that text mode at the end, so with 1Dh Windows ended in 50 lines with the 8x8 font; boot-test `winprompt` checks the mode after Windows (mode 3, 80x25, 16-line font, as 3.1's).

**What WIN.COM is.** On 3.1, WIN.COM (44170 bytes) is exactly SYSTEM\WIN.CNF (16112, a .COM program) + VGALOGO.LGO (1280) + VGALOGO.RLE (26778), concatenated (SETUP.INF names the pieces per display: `1:win.cnf`, and a "logo code" and "logo data" column, e.g. vgalogo.lgo/vgalogo.rle, egalogo, cgalogo, herclogo). The .RLE is a standard Windows bitmap, 4 bits a pixel, BI_RLE4 (`file`: 355 x 425); the .LGO is code (not examined). Behaviour: WIN.CNF alone, or WIN.CNF + .RLE, starts Windows without a logo; WIN.CNF + VGALOGO.LGO + any BI_RLE4 bitmap shows that bitmap centred in mode 12h (w16's own 640 x 480 logo was shown correctly that way); an uncompressed bitmap in its place gives a black screen. w16 does not use or ship any Microsoft logo code or image: our WIN.COM draws the bitmap itself (palette from the bitmap, centred, mode 12h set with INT 10h) and finds it as the first "BM" after the program whose file size reaches exactly to the end of the file, so `copy /b WIN.CNF+X.LGO+X.RLE WIN.COM` (as Setup does when the display changes) also works, the .LGO bytes being skipped, never run. The whole file must fit a .COM (DOS loads at most about 64 KB): a DOS limit. Nothing else was seen to depend on WIN.COM's size or format (Setup rebuilds it from WIN.CNF; WINVER reads KERNEL's version, not WIN.COM).

**Order of checks and messages** (exact bytes; messages go to standard output; the error messages end lines with LF CR, the others with CR LF; exit codes in brackets):
1. DOS older than 3.10 (DOSBox-X `ver=3.0` and 2.11; 3.1 passes): `Must have MS-DOS version 3.10 or greater` LF CR [FFh].
2. Windows already running [0]: INT 2Fh AX=160Ah answered (AX=0) with CX=2: `You are already running Standard mode Windows.`; CX=3: `You are already running Enhanced mode Windows.` + `■ Press ALT+ENTER to switch this MS-DOS prompt between a window` / `  and a full screen.`; else, or INT 2Fh 1600h with AL other than 00h/80h, or 4680h returning AX=0: `You are already running Windows.`; each followed by `■ Type Exit and press Enter to quit this MS-DOS prompt and` / `  return to Windows.` / `■ Press ALT+TAB to switch to Windows or another application` (■ is FEh). A task switcher answering 4B02h is not a refusal. (WC7 answers these as a TSR.)
3. The switches: only the words at the start of the tail. `/3`, `/S` `/s` `/2`, `/R`, `/?`, with `/` or `-`, also run together (`/3/S`); parsing stops at the first word that is not one of them (`/B`, `/D:F`, `x`, `/x`), so `WIN x /3` and `WIN /x /s /3` are 386 enhanced with the tail untouched. Each recognised switch is replaced by two blanks in place. /S wins over /3 whatever the order. `/?` anywhere among them shows the help; `/R` (also after `/3`): CR LF `This version of Windows does not run in real mode.` CR LF CR LF `To run Windows in standard mode, type win /s.` CR LF `To run Windows in 386 enhanced mode, type win /3.` CR LF [FFh].
4. Help (`/?`): CR LF `Starts Windows.` ... (the usage text, w16 has it byte for byte), then `In this configuration, WIN without any switches will launch enhanced mode.` or `... standard mode.`; exit code 3 or 2, the mode it would choose. Where Windows cannot run at all (8086, no XMS) the last line is left out and the exit code is 5.
5. `/3` on an 80286 or 8086: `Cannot run in 386 enhanced mode;` LF CR `enter WIN to run in standard mode.` LF CR [FFh]. An 8086 otherwise: `Your computer cannot run Windows in either standard or 386` LF CR `enhanced mode.` LF CR [FFh] (DOSBox-X cputype=8086/286).
6. No XMS driver: with extended memory (INT 15h AH=88h nonzero): `Missing HIMEM.SYS;` LF CR `Make sure that the file is in your Windows directory and that its` LF CR `location is correctly specified in your CONFIG.SYS file.` LF CR; without (memsize=1): the "cannot run" message [FFh both].
7. The mode by itself: 386 enhanced on a 386 or later if XMS reports at least 1024 KB free (WC8 took XMS away: 1020 KB free gives standard, 1024 KB enhanced; memsize=2 standard, 3 enhanced) and WIN386.EXE is found; otherwise standard (also when WIN386.EXE is missing). `/3` skips the memory test (1 MB of XMS: WIN386 is started anyway). UNVERIFIED: whether "free" is XMS's total or largest block (they were equal in every test).
8. The programs, searched (DOSBox-X file log) in WIN.COM's own directory, its SYSTEM, the current directory, each PATH directory, each PATH directory's SYSTEM, with lower-case names (`C:\WINDOWS\system\win386.exe`). Standard mode: DOSX.EXE first (`Cannot find DOSX.EXE needed to run in standard mode;` LF CR `check to ensure the path is correct or reinstall Windows.` LF CR [FFh]), then WSWAP.EXE (`Executable Not Found` CR LF [FFh], after the screen was set back to the starting video mode: it clears). Enhanced: `Cannot find WIN386.EXE needed to run in 386 enhanced mode;` LF CR `check ...` [FFh]. The third place was only seen where the current directory and WIN.COM's were the same (UNVERIFIED that it is the current directory). Windows 3.1's WIN.COM also searches SYSTEM.INI the same way and opens a garbage name once (`C:\W2\` followed by binary bytes): not copied.

**The logo** is shown only when nothing but recognised switches was given (`WIN`, `WIN /3`, `WIN /S`; not `WIN :`, `WIN calc`, `WIN /3 /B`, `WIN /S x`): the started program finds video mode 12h, else the mode WIN was started in. It is not shown again on a restart.

**What the programs get.** Every program WIN.COM starts gets WIN.COM's own FCBs (as DOS parsed them from WIN's tail) and an environment that is WIN.COM's with `windir=<WIN.COM's directory>` (lower-case name, no trailing `\`, e.g. `C:\WINDOWS` also when started as `C:\WINDOWS\WIN` from C:\ or through the PATH from elsewhere) appended after the existing strings; an existing upper-case `WINDIR=` is kept. WIN386.EXE gets WIN's tail with the switches blanked (`WIN /3 a` gives `    a`; `WIN /3 /D:FX` gives `    /D:FX`: /D is WIN386's). WSWAP.EXE gets the tail `<space>08h` (UNVERIFIED what the 08h means), which WIN.COM also writes into its own PSP; WSWAP gets everything else from WIN.COM's INT 2Fh handler. INT 23h and 24h are not hooked; INT 22h in the child is WIN.COM's return address, as for any EXEC.

**WIN.COM's INT 2Fh handler** (observed with WC3, an INT 2Fh spy put between WIN.COM and WSWAP/WIN386, and WC4-WC6 calling the functions):
- 160Ah (the Windows 3.1 SDK's "identify Windows version and type"): AX=0, BX=030Ah, CX=2 (standard) or 3 (enhanced). Both modes.
- 4B20h: AX=0, DX:CX = a buffer in WIN.COM, for ExitWindowsExec (Pietrek ch. 1). What KERNEL writes there is UNVERIFIED: WC9 (ExitWindowsExec from the shell) got FALSE back on 3.1, so the program-to-run path was not observed; w16 reads it as "path arguments" and runs it when Windows exits with 44h.
- Standard mode only, 4A05h, function in SI, the interface WSWAP uses (Pietrek: "an interface to the Standard mode task switcher"): SI=5 returns DX:AX (and DI=AX) = the switcher block, SI=0Ah; SI=2 (CX=10h, from WSWAP and DSWAP) decrements block byte 17Ch and returns DX:AX -> a zeroed buffer (3.1: DX 010Fh in two runs with WIN.COM at 0192h and 0194h, the 64 bytes there 0; w16: 10h paragraphs of its own; it returned a pointer into its code before, and DSWAP hung), DI -> the block, SI = the SI=6 pointer; SI=4 returns AX = the word at 17Ch (0102h while a DOS program starts), DI -> the block, SI=8; it leaves WSWAP's swap-file number at block+4F2h alone (WSWAP needs it to swap Windows back in after a DOS program); w16 clears that number before WSWAP is started again for a restart, else it tried to resume the old swap file ~WOA<n>.TMP and quit (UNVERIFIED which side clears it on 3.1); SI=3 (WSWAP about to run a DOS program) takes 17Eh from 0Fh to 0Dh and returns AX = 17Ch:17Eh (020Dh), SI = an offset (3.1: 0B8Fh, UNVERIFIED); SI=6 returns ES:AX (and DX) = WIN.COM's segment : the name field of the block's last task record, SI=7 the same without ES; SI=0, 1, 3 return AX unchanged and SI doubled. WSWAP calls SI=5 when it starts and SI=4 (and, after a full Windows session, SI=2, SI=5, SI=4) when it ends. **DOS programs**: to run one (WINOLDAP: a PIF, the MS-DOS Prompt) WSWAP calls SI=4, SI=3, swaps Windows out and ends, leaving the program's path at 189h (`Z:\COMMAND.COM`), its tail at 05h, 17Ch not 0 and 17Dh 1; WIN.COM runs DSWAP.EXE (found in the system directory as WSWAP.EXE is, with WSWAP's tail `" \x08"`; E10, an INT 21h/10h recorder, docs/notes/keyloss-handover.md), which sets 80x25 (INT 10h 1202h, mode 3), shows the MS-DOS Prompt's banner and runs the program itself, and then starts WSWAP again (w16 ran the program itself before: no banner, the text mode Windows left; without DSWAP.EXE it still does; boot-test `winprompt`), which swaps Windows back in; between the two the block's 16Fh went up by one, 17Ch down by one, 17Eh back to 0Eh and the program's task record was marked free (bytes 11h-13h `00 80` and one more) (WC3 in DOSBox; boot-test `winoldapw`). The first two are DSWAP's doing when it runs (its SI=2 counts 17Ch down): w16's WIN.COM did them as well, WSWAP's second run found 16Fh 03 and 17Ch 00 (3.1: 02 and 01; WC3 in front of WSWAP), Windows' end left 17Dh at 1, and WIN.COM ran WSWAP and DSWAP in turn, each returning at once, until 17Ch ran out (UNVERIFIED: at that end 3.1's block has 17Ch FFh and 17Dh 0, w16's 0 and 1). **The screen**: when WSWAP returns in standard mode for a DOS program, and when Windows ended normally or restarts (exit code 0, 42h, 44h; after an error, exit code FFh or 7 in tests/wincom.sh, the screen is left as it is, as in enhanced mode), WIN.COM sets the video mode and the cursor shape it found at its start (E10: INT 10h AX=0003h, then AX=0100h CX=0607h; with mode 1 and cursor 0205h set before WIN, 0001h and 0205h); boot-tests `woadosbat` and `exitwin` check text mode at the end.
- What the 4A05h functions do with the task records, which program WIN.COM runs after WSWAP or DSWAP ends, and the screen then: §9.9 (SW1, 2026-11-14), which corrects the guesses above: SI=3 makes 17Eh its record's next, SI=2 frees record 17Eh (the record was freed by WIN.COM after DSWAP before), SI=6/7 walk the ring; WIN.COM runs record 17Eh's session while 16Dh says Windows is swapped out, with INT 10h AX=0003h before each child but the first.
- Standard mode only, 4B05h (MS-DOS 5 task switcher, identify instance data): passed down the chain first, then ES:BX = WIN.COM's startup info (version 3, next = what came back, instance list) whose one instance item is the 4-byte link of the last system file table block (DOS list of lists + 4, followed to the end: 0115:0000 in DOSBox-X; WC8).
- Everything else goes down the chain (1600h, 4680h, 4B02h, 4B01h are not answered).

**The switcher block** (WC4 dumps; offsets from its start): 00h 01h; 01h processor, 02h for an 80286 and 04h for a 386 (the WF_CPU286/WF_CPU386 values); 02h-04h 00 3B 00; 05h DOSX's command tail: length, `krnl286`, a blank, WIN's blanked tail, CR (`WIN /S /B X  y`: `krnl286     /B X  y`; on a restart length 0); D7h the swap directory with a trailing `\`: SYSTEM.INI [NonWindowsApp] SwapDisk= if it can be made current (tested with INT 21h 47h/3Bh), else the TEMP variable's directory, else the root of the first drive from A: that INT 21h 4409h accepts and 4408h does not call removable (3.1 asks those, no INT 13h: DOSBox answers 4409h for C: only and gets `C:\`, DOSBox-X also for A: and gets `A:\`); 11Bh a second directory: that drive's current directory when it is the current drive (3.1 in DOSBox: `C:\WINDOWS\` started from C:\WINDOWS, `C:\TMP\` from C:\TMP whatever TEMP is, `C:\` from Z:; boot-test `wincomswap`), else TEMP's if valid and not a root, else that root; when SwapDisk= did not give the first one, Windows 3.1's puts the root's drive letter on TEMP's path and a second `\` (DOSBox-X, TEMP=C:\TMP: `A:\TMP\\`), which w16 copies; an invalid TEMP leaves its tail after `A:\` in 3.1's (not copied); 16Fh-184h written by WSWAP when it ends (16Fh 1, 171h Windows' exit code, 173h its swap-file number, 183h its memory size), 177h 19h, 178h-180h 02 70 00 30 00 00 0F FF FF (17Ch the SI=2/SI=4 counter); 189h DOSX.EXE's full path as found (`C:\WINDOWS\system\dosx.exe`); 1F0h fifteen 32h-byte records (byte 12h = 80h, word 19h = FFFFh, a name at 1Bh; the last one named `dosx.exe`: WSWAP's task list?); 4DEh 22h more bytes (FF FF FF FF ... 46 68 0F ... `0310` 01), copied as constants; 4F2h WSWAP's swap-file number while it runs. Without the task records DOSX failed with "Insufficient extended memory to run Windows in standard mode." UNVERIFIED: the meaning of every constant above, and whether 177h follows the screen (it stays 19h with the screen in a 30-line mode).

**Running, exit, restart.** Windows 3.1's WIN.COM hooks INT 2Fh, shrinks itself (to 57h paragraphs for enhanced, EFh for standard, environment included; w16's keeps D5h in both modes, environment included: its start-up code and the logo are given back) and EXECs. Enhanced mode: the exit code is WIN386's; standard mode: it is block byte 171h if WSWAP marked the block (byte 16Fh, or the SI=2 counter), else FFh (a WSWAP stand-in that does not talk to WIN.COM gives FFh whatever it exits with). Exit code 42h (EW_RESTARTWINDOWS) or 44h: Windows is started again with an empty tail (WIN386's and DOSX's tails are empty, no `krnl286`), without the logo, the video mode being set back first; for 44h the ExitWindowsExec program first. Enhanced mode sets the video mode it found when WIN386 ends with 0, 42h or 44h; after an error code it leaves the screen alone (so Windows' message stays). Standard mode leaves the screen to WSWAP/DOSX. INT 2Fh is unhooked before WIN exits. Nothing is printed on a normal exit. Any other exit code of KERNEL's comes out as WIN's (WC1 as KRNL386 exiting with 7, in both modes).

**Not done** (Windows 3.1's WIN.COM does them; no effect seen in DOSBox-X): shrinking SMARTDrive (it opens `SMARTAAR`), the NetBIOS check (INT 5Ch with an invalid NCB), INT 21h 52h-based file-handle count and the "Insufficient file handles" message (DOSBox-X `files=20` made no difference: UNVERIFIED), INT 2Fh 4B03h before and 4B01h after WSWAP, INT 21h 38h, INT 2Fh 1203h, the Video Seven check (INT 10h 6F00h) of the logo code, the "Fatal exit! Windows cannot be swapped back" message (seen once when WC6 made SI=2 four times; the condition is not known). The /D switches (F, S, V, X) are WIN386's: WIN.COM passes them on (and they stop its switch parsing).

**Results.** `WIN /S` with our WIN.COM and Microsoft's WSWAP, DOSX and KRNL286 reaches Program Manager pixel-identical to Microsoft's WIN.COM (`tests/dropin.sh -r -w`), and so does our WIN.COM with our KRNL286 (`tests/dropin.sh -w -i w16:watchdog=0`); `WIN /3` in DOSBox-X (Microsoft's WIN386 and KRNL386) reaches Program Manager pixel-identical to Microsoft's WIN.COM. A DOSBox-X artefact: after our logo is drawn (video memory read back correct), 560 bytes of plane 2 at A000:1CC0 (screen lines 92-98) are cleared when the next program is EXECed, which does not happen after 3.1's .LGO; not explained (UNVERIFIED whether real hardware shows it).

### 9.6 DOSX.EXE: the Standard-mode DOS extender

w16's DOSX.EXE is `dosx/` (NASM, `nasm -f bin`, one 64 KB segment with a hand-made MZ header; `make` builds `build/dos/DOSX.EXE` and `DOSXT.EXE`, the same with a trace buffer). It replaces SYSTEM\DOSX.EXE: `tests/dropin.sh -x` (`-X FILE` for another build) puts it into the scratch copy of the reference install, and `RUNTESTS_DPMI=dosx tests/run-tests.sh` runs the whole test suite on it instead of HDPMI16 (KERNEL started as `DOSX.EXE krnl286 PROGRAM.EXE`, DOSX.EXE beside each KRNL286.EXE). Everything below was observed black-box on the reference 3.1 install with the DX probes (`tests/dos/dx1.asm`, `dx2.asm`, `dx5.asm`: .COM programs in the KERNEL slot, like TK18; `tests/apps/dx6.c`, a Windows program run as the shell) and TK18; sources otherwise are the DPMI 0.9 specification, XMS 3.0, the Intel 80286/80386 references and the IBM PC AT Technical Reference (CMOS shutdown code 0Ah). Microsoft's DOSX, WSWAP and KRNL286 were never disassembled. HDPMI (`.tools/hx`, binaries only) was not looked at.

`tests/dosxtest.sh [-c 286] [-a ARGS] [DXn...]` runs each probe under Microsoft's DOSX and under ours (WIN /S in a scratch copy, the probe as KRNL286.EXE) and prints both logs and their diff; `-a` gives the probe a command tail (DX5 takes the test number there).

**Results.**
- `WIN /S` (`tests/dropin.sh`), 386 (DOSBox): Microsoft's KRNL286 on our DOSX reaches Program Manager pixel-identical to Microsoft's DOSX (`-r -x`); so does w16's KRNL286 on our DOSX (`-x`), and the all-w16 start chain, our WIN.COM → Microsoft's WSWAP → our DOSX → our KRNL286 (`-w -x`), each compared with ImageMagick `compare -metric AE`: 0 pixels.
- 80286 (`-p 286`, DOSBox-X cputype=286, mode switches back to real mode by processor reset): Microsoft's KRNL286 on our DOSX boots to Program Manager pixel-identical to Microsoft's DOSX. w16's KRNL286 on our DOSX and on Microsoft's boots there too, pixel-identical to Microsoft's KRNL286 (boot-test `k286`). The watchdog ([w16] watchdog=, which tests/dropin.sh sets to 15) stops when the boot is over: it used to keep running and end an idle Windows 15 seconds after the last boot log line, which with a fixed-time screenshot (`DROPIN_FIXED=1 -t 150`) left a black screen that looked like a 286 boot failure.
- The test suite (35 programs, also the DOSBox-X floating-point runs) passes on our DOSX as on HDPMI16 (`make test` still uses HDPMI16).
- DX1 and DX2 under both DOSXes: every descriptor-management result matches (selectors handed out and their order, the free chain, the descriptors' bytes, the failures), the protected-mode entry state matches (CS 02E7, DS=SS 027F, ES 02DF with limit 0100h, environment 02D7, CPL 3, IOPL 3, GDT/IDT limits, LDTR 0038, TR 0088, the GDT entries a ring-3 program can see); the differences are listed below.

**What Microsoft's DOSX does** (values under DOSBox, memsize=16; DX probe lines in brackets):
- *Memory.* It takes every byte XMS has: XMS function 08h reports 0 KB free once DOSX runs [xms_08_free]; three handles exist then, 3B40h KB (locked), 54h KB at 130000h (IDT at 130000h, GDT at 130800h, DOSX's protected-mode code at 130920h, the LDT at 134C50h) and 3AECh KB at 145000h (the client's memory) [xms_handle, gdtraw]. INT 15h AH=88h gives 0 (HIMEM's). It keeps 575h paragraphs of DOS memory (plus a 0Ah-paragraph environment) [dosx_block_paras].
- *GDT* (36 entries, limit 011Fh) [gdtraw]: 08 GDT alias, 10 IDT alias, 18 data at 0, 20 DOSX's PSP, 28 DOSX's environment (limit 7FFFh), 30 code at F0000h, 38 the LDT (DPL 3), 40 data at 400h, 48 DOSX's data, 50 its protected-mode code (DPL 3; the default interrupt handlers, the vendor entry), 58 its real-mode code segment as code, 68/70/78 ring-0 data and code, 88 a 286 TSS (busy, DPL 3), 90 the TSS as data, 98 the LDT as data (the vendor entry's 009Bh), A0 a 4 GB data segment (386), C8 a 286 call gate (DPL 3, 0 parameters) and D0 one with 5 parameters into ring 0, D8 DOSX's data (limit FFh), E0 ring-0 code; 60, 80, A8-C0, E8-118 null. KERNEL leaves the GDT alone (DX6 under Windows).
- *IDT:* 286 interrupt gates, DPL 3, all 256 [idtraw]; 00h-1Fh go to ring-0 code, 20h-FFh straight to the protected-mode handler (0204h's value: the IDT is the vector table for them). The PIC stays at 08h/70h [dpmi_0400 DX=0870].
- *Default protected-mode handlers* [pmvec]: 0053:0E30+3n, a table of 3-byte entries that reflect to real mode, except 01h, 03h, 30h, 41h (an IRET) and 10h, 13h, 15h, 19h, 21h, 25h, 26h, 28h, 2Fh, 31h, 33h, 4Bh, 70h (handlers of their own). Exception handlers [excvec] 0053:1190+3n except 06h and 08h-0Eh. 0202h/0203h refuse 20h and up.
- *Exceptions:* a handler (0203h) is called on a 4 KB stack of its own, SS = 02C7h (base 145020h, limit FFFh), SP = 0FECh, IF clear, with the DPMI 0.9 frame (return 00E0:140C, a ring-0 selector the RETF faults on, error code, IP, CS, FLAGS, SP, SS); the client then continues as the frame says [DX5 F00D]. Without a handler, 06h and 08h-0Eh end Windows with `Standard Mode: Fault outside of MS-DOS Extender.` and a register line (EC CS IP AX BX CX DX / SI DI BP DS ES SS SP), errorlevel 255 [DX5 F000, F002]; a protected-mode interrupt handler for 0Dh does not get the fault [F00F]; 00h (divide) goes to INT 0 in real mode [F001]; a software INT 0Dh reaches the 0Dh interrupt handler [F00E]. A fault in DOSX itself: `Standard Mode: Fault in MS-DOS Extender.` with the same register line [DX5 0502h].
- *INT 31h* [TK18, DX2, DX5]: 0000h-000Dh work (0000h with CX=0 hangs [F00C]; 0001h refuses 0, GDT selectors, FFFFh and 0002h's selectors; 0006h-000Ch work by LDT index whatever the table bit and check nothing: DPL-0 code, not-present, gates are all written; 0008h makes limits above 1 MB page-granular; 000Ah of a GDT selector returns garbage), 0100h and 0102h fail (AX=8, BX=0), 0101h succeeds doing nothing, 0200h-0205h (0205h takes any selector), 0300h-0304h, 0305h/0306h fail with AX=0, 0400h (AX=005Ah, BX=000Ah, CL=4 on DOSBox's 386, DX=0870h), 0500h (largest free block, and it in 4 KB pages at 14h and 1Ch, 20h = 0, the rest -1), 0501h, 0502h and 0503h (both fault in DOSX with a bad handle, 0503h also with a good one [F006, F007]), 0600h-0603h succeed, 0604h gives 1000h, 0700h/0701h fail with AX=0, 0702h/0703h succeed, 0800h returns the address given, 0900h-0902h (AL = the state, AH stays), 0A00h fails with AX=0, 0B00h-0B03h. **Every other function** (000Eh, 000Fh, 0010h, 0103h, 0207h, 0307h, 0401h, 0405h, 0504h-050Bh, 0801h, 0C00h, 0C01h, 0D00h, 0E00h, 0E01h, 0F00h, 1000h tried) ends Windows: `Insufficient extended memory to run Windows in standard mode.` / `Increase the amount of available extended memory.`, errorlevel 255 (DOSX prints it itself: also when started directly).
- *The LDT* (DX1, DX2): descriptors 01h-4Fh are 0002h's, given from 4Fh (027Fh) down, never freed, reused for the same segment; 50h-59h are DOSX's own (two scratch descriptors, 02AFh the top of extended memory, the exception stack 02C7h); the free ones from 5Ah (02D7h) on form a chain through their limit fields (byte offset of the next, 0 at the end), never-used ones with base bits 16-23 = 80h; 0000h takes from the head, 0001h puts at the head (keeping the base, access byte 0, byte 6 0: limit bits 16-19 and G, D, AVL cleared, TK61 "huge tile 0 desc[6]"), several at once are the first run of free descriptors walking the chain; fresh descriptors are data, present, DPL 3, base and limit 0. At the switch: DS (and SS) through 0002h, then environment, PSP, CS from the chain.
- *A freed selector left in FS or GS* (TK61 "deadfs", §9.2 "Freed selectors"): Microsoft's DOSX ends Windows at the next timer tick or INT 21h ("Standard Mode: Fault in MS-DOS Extender", 005B:0B58, error code the selector); after INT 31h FS still holds it. w16's DOSX: the same after INT 31h; elsewhere it loads 0 (as WIN386 does) instead of ending Windows (`dosx/ring0.inc` fsgs_restore), a deliberate difference.
- *Extended memory* (0501h, INT 21h 48h): an arena with a 16-byte header before every block, byte-granular, first fit, split; INT 21h 48h from protected mode returns a selector to extended memory (tiled for more than 64 KB, plus a descriptor for the next header), 49h frees and clears ES, 4Ah faults in DOSX [F008, F009]; failure: AX=8, BX = paragraphs. 0501h's handle (SI = DI) is the first of two descriptors it takes for the block.
- *Real-mode calls* (0300h-0302h): SS:SP = 0 means DOSX's stack (05DE:4238 there); the stack words are copied above the return address; 0302h's procedure starts with flags 3002h whatever the structure says; the structure gets the general registers, flags, DS and ES back, SS:SP and CS:IP stay.
- *Callbacks* (0303h): 16 at most, real-mode addresses 0A09:0E70+3i; the procedure runs on DOSX's ring-3 stack (SS 004B) with IF clear, DS:SI the real-mode stack (a selector), ES:DI the structure; freeing twice succeeds.
- *Interrupts:* hardware interrupts in protected mode go to the client's handler on the interrupted stack; DOSX's own handlers for INT 10h and 15h translate pointers (MOUSE.DRV uses INT 15h C0h's ES:BX; VGA.DRV INT 10h's DAC calls). INT 15h C207h (set the PS/2 pointing-device handler) with a protected-mode ES:BX, as MOUSE.DRV calls it, is translated: the BIOS gets a real-mode entry of DOSX's, which far-calls the handler in protected mode with the BIOS's four words (the last pushed first: 0, Y, X, status) and returns to the BIOS with RETF. Observed with Microsoft's DOSX: TK47 (`tests/apps/tk47.c`, started from WIN.INI load= under `tests/inputrun.sh`) gave C207h a handler of its own in a block below 1 MB, and it was called in protected mode (MSW bit 0), on a stack that is neither the caller's nor the block. w16's DOSX passed the call on unchanged, so DOSBox's BIOS called the selector:offset as a real-mode address once the mouse was enabled, and Windows stopped taking input (the `allw16input` failure); now the menus and the pointer under `tests/scripts/input/menus.txt` are pixel-identical to Microsoft's DOSX. A packet the BIOS delivers while the client has not yet taken INT 74h (IRQ 12) in protected mode is dropped: MOUSE.DRV enables the device (C200h) and only then sets INT 74h, and mouse motion during the boot (with DOSBox's autolock=false, motion arrives without a click) could reach its handler in that gap, straight from the BIOS in real mode; IRQ 12 then stayed in service and the pointer never moved again (1 boot in 5; never with Microsoft's DOSX). Seen with MS1 (`tests/dos/ms1.asm`, a real-mode TSR loaded before WIN that records the BIOS's C2xxh calls, INT 74h and handler calls; TK49 writes its log out): in the stuck boots the first INT 74h came while IVT 74h was not yet DOSX's. With the drop, 10 of 10 such boots kept the mouse (boot-test `allw16nolock`). The same symptom on Microsoft's DOSX (boot-test `input`, bootnolock.txt: one mouse event during the boot, then none, keys still working) came from w16's KERNEL: its idle loop made INT 2Fh AX=1689h (the 386 Enhanced mode idle call) in real mode on every pass beside INT 28h, which Windows 3.1's KRNL286 never does (MS5, `tests/dos/ms5.asm`: about 455,000 INT 28h in 30 idle seconds for both, 1689h only for w16), doubling the idle loop's trips through the DOS extender; with Microsoft's USER, GDI, DOSX and MOUSE.DRV on w16's KRNL286 9 boots in 20 lost the mouse, none in 40 without the call (`drivers/mouse/README.md`). Under the mouse stress test (`tests/mousestress.sh`: MD3 as the shell, DOS I/O and idle phases, the pointer moved without a pause, the driver's Disable and Enable every 200 ms) Windows 3.1's own stack loses the mouse the same way within minutes (all 4 runs; IRQ 12 left in service at the slave PIC, nothing at the master), with Microsoft's DOSX and Microsoft's MOUSE.DRV together only: w16's DOSX with Microsoft's driver, and w16's driver on Microsoft's DOSX (even enabling the device before taking INT 74h, as Microsoft's does) never did. w16's DOSX sends no EOI of its own and drops a PS/2 packet the BIOS delivers before the client has taken INT 74h, which is what keeps Microsoft's driver out of that state on it: built without that drop (DOSXND.EXE), w16's DOSX with Microsoft's driver stalled in 3 of 4 runs the same way, with it in none. The end of IRQ 12 is lost when the BIOS hands Microsoft's MOUSE.DRV a packet through its C207h handler while the driver does not hold INT 74h (after its Disable, before its Enable has hooked again); Microsoft's DOSX passes such packets on. **Exit.** When the client ends (INT 21h 4Ch from protected mode), w16's DOSX makes the client's own PSP DOS's current one again (INT 21h 50h) before ending it: KERNEL makes its tasks' PDBs current, DOS ends the current PSP and returns to that PSP's parent, which in a task's PDB is a selector; ending Windows from Program Manager then hung after DOSX's exit, with Microsoft's KERNEL and with w16's. DOSX leaves the video mode as the client left it: Microsoft's makes no INT 10h call after Windows' display driver sets mode 3 (E10, an INT 10h recorder before WIN). It is WIN.COM that sets the mode and cursor shape it found at its start when WSWAP returns (9.5); w16's DOSX used to set back the mode it found at its own start, which after the logo is 12h, and Windows ended in VGA graphics with the BIOS's text on it. boot-test `exitwin`: File > Exit Windows > OK with every w16 part, the DOS prompt in text mode, 0 pixels against Microsoft's. Flags saved in real mode for a callback or a hooked real-mode interrupt lose NT before protected mode sees them (with NT set an IRET there faulted: DOSBox's "TSS not busy for IRET").
- *Vendor API:* INT 2Fh 168Ah with DS:SI = "MS-DOS" in protected mode: AX=0, ES:DI = 0053:1CCD; AX=0100h there gives 009Bh, CF clear; another name leaves AX=168Ah; INT 2Fh 1686h gives 0.
- *Starting and ending:* real-mode INT 2Fh, 24h hooked (22h is DOS's EXEC return); KERNEL's tail is DOSX's minus its first word, its environment DOSX's; DOSX exits with KERNEL's exit code (1 and 255 tried, also when run directly, without WIN.COM).
- *80286* (DOSBox-X): the same, except that the client's selectors at the switch are CS 02FFh, ES 02F7h, environment 02EFh (more of DOSX's own first) and CL=2.

**How w16's DOSX does it.** Real mode: CPU check (CL=2/3/4: 4 when EFLAGS.AC can be set, as Microsoft's reports on DOSBox), XMS (every block XMS gives, locked, A20 enabled locally; the LDT at the start of the lowest, the client's arena after it and over the others: "More than 64 MB" below), GDT and TSS in DOSX's own memory, the IDT in extended memory after the LDT (resident: 744h paragraphs, against Microsoft's 575h: see "Resident size" below), INT 2Fh/24h hooks, EXEC of KRNL386.EXE (386 or later) or KRNL286.EXE from DOSX's directory on DOSX's own stack. Protected mode: the client at ring 3, IOPL 3; ring-0 code always with interrupts disabled; ring-3 stubs (the default handlers, INT 10h/15h/21h/31h, the vendor entry) enter ring 0 through the call gate C8h; vectors 00h-1Fh go to ring 0, which tells an IRQ (the master PIC's in-service bit) from an exception and from a software INT (CD nn before the return address) and delivers it. To real mode: clearing CR0.PE on a 386; on an 80286 a reset through the keyboard controller with CMOS shutdown code 0Ah, the BIOS resuming at 0040:0067. Real-mode callbacks and hooked real-mode interrupts (the IRQs, 1Ch, 23h, 24h once the client has a handler) come back through a structure on DOSX's stack and run the client's code on DOSX's ring-3 stack. INT 21h from protected mode copies through a 4 KB buffer (reads and writes in pieces of it), maps DOS's segments in returned pointers to selectors (0002h's), keeps the client's DTA separate from DOS's, and does 48h-4Ah in extended memory; exits with 4Ch from protected mode run DOS's 4Ch in real mode for the client's PSP, DOS then returning to DOSX's EXEC. Fatal messages are printed after the client's program has ended (`pending_msg`), so they follow everything else on the screen.

**Deliberate differences.** No fixed limits where Microsoft's has them: callbacks (Microsoft's: 16; w16's: as many as real-mode addresses reaching DOSX's 16 callback entry points allow, see "Callbacks: the bound" below; the first 16 records in DOSX's data, the rest in extended memory). INT 31h 0000h with CX=0 fails instead of hanging; 0502h/0503h with a bad handle fail instead of faulting; 0503h and INT 21h 4Ah work (moving the block if needed for 0503h). Untranslated INT 21h functions get an empty buffer in real mode, and 60h/65h are translated (below). The real-mode callback addresses and DOSX's segment and offsets differ.

**Debugging aid.** `DOSXT.EXE` (`-dTRACE`) records every gate call (INT 21h, 31h, 10h, 15h, reflected interrupts, default exception handlers) with the client's registers, its result, and every exception or interrupt through vectors 00h-1Fh, in extended memory, and writes them to C:\DOSX.TRC after INT 31h calls (never after an interrupt or INT 21h: DOS's caller could be anywhere, and a client may ask for DOS's extended error next) and at the end; `tools/dosxtrc.py C:\DOSX.TRC` prints them. Found with it: KRNL286's invalid-opcode handler (its CPU test) returns with the handler's BP, which KERNEL then uses: with the exception stack anywhere else than Microsoft's 02C7h:0FECh KERNEL's boot depended on DOSX's memory layout.

**Open / UNVERIFIED.**
- Not observed, so not implemented or guessed: INT 21h 0Ah, 26h/55h, 5Eh/5Fh beyond what DOSBox answers, 44h 0Ch's block size; INT 13h beyond 02h/03h/08h, INT 15h beyond C0h/C1h/88h, INT 10h beyond the functions in `dosx/int10.inc`, INT 33h beyond 09h/0Ch/14h-16h, VDS beyond 8102h (DOSBox has no disk or VDS for the rest to show anything); what the vendor entry does for AX other than 0100h, the critical-error handler's answer, the messages for a missing 286 or XMS, whether hardware interrupts from real mode go to protected-mode handlers on Microsoft's DOSX (w16 hooks them, as DPMI 0.9 says).
- The task-switcher side: DOS applications under Standard mode (WINOLDAP, WSWAP) could not be compared: Windows stops after WinExec of a DOS program under Microsoft's DOSX too in DOSBox (below).
- The probe-visible differences left: the exception and IRQ default handler offsets, XMS handles and sizes, the LDT's own descriptors' bases (0287h-02CFh), INT 31h 0302h's flags after the call (Microsoft's returns the real-mode flags, DF included, to the client), 0400h's CH, the MSW's reserved bits.
- w16's DOSX is still larger in DOS memory than Microsoft's (744h against 575h paragraphs; "Resident size" lists what could move next).
- The 80286 path was tried in DOSBox-X only; real hardware (the 8042's reset timing, BIOSes that do not honour shutdown code 0Ah) is untested, and LOADALL is not used.

**INT 21h and the BIOS interrupts, probed** (`tests/dos/dx4.asm`, `dx8.asm`: each function called from protected mode with its buffers in extended memory, so it works only if DOSX translates it; `tests/dosxtest.sh DX4 DX8`). Microsoft's DOSX translates: 09h; 1Ah/2Fh (the client's DTA is kept apart; 2Fh returns it) with 4Eh/4Fh (43 bytes, the client's DTA copied in first: bytes DOS does not write keep the client's); 3Fh/40h of any size up to FFF0h (in pieces); 47h, 39h-3Bh, 43h, 56h, 5Ah (the completed name back), 5Bh, 41h, 6Ch; 29h; 38h (34 bytes); 44h 00h and 0Dh (the parameter block copied back); 4B00h (name, parameter block, command tail and FCBs from protected mode; 4Dh gives the program's exit code); 65h is **not** translated (01h leaves the buffer, 21h/22h leave the string; AX=CX for 01h/21h), nor 60h (error 2), nor the FCB functions 0Fh, 10h, 13h-17h, 21h-24h, 27h, 28h (they fail with AL=FFh: DOS got the selector values as segments; DX4's 11h/12h failed only because its 16h create had), but 11h/12h (FCB search, plain or extended, the FCB and the DTA anywhere) are translated: the found entry lands in the client's DTA (`tests/apps/tk36.c` under Windows; File Manager reads the volume label this way), nor the pointers 1Fh and 63h return (DS stays); 1Bh, 1Ch, 32h, 34h, 52h, 51h/62h return selectors. INT 10h: 1130h's ES:BP and 1Bh's buffer (whose first far pointer, to the BIOS's static table, becomes selector:offset), 1009h/1017h buffers. INT 13h 02h: 512 bytes per sector copied back (08h's ES:DI is not converted). INT 15h C0h. INT 25h with the flags left on the stack. INT 33h 16h (the state, its size from 15h) and 09h; 0Ch's handler is passed on untranslated (real mode would call the protected-mode address). INT 4Bh AX=8102h (VDS version) is answered by DOSX itself: AX=0100h, BX=0002h, CX=0001h, DX=0008h, CF clear. w16's DOSX does the same (`dosx/int21.inc`, `int10.inc`, `intbios.inc`), with two deliberate differences: the untranslated functions get an empty buffer in real mode instead of the selector values as segments (so DOS does not write into low memory), and 60h/65h are translated (w16's KERNEL uses them; Microsoft's KERNEL does not). DX4/DX8 under both DOSXes then differ only in buffer bytes DOS leaves unwritten (Microsoft's transfer buffer keeps earlier data there), selector numbers of 0002h-style mappings made in a different order, and DOSBox's INT 25h AX.

**INT 31h 0501h and 48h selectors.** Microsoft's 0501h takes two descriptors per block and returns the first one's selector as the handle in SI and DI; INT 21h 48h takes one descriptor after the tiles (it maps the header after the block); freeing puts the first one back last (it is the next one given out). w16's does the same, so after any mix of these calls the selectors match (DX2: 8347h, 8357h, ... 83AFh, 83BFh, ...).

**Callbacks: the bound.** A real-mode callback address must point into DOSX's own resident code. w16's DOSX has 16 entry points a paragraph apart; callback c = 16i + e is entry e reached as (cb_seg + e - i):(cb_off + 16i), so the offset gives i and the segment then gives e. The bound is the number of segment:offset pairs that reach those entries: 16 x min(4096, cb_seg + 1), about 36000 where DOSX loads (a real-mode offset is below 10000h, a segment not below 0); records beyond the first 16 live in extended memory and may exceed 64 KB. More entry points would raise it at 16 bytes of DOS memory each; no table limits it.

**Resident size.** 0A89h paragraphs became 744h (DX1 dosx_block_paras): the IDT and the ring-3 host stack (G_HD3, used for callbacks and hooked real-mode interrupts) moved to extended memory (the IDT now sits there as Microsoft's does, after the LDT), the host stack shrank to 6 KB, DOSX's start-up stack to 512 bytes, and the command tail, path, EXEC block and fatal-message buffer share the transfer buffer (they are used only before KERNEL runs or after it has ended). Still in DOS memory: the code (with the 256 default-handler stubs and the real-mode hook stubs), the GDT, the TSS, the LDT bitmap, the vector tables, the transfer buffer (4 KB) and the host stack (6 KB). Further candidates: the LDT bitmap and the protected-mode vector tables (only ring 0 uses them), the 256 default-handler stubs (they could be denser).

**More than 64 MB** (boot-test case `bigmem`: the probe TK63, `tests/apps/tk63.c`, as the shell in DOSBox-X with memsize 128 and 256; `tests/dos/xm1.asm`, XM1.COM, prints what the XMS driver reports). XMS 2.0's functions 08h and 09h count KB in 16 bits, so one block is at most 65,535 KB; XMS 3.0's 88h (query any free memory: EAX the largest block, EDX the total, ECX the highest address, BL 0) and 89h (allocate, EDX KB) count in 32 bits and need a 386.
- *Windows 3.1 has no 64 MB ceiling* (Microsoft's WIN.COM, WSWAP, DOSX and KRNL386, 128 MB, DOSBox-X's own XMS 3.0 driver): DPMI 0500h's largest block 65,450 KB, GetFreeSpace(0) 129,137 KB, and TK63 allocated and wrote 129,152 KB (28 blocks). So Microsoft's DOSX takes several XMS blocks of up to 64 MB each; its 0500h gives the largest of them, and its KRNL386 counts them all. GlobalCompact(0) gave 16,320 KB and the largest GlobalAlloc FF0000h bytes (16,320 KB, KRNL386's limit for one block). In 386 Enhanced mode (Microsoft's WIN386) it uses all of it too: 0500h's largest 258,964 KB (with the paging file), its physical pages 130,224 KB, GetFreeSpace 189,038 KB, 186,752 KB allocated and written.
- *Microsoft's WSWAP keeps a block of its own.* XM1 as DOSX.EXE (so that WSWAP starts it) at 80 MB: 08h gives AX=FFFFh, DX=3A3Fh, while 88h gives EAX=EDX=3A40h KB (14,912); after 89h has taken that, 08h still gives FFFFh and 88h 0. In plain DOS the same machine gives 08h FFFFh/FFFFh and 88h 13A40h KB. So WSWAP holds up to 64 MB, which a client gets only through WSWAP's 08h/09h (the XMS interface of its own that `swap/wswap.asm`'s header mentions), and 88h/89h reach the driver past it. (Black-box only; how WSWAP does it was not looked at.)
- *w16's DOSX* (`dosx/init.inc` xms_init, `mem.inc` arena_init): on a 386 it takes blocks until XMS has none of 64 KB or more left (at most 16): with an XMS 3.0 driver 88h/89h first (the largest block, whatever its size, at most 3 GB at once), then 08h/09h, which also empties WSWAP's block. The blocks are locked and sorted by address; the LDT, the IDT and the ring-3 host stack go at the start of the lowest (a lowest block too small for them is given back), and the client's arena runs over all of them: blocks that follow one another are one free block, and between the others a used header spans the gap, so a_next steps over it and nothing merges across it. 0500h's largest block is the largest of those. On an 80286 one 08h/09h block, as before (24-bit addresses: 16 MB at most).
- *[w16] LargeMemory=0* (`docs/w16-settings.md`) makes DOSX use 08h/09h only and keeps every XMS block apart, as Microsoft's DOSX does: then 0500h's largest is at most 64 MB, all the memory is still used. At 64 MB or less (every other test: memsize 16) the two give the same.
- *Found on the way:* LGDT and LIDT with a 16-bit operand size load 24 bits of the base. The IDT is in extended memory, and once XMS 89h gave a block above 16 MB Standard mode hung at its first interrupt; a 386 now loads them with `o32` (`switch.inc` to_pm).
- *Results* (w16's DOSX and KRNL386): with w16's WSWAP (which keeps no block) 0500h's largest 128,403 KB at 128 MB and 259,475 KB at 256 MB, GetFreeSpace the same plus the heap's free blocks, 128,640 and 259,648 KB allocated and written. Under Microsoft's WSWAP at 256 MB 259,584 KB allocated and written, but 0500h's largest and GetFreeSpace show 195,135 and 195,544 KB: WSWAP's block lies apart from the rest, and w16's KERNEL counts DPMI's largest block, not all free memory (3.1's KRNL386 counts all of it: GetFreeSpace 129,137 KB beside 0500h's 65,450; UNVERIFIED how). With LargeMemory=0 at 128 MB: 64,338 KB largest, 128,576 KB allocated, as 3.1's 65,450 and 129,152.

**DOS programs from Standard mode** (`tests/dosapp.sh`: Microsoft's KRNL286, the shell DX7.EXE starting C:\DXD.COM with WinExec; `-s` puts the INT 2Fh spy WC3 between WSWAP and DOSX). Under Microsoft's DOSX in DOSBox (and DOSBox-X), WinExec returns a module handle, WINOLDAP loads, calls WIN.COM's INT 2Fh 4A05h SI=5 (the switcher block) and maps the block with INT 31h 0002h, opens the program and its PIF data, and then Windows stops: no further DOS or DPMI calls, DXD never runs, the screen keeps the desktop with an empty icon title at the bottom left (also with [NonWindowsApp] SwapDisk=C:\). w16's DOSX behaves the same up to that point (DOSXT trace). So the switch could not be compared yet; the WINOLDAP work (WO probes) is expected to find what WINOLDAP waits for. What Microsoft's DOSX does through INT 2Fh in real mode (WC3): 1605h at start (DX=1, DI=030Ah, everything else 0) before XMS, D200h (a QEMM check), 4300h/4310h; at the end 1606h (DX=1); KERNEL adds 4A01h, 4000h, 4007h. DOSX itself does not call 4A05h: the task records WSWAP reads come from WIN.COM. w16's DOSX now broadcasts 1605h and 1606h the same way.

**The `windir=` variable** (`tests/dosxenv.sh`: DOSX.EXE run directly, without WIN.COM, TK18 as KRNL286.EXE logging the environment it gets). Microsoft's DOSX passes its own environment to KERNEL unchanged: with no `windir=` at all KERNEL gets none; an upper-case `WINDIR=C:\FOO` (COMMAND's SET) stays as it is and no lower-case one is added; a lower-case `windir=C:\FOO` (WINENV.COM) stays, pointing elsewhere than DOSX's directory; started from another directory, still nothing is added. So for Windows 3.1's DOSX, Pietrek's statement (ch. 1, BootStrap) that the DPMI host places `windir=` in the environment before starting KRNLx86 does not hold: WIN.COM appends it (§9.5), and DOSX passes on what it got. (WIN386 was not tested.) w16's DOSX gives KERNEL its own environment block as it is too (EXEC with environment segment 0), with the same results in all four cases.

**The install tree** (`make install-tree`, also part of `make all`): `build/w16/` is a Windows 3.1-style directory holding only w16's outputs, each where the reference install keeps Microsoft's: in the Windows directory `WIN.COM`, `PROGMAN.EXE`, `NOTEPAD.EXE`, `CLOCK.EXE`, `CALC.EXE` and `CARDFILE.EXE`; in `SYSTEM\` `KRNL286.EXE`, `DOSX.EXE`, `USER.EXE`, `GDI.EXE`, `SHELL.DLL`, `COMMDLG.DLL`, `WINOLDAP.MOD`, `VGACOLOR.2GR`, `WIN87EM.DLL`, `MOUSE.DRV`, `KEYBOARD.DRV`, `COMM.DRV` and the w16 fonts (`VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON`, `SSERIFE.FON`, and since 2026-11-21 `COURE.FON`, `SERIFE.FON`, `SMALLE.FON`, `SYMBOLE.FON`, `DOSAPP.FON` and the grabber's four `*WOA.FON`, with WIN.INI `[fonts]` and SYSTEM.INI `[386Enh]` entries as 3.1's Setup writes them); and `README.TXT` (from `assets/README.TXT`), which lists what to copy in from one's own Windows 3.1 to boot it (MMSYSTEM, OLECLI, OLESVR, the remaining `[boot]` drivers, the other fonts, WSWAP; DSWAP and the grabber for DOS programs; never KRNL386.EXE). USER, GDI and WINOLDAP joined the tree on 2026-11-13 (until then they were built into `build/dos/` only); WIN386.EXE joined it on 2026-11-20 (`SYSTEM\WIN386.EXE`, with SYSTEM.INI's `[386Enh]` naming its built-in devices as 3.1's Setup writes them: without those `*` names the loader loads none and KRNL386's first VxD call ends Windows with "Invalid VxD dynamic link call to device number 0003"); WIN.COM on a 386 with 1 MB of XMS free now starts 386 Enhanced mode by default, as 3.1's does, and DOS programs there still need the user's WINOA386.MOD; boot-test case `treealone` part `treealone3e` boots the tree alone with `WIN /3`. `build/dos/` is unchanged. `tests/w16tree.sh` lays the whole tree over a scratch copy of the reference install and boots `WIN /S`: Program Manager comes up (`boot: complete`).

**Default INI and group files** (`assets/`, installed into the tree's Windows directory by `make install-tree`, INIs with CR LF): `SYSTEM.INI` and `WIN.INI` (w16's own, laid out as 3.1's, naming only what w16 ships), and Program Manager's `PROGMAN.INI` with three groups, `MAIN.GRP`, `ACCESSOR.GRP` and `STARTUP.GRP` (`assets/progman/`). The tree is meant to be installed as `C:\W16`.
- *Which groups and items:* only programs w16 ships. Main: DOS Prompt (`COMMAND.COM`, through WINOLDAP, WSWAP, DSWAP and the 286 grabber VGACOLOR.2GR, all w16's, §9.9, §9.11) with Program Manager's stock DOS-prompt icon (index 9, as Setup's), and Read Me (`README.TXT`, opened in Notepad through WIN.INI's `txt=` association, with Notepad's icon, as Setup's Read Me is `README.WRI` with Write's), Control Panel, File Manager and Registry Editor (`REGEDIT.EXE`, its own icon, 15.9). Two deliberate deviations from 3.1's Main, by the user's decision (2026-11-21), on by default as compatible quality-of-life changes: the prompt is titled "DOS Prompt" (3.1: "MS-DOS Prompt"), and the Registry Editor is there at all (3.1's Setup puts REGEDIT in no group). The group is a setup-time file (`assets/progman/MAIN.txt` -> `MAIN.GRP`), so there is no `[w16]` key: README.TXT tells the user how to get 3.1's Main (delete the item, rename the prompt with File > Properties; Program Manager saves MAIN.GRP). No test compares w16's Main with Setup's (the w16-tree boot tests run Microsoft's and w16's Program Manager on the same group files). Accessories: Notepad, Cardfile, Calculator, Clock, in Setup's order. StartUp: empty, as Setup makes it. No Games group (w16 has no games yet); "Windows Setup", Print Manager, Clipboard Viewer and PIF Editor are left out until w16 has them.
- *Layout (observed in the reference install's Setup-made files, format facts):* `PROGMAN.INI` `[Settings]` `Window=68 48 580 384 1`, `display.drv=vga.drv` (the display the cached icons are for), `Order= 2 3 1` (bottom first, Main on top; Setup writes the leading blank); `[Groups]` `GroupN=` full paths. Main normal at 0,0-416,202, minimised position -1,-1; the other groups minimised (show 2), each normal rectangle one 22-pixel cascade step further (Accessories 22,22-438,224 at 21,218; 3.1's Games 44,44 at 96,218 and StartUp 66,66 at 171,218 — w16's StartUp takes Games' place, the next step). Items on the 75 x 72 grid from x 21, five to a row, in the definition's order; `cItems` is the number of items (at least 1: an empty group has one empty slot); header words 18h-1Fh 32, 32, bytes 01 04, 0.
- *Paths:* Setup writes each item's command as a bare name (`NOTEPAD.EXE`, `README.WRI`, `DOSPRMPT.PIF`) and its icon file mostly as a full path (`C:\WINDOWS\NOTEPAD.EXE`) but bare for `PROGMAN.EXE` and `write.exe`; both Program Managers find bare names (WinExec/ShellExecute's search; w16's `icon_file`, OpenFile's), and w16's reads the icon file only when the cached bits do not suit the display (UNVERIFIED when 3.1's does), so w16's groups use bare names for both and do not depend on the directory. `[Groups]` are full paths, `C:\W16\MAIN.GRP` ...: Windows 3.1's Program Manager loads bare names too (found through OpenFile's search, from the current directory C:\W16), but writes them back as full paths when it saves its settings on exit (observed: `Group1=MAIN.GRP` became `Group1=C:\W16\MAIN.GRP`); w16's keeps them as they are (a difference, not yet changed). A tree installed elsewhere needs `[Groups]` edited (or bare names, which both read).
- *The files are generated:* `tools/mkgrp.py` builds a .GRP file from a text definition (`assets/progman/*.txt`: name, show state, rectangles, items as `title | command | icon file [index]`) and caches each item's icon as Program Manager does: the icon file's n-th RT_GROUP_ICON, its 32 x 32 16-colour image, converted to the VGA driver's format (12-byte header: hotspot 16,16, 32 x 32, 4 bytes per row, 4 planes, 1 bit; the AND mask top row first; the XOR bits top row first, each scan line's four planes in turn, plane n = bit n of the device colour index: red, green, blue, intensity, with index 7 dark gray and 8 light gray). Every icon comes from w16's own build of the program, so the files hold only w16's data. The generated files are committed (`assets/progman/*.GRP`); `make groups` makes them again, and `make test` (`grpcheck`) fails when they differ from what the definitions and the current icons give.
- *Format check:* `tests/grpref.py` (in `make test`, skipped without a reference install) turns each of the reference install's Setup-made group files into a definition, rebuilds it with `mkgrp.py` from the reference's own programs' icons in a temporary directory, and compares: all four (Main, Accessories, Games, StartUp) come out byte for byte, cached icon bits included (that comparison is how the colour order above was found). Nothing of it is kept.
- *Boot test* `w16groups` (`tests/boottest.sh`): the tree alone as `C:\W16` (`tests/dropin.sh -T`; for now with Microsoft's WSWAP.EXE; w16's own MMSOUND.DRV since 2026-11-14), real keys: Read Me, Accessories restored, Notepad and Calculator started and closed, Windows ended. Microsoft's PROGMAN.EXE reads w16's group files and starts every item; w16's PROGMAN gives the same screens (the minimised groups' own pictures left out) and saves the same group files and PROGMAN.INI. It found that 3.1's Program Manager saves a group whose items' z-order changed by selection (Read Me, selected, first in `rgiItems`); w16's now marks the group changed then (`shell/progman/group.c`, raise_item/lower_item).
- *Program Manager's DDE interface, and a third-party installer* (2026-11-15). The 7-Zip 26.05 Windows 3.0/3.1x setup (NE for Windows 3.00, raw DDE, the user's file; boot test `zipsetup`) installed its files on the tree and then reported "Program Manager did not accept CreateGroup." followed by a stray "WA". Observed with the DDE spy (16.1, OC2, now working on w16's USER) on 3.1, on the tree, and crosswise (w16's PROGMAN on Microsoft's USER and KERNEL, and the reverse): the installer REQUESTs its group "7-Zip 26.05 - 16-bit" (3.1: negative ACK), then EXECUTEs `[CreateGroup(7-Zip 26.05 - 16-bit)]`, `[ShowGroup(7-Zip 26.05 - 16-bit,1)]`, two `[AddItem(C:\7-Zip\X.EXE,Title,C:\7-Zip\X.EXE,0)]` (one command per message, arguments unquoted), REQUESTs the group twice, reads PROGMAN.INI [Groups] and the .GRP file to verify the items, and TERMINATEs. The failure was w16's PROGMAN alone (it failed on Microsoft's USER too, and Microsoft's PROGMAN worked on w16's USER): it made a new group's file name from the name less its blanks, "7-ZIP26..GRP", which DOS refuses. 3.1 keeps only letters and digits (`tests/scripts/dde/names.txt`: 7ZIP2605.GRP, QZ.GRP for every other printable character, a digit from 0 for a name in use: MAIN0.GRP, COLLIDE0.GRP). The stray "WA" was w16's USER too: the STATIC control drew its text from a 256-byte buffer, so the 377-character report stopped after 255 characters, in the "WARNING: ..." line; on Microsoft's USER the same failure showed the whole warning. Found and fixed along the way, each observed on 3.1: a new group's file has one empty item slot (cItems 1); DDE AddItem without a working directory, of a command with a path, gets that directory as its path tag (8101h, "C:\7-Zip\"; without it the installer's own check of the .GRP file failed: "Program Manager shortcut verification failed."); a group's REQUEST reports the window's state as 1 active, 4 restored and not active, 7 minimised and not active, 3 maximised, and lists the items added since loading newest first, then the loaded ones in rgiItems order; ShowGroup(name,1) leaves an already restored group as it is (no activation), and AddItem, DeleteItem and ReplaceItem work on the active group (w16 used the last CreateGroup or ShowGroup's); an EXECUTE's commands are all parsed before any runs, an unknown one runs none, and the run stops at the first that fails; TERMINATE is answered with SendMessage (3.1 sent the installer two: it answers a server's TERMINATE once more); the command block comes back with a NUL at the end of each argument of a known command (`tests/scripts/dde/block.txt`). Now the spy's logs of the installer agree with 3.1's but for the time (3.1 answered the whole conversation within one 55 ms tick, w16's PROGMAN takes about 0.5 s on the tree, writing the group file for each command), and its .GRP file and PROGMAN.INI are byte for byte 3.1's. Seen and not yet looked into: in the installer's dialog, its disabled edit control's text is gray on 3.1 and black on w16's USER (US4's disabled edit was drawn as usual); w16's message box for the report is 20 pixels shorter, its text 8 pixels lower (MessageBox's layout for a long text, UNVERIFIED rules); 3.1 acknowledged CreateGroup("---") and CreateGroup("Q\xE9Z") but made no group; and when a DDE DeleteGroup rewrites Order=: one group created and deleted left "Order=2 3 4 1 5" (no leading blank, the deleted group still named: what w16 does), but with two created, deleting either or both left Order= as the second CreateGroup wrote it (" 2 3 4 1 5 6"), so the rule is not known (w16 rewrites it at every delete). Tests: boot test `ddescen` (tests/ddescenario.sh, every tests/scripts/dde script: Microsoft's and w16's PROGMAN give the same answers, command blocks and files) and `zipsetup` (the installer itself when its file is there: w16's PROGMAN on Microsoft's rest 0 pixels and the same .GRP and PROGMAN.INI as 3.1's; the tree under KRNL386 and KRNL286: 3.1's .GRP, its group window one cascade step higher than on 3.1, which has one group more).

### 9.7 The rest of KERNEL's exports; THHOOK and TOOLHELP

228 of the 233 ordinals Windows 3.1's KERNEL has (`docs/exports/KERNEL.txt`) are exported now; missing are EMSCopy (160: the one call tried ended the probe on 3.1) and NetBIOSCall (103: not observable without a NetBIOS in DOSBox; only WfW's MSSFS imports it); §9.4's `__GP`, HasGPHandler and SetSigHandler came later (TK38-TK41). Each was observed on 3.1 first, as the shell of the reference install (`tests/dropin.sh -r`, Microsoft's KRNL286; `tests/refrun.sh` for KRNL386), by three probes:

- **TK50** (`tests/apps/tk50.c`, `tk50s.asm`; in `make test`, with `tk50ref.h` from the KRNL286 run): calls each export through GetProcAddress with known values in every register and logs the result, the side effects, the bytes it popped and which registers it kept (BX, CX, DX, SI, DI, BP, DS, ES, the carry set and clear). The "kept" masks in `tk50ref.h` are minimums: w16 may keep more, never less. Calls that ended the probe on 3.1 are only made on request (EMSCopy with zeros, IsROMFile, DebugDefineSegment); IsSharedSelector(0) and IsROMFile(0) fault on 3.1 (IsROMFile because it removes 6 bytes of arguments, not the word TK50 passes: below).
- **TK51** (`tk51.c`, `tk51s.asm`, the child TK51C): THHOOK word by word, every notification KERNEL sends to a ToolHelpHook procedure (all registers recorded) while a DLL is loaded and freed, a string is written and a program runs, then Microsoft's TOOLHELP.DLL on top: GlobalInfo/First/Next/EntryHandle/EntryModule, ModuleFirst/Next/FindName, TaskFirst/Next/FindHandle, StackTraceCSIPFirst/Next, MemManInfo, SystemHeapInfo, TimerCount, LocalInfo/First/Next, ClassFirst/Next and NotifyRegister. Run it on both KERNELs (`tests/dropin.sh -f build/dos/TK51.EXE=TK51.EXE -f build/dos/TK51C.EXE=TK51C.EXE -i 'boot:shell=C:\TK51.EXE'`, with and without `-r`) and compare the logs.
- **TK52** (`tk52.c`): FatalAppExit, DebugBreak, UndefDynlink, one per run (command tail `f`, `b`, `u`; `q` makes a message queue first), with a screenshot.

Findings (KRNL286 unless noted; implemented in `kernel/toolhelp.c`, `kmisc2.c`, `khook.asm`, SetPriority in `task.c`, LocalNotify in `local.c`):

- **THHOOK** (332, a data entry): 00h/02h the Burgermaster's handle and selector (the same), 04h KERNEL's module table, 08h KERNEL's PSP, 0Ah the PDB of the (first) task, 0Eh the first task, 10h the current task, 12h/14h 0 (LoadTDB, LockTDB), 16h 3FFCh, 18h 72h (dword: the selector table), 24h/26h/28h GDI's, USER's and the shell's module (Schulman gives KRNL386's places, 4 bytes on). Other words hold values of unknown meaning (1Ch, 20h, 2Eh-3Ch, 46h-4Ah, 56h-6Ch, 86h-9Eh); w16 leaves them 0. GetLPErrMode (99) points into KERNEL's data segment at a byte 0.
- **Notifications** (ToolHelpHook 341 returns the previous procedure, 0 at first; RegisterPtrace 202 takes one too): KERNEL calls the procedure with DS = its own data segment and AX = 12h (OutputDebugString, ES:SI the string), 50h (a segment loaded: BX its number − 1, CX the selector, DX 0, SI its flags in the module table, ES:DI the module name), 64h (a DLL about to be initialised: CX:BX its entry point, 0:0 for none, SI the hModule), 65h (a module deleted: ES the hModule), 52h (a freed segment: BX), 0Dh/0Eh (task out/in), 59h (a task starts, on its own stack: CX:BX its entry point), 62h (a task ends: BL the exit code). TOOLHELP turns these into NFY_OUTSTR, LOADSEG (wSegNum = BX + 1, wType = SI), STARTDLL, DELMODULE, FREESEG, TASKOUT/IN, STARTTASK, EXITTASK. On w16 TOOLHELP's NotifyRegister now reports the same kinds in the same order; w16 loads every segment with its module, so it sends a LOADSEG for each where 3.1 sends one per segment brought in.
- **TOOLHELP on w16's KERNEL** (TK51, both logs compared): the same GlobalInfo counts and walks (Burgermaster count + 1, LRU and free lists), the same modules with the same usage counts and paths, the same task entry (stack fields from the DGROUP header, queue, PSP), the same stack trace, MemManInfo, TimerCount and USER's classes; SystemHeapInfo works once GetHeapSpaces does (below). Differences left (LocalInfo/LocalFirst/Next and GlobalEntry's wHeapPresent were one: w16's local heap had KRNL386's information block; it has KRNL286's now, TK53), and GlobalEntryModule for KERNEL's own segments (made by the bootstrap, not in the global heap).
- **GetHeapSpaces** (138, which USER's GetFreeSystemResources and TOOLHELP's SystemHeapInfo use): "total" is FFFFh less the offset of the heap's information block (USER: F51Bh for pHeap AE4h) and "free" that less the bytes in use. w16 gave the heap's present size, and USER's percentage computation divided by zero (Program Manager's About box would have too).
- **A module table is its own owner** (TOOLHELP: GT_MODULE).
- **GetSetKernelDOSProc** (311): the procedure KERNEL hands INT 21h on to, at first DOSX's (0053:02D2): KERNEL's own INT 21h handler, DOS3Call and NoHookDOSCall all go through it. In w16 it is `old21`.
- **SetPriority** (32): adds the low byte of the second argument to the TDB's priority byte (08h) in 8 bits, clamps to −32..15, re-sorts the task list, returns the result sign-extended.
- **hmemcpy** (348): memmove across 64K boundaries, DX:AX = 0, the carry clear. **IsDBCSLeadByte** (207): 0 for every byte. **Reserved3/4** (79/80): Windows 2's AnsiUpper/AnsiLower (a character when the selector is 0, else the string in place; E9h ↔ C9h). **IsSharedSelector** (345): 1 for a DLL's (KERNEL's, USER's) selectors and handles, 0 for a program's and an hTask. **GetCodeHandle** (93): DX:AX = selector:handle of the segment of an address (also a data address), or of MAKELONG(segment number, hModule). **PatchCodeHandle** (110): DX:AX = selector:handle; a program's exported "push ds/pop ax/nop" prologue reads three NOPs. **OpenPathname** (75): a DOS open, FFFFh on failure. **CallProcInstance** (53): enters the procedure at ES:[BX+2] with AX = ES:[BX] (the hInstance). **SwitchStackTo/Back** (108/109): To puts the caller's SS:SP (less 2, where its BP goes) at 04h/02h of the new segment, wTop at 0Ah, wSP at 0Ch and 0Eh, copies the caller's frame below BP to just below wSP and returns there with BP = wSP; Back returns to the stored SS:SP, popping BP (`khook.asm`). **LocalNotify** (14): sets the heap's notification procedure (KRNL286: information block 18h), returns the previous one; KERNEL calls it with LN_OUTOFMEM (bytes needed), LN_MOVE and LN_DISCARD (TK54; §9.2 local heap). **DefineHandleTable** (94): AX = the caller's DS. **SetSwapAreaSize** (106): AX = the request. **GetFreeMemInfo** (316): −1. **MemoryFreed**, **LimitEmsPages**, **SwapRecording**, **GetLastDiskChange**, **Get/SetWinDebugInfo**: 0. **DiagQuery** (339): 1 under `WIN /B`. **InitTask1**, **FatalExitHook**: store a procedure (FatalExitHook returns the previous one). No-ops that keep every register: ValidateCodeSegments, ValidateFreeSpaces, EnableDos, DisableDos, EnableKernel, A20Proc, ReplaceInst, K329 (the retail KERNEL leaves the buffer alone), _DebugOutput (C convention), FlushCachedFileHandle.
- **FileCdr** (130; Schulman ch. 5; TK64, `tests/apps/tk64.c`, with its helper task TK64A and the copies TK64M/O/P that each run one case of a task ending; `make test` without USER, boot-test case `filecdr` with it; reference `tests/tk64ref.sh`; KRNL286 and KRNL386, Standard and 386 Enhanced mode alike but where noted). One callback for the whole system: FileCdr(proc) installs it (NULL: removes it) and returns 1 unless another task installed the one there, then 0 and nothing changes, even when that task has ended; FileCdr(-1) returns it. KERNEL's INT 21h hook calls it, in the calling task and on its stack, after DOS succeeded with 39h, 3Ah, 3Ch, 41h, 4301h, 56h, 5Ah, 5Bh or 6Ch (any 6Ch, also an open of a file that is there), never for 3Bh, 3Dh, 3Eh, 40h, 45h, 4300h, 47h, 4Eh, 57h, 68h, FCB calls or a failed call; DOS3Call and KERNEL's file APIs pass through it (_lcreat, OpenFile OF_CREATE/OF_DELETE; not _lopen), NoHookDOSCall does not. Arguments (PASCAL): the caller's AX as it was (3C55h stays) and a copy of the name as given with "D:" in front (drive upper-cased) and, for a relative name, "\" + that drive's current directory + "\" (no case change, no "."/".." resolution, no truncation: "C:\TK64D\c64a.txt", "C:/TK64D/../tk64d/./c64g.txt"); 56h gives the old name, 5Ah the name DOS made. Its return value is ignored; the caller gets DOS's registers and carry whatever the callback leaves; calls nest (the callback's own changes call it again). Task end: KERNEL keeps the callback; USER's AppExit (only for a program that called InitApp) removes it when it lies in the ending program's module (Pietrek ch. 3). One whose code segment is gone is removed at the next change instead of being called (Standard mode; 386 Enhanced mode faulted the caller there). w16: `kentry.asm` cdr_check (a compare when nothing is installed), `kmisc.c` filecdr_notify, kmisc_app_exit (USER's part, done by KERNEL from the task's end so that w16's USER needs nothing). Until 2026-11-20 w16 stored the callback and never called it, and dropped it at its task's end.
- **FatalAppExit** (137): with a message queue, USER's SysErrorBox(text, "Application Error", 0, 8008h, 0): one "Close" button (pixel-identical on w16); the program then ends. **UndefDynlink** (120): the same box with "Call to Undefined Dynalink". **DebugBreak** (203): returns when no debugger is there.
- **IsROMModule** (323) and **IsROMFile** (326): Schulman gives each one HANDLE argument, but both remove 6 bytes of arguments on 3.1 (TK31, which now logs "SP moved" for any call that does not remove exactly what it was passed; KRNL286 and KRNL386), return AX = 0 with the carry clear and keep BX, CX, DX, SI, DI, BP and ES. What the arguments mean is not known (FALSE either way outside ROM Windows). Microsoft's VER.DLL calls IsROMModule through GetProcAddress (§20.6); w16's took 2 bytes until 2026-11-18. Now `khook.asm`, RETF 6.
- UNVERIFIED (marked in the sources): THHOOK's unknown words and HeadPDB with several tasks; the exit code after FatalAppExit; DisableKernel and DebugDefineSegment (no-ops here: the calls tried ended the probe on 3.1); what SwitchStackBack copies back; InitTask1's use; LogError/LogParamError's notifications (66h/67h, Schulman's layout); DebugBreak with a debugger.
- Registers: where 3.1 keeps more than a KAPI function does, a wrapper keeps BX, CX, DX, ES and the flags (`KEEPREGS` in `khook.asm`: SetPriority, DiagQuery, SwapRecording, OpenPathname, IsSharedSelector, GetCodeHandle); GetSetKernelDOSProc and FatalExitHook are KAPI_ES. `tk50ref.h`'s "kept" masks are the minimum.
- Importers in the reference installs (`tools/neimports.py --importers KERNEL`): FatalAppExit (23 programs and DLLs of 3.1, TOOLHELP among them), IsDBCSLeadByte (COMMDLG, Notepad, Cardfile, Calendar, Paintbrush, SysEdit), SetSwapAreaSize (Terminal), RegisterPtrace (TOOLHELP), FlushCachedFileHandle (Setup), _DebugOutput (WfW's COMMCTRL), NetBIOSCall (WfW's MSSFS); hmemcpy by name (GetProcAddress) in Sound Recorder; THHOOK and ToolHelpHook by GetProcAddress in TOOLHELP (TK51 exercises it).

### 9.8 WINOLDAP.MOD: DOS programs from Standard mode

w16's WINOLDAP.MOD is `winoldap/` (C, OpenWatcom, medium model, with `shell/start.asm`'s start-up and `winoldap/woasm.asm` for the register-level calls; `make` builds `build/dos/WINOLDAP.MOD`). Copied over `WINDOWS\SYSTEM\WINOLDAP.MOD` of the reference install it runs DOS programs under `WIN /S` with Microsoft's WIN.COM, WSWAP, DSWAP, DOSX and KERNEL. Everything below was observed black-box (Microsoft's WINOLDAP.MOD, WSWAP.EXE, DSWAP.EXE and DOSX.EXE were never disassembled): with `tests/worun.sh` (WIN /S in DOSBox-X on a virtual X display, real key presses through xdotool, screenshots, DOSBox-X's file log), the WO probes (`make woprobes`: WO1.MOD, a logging stand-in with WINOLDAP's module name and exports; WO2.EXE, a protected-mode INT 2Fh/31h/21h spy started from WIN.INI `load=` that also copies WIN.COM's switcher block at every switch; WO3.COM, a real-mode INT 21h/2Fh spy to put between two programs of the chain), WC1/WC3/WC4 of §9.5, and the published NE, resource and PIF formats (`tools/neinfo.py`, `tools/neres.py`). Schulman (*Undocumented Windows* ch. 5, 6) and Pietrek (ch. 1, 3) were read for orientation; every fact used was confirmed by observation.

**What it is** (`tools/neinfo.py`): module WINOLDAP, "Microsoft Windows Old Application Support Module (3.10)", NE flags 0312h (multiple data, Windows API), expected version 3.10, two segments (code 3B66h bytes, data 1468h), heap 320h, stack 1200h; exports WNDPROC @1 and DIALOGPROC @2 (w16's has the same names and ordinals). Imports: KERNEL (among the usual: InitTask, WaitEvent, the global/local heap, OpenFile, _lopen/_lclose, profile functions, DOS3Call, GetDOSEnvironment, AllocSelector, GlobalDOSAlloc/Free, __AHINCR, __WINFLAGS, and the undocumented WinOldApCall 151, IsWinOldApTask 158, GetWinOldApHooks 344, FatalAppExit 137), USER (MessageBox, Enable/DisableOEMLayer 3/4, InitApp, the window, dialog, menu and clipboard functions, GetNextQueueWindow 274, CreateIcon, wsprintf, AnsiUpper/Lower, lstrcmpi), GDI GetNearestColor, KEYBOARD AnsiToOem/OemToAnsi, COMM 17 and 18 (names unknown). Resources (`tools/neres.py`): an icon (32 x 32, 1 and 4 bits), the "Parameters" dialog (206 x 48: static "Parameters", edit id 10, OK), version info ("Non-Windows application component for standard mode"), and the message strings 1-60 (w16 has them word for word: §6.8). WINOA386.MOD (Enhanced mode, out of scope) has its own; Schulman lists WinOldAppHackOMatic and INT 2Fh 1600h/1683h/1684h/4001h/4002h calls for it.

**How it starts** (WO1, SH14): `WinExec` of a DOS program (.COM, .EXE without NE header, .BAT, .PIF; and of any other file that is no Windows module, a document too: DOS then runs it as a .COM program) makes KERNEL load WINOLDAP.MOD (searched as `C:\WINOLDAP.MOD`, the Windows directory, SYSTEM; a new instance each time) as a task and return its hInstance. `LoadModule` of such a file returns 11 and starts nothing, in both modes (SH14; also a DOS .EXE). WinExec hands a .PIF to WINOLDAP unopened: a missing one gives WINOLDAP's "Cannot find the specified file." box, and WinExec its instance. InitTask gives ES:BX = PSP:81h, DX = nCmdShow (1 from Program Manager), DI = hInstance, SI = 0 (hPrevInstance is 0 for every instance). The command tail is `args NUL fullpath LF`, its length byte counting args, the NUL and the path (`C:\WC1.COM hello` gives `10 'hello' 00 'C:\WC1.COM' 0A`; MS-DOS Prompt gives `18 00 'C:\WINDOWS\DOSPRMPT.PIF' 0A`). The task's TDB flags (16h) are 0001h (TDBF_WINOLDAP), its TDB name WINOLDAP, PSP 48h 0. The environment is WIN's. WinOldApCall(0) gave AX=BX=CX=DX=SI=0, DI=003Bh (KRNL386), GetWinOldApHooks 0.

**32-bit programs** (a PE file: "PE\0\0" at the MZ header's e_lfanew; SH14 and Program Manager's File > Run of `tests/scripts/runassocfiles.py`'s PE.EXE, a minimal PE32 image whose MS-DOS stub leaves a marker file; the reference install has no Win32s). Windows 3.1 reads the MZ header and the 64 bytes at e_lfanew, then looks for Win32s' loader W32SYS.DLL as a library is searched (current directory, Windows directory, SYSTEM, the task's directory, PATH: DOSBox-X's file log), once per session (later PE files were not followed by another search). Without it, in both modes: `LoadModule` returns 21 (the SDK's "requires 32-bit extensions") and starts nothing; `WinExec` and `ShellExecute` hand the file to WINOLDAP like a DOS program and return its instance: DOS runs the stub (most say "This program cannot be run in DOS mode." and end: the screen flashes black); Program Manager's Run says nothing. Given a 21 (by w16's KERNEL), Program Manager 3.1 shows "Unexpected DOS error: 21." in its "Application Execution Error" box. w16's KERNEL does the same search and returns 21 from LoadModule; **with `[w16] PEError=1` (the default, §6.8) and no W32SYS.DLL, WinExec returns 21 too** and starts nothing, ShellExecute passes it on, and w16's Program Manager shows "Cannot run C:\RA\PE.EXE. This 32-bit program requires Win32s, which is not installed." in its "Application Execution Error" box (w16's own text; the style and handling of its other boxes, §13.4). With W32SYS.DLL found, w16 does what 3.1 without Win32s does whatever PEError says: UNVERIFIED what Windows 3.1's KERNEL then does with W32SYS.DLL (w16 has no Win32s hand-over; the Win32s references below make it observable). Boot-test case `runpe`: PEError=0 as 3.1 (every screen, the stub run), PEError=1 the box and nothing run.

**Win32s references** (`tests/win32ssetup.sh`, TESTING.md section 1): `.tools/ref/win31s/cdrive` and `.tools/ref/wfw311s/cdrive` are the Windows 3.1 and WfW 3.11 references with Microsoft Win32s 1.30c installed by its own Setup (run in 386 Enhanced mode, which Win32s needs; it does not run in Standard mode), for work on 32-bit programs; `tests/dropin.sh DROPIN_REF=` boots w16's parts on them. What Setup did, in both (its screens and the full lists: the script's header and its `install.log`): W32SYS.DLL, WIN32S16.DLL, WINMM16.DLL, WIN32S.INI (`[Win32s] Version=1.30.172.0`), 32-bit WinHelp (WINHLP32.EXE) and OLE 2 (COMPOBJ, STORAGE, OLE2*.DLL, TYPELIB, STDOLE.TLB, OLE2.REG merged into REG.DAT) in WINDOWS\SYSTEM, and on 3.1 only a new OLECLI.DLL (3.1's kept as OLECLI.W31; WfW 3.11's is left alone); the 32-bit side (W32S.386, W32SKRNL.DLL, W32SCOMB.DLL, WIN32S.EXE, the Win32 DLLs such as COMCTL32, COMDLG32, CRTDLL, OLE32, VERSION, WINMM, WSOCK32, and the NLS tables) in WINDOWS\SYSTEM\WIN32S; SYSTEM.INI `[boot] drivers=mmsystem.dll winmm16.dll` and `[386Enh] device=C:\WINDOWS\SYSTEM\WIN32S\W32S.386` (the old file kept as SYSTEM.OLD); the sample game Freecell (Setup's default) in C:\WIN32APP\FREECELL with a "Win32 Applications" group (WIN32APP.GRP, PROGMAN.INI). A 32-bit test program (`tests/win32s/w32t.c`, OpenWatcom's wcc386 and wlink `system nt_win`) runs on both: GetVersion returns 80AC0A03h (the high bit: Win32s; Windows 3.10, on WfW 3.11 too).

**What Windows 3.1's WINOLDAP does** (WO2, file log, in this order):
1. INT 2Fh AX=4A05h SI=5 (WIN.COM's handler, §9.5: DX:AX = the switcher block), INT 31h 0002h on its segment. No answer: string 55, "Standard-mode switcher is not running. Cannot start application."
2. The PIF: the one given; else `NAME.PIF` beside the program, then `NAME.PIF` and `_DEFAULT.PIF` through OpenFile's search (current directory, Windows directory, SYSTEM, PATH). It reads (INT 21h directly) after seeking to the end: for a file of 171h bytes or more the header at 171h ("MICROSOFT PIFEX"), then the headers its chain leads to (each header's next offset; FFFFh the end) until "WINDOWS 286 3.0", whose data it reads at the header's data offset (4 bytes: the XMS KB limit and required; 2: the 286 flags) — at 19Dh and 1A1h in Setup's DOSPRMPT.PIF, 21Bh and 21Fh in a PIF of 3.1's PIF Editor (the 386 section first); then 63h (1: flags), 20h (4: memory KB), 24h (3Fh: program), 65h (40h: start-up directory), A5h (40h: parameters), EDh (2), 16Fh (2), 02h (1Eh: title). Without the 286 section (no PIFEX header, a 2.x PIF, a next offset past the end, a file cut short) only the base is read; the checksum is not looked at (`tests/pifread.sh -m std`, DOSBox-X's file log; `winoldap/pifread.c`, shared with WINOA386, §12.11). What it does with the fields, seen in the program's task record (WO2's snapshots, one setting at a time): +1Fh (a word) KB Required, at least 128 (-1 too); +28h 01h when the program may not be switched away from (63h 04h Prevent Program Switch, or it uses COM1-COM4 or the keyboard directly: 63h 80h, 40h, the 286 flags 4000h, 8000h, 16Fh 10h), 02h graphics (63h 02h), 04h Close Window on Exit (63h 10h), 08h the MS-DOS Prompt banner; +29h the reserved keys: 01h Alt+Tab, 02h Alt+Esc, 04h Ctrl+Esc (286 flags 01h, 02h, 10h), 08h No Save Screen (20h), 10h No Screen Exchange (63h 08h, and Alt+PrtSc or PrtSc reserved, 04h, 08h). 63h 01h and 20h, EDh, 16Fh but its 10h, 170h and the 286 flags' 40h changed nothing. What WSWAP and DSWAP make of them is theirs (with Close Window on Exit off DSWAP waits for a key after the program). UNVERIFIED: the XMS limits (with them set WO2 recorded no block at the switch; the program got 135 paragraphs less).
3. The program: from the PIF (COMMAND.COM is COMSPEC's: `Z:\COMMAND.COM` was opened directly, no search) or the path KERNEL gave; it is opened to check it is there ("Cannot find the specified file. Make sure that the path and filename are correct.", string 16, in a box titled with the program's name). The title is the PIF's, else the name of the file WinExec was given without extension in capitals (`WC1`; `PROBE` for a PIF C:\PROBE.PIF whose title is blank). Parameters: the arguments given to WinExec as they are (`hello world`), else the PIF's to their first NUL with their blanks, after one blank (` a b`; `  sa  sb  ` gave `   sa  sb  `; 64 characters without a NUL all 64); `?` shows the Parameters dialog first. A .BAT named by a PIF runs as COMSPEC with the tail ` /c C:\WC1.BAT  stdargs`. A missing program's box is titled with the program as the PIF names it (`C:\NOWHERE.COM`). The PIF's start-up directory becomes the current directory (WC1 found itself in `C:\WINDOWS\SYSTEM`; the block has no field for it).
4. KERNEL closes the files it keeps open (all of Windows' module files close here in the file log; Schulman: WinOldApCall with a mode other than 0 closes KERNEL's cached files; w16 calls WinOldApCall(1) at this point). Without it the second DOS program's switch ended Windows with WSWAP's "Fatal exit! Windows cannot be swapped back" (DOS out of file handles). Then SYSTEM.INI `[boot] 286grabber=` through OpenFile's search: missing, "Cannot find the file WINOLDAP.GRB. ..." (string 11; pixel-identical in w16).
5. Its window: class `WOA`, style 34CA0000h (overlapped, caption, system menu, minimize box, clip siblings, minimized, visible), titled as above: the icon at the bottom left while the program runs and while it is swapped out.
6. Room for the program's swap file: free space on the swap directory's drive (block D7h) against block 183h (paragraphs; 96ACh in DOSBox = 603 K): string 9, "Insufficient disk space on drive / A / Desired disk space: 603 K / ..." (the drive letter 16 blanks into the field) — a warning only: it switches afterwards all the same.
7. The block (below), then a GlobalAlloc'd buffer (GlobalAlloc(0040h, block 183h x 16 + 1000): Microsoft's WINOLDAP on w16's KERNEL asks 9DB08h with 183h = 9D72h; on Windows 3.1's KRNL286 WO2 sees the buffer's selector with limit DB0Fh, and with 16 KB less DOS memory (`LOADFIX -16 WIN /S`) 183h = 9926h and limit 964Fh, i.e. 99648h rounded to a paragraph; its linear address, INT 31h 0006h, goes to block 185h; WSWAP keeps Windows' low memory there while the program runs: afterwards it holds DOSX's PSP and code), then DisableOEMLayer, **INT 2Fh AX=4B80h** (DOSX passes it to real mode; it returns at once) and **INT 21h AH=0Bh** (AL still 80h): the switch happens inside that DOS call. WSWAP saves Windows' state in `~WOA100F.TMP` (16 + 8 + 4 + 2 + 4 bytes) in the swap directory and exits to WIN.COM, which runs DSWAP; DSWAP loads the grabber (block 87h) and runs the program (block 189h, tail at 05h) as its child, with WIN.COM's environment, video mode 3, the cursor at 0,0 and its own INT 08h, 09h, 15h, 1Ch, 21h-24h, 2Fh handlers (INT 10h the grabber's). For COMMAND.COM without parameters (task record flag 08h) it first prints its banner (`■ Type EXIT and press ENTER to quit this MS-DOS prompt and / return to Windows. / ■ Press ALT+TAB to switch to Windows or another application.`, in a double box) to standard output: under `WIN > FILE` it goes to the file. Alt+Tab, Ctrl+Esc, Alt+Esc or the program's end make DSWAP save the program (`~WOA10nn.TMP`, nn its record number) and return to WIN.COM, which runs WSWAP again; WSWAP reads `~WOA100F.TMP` back, deletes it, and the DOS call returns.
8. EnableOEMLayer, frees the buffer. Block 172h nonzero (08h after the disk-space warning: WSWAP could not swap): the task ends, its icon goes. Its record free again (byte 29h = 80h): the program has ended; INT 21h 3Dh of `<second swap dir>~WOAfffe.tmp` (not found), the record's window handle set to FFFFh, the task ends. Block 178h bit 08h (Ctrl+Esc): it runs TASKMAN.EXE (WinExec). Otherwise the icon stays, next in Z order after the window WSWAP came back to (block 17Eh names its record; w16 brings its icon to the top and activates that window), so that USER's Alt+Tab returns to the program left last; opening it (Alt+Tab, double click, the Task List's Switch To) switches back the same way, without reading the PIF again.

**WIN.COM's switcher block as WINOLDAP uses it** (offsets from the 4A05h/5 pointer; with §9.5's): 05h the program's command tail (length, text, CR: DOSX's tail is overwritten; length 0 for none); 87h the 286 grabber's full path; 173h FFFFh at each switch (WSWAP writes the session number there); 178h bit 08h Ctrl+Esc (cleared at each switch); 17Ah-17Dh `0F 10 count 01` (17Ch: tasks in the list besides Windows itself, WSWAP lowers it when a program ends); 17Eh 0Fh at each switch (WSWAP leaves the record to activate there); 183h the paragraphs DOS programs get; 185h the buffer's linear address; 189h the program's path (at a resume: what was there before, DOSX's path). **Task records**: sixteen of 32h bytes from 1D9h (§9.5's "records at 1F0h" are these, seen from their 1Bh), numbered 0-15, record 15 being Windows itself (`dosx.exe`): name at +0 (30 bytes), +1Fh 80h for a DOS program, +21h FFFFFFFFh for a Windows entry, +28h flags (0Ch a new DOS program with DSWAP's banner, i.e. COMMAND.COM without parameters, 04h any other or once swapped out, 06h a Windows application, 46h Windows), +29h kind (80h free, 00h DOS program, 28h Windows application, 68h Windows), +2Ah the next record in switching order (a ring through record 15), +2Bh the session it lives in (its swap file; 0Fh for Windows applications), +2Ch 10h, +2Dh 01h once swapped, +30h the window handle. Records are taken from 14 downward. The first DOS program also gets a record for the Windows application the user was in (Program Manager: its title and handle; also when started from File > Run or after a PIF's Parameters dialog: w16 takes the window active when the task started, else the first visible titled top-level window) before its own, and it is not changed afterwards; each started or resumed program is moved to the front of the ring (after record 15); WSWAP/DSWAP rotate it when switching back. A second MS-DOS Prompt: record 12, count 3. "Too many non-Windows applications. Cannot start application." (string 56) when no record is free: a limit of WIN.COM's block, not of WINOLDAP's.

**w16's WINOLDAP** does all of the above in the same order and writes the same bytes; differences: it reads the PIF through KERNEL (_lread) instead of its own INT 21h calls (the same DOS calls reach DOS); it switches from its message loop, a 50 ms timer tick after WinExec returned (Windows 3.1's caller also runs again before the program does); when the shell has no window of its own (`tests/dosapp.sh`'s DX7) Windows 3.1's WINOLDAP never switches at all (it stops after loading a USER segment, no more DOS or DPMI calls: it seems to look for another top-level window to enter as the Windows application and never finds one; with Program Manager as the shell and DX7 from `load=`, DXD runs under Microsoft's DOSX and KRNL286), while w16's enters the desktop window in that record (WSWAP needs one: without it Windows ended when the program did); the icon is w16's own (PROGMAN's stock DOS-prompt icon, §6.8).

**Tests.** `tests/winoldap.sh [-o DIR] [-K | -k KRNL286.EXE] [SCENARIO...]` runs each scenario (`tests/scripts/winoldap/*.txt`: prompt, two, com, bat, pif, errors, noswap) with Microsoft's WINOLDAP.MOD and then w16's, and compares WO2's block snapshots (the buffer address masked), WC1's record of what the DOS program got, and the screenshots (the icon corner of Windows screens masked; up to 40 pixels on text screens for the cursor). Not in `make test` (about 3 minutes a scenario, needs xvfb-run, xdotool, ImageMagick).

**Results** (`tests/winoldap.sh`, Microsoft's KRNL386 on DOSBox-X; build/wo/shots-20261108-212925): prompt, two, com, bat, errors, noswap pass: every block snapshot byte-identical to Microsoft's WINOLDAP (the buffer address aside), WC1's record identical, every screenshot identical (the DOS screens, the Task List, Windows after each return, the error boxes) apart from the masked icon corner; pif differed only in the Parameters box's caret (34 pixels, its blink phase; the run before was 0), which the 40-pixel allowance now covers. On Microsoft's KRNL286 (`tests/worun.sh -K`, and `tests/boottest.sh winoldap`: COMMAND.COM /C ECHO HI from WIN.INI run=, 0 pixels against Microsoft's WINOLDAP) the same.

**With w16's KERNEL.** Since master f9bed36 w16's KERNEL starts WINOLDAP for DOS programs and exports WinOldApCall, RegisterWinOldApHook and GetWinOldApHooks (§9.4); `tools/kimports.py -m WINOLDAP.MOD` had listed FatalAppExit 137, WinOldApCall 151 and GetWinOldApHooks 344 as the missing ones. Microsoft's WINOLDAP then runs on w16's KRNL286 (the boot agent's result). w16's WINOLDAP on w16's KRNL286 (`tests/boottest.sh winoldapk`, plain DOSBox, as w16's KERNEL does not yet boot under DOSBox-X, §9.4 gap 1): the program runs (`HI` printed, the same block, no banner) and Windows is swapped back in, then KERNEL faults (exception 0Dh at KERNEL:1 18A4h, in global.c's internal code, error code 1974h, ES a 16-byte block, SS our DGROUP) right after reading a file and before our task ends: not a KERNEL bug: w16's WINOLDAP sized the buffer as (top of memory - 1600h) x 16 = 8A000h, but WSWAP's image of Windows' low memory starts lower on w16's KRNL286 (the buffer's first paragraph held 03FB:0), so WSWAP wrote past the buffer and wiped the next arena's header. With the size as Microsoft's WINOLDAP computes it (above) `winoldapk` is 0 pixels. The command tail's LF stays in the PDB after InitTask, as on 3.1 (WO1: `0F 00 'Z:\COMMAND.COM' 0A`; InitTask makes only a CR a NUL): w16 made it a NUL, and Microsoft's WINOLDAP, searching for the LF after the path, ran past the PDB (the TDB's second half) into the next TDB and zeroed a byte of its task link; the second MS-DOS Prompt then hung Windows (boot-test `swapesc`, part mswoa).

**Open / UNVERIFIED.** What the 1000 bytes beyond block 183h's paragraphs are for, and what decides the disk-space figure besides block 183h; the XMS limits of the PIF's 286 section and what string 3/7 (memory) needs; what Windows 3.1 reads from SYSTEM.INI after the PIF and from WIN.INI (a key of [NonWindowsApp]? not seen); COMM 17/18 (suspending the COM ports, strings 4/32); the clipboard (Alt+PrtSc in a DOS program, the "Paste" item, strings 33-38) and the icon's system menu; the "Switching..." string; WM_QUERYENDSESSION ("Application still active", strings 39/40) and closing the icon; DOS programs switching straight to each other (Alt+Tab with two swapped out); the 0Ah after the path in the command line; how .BAT files and COM ports are handled beyond the observed runs. WINOA386.MOD (386 Enhanced mode) is not started.

### 9.9 WSWAP.EXE and DSWAP.EXE: the Standard-mode task switcher

w16's WSWAP.EXE and DSWAP.EXE are `swap/wswap.asm` and `swap/dswap.asm` (NASM, `swap/swap.inc` for what they share; `make` builds `build/dos/WSWAP.EXE`, `DSWAP.EXE` and `DSWAPD.EXE`, a build that appends a line to C:\DSWAP.DBG at its DBG points, and installs the first two in `build/w16/SYSTEM`). They are .COM-format images under the names WIN.COM looks for (DOS loads any file without an MZ header as one). Everything below was observed black-box on the reference 3.1 install with the SW1 probe (`tests/dos/sw1.asm`, `make swprobes`: a resident spy loaded before WIN that records INT 21h, 2Fh, 15h, 10h and XMS calls into an XMS block of its own, with the MCB chain, vector table, BIOS data area and WIN.COM's switcher block at every EXEC, termination and INT 2Fh 4A05h/4B80h call; it keeps its own INT 21h/2Fh entries in front of later hooks, so it also sees what WIN.COM and the swappers answer themselves, and it joins the MS-DOS 5 task switcher's notification chain to record those calls; `SW1 D` writes C:\SW1.LOG, `tests/dos/sw1dump.py` prints it), DOSBox-X's file log and screenshots (`tests/worun.sh`). The MS-DOS 5 task switcher interface is the public one (MS-DOS Programmer's Reference; Ralf Brown's Interrupt List, INT 2Fh 4B01h-4B05h). Microsoft's WSWAP.EXE, DSWAP.EXE, WIN.COM and the grabbers were never disassembled.

**The protocol** (all three parties; with §9.5's and §9.8's block):
- *WIN.COM* runs WSWAP first. Whenever WSWAP or DSWAP ends it looks at the block: 17Ch FFh means Windows has ended (WIN exits with 171h); otherwise it runs the session of record 17Eh, putting the record's bytes 2Bh/2Ch (the session's number, 0 for a program not yet started) at 173h: WSWAP when it is Windows' (0Fh), DSWAP otherwise, both with the tail " \x08", after INT 10h AX=0003h (not before the first WSWAP). When WIN ends in standard mode it calls the MS-DOS 5 clients' notification functions with AX=7 (switcher exit), BX=1, ES:DI=0:0.
- *WIN.COM's INT 2Fh 4A05h functions* (SI): 5 the block (DX:AX); 4 AX = the word at 17Ch; 3 17Eh := record 17Eh's next (WSWAP: the program WINOLDAP put first), AX = 17Ch:17Eh; 2 the session at 17Eh has ended: its record freed (bytes 28h, 2Bh-2Dh 0, 29h 80h, 2Ah 0Fh; record 15 keeps its next) and unlinked from the ring, 17Eh := its next, 17Ch less one (FFh after Windows' own), DX:AX a zeroed buffer (CX=10h paragraphs); 6 ES:AX = DX:SI = the name of the record BX steps after 17Eh in the ring (DSWAP's Alt+Tab title); 7 the same, and that record becomes 17Eh and the ring is rotated to start with it after record 15 (the others keep their cyclic order). WINOLDAP, resuming a program from the Windows application, makes the program first and that Windows application second, the others after them in their order (3.1's: 13 12 14 and 14 12 13 both became 12 14 13); when a program ends and the ring goes on to another DOS program, DSWAP goes back to Windows (178h bit 08h, SI=7 BX=1) all the same. w16's WIN.COM (`win/win.asm`) does all of this now; before, SI=3 counted 17Eh down by 2, SI=2 only counted 17Ch, SI=6/7 always named record 15, and the record was freed by WIN.COM after DSWAP whether the program had ended or been switched away from.
- *Sessions and files*: a session's number is 10xxh, xx its record (Windows 0Fh); its swap file `~WOA10xx.TMP` in the swap directory (block D7h), hidden and read-only while it exists, its attributes cleared and the file deleted when it is read back. MS-DOS 5 task switcher clients are told with that number in BX: 5 (create), 3 and 4 (activate, session active, CX=1) when a session starts and each time it comes back, 1 and 2 (query suspend, suspend) when it is switched away from, 6 (destroy) when it ends; Windows 3.1's DOSX is such a client (INT 2Fh 4B01h answered in real mode).

**WSWAP** (Windows 3.1's, SW1): at the start SI=5; a search for stale `~WOA1*.*` (both swap directories) and `~GRB*.*` (the second); the block: 16Fh 1, 183h = the end of the memory arena less its own PSP (the paragraphs a DOS session gets), record 15's session 0Fh/10h (4F2h); INT 2Fh 4B05h (WIN.COM declares the last system file table block's link as instance data), notifications 5, 3, 4 for 100Fh; hooks of INT 21h and 2Fh (also 1Ch, 2Ah, 5Ch); DOSX.EXE (block 189h) with block 05h's tail. Windows 3.1's WSWAP also takes all free XMS memory itself and hands DOSX its memory through an XMS entry of its own (INT 2Fh 4310h; block 163h its handle): w16's does not, DOSX takes XMS directly (UNVERIFIED that anything depends on it). *The switch*: WINOLDAP's INT 2Fh 4B80h, then its INT 21h AX=0B80h, which DOSX passes to real mode, reaches WSWAP's INT 21h hook: notifications 1 and 2; Ctrl+Break checking, allocation strategy, UMB link, DTA and current PSP kept; its own PSP current; SI=4, SI=3, SI=5; INT 21h 36h on the swap drive; `~WOA100F.TMP` (34 bytes: its MCB and what it needs to come back; w16's contents are its own); the memory from the end of its loader to the arena's end (Windows' conventional memory: DOSX, KERNEL, the tasks) into WINOLDAP's buffer at block 185h (Windows 3.1's through its XMS handle; w16's with INT 15h AH=87h); the vector table, PICs and instance data as they were at its start; 16Dh = 100Fh, the records of session 0Fh marked swapped (byte 2Dh 1); SI=4; 173h = 100Fh; its own MCB made to reach the arena's end so that DOS frees everything; INT 21h 4Ch, code 0. No room (36h fails or less free than block 183h's paragraphs: UNVERIFIED Windows 3.1's figure), or no buffer at 185h: 172h = 08h, the Windows application after the program current again and first in the ring (as SI=7 BX=1 does), 173h = 100Fh, and the INT 21h call returns (WINOLDAP then ends the task, §9.8). *Back*: WSWAP started with record 15 holding a session reads the state file, the image back over itself (its loader, below the image, does that), restores the MCB, deletes the file, the vectors, PICs, instance data, PSP, strategy, DTA, empties the keyboard buffer (INT 21h 0C00h), notifications 3 and 4, and returns from INT 21h AX=0B80h with AL=0. *Windows' end*: notification 6 (CX=1), 16Dh cleared, SI=5, SI=2 (17Ch FFh), SI=5, 178h bit 08h, SI=4, 173h = 100Fh, Windows' exit code at 171h and as its own.

**DSWAP** (Windows 3.1's, SW1, screenshots): with 173h 0 a new program (record 17Eh): INT 10h AX=1202h BL=30h, AX=0003h, AX=0100h CX=0607h (3.1's grabber's InitScreen does these); the MS-DOS Prompt's banner when the record's flag 08h is set (COMMAND.COM without parameters), to standard output with INT 21h AH=09h: CR LF, a box of CD/BA characters 66 columns wide from column 7 (C9h ... BBh), "■ Type EXIT and press ENTER to quit this MS-DOS prompt and" / "return to Windows." / "■ Press ALT+TAB to switch to Windows or another application.", C8h ... BCh, CR LF (w16's says "w16" there); its drive, directory and the device information of handles 0-4 kept; notifications 5, 3, 4; 16Fh one more, the record's flag 08h off, its session = its number and 10h; the program (block 189h, tail 05h, FCBs from the tail, DSWAP's environment) as its child. *Hot keys* (Windows 3.1's DSWAP hooks INT 09h, 15h, 16h, 08h and others; w16's uses INT 15h AH=4Fh and switches from INT 16h, 28h or the timer when DOS allows it): Alt+Tab shows a screen of its own while Alt is held, all in attribute 70h with a blue bar (10h) on row 1 from column 2 to 77 and the next task's title (SI=6, BX=1, 2, ... for each Tab) centred in it in 1Fh, the cursor on row 25; letting Alt go takes it (SI=5, SI=7 BX=n); a quick Alt+Tab goes straight to the next task. Alt+Esc: SI=3 (the next task, no reordering). Ctrl+Esc: SI=5, 178h bit 08h (WINOLDAP then runs the Task List), SI=7 BX=1 (the Windows application after it; w16 takes the first Windows record in the ring, UNVERIFIED with several DOS programs). Switching away: notifications 1, 2; the program's DOS state and screen kept; SI=4; the swap file `~WOA10xx.TMP` (Windows 3.1's holds its MCB and the memory blocks in use, 23 KB for COMMAND.COM; w16's the screen, then its own memory and every block after it in use, a free block's header only); record byte 2Dh 1; SI=4; 173h its number; ends. *Back*: started with 173h = its number it reads the file back, the screen, vectors, DOS state (directory, PSP, DTA, handles), notifications 3 and 4, and the program goes on. *The program's end*: notification 6, the text screen, its own directory and handles back, SI=5, SI=2 (WIN.COM frees the record), SI=5, 178h bit 08h, SI=4, its session's number at 173h, INT 21h 4Ch code 0 (3.1's WIN.COM ends with FFh when it finds 173h 0 after a child: w16's DSWAP left it 0 when its program ended in the session's first run, and Windows did not come back). Windows 3.1's DSWAP saves the screen through the 286 grabber (block 87h, loaded with its InquireSave/SaveScreen/RestoreScreen, the DDK's documented interface); w16's does it itself: the BIOS video fields, the DAC and attribute palette, and the screen memory of text modes, CGA modes, 13h and the EGA/VGA planar modes (UNVERIFIED: loaded fonts and other register states). It needs no grabber; WINOLDAP still looks for the one SYSTEM.INI names (§9.8).

**Results** (installation-05): with Microsoft's WIN.COM, DOSX, KERNEL and WINOLDAP, `tests/swap.sh` passes all six scenarios: every block snapshot byte-identical to Windows 3.1's swappers' (163h-168h and 185h aside), WC1's record, the swap files made (names, order) and none left, every screen (the banner box aside), including the Alt+Tab title screens, Alt+Esc, Ctrl+Esc and its Task List, two programs, a PIF with parameters, Windows' refusal to end with a program swapped out, and no room for a swap file. Boot test: swapdos, swapalt, swapall (w16's whole install tree: WIN.COM, WSWAP, DSWAP, DOSX, KRNL286, USER, GDI, PROGMAN, WINOLDAP, drivers, fonts) and w16groups without any Microsoft swapper. A mouse click on w16's install tree faulted in DOSX ("Fault in MS-DOS Extender", EC=4440): DOSX used its uninitialised data as DOS left it, and its callback-stack selector cache held old memory contents that 3.1's WSWAP happened to leave zero; DOSX zeroes it now. With w16's WIN.COM or WINOLDAP on both sides the two-program scenario passes too since the ring rules above. Open (docs/notes/swap-handover.md): w16's WIN.COM, WINOLDAP and DOSX together under DOSBox-X with Microsoft's KRNL386 (tests/swap.sh -W).

Boot-test `swapesc` (the install tree alone; parts k386, k286, mswoa) runs the user's steps: Alt+Esc out of the prompt, the icon double-clicked, Alt+Tab out and in, a second prompt, EXIT in both; its bugs were USER's (a window made with WS_MINIMIZE stayed where it was made, an icon without a maximise box did not open on a double click, SetWindowPos without SWP_NOACTIVATE did not activate a shown window, GetNextQueueWindow returned icon titles: probe US24) and KERNEL's (the tail's LF, §9.8); docs/notes/swap-handover.md.

**Tests.** `tests/swap.sh [-w] [-O] [-x] [-W] [-K] [SCENARIO...]` runs each scenario of `tests/scripts/swap/` (prompt, two, ctrlesc, pif, exitwin, nodisk) with Microsoft's WSWAP/DSWAP and with w16's, the rest Microsoft's (or w16's WIN.COM, WINOLDAP, DOSX with -w, -O, -x), and compares WO2's block snapshots (163h-168h and 185h left out), WC1's record, the swap files made and left, and the screenshots (the banner box left out).

### 9.10 KRNL386.EXE: the same KERNEL for a 386

On a 386 Windows 3.1 runs KRNL386.EXE in Standard mode too (DOSX starts it when it is there, §9.4), and 386 Enhanced mode always does (WIN386, §12.2). w16's KRNL386.EXE is built from the same `kernel/` sources as KRNL286.EXE with `-DKRNL386` (Makefile `K386OBJS`, objects in `build/kernel386`); KRNL286.EXE is unchanged by it. The NE tables of Microsoft's two files (`tools/neinfo.py`, format facts) have the same 237 exports, names, ordinals, segment numbers and flags, so both builds share `kernel/kernel.lnk`. The C code is 386 code (`wcc -3 -zfp -zgp`: with FS free, Watcom's code kept selectors in FS, and a 386 DOS extender reloading FS around an interrupt faulted on one freed meanwhile); hand-written 386 code is in `kernel/k386.asm`. The differences, each observed on Windows 3.1 (details and the open points in `docs/notes/krnl386-handover.md`):
- **Global heap** (`kernel/global386.c`, TK58 `tests/apps/tk58.c`, the KRNL386 counterpart of TK19): KRNL386's layout as Pietrek (ch. 2, Tables 2-2 to 2-4) and Schulman (ch. 5) publish it, every field used checked: a Burgermaster over 64 KB with an 80h-byte header, 20h-byte arenas at 32-bit offsets from 80h (next, previous, address, size, handle, owner, lock count, page-lock count, flags, selector count, free/LRU links), the selector table of dwords at its end (THHOOK 18h a dword, 16h 8000h), "not there" arenas around later pieces. Handles are even for moveable blocks and odd (the selector) for fixed ones, and a program's GMEM_FIXED request gives a moveable block once Windows is up, a DLL's a fixed one (GbTop, 10.7's DRP `galloc`; a GlobalDOSAlloc block's handle is its selector; count 'M', page lock 1); GlobalSize rounds up to 20h; GlobalAlloc(GMEM_FIXED, 0) gives a discarded block; GlobalFix counts for fixed blocks and GlobalFlags shows it; GlobalPageLock/Unlock move the lock and page-lock counts and return the latter (w16 also locks the pages with DPMI 0600h under WIN386); a discarded block's arena is freed and its selector table entry is the owner; a huge fixed block grows without GMEM_MOVEABLE. Microsoft's TOOLHELP picks the layout itself (it seems by the Burgermaster) and reads w16's KRNL386 heap as KRNL386's (TK51, TK53, TK58).
- **Local heap**: the information block in KRNL386's layout (2Ah bytes, dword sentinels and discard total, "LH" at 28h: TK53 under KRNL386, TK3, TK13).
- **Selectors** (`kernel/selector.c`, TK17/TK17A): GetSelectorLimit is LSL's 32-bit limit, PrestoChangoSelector returns dst | 1, LongPtrAdd takes adds up to FF0000h (w16: below 16 MB; UNVERIFIED between FF0000h and 7FFF0000h). THHOOK has hGDI, hUSER and the shell at 28h-2Ch (TK51).
- **386 Enhanced mode** (`kernel/k386c.c`, TK57 `tests/apps/tk57.c`): INT 2Fh 1600h at start-up; GetWinFlags then has WF_ENHANCED for WF_STANDARD and WF_PAGING when the DPMI host reports a paging file (Microsoft's KRNL386 on w16's WIN386, which reports none and a 386 in DPMI 0400h, gave 0425h; on Microsoft's WIN386 0C29h); the first task's InitTask gives the Reboot device KERNEL's Ctrl+Alt+Del procedure and sanity byte (0201h; 0202h then returns a KERNEL code selector, as on 3.1), the byte is set again at every task switch and idle pass and by the procedure, which Microsoft's Reboot device runs as an interrupt when the byte stayed 0; the procedure then calls Reboot 0203h with the running task's module file name (the "not responding" screen) and ends the task when the answer is Return (§12.5; boot-test k386cad, k386hung); DOS programs start SYSTEM\WINOA386.MOD (its module name is WINOLDAP), which imports GRABBER: the module GRABBER is SYSTEM.INI's [boot] 386grabber (VGA.3GR), usage 2 while COMMAND.COM runs (TK57 `mod`; w16 loads it just before WINOA386.MOD, UNVERIFIED when 3.1 does).
- **Large memory** (boot-test `bigmem`, §9.6 "More than 64 MB"): the global heap needed no change for 128 and 256 MB under w16's DOSX and WIN386 (and 3.1's): its sizes and addresses are 32-bit, it grows by DPMI chunks (1 MB, or the request) recorded in a table that grows, a block is at most FF0000h bytes (255 tiles, as KRNL386: 3.1's largest GlobalAlloc at 128 MB was FF0000h too), and TK63 took 16 to 41 such blocks (up to 259,648 KB) and wrote every page. GetFreeSpace and GlobalCompact add DPMI 0500h's largest block, so they see the larger numbers, and TOOLHELP's MemManInfo copies 0500h. Differences from 3.1 seen there: 3.1's KRNL386 gave GlobalCompact(0) 16,320 KB (FF0000h: the largest block it could make) where w16's gives the largest free memory, and its GetFreeSpace counts all free DPMI memory where w16's counts the largest block (the same when DOSX has one block, as under w16's WSWAP).
- **The end** (both builds; `kernel/util.c` kexit_to_dos, `kmisc.c` ExitKernel): KERNEL makes its own PSP DOS's current one before its final INT 21h 4Ch, as Microsoft's KRNL386 does (it ends Windows "with its own PSP current", §12, WIN386 handover note). A DPMI host tells the client's end from a task's by that PSP: w16's WIN386 took a 4Ch made with Program Manager's PDB current for the task's end and returned, and Exit Windows then faulted in USER (a user's report, 2026-11-19: `docs/notes/krnl386-handover.md`). ExitKernel takes the one word USER passes (Pietrek ch. 1, `ExitKernel(ExitCode)`) and pops it; the exit code DOS gets stays 0 (UNVERIFIED).

**Tests.** `tests/dropin.sh -3` drops build/dos/KRNL386.EXE into the KRNL386.EXE slot (`-K FILE` another file, `-3 -r` keeps Microsoft's as the reference; with `-c 'WIN /3 /B'` 386 Enhanced mode). `make test` runs every probe under both kernels on HDPMI16 and on w16's DOSX (`RUNTESTS_KERNEL="KRNL286 KRNL386"`); the probes' tables are KRNL386's and tklog.h's k286() picks the KRNL286 ones only for a KRNL286 heap, so w16's KRNL386 is checked against KRNL386's values. Boot-test cases: `k386` (w16's KRNL386 under Microsoft's DOSX, USER, GDI against Microsoft's KRNL386), `k386all` (w16's WIN.COM, DOSX, KRNL386, USER, GDI against the same), `k386e` (386 Enhanced mode: w16's KRNL386 on w16's WIN386 and on Microsoft's, each against Microsoft's KRNL386 on the same WIN386), `k386cad` (Ctrl+Alt+Del under Microsoft's WIN386): all 0 pixels. `exittree`: Exit Windows from Program Manager on the install tree laid over 3.1 (3.1's INI files, plain DOSBox) under w16's WIN386, Microsoft's WIN386 and in Standard mode must end cleanly (KERNEL's "back to DOS", no fault report, the prompt in text mode). The install tree has SYSTEM\KRNL386.EXE, and DOSX starts it on a 386, so users of the tree run KRNL386: `treek386` runs the tree alone under its KRNL386 against its KRNL286 (every item of the default groups; windows moved over the desktop stamp), and `treealone`, `w16groups` and `missingdll@tree` run it under KRNL386 too. A library that cannot be found gets KERNEL's "File Error" box ("Cannot find OLECLI.DLL", USER's SysErrorBox, unless SetErrorMode(8000h)), as on 3.1 (TK59, boot-test `missingdll`; docs/notes/krnl386-handover.md).

### 9.11 The display grabbers: VGACOLOR.2GR

w16's Standard-mode display grabber is `drivers/grabber/vgacolor.asm` (NASM, a binary image; `make` builds `build/dos/VGACOLOR.2GR` and installs it in `build/w16/SYSTEM`); details in `drivers/grabber/README.md`, working notes in `docs/notes/grabber-handover.md`. The interface is the DDK's documented one ("Display Grabbers": the jump table at 00h-15h, InquireGrab's extended functions FFF4h-FFFFh, GRABINFO, GRABREQUEST, GRABST); how DSWAP uses it and what Windows 3.1's grabber answers were observed black-box with three probes (`make grprobes`): GR1.2GR (`tests/dos/gr1.asm`), a stand-in DSWAP loads, recording each call's registers, the INT 10h calls made meanwhile and the documented structures, and forwarding to the real grabber, which the test appends to it; GRT.COM (`tests/dos/grt.asm`), which loads a grabber and calls every function itself without Windows in each video mode, and checks every VGA register, the DAC, the BIOS's video fields and the four planes before SaveScreen and after RestoreScreen, with the screen scribbled over in between; GV1.COM (`tests/dos/gv1.asm`), fixed screens per mode; and CB1.EXE (`tests/apps/cb1.c`), a clipboard viewer recording every change. Microsoft's grabbers were never disassembled.

**How DSWAP uses it** (GR1, Microsoft's DSWAP and WINOLDAP): DSWAP loads the image into its own memory at every run (each start and each switch back) and puts its GRABREQUEST right after it. A program's first run: SetSwapDrive (BL = the drive of the second swap directory, ES:DI = `<second swap dir>~WOAFFEE.TMP`), InquireSave(1) (3.1's: 2452h bytes; DSWAP keeps a buffer of that size), EnableSave, InitScreen(25). Switching away: SaveScreen (AX = that size, ES:0), DisableSave, InitScreen(25). Back: EnableSave, RestoreScreen. PrintScreen in the program: FFF5h (obsolete; BX=0100h), GetInfo, GetBlock with no buffer and grStyle 80h (the size), GetBlock into DSWAP's buffer, FFF4h (obsolete); DSWAP hands the data to WINOLDAP through `~WOAfffe.tmp` in the second swap directory, and WINOLDAP, back in Windows, puts it on the clipboard as CF_OEMTEXT and CF_TEXT (CB1). The program's end: InitScreen(25), DisableSave.

**What 3.1's grabber answers** (GRT, in every BIOS mode 0-7, 0Dh-13h): InquireGrab(1) 100Ah, (2) 4010h; GetVersion 0201h; GetInfo AX 1 in text modes and 0 in graphics modes, with GRABINFO and INFOCONTEXT's further fields filled per mode (screen type 0 text, 1 CGA, 3 EGA/VGA; 2400 x 1800 tenths of a millimetre; columns, rows, mouse scale, pixels, bytes a line, bits and planes, the screen's address and page length: the table in vgacolor.asm); GetBlock: in text modes the clipboard format is GRABST's GT_TEXT, gbSize, then the characters row by row with CR LF between rows and two NULs, the native format the character/attribute pairs, any block within the screen (ERR_BOUNDARY otherwise); in CGA modes 4-6 only the whole screen in the clipboard format, GT_NEWBITMAP 640 x 200, one plane, one bit a pixel, the two banks' lines in turn (gbWidth2 4800 in modes 4 and 5, 2400 in mode 6; the data call answers AX = 0708h, not the size); in modes 0Dh-13h ERR_UNSUPPORTED (FFh, CF set) for everything. InitScreen(n): INT 10h 1202h BL=30h, 0003h, for 43 lines or more 1112h BL=0, then 0100h CX=0607h. Its RestoreScreen is exact in text and EGA/VGA modes except the font plane; in the CGA modes it sets mode 7 (GRT) and, under DSWAP, Alt+Tab does not leave the program at all.

**w16's** answers the same (GetVersion, InquireGrab, GetInfo, GetBlock: compared byte for byte by `tests/grabber.sh grt`, but the CGA data call's AX, which is the size). Its own design: no hooks; SaveScreen reads every VGA register, the DAC and the BIOS fields into the save buffer (8953 bytes, less than 3.1's) and writes the video memory to a file of its own, `~GRBxxxx.TMP` in SetSwapDrive's directory (the name WSWAP clears at start): the four planes read as planes (64 KB each) in text and EGA/VGA modes, the B800h window in CGA modes (written back as planes, DOSBox-X showed a CGA screen wrong); RestoreScreen sets the mode through the BIOS without clearing, writes the memory back, deletes the file, and writes every register, the DAC and the BIOS fields. Without a file a text mode's page is kept in the save buffer (a graphics screen is then lost). Every mode comes back exactly (GRT). Not done: the EGA register interface calls 3.1's makes (INT 10h F1h/F3h/F7h, for EGA.SYS).

**Tests.** `tests/grabber.sh [-o DIR] [-g FILE] [-O] [SCENARIO...]`: `grt` (above) and the Windows scenarios of `tests/scripts/grabber/` (text, cga, ega, prompt) under Microsoft's DSWAP, WSWAP and WINOLDAP (`-O`: w16's WINOLDAP), each with Microsoft's grabber and with w16's: the screenshots and CB1's clipboard records compared (DOSBox-X puts the second swap directory on A:, so the scenarios mount A: and B: on C:'s directory for DSWAP's `~WOAfffe.tmp`). Boot test: `swapall` runs w16's install tree, its VGACOLOR.2GR included.

## 10. USER

w16's USER.EXE is `user/` (C, large model, plus `user/uentry.asm`), built as `build/dos/USER.EXE`. It is a drop-in replacement for Windows 3.1's `WINDOWS\SYSTEM\USER.EXE`: `tests/userboot.sh` copies it over Microsoft's in a scratch copy of the reference install, boots `WIN /S` and compares the screen with the same boot on Microsoft's USER. Everything below was observed black-box (screenshots, probes, the US1 call recorder) or taken from NE file headers; Microsoft's USER, GDI, drivers and applets were never disassembled. Probes are US1-US5; `grep -rn UNVERIFIED user` lists what rests on guesses.

### 10.1 The interface (from the NE tables)

`tools/neimports.py MODULE WINDOWS_DIR` reads a module's entry table, every NE file of the install that imports it, what it imports, and its resource table; `docs/user/{exports,importers,imports,resources}.txt` hold its output for 3.1's USER, and `user/ordinals.txt` the export list w16 builds from (names, residency, argument bytes from Wine's X11-licensed specs).

- **Exports:** 413 entries: 412 functions and `__GP` (resident, ordinal 484: the parameter-validation fault table KERNEL consults). Resident names: `USER`, `WEP` (8), `__GP`. 144 entries are in fixed preload code segment 1, the rest in discardable segments (USER has 43 code segments). Two data segments: 44 (886 bytes) and the automatic one, 45 (2744 bytes, local heap 157Ch). Module flags 8301h; entry point 3:0000. w16's USER has the same 413 entries (since US27, 10.3: `__GP` is its own table, `user/gp.asm`; an unnamed movable entry 533 that wlink had added for the `__loadds` selector fixups is gone, `tools/nemovsel.py`).
- **Importers:** 55 files of the install import 303 USER ordinals; LoadString (48 files), PostMessage (45), MessageBox, SendMessage (43), DefWindowProc (42), RegisterClass, GetSystemMetrics (41) lead (`docs/user/importers.txt` ranks all). 110 ordinals are imported by no file, among them the ones KERNEL calls (SignalProc 314, IsUserIdle 333, FinalUserInit 400: 10.2). **No boot module (KERNEL, GDI, the drivers) imports USER**: they get its entry points at run time.
- **USER imports** KERNEL (96 ordinals), GDI (98), DISPLAY 101-104 (Inquire, SetCursor, MoveCursor, CheckCursor), KEYBOARD 1-7, 134, 135, MOUSE 1-3, SYSTEM 1-6, SOUND 17 (DoBeep), COMM 1-15.
- **Resources:** only 29: group cursors 32646 (size-all) and 100, group icon 32647, menus 1 and 2 (system menus), dialogs 1, 9, 10, 100, 101, 14 string tables, a version resource. The frame bitmaps (OBM_*), the system cursors (OCR_*) and icons (OIC_*) are the **display driver's** resources (VGA.DRV has them all), so USER draws Windows' frames with the driver's artwork; w16 needs its own pictures only for those few.

### 10.2 The boot contract (probe US1)

**US1** (`tests/ustrace.sh`) puts forwarding modules (`tools/mkinterpose.py`) in front of USER and between USER and GDI, DISPLAY, KEYBOARD, MOUSE, SYSTEM, SOUND and COMM, in a scratch copy: Microsoft's USER runs as module XUSR, its imports renamed in its NE header to the forwarders (`tools/nerename.py`). Every call's registers, stack words, pointer arguments and (for GDI and the drivers) results go to `USTRACE.DLL` (`tests/user/ustrace.c`); `tools/ustrace.py` decodes them, naming callers module:segment. With all forwarders in place Windows 3.1 still boots to its normal Program Manager. `-u build/dos/USER.EXE` traces w16's USER the same way. `docs/user/boot-trace.txt` is the trace of a boot with Program Manager (`tools/ustracesum.py`; byte dumps left out).

Observed (KRNL286; handles are those of that run):
- **USER's entry point** (KERNEL calls it like any DLL's, the boot task current) does, in this order: SYSTEM.CreateSystemTimer(0, DISPLAY.CheckCursor) (rate 0: every system tick); KEYBOARD.Inquire (returns 6), KEYBOARD.SetSpeed(-1) then SetSpeed(5Fh) (WIN.INI KeyboardSpeed 31 = 1Fh, KeyboardDelay 2 in bits 5-6); MOUSE.Inquire (returns 10h); DISPLAY.Inquire (CURSORINFO, returns 4); 24 CreateRectRgn(0,0,0,0), each MakeObjectPrivate'd; CreateDC("DISPLAY",0,0,0) six times, five of them with SetDCHook(hdc, a procedure in USER's fixed segment, dwData = a near pointer into USER's data, 14h apart: the DC cache entries) and all private; GetDCState of one (the default state; later every release does SetDCState(hdc, it)); two CreateCompatibleDC; an 8x8 bitmap of rows 55h/AAh made into a pattern brush (the gray brush; FinalGDIInit gets it); GetDeviceCaps(LOGPIXELSX 58h, VERTRES 0Ah, BITSPIXEL 0Ch, PLANES 0Eh, RASTERCAPS 26h); the system colours' brushes (CreateSolidBrush, GetNearestColor); GetTextMetrics and GetTextExtent of the 52 letters a-z A-Z (the dialog base units); the display's OBM bitmaps read as DIBs (core and 3.x headers) into bitmaps with SetDIBits and copied by BitBlt into a 368x18 colour strip (caption buttons, scroll arrows) and a 240x15 mono strip; cursors and icons made into device format: CreateBitmap(32,64,1,1) + SetDIBits + GetBitmapBits(100h) for a cursor, CreateCompatibleBitmap(32,32) + SetDIBits + GetBitmapBits(200h) and CreateBitmap(32,32,1,1) for an icon; SYSTEM.EnableSystemTimers; KEYBOARD.Enable(USER's keyboard event procedure, USER's 256-byte key state array); MOUSE.Enable(USER's mouse event procedure); DISPLAY.MoveCursor(320, 240); DISPLAY.SetCursor(the hourglass, hot spot 16,16); the desktop painted (PatBlt 640x480 PATCOPY); GetTextExtent("Ignore") (message box buttons); AddFontResource of every WIN.INI [fonts] value in file order; CreateFontIndirect (MS Sans Serif, height -11). DiagOutput writes `INIT=Keyboard` ... `INITDONE=Lang Driver` (each with CR LF) around these steps; the `LoadStart = DISPLAY.drv` line of BOOTLOG.TXT inside `INIT=Display` comes from **GDI's CreateDC**, not from USER.
- **GDI writes into the driver name given to CreateDC** (w16's USER first passed a string constant "DISPLAY" followed in memory by "taskman.exe": GDI tried to load `DISPLA`taskman.exe.drv` six times; with a 16-byte writable buffer it loads `DISPLAY.drv`): the caller must pass a writable buffer with room, as Windows' USER evidently does. UNVERIFIED what exactly GDI writes there.
- **KERNEL calls into USER** (through the USER module's exports, found at run time): GetFreeSystemResources(0) once while booting; **SignalProc (USER.314) with code 40h** and the handle of each module loaded after USER (SHELL, MMSYSTEM, ...); **FinalUserInit (USER.400)** after the shell is loaded, with the shell's task current; **IsUserIdle (USER.333)** in its idle loop (65535+ calls in 45 s); **ExitWindows(0, 0)** when the shell ends (seen with w16's USER on Microsoft's KERNEL), after which Windows 3.1's USER writes `INIT=TSRQuery`/`INITDONE=TSRQuery`. Pietrek (ch. 3) describes the rest, which w16 follows (UNVERIFIED by observation so far): InitApp makes USER.314 the task's signal procedure (SetTaskSignalProc), and KERNEL calls it with 20h when a task ends (0666h when it is killed), 80h when a library is freed; USER then destroys the task's windows, timers and queue (and calls GDITaskTermination).
- **FinalUserInit** does: CreateCompatibleDC, CreateBitmap(320,18,1,1), text colour/background set-up, then GDI.FinalGDIInit(the gray brush); `INIT=Final USER`/`INITDONE=Final USER`, then `INIT=Installable Drivers`: SYSTEM.INI [boot] drivers= (MMSYSTEM.DLL), whose driver procedure gets DRV_LOAD (it calls DefDriverProc with message 1) and which opens its own [drivers] (OpenDriver("timer"), GetDriverModuleHandle, OpenDriver("joystick"), a hidden window).
- **InitApp** (USER.5): GetSystemMsecCount, CreateSystemTimer(10000, USER's timer procedure) (§9.4: SetTimer later re-creates it at the timers' rate).
- **USER's calls into GDI while Program Manager starts and paints** (counts over 45 s): SelectObject 160, GetTextExtent 159, SetRectRgn 123, SetDCState 103, CombineRgn 81, IsGDIObject 81, GetObject 66, SetTextColor 64, DeleteObject 57, SetBkColor 55, GetRgnBox 55, SetDIBits 53, SetBrushOrg 51, MakeObjectPrivate 47, PatBlt 43, GetTextMetrics, MoveTo, IntersectClipRect, SaveDC, RestoreDC, GetCurrentPosition, GetViewportExt, GetWindowExt, GetCurLogFont (38 each), BitBlt 37, SelectVisRgn 34, SetDCOrg 34, CreateBitmap 30, CreateCompatibleBitmap 29, GetBoundsRect 29, CreateRectRgn 27, GetBitmapBits 27, UnrealizeObject 27, StretchBlt 22, SetBitmapBits 22, AddFontResource 22, GetClipBox 17, GetStockObject 17, TextOut 13, GetDeviceCaps 12, CreateSolidBrush 10, GetNearestColor 10, SetBkMode 9, GetBrushOrg 8, **FastWindowFrame (GDI.400) 7** (window frames: FastWindowFrame(hdc, lprect, 1, 1, PATCOPY)), CreateDC 6, SetDCHook 5, CreateCompatibleDC 4, RectInRegion 3, ExtTextOut 3 (the caption title, ETO_CLIPPED), CreateFontIndirect, CreatePatternBrush, DeleteDC, GetDCState, FinalGDIInit 1. A window DC is prepared by SetRectRgn/GetRgnBox/CombineRgn on USER's scratch regions, SelectVisRgn(hdc, region in screen coordinates), SetDCOrg(hdc, x, y) and SetBrushOrg(hdc, x, y) with the window's or client's screen corner, and released with SetDCState(hdc, default). Menu text is TextOut at the item's x + 8, then GetTextExtent of the text before and including the mnemonic, and a PatBlt (1 pixel high) under it. Calls from the drivers into USER at interrupt time (keybd_event, mouse_event) were not exercised (no input during the boot).
- DISPLAY.CheckCursor is called by SYSTEM.DRV's timer about 25 times a second; DISPLAY.SetCursor with each new cursor (CURSORSHAPE: the same 12-byte header as an icon block, then AND and XOR masks).

### 10.3 w16's USER

- **Build:** `make user` (part of `make dos`). `user/*.c` define the exports with their Windows names and windows.h prototypes (far pascal; OpenWatcom's Windows prologue, `push ds; pop ax; nop`, which KERNEL's loader patches for exported entries of a single-data library, so exports load USER's DGROUP); functions windows.h does not declare are `U_<NAME>`. `tools/mkuserexp.py` reads the objects' public names and writes the export list and, for every ordinal not implemented, an assembly stub of the same name that notes the call (`UNIMPL`; `[w16] userlog=1` in SYSTEM.INI makes USER log to C:\USER.LOG) and returns 0, popping the argument bytes `user/ordinals.txt` gives. 230 of 412 ordinals are implemented. Code is fixed and preload (interrupt-time entries need fixed code); the data segment is moveable like 3.1's (segment 45, flags D71h), so its local heap grows: a fixed 7K one ran out by US4's fourth dialog and controls silently failed to be created.
- **DGROUP must start with the 16 bytes of instance data** (KERNEL keeps the local atom table's pointer at DS:8): `uentry.o` is linked first so its `_NULL` segment comes first. With C data there instead, RegisterWindowMessage hung on Microsoft's KERNEL and divided by zero on w16's.
- **Data structures:** a WND per window in USER's local heap, the HWND its near offset, fields at Pietrek's offsets (Windows Internals table 4-2: next, child, parent, owner, window and client rectangles, queue, update region, class, hInstance, window procedure, flags, style, ex-style, menu/id, text, scroll, properties, last active, system menu), extra bytes after it; US15 read every field a tool reads at these offsets on 3.1 (the layout of Schulman's WND, ch. 6) and w16's agree. Classes are local blocks too, with the layout of Schulman's CLASS, which US15 also checked on 3.1 (hcNext, the 'NK' signature 4B4Eh, the name's atom, hDCE, the window count, then the WNDCLASS with the module as hInstance and both string pointers 0, then the class extra bytes; w16's own layout before differed); a class's name is its atom in USER's DGROUP atom table (compared without regard to case), with no second copy; a string menu name is a copy in the string heap (below), lpszMenuName pointing to it. Menus, window text and what applications never reach directly are in heaps of their own (below). Queues are global blocks (8 posted messages by default, SetMessageQueue: 3.1 SDK), one per task, the selector stored with SetTaskQueue. HICON/HCURSOR blocks have the 12-byte header layout of 6.11.
- **Menu heaps** (as 3.1's, US14 in 10.4): `menu_heaps_init` allocates two 418h-byte global blocks (GMEM_DDESHARE | GMEM_MOVEABLE | GMEM_ZEROINIT) and makes each a local heap with LocalInit(sel, 12h, 417h) (Pietrek, Windows Internals ch. 1, LoadWindows). MENU structures (14h bytes, fixed; the HMENU is their near handle) and their item arrays (1Ah per item, Schulman ch. 6) go in the first, item texts in the second; nothing of a menu is in DGROUP, so menus no longer eat the room windows need. `menu.c` reaches them through far pointers built with MK_FP(heap selector, handle), and calls LocalAlloc/ReAlloc/Free/Size through `uentry.asm`'s `lh_*` (DS is the heap's selector for the call, USER's again after; never at interrupt time). A heap's selector is taken again (GlobalHandle) after each allocation in it, and no far pointer into a heap is kept across a call that allocates or sends a message (the owner may change the menu in WM_INITMENU/WM_INITMENUPOPUP/WM_MENUSELECT). `tools/castcheck.py` also flags `lh_alloc`/`lh_realloc` results cast to far pointers. GetFreeSystemResources follows US14's rule. The system menu is shared, as 3.1's (US23): one is made at start-up and every window's menu loop shows it; only GetSystemMenu(h, FALSE) gives a window a copy of its own (destroyed by GetSystemMenu(h, TRUE) or with the window); MDI children share a second one (Close Ctrl+F4, Next; made on first use). When a task ends normally and DGROUP has more than 4096 bytes free, USER shrinks DGROUP and both menu heaps as far as they go (`user_heaps_shrink`, Pietrek ch. 3), so a burst of menus or windows no longer leaves them at their largest.
- **USER's other heaps** (`user/uheap.c`; US15 in 10.4): 3.1 keeps window text, and the global atom table (where properties' string names go), in a fourth heap, the 100h-byte one US14 found, which GetFreeSystemResources does not count. w16 has three such heaps, made as the menu heaps are (a moveable global block, LocalInit(sel, 12h, size - 1)) and reached the same way (`UH_PTR(heap, handle)` = MK_FP(selector, handle); Local* through `lh_*`, the atom calls through `uentry.asm`'s `at_*`, DS switched; selector taken again after every allocation; no pointer into a heap kept across a call that allocates in it or sends a message; never at interrupt time): the **global atom heap** (100h bytes at start, as 3.1's; GlobalAddAtom/FindAtom/DeleteAtom/GetAtomName; RegisterWindowMessage, RegisterClipboardFormat and class names stay in DGROUP's atom table, as on 3.1, where GlobalFindAtom does not find them), the **string heaps** (window text, WND+32 its handle, 0 for an empty text, as 3.1's, the heap's number in w16's own WND flags; a new heap is made when those there are full, up to 16, where 3.1's one 64K text heap limits it to about 510 windows with 120-character titles; CreateWindow fails, as on 3.1, and WM_SETTEXT returns 0, when no heap has room; class menu names in the first) and the **object heap** (property lists: one block per window, WND+36 its handle, an atom and a handle per property, string names global atoms with a reference each, dropped with the window, as on 3.1; hooks; the DC cache's DCE records; window scroll-bar state, WND+34; edit controls' state and text; list box items and strings). DGROUP keeps what 3.1 keeps there and tools read: WNDs, CLASSes and their atoms, registered messages and formats, list box and combo box state, window placements. Its static data is 2CEh bytes (3.1's: AB8h; w16's was 1742h: string literals are in the code segments, `-zc`, and big tables are far data). Timers were already out of DGROUP (fixed global chunks: `timer.c`; 3.1 allows 31 at once, w16 any number). `tools/castcheck.py` flags `uh_alloc`/`uh_realloc`/`uh_strdup` results cast to pointers, near ones too.
- **GetFreeSystemResources** keeps 3.1's rule over the heaps 3.1 has: GFSR_USERRESOURCES is the least of DGROUP's, the menu heap's and the menu-string heap's percentages (US14). The new heaps are not counted, on evidence: 3.1 does not count its own text-and-atom heap (US15: with that heap full of window titles, at 0 bytes free, GFSR_USERRESOURCES still said 44%), so the figure stays the same kind of number (what is left for windows, classes and menus). With the new heaps a window can still fail for a full string or object heap while the percentage is high, as on 3.1 for its text heap.
- **[w16] ExtendedUserHeap=1** (SYSTEM.INI; read at USER's start-up; **default 0**). With it, windows leave DGROUP as well, as on Windows 95: each HWND is still the offset of a fixed DGROUP block, but of a 4-byte stub {heap number with the signature 57h, offset}; the WND and its extra bytes, after a word holding the HWND, are a block of one of up to 16 window heaps (64K local heaps made whole, so their selectors never change). A window costs DGROUP 0Ch bytes instead of 40h, and about 5200 fit (US15) instead of 989. **It can break programs that read USER's internal structures directly** - Spy-type tools, desktop replacements, debuggers, some hook DLLs, anything that walks USER's heap with TOOLHELP or follows an HWND into USER's data segment: they find stubs, not WNDs (US15's layout checks fail with it on). Documented APIs behave the same either way. Both layouts share one code path: a PWND is a far pointer, made only by `PW(hwnd)`/`valid_wnd` and turned back by `HW(pwnd)` (`user/win.c`); with the setting off PW is DGROUP:hwnd and the result is the default layout exactly. `DROPIN_INI=w16:ExtendedUserHeap=1` runs any boot test with it.
- **[w16] DesktopVersion** (SYSTEM.INI; read at USER's start-up; **default 1**; **0 is stock Windows 3.1**): the desktop shows `<Mode> Mode w16 v<version> Debug Release <build>` in its bottom right corner, e.g. "Standard Mode w16 v0.1 Debug Release 53e4e75", the way Windows 3.0's debug pre-release builds showed "... Mode Windows v3.0 Debug Release 1.33" (style reference: a screenshot of one the user supplied; white bold italic text on the desktop colour). The mode comes from GetWinFlags (Standard, 386 Enhanced, Real without WF_PMODE), the version from the file `VERSION` and the build from the source tree's commit (`build/w16ver.h`, BUILDING.md). `user/deskver.c` draws it, w16's own design: the System font, slanted one pixel for every 3 rows above the cell's bottom row (italic made by USER from a monochrome bitmap of the text, so that it does not depend on GDI simulating italic for a raster font; made once and kept, private), a black shadow one pixel right and down (the reference's letters have dark edges, and white alone hardly shows on 3.1's light gray desktop), then the white text, both transparent (two BitBlts of the bitmap, SRCAND and SRCPAINT); the box of text and shadow 4 pixels from the screen's right edge and 2 from its bottom. It is part of the desktop's background: drawn in the desktop window's WM_ERASEBKGND after the colour, so it is clipped to the region being repainted, lies under every window and is repainted with the desktop. With `DesktopVersion=0` none of it runs (no GDI call, nothing drawn). Boot-test case `deskver`: `DesktopVersion=0` on w16's USER and GDI gives 3.1's screen to the pixel; with every w16 part, Program Manager is dragged over the stamp, dropped partly over it and dragged back (`tests/scripts/input/deskver.txt`), and each screen with `DesktopVersion=1` must equal the same run's with 0 plus the stamp as `tools/deskstamp.py` draws it from w16's System font (corner uncovered: exactly; partly under the window: only stamp pixels differ, in the stamp's colours; mid-drag, the frame drawn by XOR over it: only inside the stamp's box). With Microsoft's fonts the stamp is drawn in their System font.
- **[w16] ScrollBarMenu** (SYSTEM.INI; read at USER's start-up; **default 1**; **0 is stock Windows 3.1**): a compatible enhancement beyond 3.1 (6.8), the scroll-bar shortcut menu Windows 2000 and later have. A right click on a window's own scroll bar (DefWindowProc's WM_NCRBUTTONUP with HTVSCROLL/HTHSCROLL) or on a SCROLLBAR control (its WM_RBUTTONUP) opens, at the pointer, on the button's release (later Windows shows it from WM_CONTEXTMENU, which DefWindowProc makes from the button-up messages: Win32 SDK), vertical: Scroll Here, separator, Top, Bottom, separator, Page Up, Page Down, separator, Scroll Up, Scroll Down; horizontal: Scroll Here, separator, Left Edge, Right Edge, separator, Page Left, Page Right, separator, Scroll Left, Scroll Right; no mnemonics. Sources: Raymond Chen, *The Old New Thing*, 2026-09-22 (Windows 2000 added it; the seven commands); the separators as transcribed in Nick Gammon's forum (gammon.com.au, thread 8417); the horizontal wording (Left Edge, Right Edge, Page Left, Page Right, Scroll Left, Scroll Right) from a Trapeze Group application's help page, which lists both bars' right-click menus (it does not say they are Windows' own, but they match the vertical one item for item); and a screenshot of the vertical menu from PsyMP3, the user's ISC-licensed program, which confirms order, wording and separators and shows no mnemonics. No Microsoft binary or resource was looked at. Each command sends the parent (a control) or the window (its own bars) WM_VSCROLL/WM_HSCROLL with 3.1's packing, wParam the code and lParam MAKELONG(position, hwndCtl) (hwndCtl 0 for a window's bars): Top/Left Edge SB_TOP, Bottom/Right Edge SB_BOTTOM, Page Up/Left SB_PAGEUP, Page Down/Right SB_PAGEDOWN, Scroll Up/Left SB_LINEUP, Scroll Down/Right SB_LINEDOWN, Scroll Here SB_THUMBTRACK and SB_THUMBPOSITION with the position whose thumb is centred on the click (UNVERIFIED which of the two later Windows sends; both, as a drag of the thumb there would, so programs acting on either move); each then SB_ENDSCROLL. **Top and Bottom fallback (w16's own):** on 3.1 SB_TOP and SB_BOTTOM come only from a scroll-bar control's Home and End keys, never from a window's own bars, so 3.x programs may ignore them: Microsoft's Write does (USER.LOG with `userlog=1`: its bar stayed at 7Eh through SB_TOP and SB_ENDSCROLL; it acts on scroll messages at SB_ENDSCROLL), so Top and Bottom did nothing there. The code is sent first, as later Windows does; then, if the bar's position has not changed and is not already at that end of its range (min for Top/Left Edge, max for Bottom/Right Edge), SB_THUMBTRACK and SB_THUMBPOSITION to that end follow, as Scroll Here sends, which every program handles; then SB_ENDSCROLL. A program that handles the code moves its bar and gets nothing more; one that moves it only later gets a thumb to the same end. Checked with real input on w16's USER (Microsoft's KRNL286 and the rest): Write (fallback taken; Top, Bottom and Scroll Here go where they say), Notepad (the edit control handles both; Bottom put the last line at the top, Notepad's bar staying at 0, so the fallback sends the same place again, harmlessly), File Manager's file list (Left/Right Edge, the list box handles them), Microsoft's Program Manager group window and Cardfile's list view (handled). US17 covers a window and a control parent that ignore them, one that handles them, and one at the end already. The investigation also found that w16's menu loop left Return's WM_CHAR in the queue after a menu closed (fixed: Notepad's Time/Date chosen by Enter now matches 3.1, and Write no longer scrolls back to its caret after Top chosen by keys). The menu is a menu-heap popup run by USER's own menu loop (`menu_track_quiet`, `user/menu.c`: the same drawing, keys, Esc and click-outside as every popup; the right button chooses too, as TrackPopupMenu's TPM_RIGHTBUTTON, which w16's TrackPopupMenu now also honours), but the window is sent none of the menu's messages (no WM_INITMENU, WM_INITMENUPOPUP, WM_MENUSELECT, WM_COMMAND), which a 3.1 program could take for its own: 3.1 has no WM_CONTEXTMENU, and the scroll messages are the only thing it sees. No menu for a disabled window or control (they get no mouse input) or a bar with nothing to scroll (an empty range, or shown disabled): later Windows' behaviour there is not documented, so the conservative choice. A program that handles WM_NCRBUTTONUP (or subclasses the control's WM_RBUTTONUP) without passing it on keeps full control. With `ScrollBarMenu=0` none of this code runs. Probe **US17** (`tests/apps/us17.c`; boot-test case `sbmenu`) right-clicks every bar with real mouse input and chooses every item by keys, by left and right clicks, Esc and a click outside, checking the items, the menu's place and the messages; with `ScrollBarMenu=0` its log (no message from any right click) and final screen are identical to Windows 3.1's run of the same probe. Boot-test case `input`, whose script right-clicks wherever the boot left the pointer, runs w16 with `ScrollBarMenu=0`. US17 also showed two scroll-bar differences from 3.1 that have nothing to do with the menu, not fixed yet (the probe steps around them): SetScrollPos(..., TRUE) on a control before its parent's first WM_PAINT paints it at once (InvalidateRect + UpdateWindow), and the parent's paint then covers it, where 3.1 shows it; and 3.1 draws a SCROLLBAR control with an empty range with its thumb at the top, w16 with none.
- **An edit control's text** (`edit.c`; probes US20 and US21 on 3.1, boot-test case `edithandle`): a moveable local block, never left locked, in the local heap of the segment the window's hInstance names: the application's DS for a control it creates, so EM_GETHANDLE (multi-line controls only; a single-line one answers 0) gives a handle it can LocalLock, LocalReAlloc and hand back with EM_SETHANDLE, as Microsoft's Notepad does to read and write files. A dialog's edit controls without DS_LOCALEDIT share a 2000-byte segment USER makes (their hInstance; the dialog's is the application's), freed with the dialog; with DS_LOCALEDIT they use the application's. A control given a LocalInit'ed global block as hInstance keeps its text there (Pietrek: up to 64K); w16 gives one whose hInstance has no local heap a private segment. Sizes as 3.1's: len + 33 bytes when made or set (34 empty, 30038 for 30005 characters; WM_SETTEXT never shrinks it), typing grows it 32 bytes at a time; EM_SETHANDLE sizes the new block to its text when KERNEL can, leaves the old one to the application, clears the modified flag; DestroyWindow frees it. Characters are read from the block each time, up to the control's own length. The default limit is 30000 characters for typing and EM_REPLACESEL (what fits goes in), not for WM_SETTEXT; EM_LIMITTEXT 0 removes it (Notepad sets FFFFh). KERNEL's LocalReAlloc cannot grow a 31.5K block at the end of a 53K heap (it would need room for a copy; US20 part H, on 3.1 too), yet 3.1's control grows it there when text is typed; w16 then allocates a block too big for the free space, which grows the heap at its end, frees it and grows the text in place (UNVERIFIED how 3.1 does it: its segment grew by less; and where another block follows the text 3.1 did not grow it while w16 does, US20 part F, not compared). A multi-line control keeps its lines' starts there too, 2 bytes a line, as 3.1 does (US21; made again on every change, so text changed behind the control's back keeps the old line count until then, as on 3.1). Not done: 3.1 also keeps the control's state (98 bytes) in that heap (US21), so the largest file Notepad opens still differs a little from 3.1's (15.1). With this Microsoft's Notepad opens, pages, edits and saves a 31500-byte file on w16's USER with every screen and the saved bytes as on 3.1; on the way Page Up/Down (a page, caret and view together), EN_VSCROLL/EN_HSCROLL for scrolls made by keys and typing, and line scanning that no longer starts from the text's top for every character (typing at the end of 31K took seconds a key).
- **Buttons held down** (`defwnd.c` track_caption_button, `scroll.c`; probe US26, boot-test case `holdbtn`): the caption's minimize and maximize (restore) buttons are drawn pressed (OBM_REDUCED, OBM_ZOOMD, OBM_RESTORED) while the left button is held on them, raised when the pointer leaves, pressed again when it returns; the command (WM_SYSCOMMAND SC_MINIMIZE, SC_MAXIMIZE, SC_RESTORE) is sent on a release on the button and nothing on a release off it (w16 acted on the press). A press on a window's scroll bar sends WM_SYSCOMMAND SC_VSCROLL/SC_HSCROLL, which DefWindowProc tracks (3.1 sent it before any WM_VSCROLL). An arrow held: the code at the press, again 200 ms later (at the next 55 ms tick, 220 on 3.1), then at every tick; the arrow drawn pressed (OBM_DNARROWD ...) also through the program's SetScrollPos repaints, raised and the repeats stopped while the pointer is off it, both back when it returns. The shaft held: pages with the same timing while the pointer stays on the same side of the thumb (from 0, held at the place of position 70, 3.1 paged seven times and stopped). A timer of USER's (SetTimer on the window, id FFF7h, its WM_TIMERs taken by the loop) wakes the loop; w16 repeated after 400 then 50 ms, and only when other messages woke it. Thumb drags in Notepad: 3.1's Notepad scrolls its text with SB_THUMBTRACK but keeps its thumb until SB_THUMBPOSITION; w16's Notepad moved the thumb at every SB_THUMBTRACK (now as 3.1; `sbdrag` part npw).
- **The move and size loop** (`track.c` next_input; probe US31, `tests/apps/us31.c`, boot-test case `trackmove`; QuickTime's Movie Player, `quicktime` part over, tree and tree386; `docs/notes/quicktime-handover.md`, "Dragging the movie"): while a window is dragged or sized, 3.1's loop takes only mouse and keyboard messages. The owner's WM_TIMER, WM_PAINT and posted messages wait until the drop; a WH_GETMESSAGE hook sees only the mouse messages (US31 on 3.1: `200x81 202`); other tasks run all through it (US31's other task: 107 of its 55 ms ticks in a 6.4 s drag, as many WM_PAINTs). The window gets WM_GETMINMAXINFO, then message 231h, then after the drop WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_MOVE, then 232h (wParam and lParam 0 both; Win32 later documented them as WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE, but 3.1 sends them). After Esc it gets 231h, a WM_WINDOWPOSCHANGING alone, then 232h. w16 waits for input (wake mask QS_MOUSE, QS_KEY and QS_SENDMESSAGE; sent messages are answered by its PeekMessages) and yields while another task's input is pending. Until 2026-11-19 the loop was a GetMessage of everything. It dispatched timers and posted messages with the frame erased and drawn again around each, and it left WM_PAINT in the queue, so GetMessage never waited and no other task ran (US31: `other=0`). Under QuickTime this hung Windows for good: QTIM.DLL's system-wide WH_GETMESSAGE hook takes its registered message out of the player's queue and posts it again (PeekMessage of that message with PM_REMOVE and PM_NOYIELD, then PostAppMessage to the player). That kept a posted message in the queue all the time, and posted messages, read before input, kept the mouse out. The frame was drawn and erased in place for ever. Differences left: on 3.1 no window's drawing shows during a drag, the dragging task's and other tasks' alike, GetDC and BeginPaint alike (US31's counters stay on screen as at the press, though they keep counting), and the windows show their new state after the drop; w16 draws them (`trackmove` compares US31's log, not its screens). w16's SetWindowPos also sends WM_NCCALCSIZE for a move alone (3.1 does not), and sends no WM_WINDOWPOSCHANGING when a drag is cancelled.
- **Mouse moves merge in the system queue** (`input.c` push_event; probe US25, boot-test cases `mmerge` and `sbdrag` part fast): on 3.1 a mouse move made while the last event still queued is a move replaces it, so a busy program gets one WM_MOUSEMOVE at the latest point, not one per place the pointer passed (US25: eight moves fed through mouse_event while the probe read nothing, one a timer tick or all in one tick, came as one; a press or a release keeps the moves on either side). Each mouse event also keeps the buttons and keys as they were when it happened (MK_ flags in wParam: a press and release made while busy came as WM_LBUTTONDOWN with MK_LBUTTON and the moves between with it), where w16 gave every message the state at the time it was read, and gave a captured window's client messages wParam 0. w16 queued every move: a drag loop that draws at each move (the scroll-bar thumb's outline, with a program slow on SB_THUMBTRACK, or all of w16 under DOSBox) fell seconds behind the pointer, working through every place it had passed (a user's report). The merging is in the system queue, so every reader benefits: the scroll-bar and window move/size loops, Program Manager's and File Manager's drags, MDI. UNVERIFIED: whether 3.1 merges a move into one meant for another window. Note for real-input tests: under DOSBox with cycles=max the guest's clock ran about four times the host's while a probe was busy (US25's message times), so host sleeps between moves are long in guest time.
- **The cursor after a capture** (`input.c`, `defwnd.c`; probe US22, boot-test case `capcursor`): ReleaseCapture makes a mouse move where the pointer is, so the window under it gets WM_SETCURSOR and WM_MOUSEMOVE or WM_NCMOUSEMOVE at once, after the call returns (3.1: released over a sizing border, the sizing arrow came up without the pointer moving); DefWindowProc's WM_SETCURSOR shows the sizing arrows over the borders and corners (IDC_SIZEWE, SIZENS, SIZENWSE, SIZENESW; w16 showed the arrow everywhere).
- **Dragging a scroll bar's thumb** (`scroll.c`, track_scrollbar; measured on 3.1 with real mouse input, probe US19 (`tests/apps/us19.c`) as the shell and Microsoft's Notepad, screenshots taken mid-drag; boot-test case `sbdrag`): the thumb's place is shown by its outline, a rectangle of the thumb's size one pixel wide, corners included, inverted through USER's 50% gray pattern aligned to the screen (on VGA the pixels where x + y is odd: the black lines go white at every other pixel, the Scrollbar gray dark gray), drawn over whatever is there. At the press it is drawn on the thumb; it then follows the pointer pixel for pixel, keeping its offset, stopped at the ends of the travel; the real thumb stays unless the program moves it (US19's bars that call SetScrollPos on SB_THUMBTRACK: the outline is then on the moved thumb; Notepad scrolls its text on SB_THUMBTRACK but moves the bar only on SB_THUMBPOSITION, so its thumb stays at the top). With the pointer outside the bar's rectangle widened by 4 pixels left and right and 1 up and down (measured a pixel at a time on a window's vertical bar and a horizontal control, both 17 thick; UNVERIFIED on other displays), the outline goes back to the starting place; back inside it follows again. Messages: the press sends SB_THUMBTRACK with the bar's position; each move that changes the position sends SB_THUMBTRACK (none when it does not change); the position is the outline's offset in the travel scaled to the range and rounded to the nearest (travel 150, range 100: 1 pixel 1, 3 pixels 2, 4 pixels 3); outside the room it is the starting place's position on that scale, which can differ from the bar's (a 120-high control at 50 sent 51); the release erases the outline and sends SB_THUMBPOSITION with the last position sent, then SB_ENDSCROLL. The task's windows repaint during the drag (US19 only invalidates on SB_THUMBTRACK and the tracked number was drawn mid-drag), so the loop dispatches WM_PAINT. A repaint of the bar itself (SetScrollPos from SB_THUMBTRACK) draws the outline again after the bar, with the same clipping. w16 before drew nothing, sent no SB_THUMBTRACK at the press, one on every mouse move, no snapping back, and no paints until the release. Seen on the way and done since: the cursor after a capture, and EM_GETHANDLE (the next two items above); sbdrag still types its text. Fixed on the way, as the Notepad part showed against 3.1: no WM_SETCURSOR while the mouse is captured (3.1 SDK; the capturing frame's I-beam had replaced the arrow mid-drag); in the edit control, Ctrl+Home and Ctrl+End go to the text's start and end, a multi-line control draws whole lines only, SB_THUMBTRACK/SB_THUMBPOSITION positions map to the nearest line (pos × (lines - 1) / 100 rounded), and the scroll code EM_GETTHUMB answers the first line × 100 / (lines - 1) rounded (Notepad's thumb after the release: 45 lines, first line 32, thumb at 73).
- **DOS calls** (`dirlist.c`, USER's only INT 21h user: LB_DIR/CB_DIR, DlgDirList; boot-test case `runbrowse`): inline `#pragma aux` code with far pointers passes the selector in ES, named in the parm list (`parm [es dx]`), and loads DS from it. OpenWatcom orders a general-register pair by its own rule (`[cx dx]` puts the segment in CX, `[bx dx]` in DX), so FindFirst's `[bx dx]` loaded the specification's offset into DS: in an all-w16 session Program Manager's File > Run > Browse... ended Windows with a #GP in LB_DIR.
- **DrawText with a prefix or a tab** draws the line in pieces; each advances by its extent less the font's tmOverhang (a synthesized bold font's), and the line's width counts the overhang once: on 3.1 "Run &Minimized" in Program Manager's Run box and COMMDLG's "File &Name:" are as wide as without the & (boot-test case `runbrowse`). Still different from 3.1 there (the case's known pixels): the Run box's disabled default OK button keeps the default border (3.1: none); in the Browse box the File Name edit's selection, the Directories list box's height and the drop-down combo boxes (frame, height; the owner-drawn Drives box shows no drive icon).
- **Messages:** posted messages in the queue's ring; input in a system queue filled by the event procedures at interrupt time, taken in order by the task it belongs to; WM_PAINT and WM_TIMER made on demand; WaitEvent/PostEvent for sleeping and waking; inter-task SendMessage queues the call on the receiver's queue and waits for the reply, handling calls to its own queue meanwhile (DirectedYield to the receiver). Timers: one SYSTEM timer (55 ms).
- **Input, as the interactive hangs taught it** (`tests/inputrun.sh`: DOSBox on a virtual X display driven by xdotool, so input comes through the emulated mouse and keyboard, MOUSE.DRV and KEYBOARD.DRV and USER's event procedures at interrupt time; SNAP.EXE's posted messages never reach that path). A reader of the system queue takes the first event that belongs to its queue and passes its filter (Pietrek ch. 7: one system queue of 120 events, which a task reads for the events that are its own); an event for another task's window stops the scan and wakes that task; an event over the desktop (a window of no task) is anyone's. PeekMessage with PM_NOREMOVE leaves the event where it is. An event is taken before WM_NCHITTEST is sent. Bugs found this way, all USER's and all on Microsoft's KRNL286 as well: a mouse event over the desktop stayed at the head for ever and all input with it (the pointer moved, nothing else did); PM_NOREMOVE (WaitMessage, the dialog loop's idle check) and filtered reads took or threw away events (keys and clicks vanished). What the event procedures and the timer tick touch at interrupt time is far data in a fixed segment (segment 4, class FAR_DATA; Windows 3.1's USER has the fixed segment 44 besides its moveable data segment): the data segment is moveable and moves when its heap grows. Until master 88dcaf4 that segment was in fact moveable in the built file (`user.lnk`'s `segment type data moveable` covered it too; it now has `segment class 'FAR_DATA' fixed`), the queues the tick and the event procedures set wake bits in were moveable global blocks (now GMEM_FIXED: GlobalLock does not keep a block from moving in protected mode), and MOUSE_EVENT read the screen size from DGROUP (now far copies). `tools/irqcheck.py` (run by `make test`) checks the NE segment table for a moveable FAR_DATA segment and disassembles the interrupt-time code - KBD_EVENT, MOUSE_EVENT_C, TIMER_TICK and every USER function they call - for any memory operand reached through DS; it flags the old build's faults. Alt opens the menu only when pressed and released alone; menus opened from the keyboard get their item letters (TranslateMessage in the menu loop).
- **Notepad's save prompt** (Alt+F4 after typing) failed on w16's KRNL286 before it had KERNEL.207 (IsDBCSLeadByte, which Notepad calls there: an unimplemented KERNEL export ends the task); on Microsoft's KRNL286 it appears as on 3.1.
- **US6** (`tests/apps/us6.c`; compare its TK.LOG with `build/ref-us/us6-31S.txt`): a multi-line edit control with both scroll bars, scrolled by EM_LINESCROLL, WM_VSCROLL/WM_HSCROLL and a thumb dragged with real mouse input (a SYSTEM.DRV timer procedure calling USER's mouse_event, USER.299, at interrupt time: `tests/apps/mevent.asm`). EM_GETTHUMB (WM_USER+14, internal in the 3.1 DDK; Schulman ch. 7 has only its number) returned 0 every time. The scroll bars run 0-100: the vertical position is first line * 100 / (lines - 1) and the first line goes down to the last; EM_LINESCROLL's low word is lines, its high word characters; SB_THUMBTRACK/SB_THUMBPOSITION positions map back on that scale; a page is the lines shown less one; EN_VSCROLL (and EN_HSCROLL after EM_LINESCROLL) come after the move, not while the thumb is tracked, and SB_THUMBPOSITION notifies even without a move. w16's edit control matches all of it but the horizontal scale (unknown: 10 characters gave 20, 35 gave 69, a thumb at 30 came back as 29).
- **Task signals and resource handlers** (with w16's KRNL286, 9.4 TK32): SignalProc (USER.314) is pascal (hTask or hModule, code, exit function, hInstance, hQueue): 40h for each module loaded after USER, 80h when one is freed, 20h when a task ends (its windows, timers, queue and GDI objects go). KERNEL installs USER.356/357 (LoadDIBCursorHandler, LoadDIBIconHandler) as the RT_CURSOR/RT_ICON resource handlers of modules loaded after USER, and InitApp sets them for the task's module: they read the resource (AccessResource) and make a discardable device-format block from DIB or device data (`user/icon.c`). Without them the first icon Program Manager loads calls an unimplemented stub and Windows ends (`w16: back to DOS`): the boot-test `userk` failure of master f9bed36 was this, a USER omission, not a KERNEL one.
- **Windows and painting:** SetWindowPos invalidates the old and new places and the windows repaint (w16 does not copy screen bits; the result is the same when windows paint the same). Visible regions: the window's rectangle cut to its ancestors' client areas, less the siblings above it (and above each ancestor) where clipping applies, less children with WS_CLIPCHILDREN, each child counting only inside the window's client area (w16's Program Manager puts its MDI client at -1,-1, partly over the frame; subtracting the whole child rectangle left its frame's inner line and menu-bar line unpainted). The desktop paints at once when invalidated (it has no task). Menus (bar, popups, a modal menu loop reading the queue), MDI (MDICLIENT, DefFrameProc, DefMDIChildProc, icon titles as #32772 windows, a maximised child's controls in the frame's bar and the window menu's list of children: US18 in 10.4), accelerators, hooks, the caret, DrawText are there, and dialogs (`dlg.c`: templates, DefDlgProc, modal and modeless, IsDialogMessage, the item functions), MessageBox (`msgbox.c`), the controls (`button.c` BUTTON and STATIC, `edit.c`, `listbox.c`, `combo.c`, `scroll.c` SCROLLBAR and windows' scroll bars). Moving and sizing (`track.c`: SC_MOVE/SC_SIZE by mouse and keyboard, the gray frame as thick as the border less its black line; dragging and sizing Program Manager with real mouse input, `tests/scripts/input/dragpm.txt`, gives 3.1's screens to the pixel, mid-drag included), MDI cascade and tile and the next-window order (`tests/scripts/user/mditile.txt`: rectangles, Z order and screens as 3.1's), WM_MDICREATE's default place for CW_USEDEFAULT (the i-th child i cascade steps down, modulo the client's height / 66 + 1, i counting every child the client has made: destroyed ones and ones at a place of their own too; probe US28 in boot-test `mdimax`; w16 counted the children there, and QuickTime's movie, closed and opened again, came back a step higher), CalcChildScroll and ScrollChildren (UNVERIFIED ranges), the Task List (Ctrl+Esc, a double-click on the desktop: TASKMAN.EXE), Alt+Tab and Alt+Esc (switching at once; 3.1 shows the title while Alt is held), mouse acceleration (WIN.INI MouseThreshold1/2, MouseSpeed). Not yet: clipboard, DDE plumbing, the installable-driver API.

- **Bad pointers; USER's `__GP`** (`user/gp.asm`; probe US27, `tests/apps/us27.c`; boot-test case `gpuser`; notes in `docs/notes/gpuser-handover.md`). US27 makes 129 calls of USER's pointer-taking functions (strings, RECTs and POINTs, MSG, PAINTSTRUCT, WNDCLASS, WINDOWPLACEMENT, templates, buffers; the edit, list box and combo box messages; SystemParametersInfo) with NULL (N), a freed selector (F), a not-present one (P, a discarded block), an offset past a small segment's limit (L), the segment's last 4 bytes (E) and, for output, a code selector (R), and logs what each returns and leaves behind. Its own DPMI exception hooks in front of KERNEL's note every fault (the module and segment) and, for one KERNEL would not recover (HasGPHandler asked about that address only, never logged), throw back to the probe, as an Application Error would have ended the task on 3.1; the few task ends out of their reach (KERNEL's own) `tests/us27run.sh` steps over by booting again. On 3.1 (KRNL286 and KRNL386, DOSBox-X as a 386) every F and P fault happens inside USER (a few in KERNEL or GDI called with the pointer) and nearly all are recovered: the function returns at once with a fixed value (0 or FALSE mostly; GetMessage -1; OpenComm -1; UnionRect and SubtractRect TRUE; IsRectEmpty and PtInRect whatever AX held), having done nothing (no text set, nothing invalidated or validated, no message removed; IntersectRect empties its destination first); some end the task (GetWindowText, GetDlgItemText, lstrcmp/lstrcmpi, IntersectRect's destination, AppendMenu/InsertMenu/ModifyMenu, GetCaretPos, RegisterWindowMessage, RegisterClipboardFormat, the list and combo box strings). A RECT or buffer at a segment's end is written or read up to the limit before the fault (SetRect leaves left and top, CopyRect copies 4 bytes, GetWindowRect writes left and top; KERNEL's GetAtomName and lstrcpyn under GetClipboardFormatName and DlgDirSelect likewise). w16: `gp.asm`'s primitives touch a caller's memory, each load and access inside a range of the table it exports (USER.484, resident, a fixed code segment; made from its `GPRANGE` labels when it is assembled; `gp_sel` loads a selector, `gp_copy`/`gp_fill` copy or fill byte by byte and say how far they got, `gp_strlen`), and every function US27 found surviving checks (`GP_BAD`) or copies through them first and returns 3.1's value; the recovery code is entered as TK40 found (registers as at the fault, SP 4 lower) and makes its primitive return. KERNEL's atom functions and lstrcpyn stop where 3.1's fault (`ptr_room`). The emulators check segment limits only in part (DOSBox none; DOSBox-X's normal core only for string instructions, never for a null selector's memory, which is linear 0, the real-mode interrupt table, nor for a data write to a code segment), so the reference (`us27ref.h`, 932 values, from two runs on each KERNEL, identical) keeps the F and P cases and only those N, L, E, R cases in which 3.1 took a fault (`tools/us27ref.py`); the probe also puts back the interrupt table after each NULL case and points L, E and R into blocks of their own, since a write the emulator lets through had broken later cases; w16's primitives use string instructions and take a null selector for a fault. Not checked, so not done: NULL where 3.1 shows no fault (w16 keeps its own NULL handling), and the cases 3.1 ends the task for (US27 skips them on w16).
### 10.4 Measured on Windows 3.1 (screenshots, US2, US3, TK26)

- **US2** (`tests/apps/us2.c`; `us2ref.h`): GetSystemMetrics 0-48 on VGA (e.g. caption 20, menu 18, frame 4 with BorderWidth=3, icon spacing 75x72, full-screen height 460), the default colours (no WIN.INI [colors]), dialog base units 8x16 (the 52 letters' width /26, +1, /2: UNVERIFIED formula, right for the system font), double-click 452 ms, caret blink 530 ms, timer resolution 1000, keyboard type 4/0/12, SPI values, the desktop (style 96000000h, class #32769, no parent, 640x480), the system classes (styles, extra bytes, brushes, cursors; GetClassInfo returns their atom: C302h... for named ones, 8000h-8004h for #32768-#32772), AnsiUpper/AnsiLower/IsChar* over 0-255, the lstrcmp and lstrcmpi order of all characters (punctuation, then accented letters upper before lower, then digits, then A a B b ...), and wsprintf (`%+d` prints "+d"). w16 matches all except the system classes it does not have yet.
- **The frame** (thick frame, caption, menu): outer black line, 2 pixels of ActiveBorder, the inner black line; the caption is SM_CYCAPTION including a black line above (the frame's inner line) and below; the system-menu box is the left (top-level) or right (MDI child) 18x18 half of DISPLAY's OBM_CLOSE, then a black line; OBM_REDUCE/OBM_ZOOM/OBM_RESTORE (19x18 each, own black line on the left) at the right; notches in the frame at SM_CXSIZE + SM_CXFRAME from each corner; the title centred between in the system font, the text cell one row under the caption's top line; the menu bar SM_CYMENU high and a black line under it.
- **The menu bar:** each item is its text's width to the nearest system character cell plus a cell (8 pixels) on each side, the text at the item's left + 8 (Program Manager's File/Options/Window/Help: 40/64/72 pixels for texts of 24/50/52). UNVERIFIED as a rule: w16's own Program Manager, with identical menu strings, gets 40/66/68 from Microsoft's USER (it changes an item with ModifyMenu before showing the window; Microsoft's measured the same widths both times, US1).
- **Minimised windows** are 36 pixels wide (TK26: 24h), the class icon drawn at 2,2; a minimised window of a class with an icon gets WM_PAINTICON and WM_ICONERASEBKGND instead of WM_PAINT/WM_ERASEBKGND (Program Manager's groups otherwise paint their contents into the icon); its title is a window right under it, text centred one pixel left of the icon's centre (measured; UNVERIFIED as a rule), in MS Sans Serif 8, black on the parent's background or white on the active caption colour.
- **DrawText** (US3, `tests/apps/us3.c`; compare its TK.LOG with `build/ref-us/us3-31S.txt` from `tests/refrun.sh`): DT_WORDBREAK breaks only at blanks, a too-long word stays whole (clipped) and widens DT_CALCRECT; a line that breaks at a blank is measured with that blank for centring and right alignment while it still fits (DT_LEFT and DT_CALCRECT always); when even the first word does not fit, centred and right-aligned drawing (not DT_CALCRECT) ends the line after the word and starts the next with the blank; centring rounds down; with DT_SINGLELINE, DT_VCENTER/DT_BOTTOM move the text and the value returned (and DT_CALCRECT's height) includes the offset. 7 of about 3200 values still differ (a lone last word "h" centred two pixels further right; a left-aligned DT_CALCRECT case).
- TK26: icon blocks from LoadIcon are discardable (GlobalFlags 0100h) and are discarded by GlobalCompact(-1) (4100h): 3.1 makes them through a resource handler; w16's are plain moveable blocks. IDI_APPLICATION on VGA is a 272-byte monochrome block. Not yet followed.

- **US4** (`tests/apps/us4.c`, `us4.rc`; `tests/userscreens.sh OUT` runs it on 3.1 and on w16's USER and compares the 12 screens it saves as C:\US4_nn.BMP): four message boxes, a dialog of every control, a Run-like dialog in MS Sans Serif 8, a modeless dialog with a combo box dropped, a window with scroll bars and a menu, its File menu and system menu opened from the keyboard, maximised, minimised. Measured from it:
  - *Dialog frame:* black line, 4 pixels of the active caption colour (3 at the top), the caption's top line white and one row into the frame, white at the caption's ends; client 24 below the top and 5 inside the other edges; DS_MODALFRAME windows are #32770 with CS_BYTEALIGNWINDOW (their left edge to the nearest 8 pixels: 136, 168, 120 where centring gives 137, 169, 122). Dialog units are rounded to the nearest pixel (base units 7x13 put x 6, y 20 at 11, 33). A dialog off the top of the screen is moved down to 0.
  - *MessageBox:* icon at 18,16 of the client area, text at 68 (DrawText, word-wrapped to keep the box within 5/8 of the screen), the taller of the two at y 16 and the other centred on it; buttons 58x28, 16 apart, 22 below, 10 above the bottom; client width the larger of text + 16 and buttons + 86; centred on the screen. The first of three buttons sits 2 pixels further left than even spacing gives (gaps 18 and 16: not understood; open).
  - *Controls:* push buttons: 1-pixel black border without its corner pixels, 2 pixels of highlight and shadow, text centred left of the shadow, the focus rectangle around the text 19 high with the system font; a focused push button gets the default button's 2-pixel border (the default one keeps it). Disabled text (buttons, statics) is grayed through GrayString's checkerboard (text kept where screen x + y is even), a disabled edit control's text is not grayed. DrawFocusRect's dots are where x + y is even and the corners are left out. Check boxes use OBM_CHECKBOXES 13x13 cells, the text 1 pixel lower and 1 further right than centring after the box gives. Group box text 9 in and 3 down, the line cleared 2 pixels each side. A list box is a pixel bigger each side than the rectangle it is given (its WS_BORDER lies outside it), and without LBS_NOINTEGRALHEIGHT it shrinks to whole items (US32 below has the rule). Combo boxes: the box 24 high with the system font, a 17-pixel raised button around OBM_COMBO (a 7x9 arrow), the dropped list's top line on the box's bottom line; a simple combo's list is indented 8, and so is a CBS_DROPDOWN's dropped list (a CBS_DROPDOWNLIST's is as wide as the box: US32). An edit's selection background is a pixel wider than its text (UNVERIFIED: 3.1 also makes it a row taller in MS Sans Serif). Scroll-bar controls thicker than 17 keep 17-pixel arrows with black beside them; shaft and thumb take the whole thickness; the thumb is at arrow - 1 + travel * (pos - min) / (max - min) rounded, its shadow's inner line a pixel in.
  - *Windows' scroll bars* lie outside the client area (NCCALCSIZE takes SM_CXVSCROLL - 1), their outer lines on the frame's or menu bar's lines, the two bars sharing a line, a BTNFACE square between them. The menu bar spans the frame's inside, over the vertical bar.
  - *Popup menus:* a black frame and a one-pixel shadow right and below starting a pixel in (light gray over white; UNVERIFIED colour); items SM_CYMENU high, separators 7 with the line 3 down; text 15 in, accelerators left-aligned 8 after the widest text, 14 from the right; a popup opens on the bar's (or, for the system menu, the caption's) lower line; opened from the keyboard its first item is selected, even a grayed one (whose text is then the highlight text through the gray pattern). The system menu grays Restore for a normal window (the other states UNVERIFIED) and its box is inverted while it is open.
  - *Minimised top-level windows* start a whole icon spacing (72) above the bottom of the screen: icon window at y 408, title at 444.
  - US4's remaining differences: w16's icon artwork (IDI_* and the application icon are w16 originals by rule), the three-button gap above, and the edit selection's height.
- **US14** (`tests/apps/us14.c`, `us14ref.h`, `us14ref286.h`; run as the shell by `tests/refrun.sh` and by `DROPIN_FIXED=1 tests/userboot.sh -n -t 120 -s build/dos/US14.EXE`: it runs about a minute, longer than a settled screen waits): menus and GetFreeSystemResources, through TOOLHELP (GlobalFirst/Next for USER-owned blocks with a local heap, LocalFirst/Next for their blocks, SystemHeapInfo). On 3.1 USER has its DGROUP heap and three more: a 100h-byte one (window text and the global atom table: US15) and the menu and menu-string heaps, 420h bytes at start. CreateMenu: a fixed 14h-byte block (handle = address) in the menu heap; each item adds 1Ah to the menu's item array there; an item's text is a block of its length + 1 in the string heap; DGROUP is not touched. Each heap grows to 64K: from a fresh boot 2712 empty menus, 2503 items in one menu (the item array's single block), 510 items of 120 characters (the string heap) fit before CreateMenu/AppendMenu fail. **GetFreeSystemResources**: a heap's percentage is GetHeapSpaces' free and total bytes, each shifted down to whole kilobytes, free * 100 / total (GetHeapSpaces' total is the most the heap could span, its free that less what is in use: 9.7); GFSR_USERRESOURCES is the least of USER's DGROUP, menu and string heaps, GFSR_GDIRESOURCES GDI's, GFSR_SYSTEMRESOURCES the lower of the two, other arguments 0. Every one of 571 values seen while menus, strings and windows filled each heap in turn fits; the probe checks the rule as it goes. SystemHeapInfo's percentages are GFSR(2) and GFSR(1). w16's USER before: menus and strings in DGROUP (2848 menus filled it, and no window could be made after), GFSR always 90. Now, on Microsoft's KRNL286: every plain value as 3.1 under KRNL286 (block sizes as TOOLHELP shows them differ between KRNL286 and KRNL386: us14ref286.h), 2726 menus, 2515 items, 511 long items (3.1's system-menu template takes room in its heaps), the rule fits every value. On w16's KRNL286 the same counts (2726, 2515, 511); there TOOLHELP's global walk shows no block with a local heap (GLOBALENTRY.wHeapPresent: KERNEL's, open), so the probe's heap observations fail and its rule check is skipped. Filling the menu heap first faulted in w16's KERNEL: LocalAlloc's growth asked GlobalReAlloc for the whole 64K, the global heap rounded that to 10010h bytes, two tiles and a new selector, and the heap's old selector was gone; growth now stops at FFF0h (`kernel/local.c`, grow). With w16's other heaps (US15, 10.3) its last CreateWindow count is 994 (3.1 under KRNL286: 956; with ExtendedUserHeap=1 the probe's cap, 4000), the rule still fits every value, and the probe looks for a block first in the heaps that grew (heaps made alike have blocks at the same offsets).
- **US15** (`tests/apps/us15.c`, `us15ref.h`, `us15ref286.h`; run as US14 is, `-t 260`): what each USER object costs in each of USER's heaps (TOOLHELP block walks and GetHeapSpaces, before and after one object), how many fit, the resource curve, and the WND and CLASS fields at Schulman's offsets read from USER's data segment against the documented calls. On 3.1 (KRNL286 sizes in brackets where they differ): a window 3Ch + its extra bytes in DGROUP, nothing else (40h in GetHeapSpaces terms) [3Ah]; its text, length + 1, in the fourth heap (G below) and not DGROUP; scroll bars 10h more in DGROUP; a class 24h + extra + its name's atom in DGROUP, a string menu name its length + 1 more there; a property 4 bytes in one block per window in DGROUP, a string name a global atom in G; a hook 10h, a CS_OWNDC window's DC 10h, in DGROUP; a timer nothing (3.1 allows 31); RegisterWindowMessage and RegisterClipboardFormat atoms in DGROUP, GlobalAddAtom's in G; an edit control nothing but its WND (its text in the application's heap), a list box 80h in DGROUP with its WND, its strings nothing. G is the 100h-byte heap of US14, grows to 64K, holds window text and the global atom table, and is not counted by GetFreeSystemResources. Also seen: 3.1's RegisterClass returned 1 rather than the class's atom (US16: it does so for a program marked for Windows 3.00, and returns the atom to one marked 3.10; w16 does the same); 3.1 accepts string-named properties after G is full but loses them (8000 set, 4078 read back with G grown before by window titles, 3965 from a fresh G).

  | US15 on Microsoft's KRNL286 | 3.1 | w16 before | w16 now | w16, ExtendedUserHeap=1 |
  |---|---|---|---|---|
  | DGROUP static data (segment size) | AB8h | 1742h | 2CEh | 2CEh |
  | windows (no text, no extra) | 952 | 904 | 989 | 5182 |
  | windows with 120-character titles | 509 | 302 | 989 | 5287 |
  | windows with 10 extra bytes and a 12-character title | 801 | 603 | 833 | 5287 |
  | classes (5-character names) | 1088 | 851 | 1131 | 1133 |
  | integer-named properties on a window | 8000+ | 4816 | 8000+ | 8000+ |
  | string-named properties (read back) | 3965-4078 | 2064 | 3968 | 3968 |
  | hooks | 3048 | 2894 | 3241 | 3241 |
  | timers | 31 | 3000+ | 3000+ | 3000+ |

  Menus are US14's (unchanged: 2726 menus, 2515 items, 511 long items). GetFreeSystemResources(GFSR_USERRESOURCES) at start 96% on 3.1, 98% on w16; along the window fill it falls with DGROUP as on 3.1 (one point per about 10 windows; 0 when DGROUP is full), and in extended mode one point per about 52.
- **US16** (`tests/apps/us16.c`, `us16ref.h`; built twice, `US16.EXE` marked for 3.00 as the linker leaves it, `US16A.EXE` marked 3.10 by `tools/nemark.py`): RegisterClass's result. On 3.1 (Standard and 386 Enhanced, KRNL386 and KRNL286) it is 1 for the 3.00 program and the class's atom (GetClassInfo's result, GCW_ATOM) for the 3.10 one, for a string name, a 20-character one, "#12345", an integer atom, CS_GLOBALCLASS and a local class named "Button" (whose atom is the system class's, C302h); 0 for a second registration. The SDK declares it ATOM; Pietrek (ch. 4) says "either 0, 1, or the atom handle of the class name". w16 follows the registering module's expected version.
- **US5** (`tests/apps/us5.c`): where menu-bar items go. Plain windows: each item is its text plus 8 pixels on each side, whatever way the menu was made or changed (resource, ModifyMenu, AppendMenu, resize, CheckMenuItem, hide and show). In a window whose class has **CS_BYTEALIGNCLIENT** (Program Manager's frame class, style 1000h in US1's trace) each item also starts on an 8-pixel boundary (rounded to the nearest), and the window's client area's left edge goes to the nearest multiple of 8 (asked for x 10 and 333, the windows went to 12 and 332). This replaces 10.4's rounding rule, which only fitted Program Manager.
  With a CS_BYTEALIGNCLIENT class it is the items' widths that are rounded to the nearest 8, not their places: a maximised Program Manager group's 19-pixel system-menu bitmap ahead of File moved File, Options, Window and Help 19 pixels right with the same widths, 40, 64, 72, 48 (Windows 3.1, the items highlighted in turn).
- **US18** (`tests/apps/us18.c`; boot-test case `mdimax`, which also drives Program Manager with real keys and a click, `tests/scripts/input/mdimax.txt`): a maximised MDI child. Its controls go into the frame's menu bar: first a popup holding the child's own system menu (GetMenuState 0914h: MF_POPUP | MF_BITMAP and nine items; GetSubMenu is GetSystemMenu(child)), drawn as the child's system-menu box (the right half of OBM_CLOSE and a black line, 19 x 18), inverted when selected; last a command SC_RESTORE, MF_HELP | MF_BITMAP (4004h), OBM_RESTORE at the bar's right end, OBM_RESTORED while selected. Choosing either sends the frame WM_COMMAND with the SC_ code, which DefFrameProc passes to the active child as WM_SYSCOMMAND. The frame's caption is "Frame - [Child]" (DefFrameProc keeps the frame's own title from WM_SETTEXT; the frame is not sent WM_SETTEXT for it); the child's style loses WS_SYSMENU; its system menu grays Move, Size and Maximize. All of it is DefMDIChildProc's WM_SIZE: a child that keeps WM_SIZE from it is maximised with nothing of this, and WM_MDIGETACTIVE then says it is not maximised. Activating another child (WM_MDIACTIVATE, WM_MDINEXT, closing it) moves the state: old WM_MDIACTIVATE(0), new WM_SIZE maximised, old WM_SIZE restored, new WM_MDIACTIVATE(1); the old one's system menu gets Restore grayed again (only then: restored by SC_RESTORE, WM_MDIRESTORE or ShowWindow its states stay); a child maximised from its icon goes back to its icon. A new child (WM_MDICREATE) restores the maximised one first and is not maximised; the maximised child minimised gives the activation to the next child that is not an icon, which stays restored; the last child destroyed is only sent WM_MDIACTIVATE(0). Alt+- opens the system menu from the bar (its first item selected); a grayed item's letter closes the menu with nothing chosen. Also measured: the window menu's list of children (a separator, then "&1 One" ... with ids from the first child id, the active one checked, none at all without children; ids are renumbered to stay consecutive when one goes), which WM_MDISETMENU moves with the maximised controls to the new menus (wParam TRUE only rebuilds the list; the old frame menu is returned); separators are MF_SEPARATOR | MF_DISABLED (0802h); a new MDI child's system menu has Minimize grayed until first maximised, an icon's Size and Minimize; a plain child window gets the top-level system menu (Close Alt+F4, Switch To...), only MDICLIENT's children the MDI one; one WM_SIZE per maximise or restore (w16 sent two), and a second one when the WM_SIZE handler takes a scroll bar away; WM_QUERYOPEN before an icon is opened; WM_CHILDACTIVATE before the WM_SIZE when a child that is not active is maximised. On the keyboard, Right from the last bar item and Left from the first select the window's system menu (its box inverted, its popup opened if one was), and Right into a popup selects its first item. US18's 3700 observations are identical on w16's USER. Not done: more than nine children in the window menu ("More Windows...", UNVERIFIED), and one Program Manager difference: a group maximised straight from its icon (another group maximised, the icon chosen from the Window menu) keeps scroll bars that 3.1 does not show (US18's own child in the same steps gets 3.1's messages; Program Manager reacts to something else, maybe the paint messages 3.1 sends during the change, not traced).
- **US32** (`tests/apps/us32.c`, `us32.rc`; boot-test case `lbclip`, which runs it as the shell on 3.1, on w16's USER and on w16's USER and GDI; its window spy `us32s.c`, US32S.EXE, loaded beside QuickTime's Control Panel): a list box's height and its item partly shown at the bottom edge. Found with QuickTime's Control Panel (QTW16.CPL, Files): its "QuickTime Files" list (LISTBOX, WS_BORDER, WS_VSCROLL, LBS_NOTIFY, 120 pixels asked in a dialog of MS Sans Serif 8) is 106 high on 3.1, eight items of 13; w16 made it 119, nine items, and the list ran under the static that QuickTime puts 112 pixels below its top ("QuickTime support application"), so a scroll drew its last row over the static and the list's border. Nothing drew outside a DC's region: the list was simply that tall. **The rule** (3.1, without LBS_NOINTEGRALHEIGHT or LBS_OWNERDRAWVARIABLE): the window, H high, becomes floor(H / item height) items + 2 high, whatever its frame (WS_BORDER, WS_DLGFRAME, none: the client area is then the rest), 0 items when H is less than one (7 asked gave 2); when it is made (item height the system font's 16, or WM_MEASUREITEM's for LBS_OWNERDRAWFIXED, asked with 16 before WM_CREATE) and whenever it is sized, from the new height. Every list box is made a pixel bigger each side than asked, border or not. WM_SETFONT changes the item height but not the window, whatever lParam, visible or not, with a SetWindowPos of the same size, SWP_NOSIZE, SWP_FRAMECHANGED or a move after it; but a dialog's list box is sized again from the height the system font gave it (73 asked: 64, then 65 with the dialog's 13; 120 asked: 112, then 104; w16's dialog manager calls `listbox_refit` after WM_SETFONT). LBS_NOINTEGRALHEIGHT and LBS_OWNERDRAWVARIABLE keep the height. A drop-down combo box's list is the height asked less 23 (the box's height less one), rounded the same way, 2 for a box of 30; a simple one's list the same; a CBS_DROPDOWN's dropped list is 8 in from the box's left and 8 narrower. **The scroll bar** of a list box runs 0-100 whatever the count, hidden too (0 to 300 items): the position is top * 100 / max rounded, max the last top item (the one that shows the last item whole: count less the whole items shown); SB_THUMBPOSITION n goes to n * (max + 1) / 100 rounded; a page is the whole items shown less one; LB_SETTOPINDEX stops at max. **The item the bottom edge cuts**: LB_SETCURSEL and the Down key scroll it into view, a click selects it without scrolling. **LBS_OWNERDRAWVARIABLE**: WM_MEASUREITEM for each item as it is added (itemHeight 16), each item its own height in the scrolling, the range, the clicks and LB_GETITEMHEIGHT(i). **GetDC** on a list box (any system control; their classes have CS_PARENTDC) gives the parent's client area, clip box -18 -16 245 114 in a dialog, as for an application's CS_PARENTDC class (US29); w16 now does so too, and USER's own drawing in a control asks for its own area (`ctl_dc`); BeginPaint stays clipped to the update region. **The focus rectangle** of a single-selection list with nothing selected: none in a dialog whose list box keeps its height, on the first item when the box was sized to whole items, and it comes when the box, painted, gets the focus again (w16's rule in `focus_shown`; what 3.1 keeps is not known). Also measured: the class styles (LISTBOX 88h, ComboLBox 808h, COMBOBOX 88h, STATIC 80h, EDIT 88h, BUTTON 8Bh, as w16's). The owner-drawn drawing, logged at first, is compared since 2026-11-20 (US33 below).
- **US33** (`tests/apps/us33.c`, `us33.rc`; boot-test case `lbdraw`, which runs it as the shell on 3.1, on w16's USER and on w16's USER and GDI, the OBS lines identical, the last run without the pixel checksums and the tabbed items' ink): how a list box draws, what a screen DC covers, LB_SETTABSTOPS, child dialogs' frames. **Owner-drawn list boxes** (fixed and variable heights, with and without LBS_HASSTRINGS, single, multiple and extended selection, multicolumn; plain ones by their pixels), each step's WM_DRAWITEMs in order (item, action, state, rcItem, inside the list box's WM_PAINT or not, its update rectangle), checksums of the pixels with an owner whose focus rectangle is an XOR, and (logged) the list box's own messages around them. 3.1 draws what changes: the caret (the focus rectangle) is taken away and put back with ODA_FOCUS around every change, sent wherever the caret item is (its rcItem off the client area too); a selection change is ODA_SELECT for the items that changed and show (single: the old off, the new on; extended keys: every other item off and the new one drawn even if it was selected; Shift from the anchor); LB_SETSEL invalidates the items instead, and the caret moved onto an item waiting to be painted is drawn by that paint; a scroll moves the pixels (ScrollWindow: the update region is the strip that comes in, WM_ERASEBKGND sent) and paints at once with the caret away (variable heights: one item at a time is scrolled, more are repainted whole; columns scroll sideways); WM_VSCROLL and WM_HSCROLL take the caret away and back for every code, SB_ENDSCROLL and an unmoved thumb too; WM_PAINT draws the items meeting the update region with ODA_DRAWENTIRE between a caret off and a caret on, and fills only where there is no item: an item's area is cleared by WM_ERASEBKGND alone, so an item painted again without erasing keeps the pixels its owner leaves (the caret's among them). The keys move the caret first (off, on), then take it away for the selection and the scroll; a click takes it away, selects and puts it back (extended: Ctrl turns the item over and moves the caret after, Shift first clears every item but the anchor, then selects from the anchor, the anchor keeping its state; the button's release takes the caret away and back); a click on the item selected draws nothing. Page Down in a variable-height box does not move. An insertion or deletion at or above the last item shown repaints the box whole (columns: always), a deletion brings the caret into view; a single-selection box keeps the selection's index across an insertion. The caret is never drawn on a hidden box, nor before the box's first paint (a dialog's list box focused while the dialog is hidden shows none); 3.1 sends a dialog's whole-item list box a WM_SIZE after its first paint, on which it shows its caret (on, off, on) and invalidates the caret item (columns: the whole box): w16 posts that WM_SIZE (`late_size`; UNVERIFIED what sends it on 3.1). The empty box's focus rectangle is item -1, state ODS_SELECTED | ODS_FOCUS, on the first line (0 high with variable heights). A hidden vertical bar keeps its position (emptied at 40: 0-100 at 40); a multicolumn box's horizontal bar runs 0-100 too, the first column shown as a share of the last first column (1 of 2: 50, 1 of 3: 33), its thumb n to n * (max + 1) / 100 columns. LB_GETSEL past the end is LB_ERR (w16 gave FFFFh). Until 2026-11-20 w16 drew every item shown again for any change with ODA_DRAWENTIRE. **DrawFocusRect** (dfr): the dots in the background colour where x + y is odd in the DC's own coordinates, wherever the DC lies on the screen, the others XORed with the text colour (w16 had them where x + y is even, and the list box compensated with white text on black); an empty rectangle draws nothing. **FillRect and FrameRect** take a brush only: a system colour's number + 1 (a class's hbrBackground) draws nothing (US33's owner fills its selected items so: on 3.1 they stay white; w16 filled them with the highlight; w16's Color applet filled its scheme items so and now makes a brush). **GetDC(NULL)** (gdc): the whole screen, a simple region (GetClipBox 0 0 640 480, type 2): GetPixel reads the top-level windows and their children and PatBlt paints over them; so do GetWindowDC of the desktop or of NULL and GetDCEx(desktop, DCX_WINDOW); GetDC of the desktop window leaves the top-level windows out (type 3), and the desktop's own painting keeps clipping them. w16 left them out of every one (and read DCX_CACHE as DCX_CLIPCHILDREN). **LB_SETTABSTOPS** (tab, tto): only with LBS_USETABSTOPS (0 without); none or (0, NULL): the default, TabbedTextOut's 8 average characters (the 52-letter base unit: 64 in the system font, 56 in bold MS Sans Serif 8, 48 in ANSI_VAR_FONT; the SDK's 32 dialog units are the same); one value: a stop every that many; the stops are dialog units of the box's font when they are set, rounded (13 units of base 7: 23), and stay when the font changes; stops count from the item's left edge, the text 2 pixels in; past the last stop, TabbedTextOut's default ones; text past the right edge clipped; LB_GETTEXT gives the tabs back. TabbedTextOut itself: one stop is an interval, a negative one divides as unsigned (the text after its tab at -30 whatever came before). Before, w16 ignored LB_SETTABSTOPS (QuickTime's Setup page put "Processor:" values one default tab further than 3.1). **Child dialogs** (dlgf): a template's WS_CAPTION with DS_SETFONT is a border, as without a font (QuickTime's Control Panel's pages are such templates: 8 pixels too wide and high on w16, and the applet sizes its window and tab control from them); WS_THICKFRAME counts its width less the border (200 x 120 units' worth of client in a 206 x 126 window, client 198 x 118); a child dialog starts at 0 in its parent when its frame would put it above or left of it, and its left edge is byte-aligned on the screen as it is made (#32770's CS_BYTEALIGNWINDOW). Top-level templates with a font and a caption the same (Character Map's stray glyph, 15.5, was the focus). **The dialog's focus** (idf): a dialog made with a NULL dialog procedure gets no WM_INITDIALOG, whatever its class, and the first tab stop has the focus once it is shown (w16 sent it to the class's procedure); with a procedure, TRUE gives the first tab stop the focus, FALSE leaves it where the procedure put it, a control made in WM_INITDIALOG too. **WS_CLIPSIBLINGS** (cs): a child of a CS_PARENTDC class (BUTTON, STATIC, EDIT, LISTBOX, SCROLLBAR, a program's own) made with it reads back without it unless its parent has WS_CLIPCHILDREN; COMBOBOX and classes without CS_PARENTDC keep it, and so does SetWindowLong; no move, z-order, show, enable or focus change adds it. w16 kept it always. Still different: QuickTime's Control Panel's Help button (ID E145h) has it on 3.1 and not on w16, which this rule does not explain (the rule only takes it away); `tests/qtcpl.sh` compares the panel's styles but that bit. **PtVisible** (pv): 0201h for a visible point, 0101h when the DC has a clip region, 0 outside (w16's GDI: 1); RectVisible and RectInRegion (the old ordinals the SDK's names import) 0101h, PtInRegion and EqualRgn 1.
- **US34** (`tests/apps/us34.c`, `us34.rc`; boot-test case `combo`, which runs it as the shell on 3.1, on w16's USER and on w16's USER and GDI, the OBS lines identical, the last run without the pixel checksums): a combo box's layout, found with Control Panel's Desktop dialog, whose drop-down list boxes w16 drew 24 pixels high beside 20-pixel edit fields (the user's "about two pixels too low/tall"). Combo boxes of the three styles made in a window and given eight fonts, dialogs of MS Sans Serif 8 and 10 and of the system font, owner-drawn ones, one laid out as the Desktop dialog, CB_SETITEMHEIGHT; for each the window, the edit's rectangle, its client area and EM_GETRECT, CB_GETITEMHEIGHT, CB_GETDROPPEDCONTROLRECT, the dropped list's place, size and top item, and checksums of the box's and the dropped list's pixels (closed, focused, the edit painted again alone). **The height** comes from the font: the selection field's text height T is f + min(f, s) / 4 - 2 (f the font's height, s the system font's), the box H = T + 6 (11, 17, 20, 22, 24, 32, 49 for f = 6, 11, 13, 15, 16, 24, 41), whatever height it was made with, and again on every WM_SETFONT; CB_GETITEMHEIGHT(-1) is H, CB_GETDROPPEDCONTROLRECT the box as made. An owner-drawn box asks WM_MEASUREITEM for item -1 once, before its list is made (offered 18, the system font's T; ODT_COMBOBOX; then the list's item, offered 16), H is the answer + 6, and WM_SETFONT leaves it. w16 kept the system font's 24 for every box and sent no WM_MEASUREITEM for the box, and its lists were not owner-drawn at all. **The list** hangs from the box's bottom line (top at H - 1), 8 in under CBS_DROPDOWN and CBS_SIMPLE, as wide as the box under CBS_DROPDOWNLIST, and is made cy - H high (a simple box's cy - H - 1) then whole items and its border (an LBS_OWNERDRAWVARIABLE list as it is), when the box is laid out, not when it drops (CB_SETITEMHEIGHT of an item leaves it); a simple box keeps its height; the list has a vertical bar only with the box's WS_VSCROLL, and none while no whole item shows; CB_SETCURSEL brings the item to the list's top. **The drawing**: CBS_DROPDOWNLIST is framed whole, the button (SM_CXVSCROLL wide, H high, framed, a raised face, the OBM_COMBO arrow centred, only its black drawn) at the right, the selection field (2, 2) to (button - 1, H - 2), the highlight's with the focus but not while the list is down, the item at (3, 3), the focus rectangle on the field in the highlight's colours; an owner-drawn field is filled and the colours set before the owner's WM_DRAWITEM (rcItem 3, 3, button - 2, 3 + T). CBS_DROPDOWN's edit is W - 24 wide and H high, the button 7 pixels to its right standing alone; CBS_SIMPLE's edit is as wide as the box. **Edit fields**: a single-line edit with WS_BORDER, and a combo box's edit (no WS_BORDER), draw their frame in their client area, which is the whole window (GetClientRect 42 x 20 for a 42 x 20 field); the text's rectangle (EM_GETRECT) is q = min(f, s) / 4 in from the top and as far from the sides (half the average width for a fixed-pitch font), f high but no lower than q from the bottom, whatever the field's height (w16 centred the text in the field: a 16- or 23-high MS Sans Serif field had it a pixel or two off, and EM_GETRECT gave the client area); the text is clipped at q from the bottom; the selection's highlight is the text's extent (a synthesized bold font's overhang included) and f + 1 high, the caret after the text's advance (extent less overhang), f + 1 high, 1 pixel wide in MS Sans Serif 8 and 2 in the system font and MS Sans Serif 10; a combo box's edit dropped shows its selection, focused or not, until it next loses the focus; an edit without ES_AUTOHSCROLL does not scroll to the caret. **Scroll bars** shorter than two arrows and a 2-pixel shaft have (len - 2) / 2 long arrows (16 in 34 and 35, 15 in 32, 12 in 26). **Dialogs**: a dialog pushed back on the screen keeps SM_CYDLGFRAME at the bottom (as SM_CXDLGFRAME at the right); a group box's line is half its font's height down (6 in MS Sans Serif 8; w16 used the system font's 8). Still different (27 of about 1100 observations): a CBS_DROPDOWN edit whose text is wider than the field, unfocused, shows a sliver of the character cut at the right where 3.1 shows none; 6-pixel Small Fonts boxes (an 8-high list's squeezed arrows); a simple box given a second font in a window too short for it (3.1's list 0 high); a simple box's pixels right after CB_SETITEMHEIGHT for an item; CB_SETITEMHEIGHT(-1) on a box that is not owner-drawn is modelled on one case.
- **US35** (`tests/apps/us35.c`; boot-test case `editwrap`, which runs it as the shell on 3.1, on w16's USER and on w16's USER and GDI, the OBS lines identical, the last run without the pixel checksums): a multi-line edit control's word wrap and undo (`user/edit.c` had neither). Controls 80, 130 and 200 pixels wide in the system font, a bold MS Sans Serif 8 and SYSTEM_FIXED_FONT given nine texts, the fixed font a pixel at a time, then scripts of keys, clicks and messages logging the text, the selection, the caret, the first line, EM_CANUNDO, EM_GETMODIFY and the parent's EN_ notifications after each step. **Lines**: a multi-line control without ES_AUTOHSCROLL wraps at its format rectangle: a character fits while the line with it is no wider than the rectangle; the line then breaks after the last space or tab before the character that did not fit, or after that character when it is the first space after a word (one space hangs past the edge, more do not: "word" and 40 spaces makes lines of spaces), or, with no space or tab before it on the line (a lone CR or LF stops the search), at that character (a word longer than the line is cut; at least one character a line); hyphens and punctuation are no break; CR LF ends a line and so does CR CR LF, a lone CR or LF is a character; a break that falls on a CR LF takes it (no empty line), and one at the text's end makes no new line. EM_LINELENGTH and EM_GETLINE count a line's characters without its CR LF; EM_LINEINDEX(-1) is the line the caret shows on, EM_LINEFROMCHAR(-1) the caret position's; EM_LINEINDEX past the last line -1. **EM_FMTLINES** TRUE (answers 1) writes CR CR LF into the text at every soft break, FALSE (answers 0) takes every CR CR LF out, the line breaks the same; after TRUE a typed character changes nothing but sends EN_UPDATE, EN_CHANGE and EN_MAXTEXT and sets the modified flag. **The format rectangle** (EM_GETRECT): the client area, 4 pixels in on every side with WS_BORDER in every font (the control draws its frame in its client area, which is the window less its scroll bars: 113 x 43 of 130 x 60 with both), cut to whole lines (4,4,126,52 in the system font for 130 x 60, 56 in MS Sans Serif, 49 in Fixedsys); EM_SETRECT and EM_SETRECTNP take a rectangle and apply the same (5,5,85,80 gives 9,9,81,73), a resize or WM_SETFONT puts the client area back; a single-line control ignores them; a single-line control without a frame answers 0,0,width,font height. **The caret** at a soft break is shown at the next line's start, except after End, Up, Down, a click past a wrapped line's end and typing there: then at the end of the line before; it is never shown right of the rectangle's right edge less 2, and a caret on a line out of view is at -20000,-20000. Up and Down take the caret's x (not a remembered column; unclamped) to the nearest position on the next line, the left one at a tie; Page Up and Down the last position not right of it; clicks the nearest; a double click selects the word left of the place clicked (from the line's start at most) with its spaces, the caret at its end; Ctrl+Left and Right go to word starts. **Alignment**: ES_CENTER shifts a line by half the room it leaves, ES_RIGHT by all of it less a pixel, trailing spaces counted, never left of the rectangle. **Tabs** default to every 32 dialog units of the control's own font (64 pixels in the system font and Fixedsys, 56 in a bold MS Sans Serif 8). **Scrolling**: the view follows the caret down only with ES_AUTOVSCROLL or WS_VSCROLL, across only with ES_AUTOHSCROLL or WS_HSCROLL (a third of the rectangle at a time, when the caret leaves the client area); EM_LINESCROLL scrolls across only then too, by the font's dialog average (8 in the system font), and always notifies EN_VSCROLL and EN_HSCROLL; WM_VSCROLL answers MAKELONG(lines asked, 1) and notifies even when nothing moved (SB_TOP and SB_BOTTOM do nothing, answer 0); EM_SETSEL brings the caret into view whatever its wParam. **Undo**: a multi-line control whose lines wrap keeps one change: an insertion alone (each typed character is one; typing or pasting over a selection makes two changes, two EN_UPDATE/EN_CHANGE pairs, and only the insertion is kept), or a deletion (Backspace, Delete, a cut, a clear), joined to the deletion before when they touch, also across an undo. EM_UNDO (and WM_UNDO, Ctrl+Z, Alt+Backspace) takes the insertion out (the caret where it was) or puts the deletion back selected, and turns the record round, so a second undo redoes; EM_REPLACESEL empties it (and answers 1), as do WM_SETTEXT, EM_SETHANDLE and EM_EMPTYUNDOBUFFER, but not EM_SETSEL, EM_SETMODIFY or EM_FMTLINES. A single-line control, and a multi-line one that does not wrap (3.1's Notepad without Word Wrap: "abc" typed over a selection undone whole, the selection put back selected, `tests/scripts/np/undo.txt`), keeps the run typed with the selection it replaced, or a run of touching Backspaces or Deletes (Backspace after typing starts a deletion: "hello world", two Backspaces, Undo puts "ld" back; undo puts back what was replaced or deleted, selected); UNVERIFIED whether EM_REPLACESEL empties a non-wrapping one's record. **Messages**: typing sends EN_UPDATE then EN_CHANGE (EN_HSCROLL between them when it scrolls), past the limit EN_MAXTEXT alone, a paste cut short by the limit EN_UPDATE, EN_CHANGE, EN_MAXTEXT; a multi-line control's WM_SETTEXT notifies nothing; EM_LIMITTEXT answers the new limit, 65535 for 0; WM_CUT, WM_CLEAR, WM_PASTE answer 1, WM_COPY the characters copied; Ctrl+C, Ctrl+X, Ctrl+V (WM_CHAR 3, 24, 22), Ctrl+Insert, Shift+Delete and Shift+Insert copy, cut and paste. **The word-break procedure** (EM_SETWORDBREAKPROC; EM_GETWORDBREAKPROC answers it, 0 before; EM_SETWORDBREAK, WM_USER+26 of 3.0, is ignored) is given the whole text and its length. To wrap, 3.1 asks WB_ISDELIMITER of the character that did not fit (if it is one: a break after it when it is a space, at it otherwise, so a hyphen starts the next line), then of the one before (a break at it), then WB_LEFT of it; Ctrl+Right asks WB_ISDELIMITER and WB_LEFT of the caret's place and goes to WB_RIGHT of one past WB_LEFT's answer, Ctrl+Left goes to WB_LEFT's answer (asking WB_RIGHT too), a double click selects from WB_LEFT's answer to WB_RIGHT's of one past it; nothing is asked at the text's ends. The procedure must be a MakeProcInstance thunk: an exported function of a program loads its DS from AX, and a raw pointer hung 3.1 itself in some of these calls (whatever AX held) and w16 in all. A 20 KB text: the same line count and the same first line after typing (3.1 took 5 ms a typed character there; w16, which wraps again only from the change's line to where the lines meet the old ones, 14 ms). A change wraps from its own line on: words never go back up to the line before (3.1's Notepad with Word Wrap: "thirteen" typed in front of "hello" went down with it and stayed down when a space and more words followed; `tests/scripts/np/undo.txt`, which with `NP_W16FILES` w16's USER passes but for the window handles); setting the text, a resize and a new font wrap it all again. **Read-only**: ES_READONLY counts only for programs marked for 3.1 (a 3.0 program's control took typing, Delete and pastes); EM_SETREADONLY always does. Still different (left out of the comparison): text with a lone CR or LF (3.1 measures them differently in a way not worked out); how far ES_AUTOHSCROLL scrolls to the right (3.1 moved 128 pixels in three steps in a 122-pixel rectangle, 40 to the left; w16 a third, 40, both ways); WM_HSCROLL's page (3.1: 2 characters) and thumb in a control with a horizontal bar; Page Down with the caret above the view; EM_SETWORDBREAKPROC's answer (not 0 on 3.1).
- **US23** (`tests/apps/us23.c` with its helper task `us23a.c`, `us23ref.h`, `us23ref286.h`; `REFRUN_FILES=build/dos/US23A.EXE tests/refrun.sh build/dos/US23.EXE`, and `DROPIN_FIXED=1 tests/userboot.sh -n -t 150 -f build/dos/US23A.EXE=US23A.EXE -s build/dos/US23.EXE`): the system menu and heap shrinking. A window with WS_SYSMENU costs nothing in the menu heaps: windows share the system menu USER keeps (Pietrek: loaded in LoadWindows); GetSystemMenu(h, FALSE) makes a window a copy (130h bytes in the menu heap and 64h in the string heap; KRNL286 128h/54h), returns that copy again, another window gets another; GetSystemMenu(h, TRUE) destroys it and returns 0; DestroyWindow destroys it. A plain child with WS_SYSMENU gets the same nine items (Restore, Move, Size, Minimize, Maximize, -, Close Alt+F4, -, Switch To... Ctrl+Esc). The same nine items by LoadMenuIndirect or AppendMenu cost FCh/4Eh: 3.1's copy is 2Ch and 6 bytes bigger, not understood (w16's copies cost FCh/4Eh: the probe's remaining 14 differences). Shrinking: the heaps stay grown while the task that grew them runs; when any task ends (US23A doing nothing) DGROUP and both menu heaps are back at their start-up sizes. LocalShrink on the probe's own heap: only the free arena at the heap's end goes back, never below LocalInit's span; it returns LocalHeapSize + 0Ah (as TK54 found; w16's KERNEL agrees). Why share rather than build per window: a copy costs about 400 bytes of the menu heaps, which are 64K each; with a shared menu only windows whose program asks for their system menu (to change it) pay, and GetSystemMenu keeps 3.1's behaviour. Everything else in US23 is as on 3.1 on Microsoft's KRNL286 and on w16's.
- **CM2** (`tests/apps/cm2.c`; `tests/commusr.sh`; boot-test cases `commuser`, `commall`, `termclose`): USER's comm functions (USER.200-215, 245; `user/comm.c`). COMMREC (`tests/apps/commrec.c`), built as COMM.DRV in front of the real driver renamed MCOM in a scratch copy (`tools/nerename.py`), records every call USER makes to the driver, values only. Windows 3.1's USER imports COMM 1-15 and finds 19 (CommWriteString), 20 (ReadCommString) and 100 (EnableNotification) at run time; most functions pass their arguments straight on, the application's pointers included; it checks itself only ids below 0, ports it has not opened (GetCommState and SetCommState IE_NOPEN, ReadComm 0, WriteComm, CloseComm and UngetCommChar -1) and EnableCommNotification's window. OpenComm takes COM1-COM9, AUX, LPT1-LPT3 and PRN with or without a colon, calls setque then inicom with a DCB of its own (9600, 7, even, 1, binary, XON/XOFF 11h/13h, limits 10: WIN.INI [ports] is not read; for a parallel port only the Id is set), returns inicom's error or the id; either queue size 0 is IE_MEMORY. WriteComm's result is negative when not everything went; a byte put back goes into ReadComm's buffer ahead of the full count read after it and is not counted, and GetCommError counts it in cbInQue. BuildCommDCB parses MODE's syntax: the rate by its first two digits (11 15 30 60 12 24 48 96 19), parity n e o m s, data bits 7 8, stop bits 1 2, 'p' for the three time-outs FFFFh; an empty field is MODE's default (e, 7, 1); a field followed by a separator and then the end of the string is dropped. The bytes each entry pops (CM2 through a thunk) are user/ordinals.txt's. CM2's 3000-odd observations, COMMREC's record and the far end's are identical with w16's USER; CM1's exchange (part C) is identical on every w16 part (`commall`). Terminal closed while sending a file (Alt+F4, Yes) now closes on every w16 part (`termclose`; with the stubs WriteComm sent nothing and Terminal never finished); the run showed two USER differences on the way, fixed: COMMDLG's file dialogs had their tab order backwards (ShowWindow brought a shown child to the top of the Z order; 3.1 leaves it in place, US18), and the separators' MF_DISABLED. Not done: what 3.1 does with ports a task leaves open (here nothing; the queues are USER's, GMEM_SHARE); Terminal's "Default Serial Port" list box shows a scroll bar and four of its five items where 3.1 shows all five, and its status bar differs (open).

### 10.5 Where it stands

`tests/userboot.sh` (options: `-k` w16's KRNL286 too, `-s PROG` another shell, `-n` no reference run):
- **Program Manager (Microsoft's PROGMAN.EXE) on w16's USER, WIN /S, Microsoft's KRNL286, GDI and drivers: pixel-identical** to the same boot on Microsoft's USER (ImageMagick compare: 0 pixels), and **also on w16's KRNL286** (0 pixels against w16's KERNEL with Microsoft's USER, itself identical to Microsoft's KERNEL, 9.4). BOOTLOG.TXT is the same (the TSRQuery lines at the end included).
- TK20 as the shell runs as on 3.1 (one difference: messages peeked in its first second, 2 against 3).
- **w16's own PROGMAN.EXE on w16's USER: pixel-identical** to w16's PROGMAN on Microsoft's USER (`-s build/dos/PROGMAN.EXE`). The 3710 pixels were USER bugs: the menu-bar rule (it rounded every item; US5), the visible region subtracting the whole MDI client rectangle (10.3), and DrawIcon using the DC's text and background colours for the masks (w16's PROGMAN leaves its highlight colour selected; 3.1's icons stay right). One PROGMAN difference remains against Microsoft's PROGMAN: 3.1's frame class has CS_BYTEALIGNCLIENT (1000h), w16's (shell/progman/main.c) has style 0, so its menu-bar items are not 8-pixel aligned and its window may sit a few pixels off.
- US4: screens 1, 5, 7-11 pixel-identical; 2-4, 6, 12 differ by w16's icon artwork, the three-button gap and the edit selection's height (10.4).
- `tests/boottest.sh user userk` (w16's USER on Microsoft's and on w16's KRNL286): 0 pixels.
- `make test` passes.

### 10.6 Next

1. tests/pmscenarios.sh and tests/pmcompare.sh on w16's USER (menus open, Run and Properties dialogs); the MessageBox three-button gap and the edit selection height (more US4 cases).
2. Measure what is still UNVERIFIED in moving/sizing (keyboard steps), CalcChildScroll's ranges, Alt+Tab's title display, the edit control's horizontal scroll scale; dragging icons.
3. USER's own resources (system menus from RT_MENU, the size-all cursor, icon 32647, string tables, the dialogs) as w16 originals.
4. (The installable-driver API is done: 10.7.) Clipboard, DDE plumbing (WM_DDE_* broadcasting, GlobalAddAtom use), WinHelp (starting WINHELP.EXE), ExitWindowsExec, the remaining timers/hooks callbacks (WH_KEYBOARD, WH_GETMESSAGE, WH_CALLWNDPROC, journal hooks), SOUND.
5. The applets, each compared with scripted input (tests/apps/snap.c): Notepad, Calculator, Clock, Write, File Manager, Control Panel, Solitaire, Cardfile.
6. Probes for the guesses listed by `grep -rn UNVERIFIED user`: queue length, the system queue's length, the cascade of CW_USEDEFAULT, icon placement, timer rate, the dialog base unit formula for other fonts, what SignalProc gets at task exit (with KERNEL's drop-in work), the DC hook (USER's SetDCHook procedure: when GDI calls it).

### 10.7 Installable drivers (`user/driver.c`; probe DRP)

Until 2026-11-17 w16's USER exported OpenDriver, CloseDriver, SendDriverMessage, DefDriverProc, GetDriverModuleHandle, GetDriverInfo and GetNextDriver as stubs returning 0, and loaded the `[boot] drivers=` modules with LoadLibrary only. MMSYSTEM opens TIMER.DRV with OpenDriver("timer") from its DRV_LOAD (US1, 10.2), so on w16's USER it had no timer device: QuickTime for Windows' Movie Player then waited for ever in a PeekMessage(PM_REMOVE | PM_NOYIELD) loop and Windows stopped (`docs/notes/quicktime-handover.md`). The SDK documents the API (Vol. 1 ch. 25, Vol. 2, Vol. 3 DRIVERINFOSTRUCT); the rest was observed with **DRP** (`tests/drv/drp.c`, run as the shell by `tests/drv/drun.sh`) and its driver **DRT.DLL** (`tests/drv/drt.c`), which logs every DriverProc call into the probe's log. Observed on 3.1 and done by w16:

- **Handles** are small numbers, the lowest free one from 1, taken when OpenDriver starts (MMSYSTEM, opened at boot, is 1; TIMER, opened inside MMSYSTEM's DRV_LOAD, 2). The open instances form a list in the order their opens *finished* (so TIMER comes before MMSYSTEM): GetNextDriver(0, 0) walks it, GND_REVERSE backwards, GND_FIRSTINSTANCEONLY gives each module's first instance in the list only (backwards too with both flags). An unknown handle gives 0 everywhere.
- **OpenDriver(name, NULL, lParam):** `[drivers] name=FILE OPTIONS` of SYSTEM.INI, else name is the file itself ("DRT.DLL" works; options ""); with a section, name must be there (no fallback). No "Cannot find" box for a missing file (MMSYSTEM's OpenDriver("joystick") at boot; w16's KERNEL showed the File Error box). The module is loaded once per instance (its usage counts the instances). A module's first instance gets **message 0**, DRV_LOAD, DRV_ENABLE (identifier 0); DRV_LOAD answered 0 frees it and ends there. Each instance then gets DRV_OPEN with identifier = its own handle, lParam1 = the options string (never NULL), lParam2 = lParam; the answer becomes the identifier of every later message, and 0 fails the open (a first instance then gets DRV_DISABLE and DRV_FREE). GetDriverInfo's alias is the name as passed ("drt", "DRT.DLL", "drt2" for a second [drivers] name of the same file, which counts as another instance of the same module).
- **CloseDriver:** DRV_CLOSE with the identifier and both parameters; 0 keeps the instance open and is returned; otherwise the identifier becomes 0 and the module's last instance gets DRV_DISABLE (with that identifier) and DRV_FREE (always identifier 0).
- **DefDriverProc** answers 1 to DRV_LOAD, DRV_ENABLE, DRV_DISABLE, DRV_FREE and DRV_INSTALL for a handle that is open (or being opened), 0 to everything else and for any other handle. **GetDriverInfo** fills hDriver, hModule and the alias only when length = 134 (sizeof DRIVERINFOSTRUCT); otherwise 0 and nothing written.
- **Boot drivers** (`[boot] drivers=mmsystem.dll`) are opened at the first InitApp once Windows is up (the shell's: US1 saw MMSYSTEM's DRV_LOAD inside Program Manager's InitApp): message 0, DRV_LOAD, DRV_ENABLE, no DRV_OPEN (identifier 0). A probe that never calls InitApp sees no drivers open at all.
- **A task's end** leaves the drivers it opened open; each module's first instance gets DRV_EXITAPPLICATION (lParam1 DRVEA_NORMALEXIT) in the ending task's context. **Windows' end** (ExitWindows): each module's first instance gets DRV_EXITSESSION, DRV_DISABLE and DRV_FREE; the other instances nothing.
- UNVERIFIED: the order among several modules at a task's and at Windows' end (DRT is the only module the probe owns), DRVEA_ABNORMALEXIT for a killed task, how many drivers 3.1 allows (w16: 64), what message 0 is.

`tests/drv/drun.sh` runs DRP on Microsoft's USER and on w16's (Microsoft's KRNL286), again with DRT.DLL as a boot driver, and under KRNL386 (Microsoft's KRNL386 and USER against w16's): the logs are identical (216, 205 and 216 lines). The same day USER also got **BeginDeferWindowPos, DeferWindowPos, EndDeferWindowPos** (`user/pos.c`, from the SDK; a moveable global block of positions that EndDeferWindowPos applies with SetWindowPos in order; UNVERIFIED against 3.1, which may move the windows in one go), which QuickTime's player called while they were still stubs.

Found on the way (QuickTime's player, `docs/notes/quicktime-handover.md`): SetWindowPos now decides what its WM_WINDOWPOSCHANGED says (place, size, SWP_NOMOVE/SWP_NOSIZE) when it has placed the window, before the messages it sends after that (the WM_SIZE and WM_MOVE held back since CreateWindow, WM_CHILDACTIVATE). A window shown for the first time that moved itself on that first WM_MOVE (QuickTime's movie window offsets itself by 24, 4 from where its player put it) got a second WM_MOVE from the show's own WM_WINDOWPOSCHANGED, which w16 had built from the place at the end, and moved again (32 pixels right and 4 down of 3.1's place); with the fix it lands where it does on 3.1. UNVERIFIED by a probe of its own: the message order is the same as before, only the stale second move is gone.
## 11. GDI

w16's GDI is `gdi/` (OpenWatcom C, large model, SS ≠ DS, `__loadds` exports; `make gdi` builds `build/dos/GDI.EXE`). It is a drop-in for Windows 3.1's `WINDOWS\SYSTEM\GDI.EXE`: `tests/gdiboot.sh` copies it over Microsoft's in a scratch copy of the reference install and boots `WIN /S` with Microsoft's KERNEL (KRNL386), USER, drivers and fonts (`-k`: with w16's KRNL286 as well; `-r`: Microsoft's GDI, for reference), takes a screenshot (`-s`) and compares it (`-C PNG`). `tests/gdiscenarios.sh` runs SNAP scripts (`tests/scripts/gdi/*.txt`, `tests/scripts/pm/*.txt`) with Microsoft's GDI and with ours and compares every screenshot.

Sources (§3): the Windows 3.1 SDK reference; the Windows 3.1 DDK documentation, *Device Driver Adaptation Guide* (the GDI ↔ display-driver contract: GDIINFO, PDEVICE, PBITMAP, DRAWMODE, LBRUSH, TEXTXFORM, FONTINFO, the font file format, each driver function's reference page; read from a mirror of the DDK's QuickHelp file `ddag31qh.hlp`, kept under `.tools/docs/ddag31`, not in the repository); Schulman et al., *Undocumented Windows* ch. 8 (object header and signatures, the undocumented exports; facts only, each confirmed by observation); Wine's X11-licensed `gdi.spec` (wine-20020228; parameter types only, `tests/gdi/gdi.spec`); and black-box observation with the GD probes below. No Windows binary was disassembled; NE tables, resource data and the files' published formats were read.

### 11.1 Observation tools (probes GD1, GD2)

- `tools/modimports.py WINDOWS_DIR [-t MODULE]`: for every NE file of an install, which ordinals of a module it imports, ranked by importer count. 156 GDI ordinals are imported statically; USER imports 98 (the USER ↔ GDI contract, 11.3), PBRUSH 77, WINHELP 56, WRITE 49. Only USER imports DISPLAY (101-104: Inquire, SetCursor, MoveCursor, CheckCursor). **GDI imports nothing but KERNEL** (83 ordinals: local and global heap, resources, profile strings, file I/O, atoms, selectors) and finds the display driver's functions with GetProcAddress by ordinal.
- **GD1** (`make gdprobes`; `tests/gdi/display.spec`, `tests/gdi/gdlog.*`, `tools/mkgdlog.py`): a DISPLAY driver that records every call with its arguments, the structures they point to and the result, and forwards it to VGA.DRV, copied as `VGAREAL.DLL` with its module name changed in the NE resident-name table (`tools/nerename.py`, scratch copies only). The cursor functions (called at interrupt time) are forwarded without recording. GDI's initialisation fails with a stand-in that lacks the driver's resources, so `tests/gdiboot.sh -l` links VGA.DRV's resources into the scratch copy of GD1 (`tools/ne2res.py`). With GD1 in place the boot's screen is pixel-identical to the plain boot.
- **GD2**: the same for GDI itself (`tests/gdiboot.sh -L`): a GDI.EXE exporting all 3.1 ordinals (names from the real name tables, parameter sizes from Wine's spec) in front of the real GDI (or ours) renamed GDJ. GD1 and GD2 write one interleaved log, `C:\GD.LOG`, so every GDI call shows the driver calls it made.
- `tools/gdtrace.py MS.LOG W16.LOG`: the diff of the drawing calls (BitBlt, ExtTextOut, Output, FastBorder, ...) two runs made into the driver, pointers neutralised: the same calls with the same coordinates, strings, clip rectangles and DRAWMODEs draw the same pixels.
- `tools/fondump.py FILE.FON`: the .FNT headers of a font file.

### 11.2 The display driver as GDI uses it (VGA.DRV, GD1)

- Not enabled by GDI's initialisation: the first `CreateDC("DISPLAY")` (USER's, at `INIT=Display`) calls **Enable twice**: `Enable(lpGDIINFO, 1, NULL, NULL, NULL)` with a buffer in GDI's data segment, returning 6Eh (the GDIINFO size); then `Enable(lpPDevice, 0, NULL, NULL, NULL)` with dpDEVICEsize (23h) bytes at offset 0 of a global segment. UNVERIFIED from the DDK text: Enable's parameter list (its reference page is missing from the mirror); GD1 saw these five.
- VGA's GDIINFO: version 030Ah, DT_RASDISPLAY, 208 x 156 mm, 640 x 480, 1 bit x 4 planes, dpNumBrushes -1, dpNumPens 80, 16 colours, dpCurves 0, dpLines 22h, dpPolygonals 08h (scan lines only: GDI must decompose rectangles, ellipses and polygons), dpText 2204h, dpClip 1 (one clip rectangle), dpRaster 46D9h (BITBLT, BITMAP64, GDI20_OUTPUT, SAVEBITMAP, DI_BITMAP, DIBTODEV, BIGFONT, OP_DX_OUTPUT; no STRETCHBLT, so GDI stretches), aspect 36/36/51, 96 x 96 logical pixels per inch, dpDCManage 4. GetDeviceCaps(i) returns the word at offset i of GDIINFO (indexes 8, 0Ah, 0Ch, 0Eh, 26h, 58h, 5Ah seen).
- VGA.DRV's exports: 1-21 as the DDK numbers them (21 is SetDIBitsToDevice, named DIBSCREENBLT), 90 DO_POLYLINES, 91 DO_SCANLINES, 92 SaveScreenBitmap, 101-104 the cursor, 400-403 PEXTTEXTOUT, PSTRBLT, REXTTEXTOUT, RSTRBLT, 500 UserRepaintDisable (called by USER through GetProcAddress, not by GDI).
- The boot to Program Manager makes **824 driver calls** and only these: ColorInfo 320, ExtTextOut 176 (most of them extent queries), BitBlt 121, DeviceBitmapBits 99, RealizeObject 33, FastBorder 7, Enable 2 (and USER's 66 UserRepaintDisable). No Output, StrBlt, Pixel or SaveScreenBitmap.
- Colours: a new DC gets the physical colours of white (background) and black (text) through ColorInfo; SetBkColor, SetTextColor and GetNearestColor call ColorInfo. VGA's physical white is 3FFFFFFFh, black 20000000h.
- Realizing: RealizeObject is called twice (the size with lpOutObj NULL, then into a buffer in GDI's heap): brush 36h bytes, pen 6. The LBRUSH GDI passes holds the **logical** RGB in lbColor (not the physical colour the DDK describes), lbBkColor the DC's physical background colour, and for BS_PATTERN the pattern's PBITMAP far pointer in lbColor and its global handle in lbHatch; lpTextXForm carries the brush origin (y:x). Fonts: RealizeObject(OBJ_FONT) with the LOGFONT returns 0 (no device fonts) and GDI uses its own.
- Memory bitmaps: one global block, a 20h-byte PBITMAP then the bits (bmBits = sel:0020h); bmWidthPlanes = bmWidthBytes x bmHeight; bmScanSegment and bmFillBytes = 65536 div and mod (bmWidthBytes x planes), set even below 64K; planes interleaved per scan line (DDK). A new memory DC's bitmap is a 1 x 1 monochrome one. GDI passes PBITMAPs as lpDestDev, also to RealizeObject.
- SetDIBits → DeviceBitmapBits(lpPBitmap, 0, start, n, bits, bmi, the DC's DRAWMODE, NULL), also for monochrome bitmaps; a BITMAPCOREHEADER is first converted on the stack to a BITMAPINFOHEADER (28h, width, height, 1, bit count, the rest 0).
- Text: **fonts reach the driver as 3.x fonts** (VGA sets RC_BIGFONT): a 2.x raster font is converted in memory (dfVersion 300h, dfFlags 1 fixed or 2 proportional, the character table of 6-byte entries at 94h, the bits right after it: System's dfBitsOffset 5DAh, face moved accordingly); FONTINFO is at offset 42h of the font's segment and dfBitsPointer = sel:dfBitsOffset. TEXTXFORM for a plain raster font: height dfPixHeight, width dfAvgWidth, weight dfWeight, the rest 0. GetTextExtent = ExtTextOut(dev, 0, 0, NULL, s, -n, ...), height in DX, width in AX; a zero count returns 0 with no driver call. TextOut = ExtTextOut at screen coordinates with lpClipRect = the clip box, no widths, no opaque rectangle, options 0; ExtTextOut passes its options and a screen-coordinate copy of its rectangle.
- PatBlt is the driver's BitBlt with no source; every BitBlt passes the destination DC's physical brush and DRAWMODE; equal-size StretchBlt is a BitBlt; FastWindowFrame (GDI.400) is FastBorder with the rectangle in screen coordinates and the clip box as lpClipRect.
- The display driver's resource GDI reads at its initialisation: OEMBIN `FONTS`, three LOGFONT headers (18 bytes) each followed by a NUL-terminated face name: OEM_FIXED_FONT (Terminal, 12 x 8, OEM charset), ANSI_FIXED_FONT (Courier 12 x 9), ANSI_VAR_FONT (Helv 12 x 9) (the DDK's stock-font resource).
- GDI's initialisation loads FONTS.FON, the fixedfon.fon file and OEMFONTS.FON with LoadLibrary (BOOTLOG.TXT); a .FON's FONTDIR is found through the resource table of the module database (its name is not always "FONTDIR": VGASYS.FON's is the integer 40h).

### 11.3 The USER ↔ GDI contract at boot (GD2)

2380 GDI calls from USER and Program Manager, 57 functions. In order: USER makes 24 empty regions (CreateRectRgn + MakeObjectPrivate), then 6 display DCs with CreateDC("DISPLAY") (each SetDCHook(hdc, USER's procedure, data) and MakeObjectPrivate), GetDCState of one (the state it resets its cached DCs to: SetDCState on every release, 126 times), two CreateCompatibleDC, the stock objects, SelectVisRgn/SetDCOrg/SetBrushOrg on each DC it hands out (the visible region in screen coordinates, then the origin), GetDeviceCaps, solid brushes for the system colours, GetNearestColor, GetTextMetrics and GetTextExtent of "abc...xyzABC...XYZ" (dialog base units), bitmaps for its OEM images and icons (CreateCompatibleBitmap/CreateBitmap + SetDIBits + GetObject, GetBitmapBits/SetBitmapBits), WIN.INI's [fonts] through AddFontResource (it returns the number of fonts in the file: 6 for SSERIFE.FON, 1 for a TrueType .FOT; each is a LoadLibrary, so BOOTLOG.TXT lists them), the icon-title font CreateFontIndirect(MS Sans Serif, -11, 400), FinalGdiInit(desktop brush), then painting.
- Return values seen: IsGDIObject returns the object type (signature − 4F46h: 7 for a DC, 2 for a brush); MakeObjectPrivate the old private flag (0); SelectVisRgn the region's complexity; SetDCOrg 0; DeleteObject 1; RectInRegion (GDI.181) 0101h for true; GetBoundsRect(hdc, rc, 8001h) 3 (DCB_SET) with the rectangle in screen coordinates, or 1 (DCB_RESET) when nothing was drawn: 3.1 accumulates bounds on USER's DCs without SetBoundsRect. Around a menu (SaveScreenBitmap) USER calls SetBoundsRect(hdc, NULL, 8003h) on its five DCs, which returned 8005h or 8007h (its 8000h, DCB_ENABLE, and DCB_RESET or DCB_SET), and after releasing a DC it reads GetBoundsRect(…, 8001h) to know whether anything was drawn over the saved screen. GDIRealizePalette returns a DWORD: USER takes a nonzero DX as a palette change (w16 returning a WORD hung USER after Calculator's menu).
- GetTextMetrics of System: 16, 13, 3, 3, 0, 7, 14, 700, 0, 0, 0, 20h, FFh, default 80h, break 20h, 21h, 0, overhang 0, aspect 96/96 (tmDefaultChar and tmBreakChar are the .FNT values plus dfFirstChar).
- Handles are GDI-heap local handles 4 apart (moveable blocks); objects start with Schulman's header (hNext, signature 'GO'.. with flags, a sequence count, a metafile list).
- Not used during the boot: Output, SetHookFlags, IntersectVisRect/ExcludeVisRect, SaveVisRgn/RestoreVisRgn, palettes beyond the defaults, metafiles, Escape.

### 11.4 Results

- Windows dragged partly off the screen (`tests/scripts/input/dragoff.txt`, real input through `tests/inputrun.sh`; boot-test cases `dragoff` and `dragoffk`): 3.1's USER cuts visible regions to the screen, w16's USER did not, and w16's GDI drew whatever the region allowed, so output past x = 639 wrapped into the next scan lines (the driver does not clip to its surface). w16's GDI now also cuts the composite region to the device surface (the screen, or a memory DC's bitmap). The left-edge glitch was USER's: CS_BYTEALIGNWINDOW rounding with C division (towards zero) put the window 8 pixels right of 3.1's place for negative x. Bisected on Microsoft's KRNL286: w16's GDI with Microsoft's USER was identical to 3.1 in every shot; w16's USER with Microsoft's GDI showed the left/bottom offset; with both fixes w16's USER and GDI match all-Microsoft (0 pixels, six shots, also on w16's KRNL286).
- `make boot-test` (`tests/boottest.sh`) has the cases `gdi` (w16's GDI on Microsoft's KRNL286), `gdik` (on w16's KRNL286) and `usergdi` (w16's USER and GDI on w16's KRNL286), each compared with the all-Microsoft boot.
- `tests/gdiboot.sh -s -C build/gdiref/ms/screen.png` (w16's GDI with Microsoft's KERNEL, USER, VGA.DRV and fonts): Program Manager at 45 s is **pixel-identical** to the reference (AE 0), BOOTLOG.TXT has the same lines. With w16's KRNL286 as well (`-k`): pixel-identical too.
- The boot hits no unimplemented export (`[w16] gdilog=1` in SYSTEM.INI makes w16's GDI write `C:\W16GDI.LOG`: unimplemented ordinals, the display Enable, the font table and mapping).
- w16's own PROGMAN.EXE as the shell: the screen with w16's GDI is pixel-identical to the same shell on Microsoft's GDI. w16's USER (§10) with w16's GDI (`tests/userboot.sh -f build/dos/GDI.EXE=WINDOWS/SYSTEM/GDI.EXE`) gives the same screen as w16's USER on Microsoft's GDI (AE 0).
- CreateDC loads `<driver>.drv` with LoadLibrary (hence BOOTLOG.TXT's `LoadStart = DISPLAY.drv` at `INIT=Display`; w16's BOOTLOG.TXT is now identical to 3.1's); GD2 shows the caller's driver-name buffer unchanged after the call (bytes dumped before and after), so 3.1 works on a copy; w16 does too.
- `tests/gdiscenarios.sh` (SNAP screenshots, Microsoft's GDI vs w16's): Notepad (2 shots), Cardfile, Control Panel, WinMine, and the Program Manager dialog scenarios newitem, props, movecopy, browse (19 shots, among them the Browse common dialog): identical; Paintbrush 25 pixels, File Manager 130, Calculator 333 and 759 (scientific; the round-rectangle buttons' corners), Write 556/1027 and Clock (TrueType fonts: w16 has no TrueType yet), Character Map 4581 (its fonts), Solitaire (random deal and card back, not comparable).
- Dialogs use MS Sans Serif 8 bold, which SSERIFE.FON does not have: 3.1 lets the driver embolden (VGA's dpText has TC_EA_DOUBLE); w16 sets txfWeight 700, the accelerator bit 200h and an overhang of 1 in TEXTXFORM, as GD1's log of GD10 shows 3.1 doing (11.9).
- SetDIBitsToDevice (WinMine's digits): the driver's DIBScreenBlt gets X, Y of the whole DIB's top left (Y = -158 for a 23-line band of a 276-line DIB drawn at y 95), the band's iScan/cScans, and the destination rectangle as lpClipRect.
- The GDI audit's last items (probe GD8, `tests/apps/gd8.c`, in the boot-test case `gdiaudit`; its triage file, `docs/notes/gdi-audit-triage.md`, is in git history): 3.1 keeps a DC's clip region in screen coordinates (GetClipRgn shows it; SetDCOrg moves it with the origin and returns the old origin; RestoreDC puts a saved one back relative to the origin; GetClipRgn calls USER's DC hook first); SetTextJustification's error term starts at count / 2 + 1 and only output moves it on (extents leave it); the character extra is kept as a device value (GetTextCharacterExtra converts it with the mapping of the moment); every bitmap's bmlpPDevice is the device's PDEVICE; Escape on a memory DC calls no driver and returns 0. w16 does all of it so.
- DIBs (probe GD7, `tests/apps/gd7.c`; boot-test case `gdidib`): 1, 4, 8 and 24-bit and RLE4/RLE8 DIBs, DIB_RGB_COLORS and DIB_PAL_COLORS (the default palette, a palette of the program's own, realized or not), through SetDIBits, CreateDIBitmap + BitBlt, SetDIBitsToDevice (memory DC and screen, clip rectangles, part of a DIB, a band of scan lines), StretchDIBits (1:1, 2:1, part, SRCINVERT), GetDIBits in RLE, a DIB over 64K. QuickTime 2.1.2's installer drew its banner (an RLE4 DIB of 487 x 74, CreateDIBitmap with DIB_RGB_COLORS, then BitBlt) black on w16: w16's VGA.DRV refused RLE, so the new bitmap stayed as made. Observed on 3.1 and done so: the display driver decodes RLE (DDK, DeviceBitmapBits) and encodes it for GetDIBits (`drivers/vga/README.md`); SetDIBitsToDevice on a memory DC draws nothing and returns FFFFh (monochrome and colour, every format; GD5 saw the same); on the screen it returns the driver's value, the lines drawn inside the clip rectangle (1 for RLE); StretchDIBits (VGA.DRV has no RC_STRETCHDIB) is SetDIBits into a bitmap in the DC's format and StretchBlt, YSrc counted from the DIB's bottom, returning the source height. A new bitmap's memory is not cleared on 3.1 (an RLE DIB's skipped pixels in CreateDIBitmap or StretchDIBits show what was there before); w16's is: GD7 leaves those cases unchecked. With w16's GDI and VGA.DRV, either alone or both, every checked value of GD7 is 3.1's.
- GD3 (`tests/apps/gd3.c`, the drawing probe; run as the shell with `tests/gdiboot.sh -f build/dos/GD3.EXE=GD3.EXE -i 'boot:shell=C:\GD3.EXE'`): about 3800 of 307200 pixels differ. Identical: thin-pen rectangles (any brush, null pen, dashed and dotted pens, XOR ROP2), lines and polylines, polygons (ALTERNATE and WINDING), the 7 x 5 and 20 x 20 ellipses, pixels, all the text, rectangle regions, InvertRect. Different: wide-pen geometry (approximated), arcs/pies/chords and round rectangles (a few pixels), the 60 x 30 ellipse's top and bottom rows, elliptic regions.
- Decomposition observed (GD1 with GD3), implemented in `gdi/draw.c`: LineTo is one 2-point OS_POLYLINE clipped to its box (right and bottom + 1); Rectangle with a pen and R2_COPYPEN is a BitBlt PATCOPY of the inside then a closed 5-point polyline; with another ROP2 the inside is scan lines; with the null pen the inside reaches right-1, bottom-1; Polygon is OS_BEGINNSCAN, one OS_SCANLINES per row (spans [left, right) from the edges' x rounded to nearest at the row's y, top row included, bottom row excluded), OS_ENDNSCAN, then the closed outline; Ellipse is scan lines (rows top..bottom-2, [x0, x1)) and one polyline of the outline runs; wide pens become scan lines drawn with the pen.

### 11.5 Implementation notes and UNVERIFIED points

- Files: `gdi/object.c` (handles, stock objects, Create*/GetObject/DeleteObject/IsGDIObject; ExtendedGdiHeap's object table), `heap.c` (GDI's own heaps, 11.7), `dc.c` (DCs, attributes, mapping modes, clipping, visible region, DC hook, bounds, SaveDC/RestoreDC, GetDCState/SetDCState), `rgn.c` (y-x banded regions, CombineRgn by band sweep; no fixed limits), `bitmap.c` (PBITMAPs including over 64K, DIBs), `blt.c`, `font.c` (font table, AddFontResource, mapper, 2.x→3.x), `text.c`, `draw.c` (lines and shapes), `driver.c`, `misc.c` (palettes for a non-palette device, Escape, MulDiv), `i4.asm` (32-bit arithmetic helpers), `entry.asm` (LibMain, WEP, `__GP`, the stubs' entry). `tools/mkgdiexp.py` generates the export table: every 3.1 ordinal; implemented ones are checked against Wine's parameter sizes, the rest are stubs that pop the right number of bytes, record the ordinal and return 0.
- Composite clipping: the visible region (screen coordinates) ∩ the clip region (device coordinates + DC origin), recomputed when either changes; output is cut into one driver call per rectangle (ExtTextOut and FastBorder get the clip box when the region is one rectangle).
- UNVERIFIED (to be checked by probes): the font mapper's weight and italic penalties (the rest: 11.10); the stock LOGFONTs of SYSTEM_FONT/SYSTEM_FIXED_FONT/DEVICE_DEFAULT_FONT and the default palette's colours; IntersectVisRect/ExcludeVisRect coordinates; the meaning of GetBoundsRect's 8000h; when 3.1 calls the DC hook; StretchBlt's sampling (nearest pixel here); MM_ISOTROPIC and LP→DP rounding; GetDIBits with a zero bit count; GdiTaskTermination (does nothing); bmlpPDevice of bitmaps made by CreateBitmap; the `__GP` table layout; DIB_PAL_COLORS; brush realization caching (w16 realizes per DC, 3.1 seems to cache per object and origin: more RealizeObject calls, the same pixels).

### 11.6 Not done yet (plan)

1. Shapes still off (see GD3 above): 3.1's ellipse stepping (the fitted inequality misses the 60 x 30 ellipse's end rows), arcs/pies/chords and round rectangles (compare their polylines in GD1 logs), wide pens (3.1's geometry for wide lines, joins and ends), PolyPolygon as one fill, FloodFill/ExtFloodFill (ScanLR), LineDDA; then more GD3 cases (every pen style and ROP2, mapping modes, clipping by complex regions).
2. Wide and styled pens, hatched and pattern brushes on monochrome memory DCs, the colour→mono BitBlt background rule; StretchBlt (3.1's own stretching); elliptic, polygon and round-rectangle regions.
3. Fonts (the raster mapper, simulations and integer scaling are done: 11.9, 11.10): vector fonts (MODERN/ROMAN/SCRIPT through Output) in the mapper and drawn, EnumFonts/EnumFontFamilies/EnumObjects, then TrueType (.FOT/.TTF rasterizer, GetOutlineTextMetrics, GetGlyphOutline): Write, Clock and anything using Arial or Times need it.
4. Metafiles are done (11.8; what is left is listed there). Printing (Escape, StartDoc/EndDoc, banding, the spooler functions 240-254, printer drivers through the same DDI; the reference install has no printer driver, so one must be installed in a scratch copy), the DM*/Brute engine (201-220) that printer drivers call, palettes on palette devices.
5. More applets in `tests/gdiscenarios.sh`; then w16's own PROGMAN, and the USER rewrite, on w16's GDI.

### 11.7 GDI's heaps and object limits (probe GD4)

**GD4** (`tests/apps/gd4.c`; `tests/gdiheap.sh` runs it as the shell with w16's GDI, `-r` with Microsoft's, `-x` with `[w16] ExtendedGdiHeap=1`; `tools/gd4sum.py NAME=LOG ...` puts logs side by side, `--curve KIND` the percentages): through TOOLHELP (SystemHeapInfo's hGDISegment, GlobalFirst/Next for GDI-owned blocks with a local heap, LocalFirst/Next for block sizes, flags and types), GetHeapSpaces and GFSR_GDIRESOURCES, what each kind of object adds to GDI's DGROUP and to GDI's other heaps, the object's own block, how many fit before GDI refuses, the GDI percentage every 32 objects, and that it comes back when they are deleted. Observation only; counts depend on what ran before (Standard mode, Microsoft's KERNEL and USER in every run).

Measured on 3.1 (block sizes as TOOLHELP gives them; every object block moveable, flags 4, TOOLHELP's wType the signature's type): pen and brush 1Ah (a pattern brush too: GetObject gives back the program's bitmap as lbHatch); font 2Ah with no face or "MS Sans Serif", 3Eh with a 31-character face (the LOGFONT up to the face's end, at least 0Eh bytes of face); rectangle region 2Ah, a 40x40 elliptic one 122h, about 6 bytes per further rectangle in a band and 8 per band; bitmap 2Ah whatever its size (the bits are global); palette 1Ah with 16 entries or 256 (the entries are not in DGROUP); a DC CEh plus a 2Ah region (F8h); selecting and drawing with a new pen adds 12h, a brush 46h, a font 16h; SelectClipRgn 4Ch; SaveDC D0h. GDI has one more heap of its own, 880h bytes with 668h in use (Schulman ch. 8: GDI's atom table), which no object touches. Limits from a fresh boot: 1717 pens, 1489 solid brushes, 1001 fonts, 1094 rectangle regions, 1710 palettes, 183 memory DCs, 185 display DCs, 1328 bitmaps, SaveDC 253 deep, 182 CS_OWNDC windows; one region of more than 384 rectangles (bands of 128) or of more than 128 one-rectangle bands is refused although DGROUP has room. At start GFSR(1) is 88%, DGROUP has 6358 bytes in use. Every GFSR(1) value fits US14's rule (10.4) for GDI's DGROUP.

**w16's GDI** (all tiers share one code path; `gdi/gdi.h`, `gdi/heap.c`, `gdi/object.c`):
- *Objects* are reached only through far pointers from `obj_get(handle, signature)` (the DC through `dc_get`, a region's rectangles through `rgn_ptr`); nothing assumes where an object is. The layouts are Schulman's (ch. 8): the 0Ah header; PENOBJ header + LOGPEN (14h); BRUSHOBJ header + LOGBRUSH + crHatchBk (16h; w16 keeps a pattern brush's copy of its 8 x 8 bits, a global block, in crHatchBk's place; lbHatch stays the program's bitmap as on 3.1; UnrealizeObject marks the brush's realizations stale in every DC instead of counting generations); FONTOBJ header + LOGFONT cut after the face as 3.1's (GetObject still gives 32h bytes, zeros after the face); BITMAPOBJ hmemBitmap, bSelected, hdc at 0Ah/0Ch/0Eh, then w16's (1Ah); PALOBJ header, palVersion, palNumEntries, then the XP of the entries (12h); the DC exactly CEh with every field Schulman names for 3.1 retail at its offset (hrgnClip 0Eh, hLPen 12h, hLBrush 14h, hLFont 16h, hBitmap 18h, dchPal 1Ah, hLDevice 1Ch (w16's DEVICE: the driver's entry points, as Schulman describes it), hRaoClip 1Eh, lpPDevice 2Ah, the DRAWMODE at 44h, cursor, window and viewport origins and extents 64h-77h, map mode 7Ch, ROP and fill modes 80h-85h, planes/bits 86h, text align 8Ch, mapper flags 8Eh, brush origin 92h, save level 9Ch, hVisRgn A0h, DC origin A2h, bounds B4h, hook C4h, hook data C8h); w16's own state is in the fields whose use is unknown (`gdi.h` marks each). The DC list for DeleteObject's check is linked through the DCs (hPDevice's place), not separate blocks. Before: a DC was C2h of w16's own layout, a font 3Ch whatever its face, a pattern brush a brush plus a bitmap object, palette entries inside the object.
- *GDI's own heaps* (`heap.c`, Tier 2): local heaps in moveable GMEM_SHARE global segments of GDI's, made as USER's menu heaps are (LocalInit at 12h), each growing to at most FFF0h, more made as they fill, an empty one other than the first freed; blocks fixed, addressed as an XP (heap number : offset), a far pointer made by `xptr()` from the heap's selector, which is taken again after every allocation in it (`entry.asm`'s `lh_*` switch DS for KERNEL's Local*). Moved out of DGROUP: region rectangles (the region object is the header and an XP, 0Eh), realized pens and brushes (the driver's blocks, sizes as RealizeObject asks: the interface is unchanged), the font table, SaveDC's states (SAVEDDC: the DC's fields and a copy of the clip region's rectangles; GetDCState's copy stays a DC object, USER holds its handle), palette entries, temporary buffers (point lists, widths, region sweeps), and the temporary regions the clip and visible-region operations used (`rgn_op_rect`). The DEVICE blocks (GDIINFO, entry points) stay in DGROUP, as 3.1's PDEVICE and logical-device blocks do. Regions are combined by one sweep over both regions' bands (linear; before, quadratic, and a fixed 2000-rectangle scratch limit made CombineRgn fail at 135 rectangles); one region holds about 8150 rectangles (one block; GD4 built with `-dGD4_STAIRS_ONLY -dGD4_STAIRS_MS=900000L` grew one to 8000 one-rectangle bands, the probe's limit, adding one band at a time).
- *GetFreeSystemResources* keeps 3.1's rule over the heaps 3.1 has: GDI's percentage is DGROUP's (USER's GFSR asks GetHeapSpaces(GDI), which measures DGROUP); GDI's own heaps are not counted. The choice: the percentage measures what limits the number of objects, and an object needs DGROUP room only; the own heaps grow by segments while global memory lasts. ToolHelp's walk of DGROUP shows every object's block with its type, as on 3.1.
- *ExtendedGdiHeap* (Tier 3; `[w16] ExtendedGdiHeap=1` in SYSTEM.INI, read once at GDI's start-up, off by default; the same scheme as USER's `[w16] ExtendedUserHeap`): each object's DGROUP block is a 4-byte fixed stub, its address the handle: an index into GDI's object table (pages of XPs in GDI's heaps), then the signature at offset 2 where TOOLHELP and IsGDIObject read it; the object itself, header included, is in GDI's heaps, its header's wMetaList naming the stub. A handle is valid when its block is there, its table entry is in use and the body's signature and wMetaList agree with the stub (not by the block's size: KERNEL may give a 4-byte request a bigger free block whole, which first made some objects invalid at random; the winfile and dragoff boot-test cases caught it). An object then costs DGROUP its stub and KERNEL's bookkeeping, about 10 bytes, so about 5100 objects of any kind fit (DCs about 1700); what is left of the limits is DGROUP's 64K for the stubs, and USER's own heaps (CS_OWNDC windows stop at 723 with Microsoft's USER). **It can break programs that read GDI's internal structures through a handle** (a DC's fields, the LOGPEN after a pen's header, the header's hNext or sequence count), and handles are no longer moveable-block handles (3.1's are 4 apart, `h & 3 == 2`). With it off GDI behaves exactly as described above. Every boot-test case that runs w16's GDI passes with it on (a run with the default changed to 1); `tests/gdiscenarios.sh` takes `GDISCEN_GDI` to run its scenarios with another GDI.EXE.

GD4's numbers (`tools/gd4sum.py`; Standard mode, Microsoft's KERNEL and USER):

| | 3.1 | w16 before (master with the DeleteDC fix below) | w16 Tier 1+2 (default) | w16 ExtendedGdiHeap=1 |
|---|---|---|---|---|
| GFSR(1) at start, % | 88 | 82 | 91 | 98 |
| DGROUP bytes in use at start | 6358 | 9824 | 4442 | 1016 |
| a pen: DGROUP bytes (+ own heaps) | 26 | 22 | 22 | 8 (+20) |
| a solid brush | 26 | 26 | 22 | 8 (+24) |
| a pattern brush | 26 | 56 | 22 | 8 (+24) |
| a font ("MS Sans Serif") | 42 | 62 | 42 | 8 (+44) |
| a rectangle region | 42 | 30 | 14 (+32) | 8 (+36) |
| a 40x40 elliptic region | 290 | 230 | 14 (+260) | 8 (+276) |
| a bitmap | 42 | 30 | 26 | 8 (+28) |
| a palette of 256 | 26 | 1038 | 18 (+1024) | 8 (+1044) |
| a DC | 248 | 318 | 234 (+116) | 24 (+344) |
| a SaveDC | 208 | 220 | 0 (+232) | 0 (+232) |
| most pens | 1717 | 1637 | 1761 | 5039 |
| most solid brushes | 1489 | 1439 | 1797 | 5131 |
| most fonts | 1001 | 651 | 1047 | 5131 |
| most rectangle regions | 1094 | 1290 | 2372 | 5131 |
| most palettes (16 entries) | 1710 | 551 | 1992 | 5131 |
| most memory DCs | 183 | 133 | 188 | 1710 |
| most display DCs | 185 | 133 | 188 | 1710 |
| most bitmaps (8x8) | 1328 | 1290 | 1469 | 5131 |
| most SaveDC depth | 253 | 225 | 16000 (the probe's limit) | 16000 (the probe's limit) |
| most rectangles in one region (bands of 128) | 384 | 135 | 8156 | 8156 |
| most one-rectangle bands in one region | 128 | 23 | 2816 (a minute's worth) | 2304 (a minute's worth) |
| most CS_OWNDC windows | 182 | 133 | 188 | 723 |

The GDI percentage after n pens (3.1 / before / Tier 1+2 / Extended): 288: 73 / 68 / 77 / 91; 800: 47 / 42 / 50 / 81; 1312: 21 / 15 / 24 / 72; 1568: 6 / 3 / 9 / 67; 2080: - / - / - / 57; 3104: - / - / - / 37; 4128: - / - / - / 16; 4896: - / - / - / 1.

Bugs found on the way: DeleteDC never freed SaveDC's states nor SaveVisRgn's copies (they hung off hNext; an unused list was freed instead): 225 SaveDCs and a DeleteDC left GDI's heap full for good; a DC whose regions could not be made was made anyway (a NULL visible region later); CombineRgn's scratch-size arithmetic overflowed 16 bits.

### 11.8 Metafiles (probe GD9; `gdi/meta.c`)

Sources: the SDK (vol. 1 2.15: which calls a metafile records, the handle table; vol. 2: each function; vol. 4 ch. 3: METAHEADER, the record layout, the function numbers, the placeable header), Schulman ch. 8 (the metafile DC is GDI object type 11, signature 'QO'; a metafile is a global block, not a GDI object; IsValidMetaFile checks the header), and **GD9** (`tests/apps/gd9.c`, run as the shell with `tests/gdiheap.sh -p build/dos/GD9.EXE [-r]`; boot-test case `gdimeta`; table `gd9ref.h` from 3.1's log by `tools/obsref.py`): every recordable call alone in a memory metafile, its words dumped (`OBS name@offset`), its header, each record's function and size and a checksum of the whole compared with 3.1's; objects and the object table; disk files byte for byte; playback into memory bitmaps against the same calls made directly; EnumMetaFile record by record; errors; the clipboard. With w16's GDI every checked value is 3.1's.

Observed on 3.1 and done so:
- **The metafile DC**: IsGDIObject 11. GetDeviceCaps answers DT_METAFILE (5) for TECHNOLOGY and 0 for every other index; the other queries fail (GetTextMetrics 0 and nothing written, GetTextExtent, GetMapMode, GetBkColor, GetPixel, GetNearestColor, GetClipBox, LPtoDP... 0); CreateCompatibleDC/Bitmap on it fail. Every recording call answers 1 (SetBkColor, SetMapMode, MoveTo, SaveDC: 1, not the old value); FloodFill and ExtFloodFill record and answer 0; SelectObject answers the previous object of the kind, starting from BLACK_PEN, WHITE_BRUSH and DEVICE_DEFAULT_FONT (USER's FillRect works through that); a region selected answers 1; SelectPalette answers 1. Not recorded: SetBrushOrg (0), FrameRgn (0), SelectClipRgn(NULL) (0), StretchBlt with no source (0), a BitBlt whose source is not a memory DC (0), SelectObject of a bitmap (0, but it takes a slot: mtNoObjects counts it). DeleteDC of a metafile DC fails; CloseMetaFile of anything else 0.
- **Records** (SDK numbers): parameters in reverse order, a DWORD low word first; FloodFill is META_EXTFLOODFILL with mode 0; PatBlt the raster operation then h, w, y, x; TextOut count, string (zero-padded to a word), y, x; ExtTextOut y, x, count, options, the rectangle only when options are nonzero (3.1 reads one at NULL when options are given without a rectangle), string, widths if given; Escape (any, QUERYESCSUPPORT too, answering 1) number, count, data; BitBlt and StretchBlt are always the DIB records (0940h, 0B41h) with the whole source bitmap as a DIB (1 bit black-and-white for a monochrome bitmap, else 4 bits with the VGA table, biSizeImage set, the rest 0); BitBlt without a source is 0940h with a 0 for the source DC and no bitmap; SetDIBitsToDevice and StretchDIBits copy the BITMAPINFO and the scan lines (DIB_PAL_COLORS: a table of words). USER's DrawText on a metafile DC comes out as TextOut.
- **Objects**: the first use of an object writes its creation record into the lowest free slot (CreatePenIndirect, CreateBrushIndirect; a pattern brush is 0142h with BS_PATTERN and its bitmap as a DIB, a DIB pattern brush 0142h with BS_DIBPATTERN, the usage and the packed DIB's whole block, GlobalSize bytes; CreateFontIndirect the LOGFONT with the face's zero and at least one byte more, rounded to a word, as the font object holds them (3.1's font object keeps the program's bytes after the face; w16's does too); CreatePalette; CreateRegion as 3.1's region object: 0, type 6, the object's sequence count, a size, the scan count, the most x values in a scan, the box, then the scans); stock objects get creation records too. DeleteObject of an object in an open metafile writes META_DELETEOBJECT and frees the slot, unless the object is selected there (then no record, the slot stays taken; DeleteObject succeeds). FillRgn, InvertRgn and PaintRgn put the region and brush in the table and record their slots; SelectClipRgn is META_SELECTOBJECT of the region.
- **The handle and the file**: CloseMetaFile returns a global block owned by GDI (GMEM_MOVEABLE, no other flag) holding the header (mtType 1, 9 words, version 300h, mtNoObjects the slots used, mtMaxRecord in words) and the records; its GlobalSize is the metafile's bytes plus 18 (rounded by KERNEL). GetMetaFileBits returns the same handle, SetMetaFileBits takes it back unchanged (owner too), SetMetaFileBitsBetter makes it GDI's. A disk metafile's file is written at CloseMetaFile (empty until then; made at CreateMetaFile) with mtType 1; its handle is a C0h-byte block of GDI's: the header with mtType 2, the file's size at 14h (GetMetaFile), and KERNEL's OFSTRUCT of the file at 18h. CopyMetaFile: memory to memory and file to file the same words; file to memory 3 words more (in mtSize; 3.1's are whatever was in memory, w16's zeros); memory to file the words up to the end record with mtSize 3 more. IsValidMetaFile: mtType 1 or 2, header 9, version 100h or 300h.
- **Playback**: under the destination's mapping; SaveDC/RestoreDC records act on the DC as they stand (a SaveDC left open stays, a RestoreDC(-1) pops the program's level); at the end the objects made are deleted, the DC's pen and brush (w16: font and palette too) are selected again and its clip region is restored, every other attribute keeps what the metafile set. A record with function 0 ends the play; unknown functions are passed over (UNVERIFIED: 3.1 did not return from a metafile with an unknown function in GD9). A disk handle whose file cannot be read plays nothing and answers 1. EnumMetaFile gives the callback a copy of each record (not the metafile's memory), the table of mtNoObjects handles, and deletes the objects at the end, also when the callback stops it (returns 0). PlayMetaFileRecord puts a new object in the lowest free slot (3.1 hangs when there is none; w16 deletes it) and deletes on META_DELETEOBJECT even a selected object. A metafile played into a metafile DC records its calls there.
- **The clipboard** (USER): SetClipboardData(CF_METAFILEPICT) makes both the METAFILEPICT block and its metafile USER's; EmptyClipboard frees both. w16's USER does the same.
- **Palettes** (GD9's palette cases, every one checked): a palette keeps a list of metafile DCs (Schulman's wMetaList): SelectPalette into a metafile DC adds the DC (once however often it is selected there), selecting another palette there takes the DC off the previous palette's list, and CloseMetaFile takes nothing off. AnimatePalette and ResizePalette write their record (META_ANIMATEPALETTE, META_RESIZEPALETTE) once for every entry into the DC the entry names, also for a palette not selected (or not even created) in that metafile: entries left by closed metafiles name the next metafile DC, which gets the same handle, so one change came out as five identical records after five selections in earlier metafiles. SetPaletteEntries records nothing. AnimatePalette's record holds the start and count, then, where the entries should be, as many words of its own arguments as lie on the stack (the entries' offset and selector, the count, the start, the palette). w16 keeps the same list (pairs in `gdi/meta.c`; an entry counts while its DC is an open metafile DC, which with handles reused as on 3.1 gives the same records) and writes the same words (past the five argument words zeros, where 3.1 has the rest of its stack). A palette grown by ResizePalette has new entries holding whatever 3.1's memory held (w16: zeros), so GD9 only shrinks palettes.

Differences that remain: the bytes 3.1 leaves undefined (padding, the region's sequence count, stock fonts' bytes after the face) are not compared. Records over 64K are not written (UNVERIFIED on 3.1). Placeable (Aldus) files: GDI does not read the 22-byte header (SDK: programs strip it and use SetMetaFileBits); 3.1's GetMetaFile does not check the header either, so a placeable file gives a handle (the file's first 18 bytes, mtType 2) that IsValidMetaFile refuses and PlayMetaFile fails (0); SetMetaFileBits of a placeable block fails (0); w16 does the same. 3.1 did not come back from CreateMetaFile with a directory that is not there or GetMetaFile of a missing file (one of the two; GD9 leaves them out): w16 returns 0.

### 11.9 Raster font simulations (probe GD10)

**GD10** (`tests/apps/gd10.c`, run as the shell with `tests/gdiheap.sh -p build/dos/GD10.EXE [-r] [-v build/dos/VGA.DRV]`; boot-test case `gdifont`; table `gd10ref.h` from 3.1's log by `tools/obsref.py`) asks System, MS Sans Serif (8, 10, 12, 14, 18, 24 point), MS Serif, Courier, Terminal and Small Fonts for every mixture of bold, italic, underline and strike-out, logs GetTextMetrics, GetTextExtent, GetCharWidth and GetTextFace, and draws the text into a 4-plane memory bitmap (TextOut transparent and opaque, ExtTextOut with ETO_OPAQUE | ETO_CLIPPED, with the caller's widths, TA_RIGHT; one case each with a blue text colour, R2_XORPEN and TA_UPDATECP), read back with GetDIBits, printed (ROW lines) and hashed. Run with `tests/gdiboot.sh -r -l -L` it gives GD1's log of the driver calls behind each picture. With w16's GDI, Microsoft's or w16's VGA.DRV, every checked value and pixel is 3.1's.

Who does what: VGA's dpText is 2204h (TC_CP_STROKE, TC_EA_DOUBLE, TC_RA_ABLE): the driver emboldens (drivers/vga/README.md, VG6), GDI does italic, underline and strike-out itself. Observed on 3.1 and done so (`gdi/font.c` font_xform, `gdi/text.c`):

- **TEXTXFORM**: txfAccelerator has 200h for emboldening (GD11: from a weight 200 above the font's, 600 for a 400 font, 900 for System's 700, scaled or not), 400h italic, 800h underline, 1000h strike-out, each when the font lacks it; txfItalic 1, txfUnderline and txfStrikeOut FFh; txfOverhang the emboldening's 1 plus the italic overhang, (dfPixHeight - 1) / 2.
- **Metrics**: tmItalic 1, tmUnderlined and tmStruckOut FFh (not 1); tmOverhang = txfOverhang; tmWeight the font's + 300 (700; System asked for 900: 1000, though 3.1's VGA.DRV then draws and measures it plain) and tmAveCharWidth, tmMaxCharWidth one more when emboldened (italic leaves the widths). GetTextExtent and GetCharWidth add the italic overhang to the driver's values (the emboldening's is in the driver's).
- **Italic**: after one extent call, the text goes to the driver's ExtTextOut once for each band of two rows from the top of the cell (an odd height's last band is one row): band k at x + overhang - k, its clip rectangle the band's rows (the first band's from the clip's top, the last's to its bottom) with the same options and opaque rectangle every time, so an opaque background is sheared with the glyphs and the opaque rectangle filled whole. TA_RIGHT and TA_CENTER, the current position and the bounds take the extent with the overhang.
- **Underline and strike-out**: after the text, underline first, each line t = height / 12 rows (at least 1), one 2-point OS_POLYLINE per row from x to x + the driver's extent (with the caller's widths, the driver's extent with those widths; without the italic overhang), the end point not drawn, clip rectangle the row's box; pen PS_SOLID width 0 in the text colour, realized for the call; R2_COPYPEN whatever the DC's ROP2 (GD10's R2_XORPEN case). The underline's top row is ascent + 1 (height - t when it would not fit); the strike-out's (2 ascent + internal leading) / 3 - t / 2, a third of the way from the baseline up to the internal leading. With italic a line starts (height - its top row) / 2 to the right. ETO_CLIPPED cuts the lines' points to the rectangle.
- **Integer scaling**: see 11.10; GD10's scaled cases (System 32, MS Sans Serif -44 and 48, Courier -26 and 39, Terminal 24, MS Serif -27, Small Fonts -11 and 40, and widths: System 16 x 14, MS Sans Serif -26 x 20, Terminal 48 x 4, each plain, italic underlined struck out, and bold; and two characters of System 128 and of Terminal 96 italic, copies over 64K) check the pixels, extents, widths and the metrics scaling changes; the italic overhang and the lines' thickness and places follow the scaled dfPixHeight and dfAscent, the emboldening stays one pixel.

### 11.10 The font mapper and integer scaling (probes GD11, GD12)

FN1 (6.9) compares w16's font *files* under Microsoft's GDI; it says nothing about w16's mapper. **GD11** (`tests/apps/gd11.c`; `tests/gdiheap.sh -p build/dos/GD11.EXE [-r] [-v build/dos/VGA.DRV]`) asks Windows 3.1 for every height -130 to 130 of System, Fixedsys, Terminal, MS Sans Serif, MS Serif, Courier, Small Fonts and Symbol, widths 1 to 255 at six heights, weight, italic, underline, strike-out, pitch, family and character set at heights that need scaling, weights 0 to 1000, no face by pitch and family, and heights to 1000; each line has GetTextFace, every TEXTMETRIC field, two GetCharWidth values and an extent (5138 requests; full widths and extents of nine scaled fonts). 3.1's log is `tests/gdi/gd11ref.txt`; `tools/gd11.py cmp` compares a run with it, leaving out the results that are TrueType or vector fonts (1156 lines: w16 maps neither yet); `tools/gd11.py scale LOG` names each result's real size and factors. With `C:\GDT.FON` present, GD11 asks test fonts of our own instead (`tools/gdtfont.py`, specs `tests/gdi/gdt-heights.txt`, `gdt-widths.txt`): pairs of sizes with chosen metrics, each face in a character set of its own, every height -70 to 70 and widths 1 to 60 at six heights; most of the rules below were pinned down that way. **GD12** (`tests/apps/gd12.c`, the shell under `tests/gdiboot.sh -l`) draws a few scaled fonts for GD1's log of what GDI hands the driver (GD1 now also dumps the first character-table entries of a 3.x font). EnumFonts lists only the real sizes.

**Who scales.** GDI: VGA.DRV has no TC_SA_ bits. For a size a font lacks, GDI builds a scaled copy in a global block of its own and hands the driver that, a 3.x font: dfPoints, dfVertRes, dfAscent, both leadings and dfPixHeight times the vertical factor y; dfHorizRes, dfPixWidth, dfAvgWidth, dfMaxWidth and dfWidthBytes times the horizontal factor x; dfDevice 0; the character table at 94h with every width times x and 32-bit offsets; the face name right after the table, the bits after the name at an even offset (System x 2: dfFace 5DAh, dfBitsOffset 5E2h); each character as many 8-pixel columns as the unscaled one's times x, each column dfPixHeight rows, every source pixel an x by y block. dfFlags and the fields after it hold whatever the block held (3.1 leaves them undefined; w16 keeps the font's). A copy can be over 64K (Terminal 8 down by 5 across: 122K; System the same: over 128K) and the offsets then cross segments (w16's VGA.DRV takes the table's offsets as 32-bit, selectors __AHINCR apart; GD10 draws both). The TEXTXFORM is the scaled font's (txfHeight dfPixHeight, txfWidth dfAvgWidth); TEXTMETRIC, GetCharWidth and GetTextExtent come from the scaled font as from a real one, the simulations of 11.9 on top. w16 (`gdi/font.c` font_scale, font_scaled) makes the header and character table when a DC realizes the size, the bits only when it is first drawn (measuring needs none: Clock sizes its digits by asking a font 1000 pixels high, which made the boot-test case treek386 miss its screens while 800K copies were built), and keeps the 4 most recently used copies no DC has; 3.1's caching is not observed (no program can see it).

**tmDigitizedAspectX is dfVertRes and tmDigitizedAspectY dfHorizRes** (ROMAN.FON's 2 and 3 show as 2 and 3; a font scaled 2 across, dfHorizRes 192, shows 96 and 192). w16 had them the other way round, which no font with equal resolutions shows.

**The mapper's rules for raster fonts** (all observed on 3.1; w16's `penalty` and `font_map`):

- Every font of the table is one candidate, at the scale its own size gives; the lowest penalty wins, the first in the table on a tie (test fonts in either order). The table's order is the order the fonts were added (FONTS.FON, the fixedfon.fon file, OEMFONTS.FON, then WIN.INI's [fonts]), each file's fonts in its directory's order; w16 added each at the front before.
- **The height factor y**: a positive lfHeight is compared with the cell height dfPixHeight, a negative one with the character height dfPixHeight - dfInternalLeading; lfHeight 0 is a 12-point character height (16 pixels: MS Sans Serif's 20-pixel font). y is the requested height over the font's plus a quarter, rounded down, 1 to 8: a factor goes up three quarters of the way to the next multiple (Terminal 12: 20 gives 12, 21 gives 24; Fixedsys 15: 26 gives 15, 27 gives 30), one rule for every font and both signs (1782 of 1782 GD11 requests). No penalty weighs it; the font's penalties are reckoned at that factor.
- **No candidate**: a font smaller than asked whose y would be more than its own (compared) height less 3. Small Fonts' 3-pixel font is never scaled and not even used for 4; its 6-pixel one goes to x 3 for cell heights, x 2 for character heights. A face of only such fonts loses to another face (a test face of one 5-pixel font gives Arial at 14).
- **The width factor x**: with lfWidth 0, y (5 at most: System 128 is 8 down, 5 across, tmAveCharWidth 35); with lfWidth, lfWidth / dfAvgWidth to the nearest whole number, 1 to 5, a half up except 1.5, which stays 1 (AW 8: 12 gives 1, 13 gives 2, 20 gives 3, 28 gives 4, 36 gives 5).
- **Penalties**, added up: the scaled height smaller than asked 150 a pixel (1 more for an unscaled font: two fonts equally far off, the bigger one wins only unscaled), bigger 600 + 150 a pixel; with lfWidth, 50 a pixel of the difference between x dfAvgWidth and lfWidth; x above 1 60 + 15 x; y of 2 to 5 11 + 20 y, 6 600, 7 1100, 8 1600; x and y unequal 410 x the larger / the smaller (integer division) - 10. The scaling terms are a fit: these numbers give every request on 3.1's fonts and all but one of GD11's 12525 on test fonts (`tests/gdi/gdt-*.txt`; the one left: 13- and 16-pixel fonts asked 32 high and 10 wide, where 3.1 takes the 13 scaled 2 across and 2 down and w16 the 16 scaled 1 across and 2 down); the constraints they meet are exact in places (x 2 by y 1 costs 900 in all, x 4 by y 1 1750, x 5 by y 2 1201), the form is not observed. Character set 65000 (not for DEFAULT_CHARSET), face 10000, family 9000 (also with a face: System asked as FF_ROMAN at 39 gives Times New Roman), a fixed pitch asked of a variable font 15000, a variable pitch of a fixed one 350.
- **"System" asked with FIXED_PITCH** is the fixed system font's face (Fixedsys, also where Courier's size is nearer: System 32 fixed gives Fixedsys 30).
- **Emboldening** starts 200 above the font's weight (11.9).
- **Stock fonts keep their device size in any mapping mode** (GD8: the default System font at 2:1 is System 16, not 16 x 2; GD5: in MM_LOMETRIC too); a created font is asked at its size on the device (GD5, 11.4). Their LOGFONT's width is not asked for either (FN1: ANSI_VAR_FONT, Helv 12 x 9, is MS Sans Serif's 13-pixel font, average width 5; the boot-test cases combo, lbclip and lbdraw show it). UNVERIFIED for the stock fonts other than SYSTEM_FONT and ANSI_VAR_FONT. (Nearest-size mapping hid both: there is no bigger System, and no width scaling.)
- Examples: System 32 is System 16 x 2; MS Sans Serif -44 its 13-pixel font x 4; height 48 its 24-pixel font x 2 (16 x 3 equally near costs more); -65 its 16-pixel font x 5 rather than 37 x 2 (one short); Courier 39 13 x 3; MS Serif -27 its 11-pixel font (SMALLE.FON) x 3; Small Fonts -11 its 6-pixel font x 2; MS Sans Serif -130 its 37-pixel font x 4 rather than anything taller than 5 across.

UNVERIFIED (not probed, kept from before): the weight (3 a hundred) and italic (4) penalties, and how family and pitch weigh without a face; the form of the scaling terms (above). TrueType and vector fonts are not mapped to (vector fonts only last, at 20000 more); where 3.1 picks one, w16 picks a raster font.

**Results.** With w16's GDI, Microsoft's or w16's VGA.DRV, GD11 gives 3.1's every mapping and metric (3982 requests compared), also with w16's own .FON files (6.9) in place of Microsoft's, under either GDI; GD10's scaled cases give 3.1's pixels; the boot-test case `gdifont` runs both with each mix of GDIs and drivers. Also found and fixed on the way: AddFontResource of a file that is not there returns 0 without KERNEL's "Cannot find" box (w16 showed it).

## 12. WIN386.EXE: 386 Enhanced mode

WIN386.EXE is the 386 Enhanced mode loader, the Virtual Machine Manager (VMM) and the built-in virtual device drivers (VxDs). w16's goal is a drop-in replacement: copied over Microsoft's in a scratch copy of the reference install, `WIN /3` boots 386 Enhanced mode with Microsoft's KRNL386, USER, GDI and drivers to a Program Manager pixel-identical to the reference. Clean room as in §3: WIN386.EXE, the VxDs and KRNL386 are never disassembled; what is known comes from the published file formats (MZ, W3, LE, the DDB), the Windows 3.1 DDK documentation (`.tools/docs/ddag31`: INT 2Fh 1600h-1609h, 1680h-1686h, Win386_Startup_Info_Struc), DPMI 0.9, XMS, the Intel 386 manuals, Ralf Brown's Interrupt List release 61 (VxD service numbers, freely distributed documentation) and black-box observation with the VX probes.

### 12.1 The recreation map

docs/win386-vxds.md (format facts, IDs, init order, APIs) and docs/win386-services.md (generated by `tools/vxdmap.sh`: per-VxD tables and the service-usage matrix) list what is to be recreated: the VMM ("WIN386", device 0001h, 242 services) and 28 built-in VxDs in Windows 3.1, 30 in WfW 3.11, plus the external .386 files. The data comes from w16's extension of the user's readexe (`tools/readexe`, ISC licence kept, the change also as a patch in `tools/readexe/patches` for upstreaming, never pushed): LE header, objects, names, entries, fixups, the DDB through the `*_DDB` export, and the dword device/service IDs after each INT 20h (the DDK's dynamic-link convention), nothing else of the code.

### 12.2 The loader contract (observed)

Observed with `tests/vxobs.sh` (scenarios in scratch copies of the reference 3.1 install, headless DOSBox-X): VX1 (`tests/dos/vx1.asm`) as KRNL386.EXE logs what the program in the System VM gets, in V86 mode and after the DPMI switch; WC3 (the WIN.COM INT 2Fh spy, §9.5) between WIN.COM and WIN386 records every INT 2Fh call made through the real-mode/V86 chain with registers in and out; VX2 puts keys in the BIOS buffer for "press a key" prompts; DOSBox-X's file and exec log (`VXOBS_LOG="files exec"`) gives the file accesses. Raw logs: `tests/vxobs.sh -o DIR`.

**Start.** WIN.COM EXECs WIN386.EXE with WIN's tail, switches blanked (§9.5), and WIN.COM's environment plus `windir=`. WIN386 also runs without WIN.COM (`SYSTEM\WIN386` from C:\WINDOWS: Windows starts; KRNL386 then gets no `windir=`, and INT 2Fh 160Ah goes unanswered, because 160Ah is WIN.COM's, §9.5). WIN386 has no `/?`: `WIN386 /?` starts Windows and KRNL386 gets the tail ` /?`. KRNL386's tail is WIN386's tail (`WIN /3` gives three blanks), its environment is WIN386's, unchanged, its parent PSP is WIN386's (memory block owner `WIN386`, 349h paragraphs with 2 or 16 MB of DOSBox-X memory when started by WIN.COM, 3ECh when run directly; UNVERIFIED what sets the size).

**Order of events** (direct start, file log and WC3):
1. Opens `QEMM386$` (read) and `386MAX$$` (read/write): device names of memory managers, both absent here.
2. INT 2Fh AX=1605h from real mode: ES:BX=0000:0000, DS:SI=0000:0000, CX=0, DX=0 (bit 0 clear: Enhanced), DI=030Ah. Preceded only by INT 2Fh 1600h. Per the DDK, CX nonzero back means a TSR refused and Windows ends (after 1606h) without a message of its own.
3. INT 2Fh 4300h/4310h (XMS), opens `SMARTAAR` (SMARTDrive), reopens its own file by full path (`C:\WINDOWS\SYSTEM\WIN386.EXE`) to read the W3 modules, opens `$DebugDD` and calls INT 68h (debugger checks), reads SYSTEM.INI from its own directory first (`C:\WINDOWS\SYSTEM\SYSTEM.INI`), then the parent (`C:\WINDOWS\SYSTEM.INI`).
4. Real-mode initialisation of the VxDs (their 16-bit objects, §12.1): INT 5Ch (NetBIOS), INT 10h 6F00h (Video 7) and 12h/BL=6, INT 2Fh 160Bh (TSR identification, from WIN386's loaded code), INT 2Fh 1302h (DOS's disk interrupt, from the same code), INT 2Fh 1607h BX=0021h (PageFile call-out); external VxDs are opened from SYSTEM by the name in SYSTEM.INI, lower case (`C:\WINDOWS\SYSTEM\vtdapi.386`).
5. Protected mode. Device call-outs (INT 2Fh 1607h issued in the System VM from V86 code at FE4E:1637): VMD (000Ch, CX=1), DOSMGR (0015h, CX=0, 4, 1, 3, 5), PageFile (0021h), V86MMGR (0006h), VMD (CX=0); INT 2Fh 1000h (SHARE check). The swap file: SPART.PAR is searched in SYSTEM, WINDOWS and each PATH directory, then the temporary swap file `C:\WINDOWS\WIN386.SWP` is created (deleted at exit). WINSTART.BAT is searched the same way (twice). INT 2Fh 1608h (init complete) with BX=0006h CX=000Eh.
6. KRNL386.EXE (WIN386's directory) is opened, then EXECed in the System VM.
7. Exit (KRNL386 ends): INT 2Fh 1609h (begin exit), DOSMGR call-out CX=2, WIN386.SWP deleted, back in real mode INT 2Fh 4310h then 1606h with DX=0, and WIN386 exits with KRNL386's exit code (§9.5: WIN.COM passes it on).

**Errors** (messages to standard output, exact; exit code FFh; the screen is left as it was):
- No XMS driver: `Cannot run Windows in 386 enhanced mode with the currently installed` CR LF `extended memory driver.` CR LF.
- No SYSTEM.INI: `Cannot find the system initialization file needed to run Windows.` CR LF CR LF `You need to run the Setup program again.` CR LF.
- A `device=` that is not found (a file or a `*name` missing from the W3 table): `Cannot find a device file that may be needed to run Windows in` CR LF `386 enhanced mode.` CR LF CR LF `You need to run the Setup program again.` CR LF `<the name as written, e.g. nosuch.386 or nosuch>` CR LF `Press a key to continue` CR LF; then Windows goes on without it.
- A dynamic link to a device that is not loaded (here: the test replaced `device=*vpicd`): `Invalid VxD dynamic link call to device number 0003, service 0001.` CR LF `Your Windows configuration is invalid. Run the Windows Setup program` CR LF `again to correct this problem.` CR LF (number and service in hex, 4 digits).
- No KRNL386.EXE: `Cannot find or load required file KRNL386.EXE. The file was not found.` CR LF.
- An 80286 (`cputype=286`, WIN386 run directly): DOSBox-X hangs (WIN.COM refuses /3 first, §9.5). UNVERIFIED what real hardware does.
- With 2 MB of memory Windows still starts (WIN.COM picks standard mode there, §9.5, but WIN386 itself does not refuse).

**The System VM as KRNL386 sees it** (VX1; Windows 3.1, DOSBox-X 16 MB):
- V86 mode (MSW FFFBh), flags 3287h (IOPL 3). XMS is there (version 0200h, driver 0205h; free 0 KB: WIN386 took it all), A20 on. Free DOS memory 75B2h paragraphs (with 16 MB).
- INT 2Fh 1600h: AX=0A03h (3.10). 160Ah: WIN.COM's answer (AX=0, BX=030Ah, CX=3). 1683h: BX=1 (the System VM), AL=0. 1680h (release time slice): AL=0. 1689h: AL=0.
- INT 2Fh 1684h (V86 API entries): only VPICD (0003h) FEFA:0B77, VTD (0005h) FEFB:0B67, VMD (000Ch) FEFC:0B57, DOSMGR (0015h) FEF9:0B87 - the devices whose DDB declares a V86 API (docs/win386-vxds.md). All four, and the DPMI entry, are the same linear address FFB17h, with a different segment each: the address identifies the entry point, as a "V86 breakpoint".
- INT 2Fh 1687h: AX=0, BX=1 (32-bit programs supported), CL=3 (386), DX=005Ah (DPMI 0.90), SI=0027h paragraphs of host data, ES:DI=FE53:15E7.
- After the switch as a 16-bit client: CS=0097h (base = the PSP's, limit FFFFh, code ring 3), DS=SS=008Fh, ES=009Fh (the PSP, limit FFh), the environment selector 00AFh in the PSP (16 MB; 00A7h with less memory). INT 31h 0400h: AX=005Ah, BX=0005h (32-bit, virtual memory), CL=3, DX=0870h (the virtual PIC bases 08h/70h). 0500h: largest block 03959000h with 16 MB, paging (swap) pages 39FCh. INT 2Fh in protected mode: 1600h AX=0A03h, 160Ah not answered, 1683h BX=1, 1686h AX=0 (protected mode), 168Ah "MS-DOS" answered (vendor API, AX=0, ES:DI back). 1684h in protected mode: VPICD, VTD, Reboot (0009h), VDD (000Ah), VMD, VKD (000Dh), VCD (000Eh), SHELL (0017h), PageFile (0021h), VTDAPI (0442h): exactly the devices whose DDB declares a PM API, all at selector 003Bh, offsets 0360h, 0362h, ... in device order (2 bytes apart: a table of breakpoints).

### 12.3 w16's WIN386.EXE (stage 1)

`make win386` builds `build/win386/WIN386.EXE` in Windows 3.1's container layout (MZ program, `e_lfanew` -> a W3 table, LE modules after it; `tools/mkw3.py` puts it together, readexe reads it like Microsoft's):
- **The loader** (`win386/ldr`: Open Watcom C, small model, and nasm) is the MZ program. It does what 12.2 lists, with Windows 3.1's messages: XMS check, SYSTEM.INI in its own directory then the parent, KRNL386.EXE present, the 1605h broadcast (CX nonzero: 1606h and exit; a V86-mode callback in DS:SI is refused with an UNVERIFIED message, untestable in DOSBox-X), then takes all of XMS (the largest block, then the blocks that continue it physically; locked, A20 on; up to 1 GB: "More than 64 MB" below), loads the "WIN386" module of its own W3 table into it with its LE fixups applied (32-bit offset and self-relative internal fixups), builds the page tables and enters the VMM with paging on. The VMM comes back to it in V86 mode as the System VM, where it EXECs KRNL386.EXE with its own tail, FCBs and environment (as observed); an ARPL breakpoint then takes it back to real mode, it releases XMS, issues 1606h and exits with KERNEL's code. It also holds, in its data segment, the breakpoints (ARPL bytes for V86, HLT bytes for protected mode), the V86 stack used for interrupts reflected from protected mode, and a 16 KB transfer buffer for DOS calls.
- **The VMM** (`win386/vmm`: wcc386 flat + nasm, linked by wlink as an LE VxD whose DDB is Windows 3.1's VMM DDB: "VMM", ID 0001h, 3.10, init order 0; no services in its table yet). Memory: the whole XMS block is a window at linear 80000000h (page allocator, kmalloc; at most 1 GB, `W386_WINDOW_MAX`: the VMs' high linear windows start at C0000000h); the first megabyte and the HMA are identity mapped for V86 mode; the client's memory from 4 MB up. GDT, IDT (DPL 0 gates for exceptions and the hardware interrupts at 50h-5Fh, so an INT n there from V86 mode or ring 3 raises #GP and is told apart; DPL 3 elsewhere), TSS with every port allowed, the 8259s moved to 50h/58h and put back at exit. The V86 monitor reflects interrupts through the IVT, answers INT 2Fh 1600h/1680h/1681h/1682h/1683h/1684h/1687h/1689h with the observed values, and traps the breakpoints. `exec_v86` runs client code for the VMM synchronously (setjmp/longjmp onto a lower part of the ring-0 stack), so nested calls, interrupts during them and protected-mode handlers for V86 interrupts all work.
- **The DPMI 0.9 host** (`dpmi.c`) for the System VM's program: the 1687h mode switch with Windows 3.1's selectors (DS 8Fh, CS 97h, PSP 9Fh, environment A7h; SS shares DS when equal), INT 31h 0000h-000Ch, 0004h/0005h (Windows' lock/unlock selector: nothing to do), 0100h-0102h, 0200h-0205h, 0300h-0302h, 0400h (Windows 3.1's values), 0500h-0503h, 0600h-0604h, 0702h/0703h, 0800h, 0900h-0902h; protected-mode interrupts go to the client's handlers or are reflected to V86 mode (the hardware interrupts' vectors 08h-0Fh and 70h-77h, also when a client's handler chains to the host's, without DS/ES translation and with the interrupted registers kept); hardware interrupts and INT 1Ch/23h/24h arriving in V86 mode go to the client's protected-mode handler if it has one; such handlers run on a 4 KB host stack as Windows 3.1's do (VX4), a nested interrupt's frame below everything the running handlers pushed; exceptions go to the client's 0203h handlers, on the host stack too (DPMI 0.9's locked stack; KRNL386's ExitCall raises exception 6 to finish a task on it and frees the task's own stack there, Pietrek ch. 3). INT 15h AX=C207h from protected mode (MOUSE.DRV's PS/2 pointing device handler) gives the BIOS a V86 breakpoint, which calls the client's handler on the host stack with the BIOS's four words (status, X, Y, 0). INT 21h AH=00h ends the client like 4Ch with code 0 (KRNL386's ExitKernel issues AX=0001h), with the client's own PSP made current first (KERNEL leaves a task's PDB current). **A task's exit** is the same call with the task's PDB current: KRNL386 ends every task with INT 21h AH=00h, AL the exit code (a task's own 4Ch goes to KERNEL's ExitCall), so that DOS closes its handles. Observed with VX9 (`tests/vx9run.sh`: a resident recorder before WIN; VX9A as the shell starts TK51C, which only exits) under Windows 3.1: DOS gets 4Ch with AL kept (4C2Ah), the PDB's parent field (16h), which protected mode sees as KRNL386's PSP *selector*, is the PSP's segment by then, and its terminate address (0Ah), which protected mode sees inside KRNL386's real-mode code, is the VMM's return breakpoint of the reflected INT 21h, so DOS's terminate makes KRNL386's PSP current and comes back to KERNEL, which goes on (INT 21h 50h follows). w16 does the same (dpmi.c task_exit: both fields set for the call and put back after it; the caller gets its own registers back). Before, every closed application ended Windows. The MS-DOS vendor API (INT 2Fh 168Ah, entry AX=0100h: a writable LDT alias), and the LDT ownership KRNL386 assumes (Pietrek ch. 1-2, observed to be needed: KRNL386 claims every all-zero descriptor below the alias' limit; a descriptor is free here iff it is all zero; the alias' limit grows 4 KB at a time as the host allocates). INT 41h (debugger) does nothing.
- **DOS from protected mode** (`dosx.c`): INT 21h with protected-mode pointers through the transfer buffer (path calls, 3Fh/40h in pieces, 44h buffers, 47h, 4Eh/4Fh with the program's own DTA from 1Ah/2Fh, 56h, 5Ah, 60h, 6Ch, 38h), 25h/35h set/get the protected-mode vectors, 50h/51h/62h with PSP selectors; INT 10h palette calls (1002h/1009h/1012h/1017h). Other calls go to V86 DOS with DS/ES translated where they map conventional memory.
- **Trace**: the VMM writes to COM1 (DOSBox-X `serial1=file`); SYSTEM.INI `[w16] vmmtrace=1` adds every INT 31h, reflected interrupt, INT 2Fh 16xxh and nested call.

**Result** (`tests/win386.sh`, and the `win386` case of `make boot-test`): with w16's WIN386.EXE over Microsoft's, `WIN /3 /B` boots Microsoft's KRNL386, drivers, GDI, USER and Program Manager to a screen pixel-identical to Windows 3.1's own WIN386 (0 pixels), and BOOTLOG.TXT is the same 96 lines through `INITDONE=TSRQuery`. The stage-1 stall before Final USER init was not the VxD APIs: KERNEL creates each task's PDB with INT 21h 55h passing the PDB's *selector* in DX (DOSX translates it the same way, dosx/int21.inc); passed through untranslated, DOS built the PSP at a wrong segment, and from PROGMAN.INI on every open failed with error 4 (too many open files), found by comparing DOSBox-X's file logs of the two boots and the open/result trace. **Reliability** (2026-11-13): on installation-05, 20 of 20 `tests/win386.sh -n` boots reach Program Manager pixel-identical to Windows 3.1's own WIN386, and 10 of 10 runs of `tests/scripts/input/win386input.txt` (mouse motion, the Help menu by keys, File > Exit Windows back to the DOS prompt) match Microsoft's WIN386 shot for shot; `win386` and `win386input` are in the default boot-test cases. The earlier intermittent failures (about half the runs: a #GP with error code 3200h in a protected-mode hardware interrupt handler, or a boot stopping after USER was loaded) were a nested interrupt's frame put on the host stack over what the running handler had pushed; a rarer one (about 1 boot in 7 with 20 at once on the host, hanging while fonts loaded) was the same on the V86 reflection stack, found with the VMM's heartbeat (every 20 s of ticks in the trace: where the VM is, its code bytes, stack and registers). With both fixed and the VxD loader in (VTDAPI.386 loaded), 40 of 40 boots, 20 at a time, were pixel-identical to Windows 3.1's own WIN386. Three more bugs kept the mouse and exit from working in a live session: IRQ 8-15 (70h-77h) chained to the host's default handler were reflected as software interrupts (DS/ES translated, "INT 74 ... not in conventional memory" for every mouse interrupt, and the V86 handler's registers copied into the interrupted code, the source of a "bad CS" panic seen after INT 2Fh 4003h); MOUSE.DRV's INT 15h C207h handler was given to the BIOS as a protected-mode address; and KRNL386's INT 21h AX=0001h exit went to V86 DOS as AH=00h. In DOSBox-X xdotool's pointer motion does not reach the guest (as for tests/md2cprobe.sh), so the input runs use DOSBox (tests/inputrun.sh), which has no serial file for the VMM's trace; the trace needs DOSBox-X (`DROPIN_CONF`, `-p 386`). VX1 under w16's WIN386 matches Microsoft's log except for the absent V86 API entries, the XMS driver version (Microsoft's V86MMGR presents XMS 2.00 to the VM; w16 passes the real driver through), MSW (Microsoft sets MP and TS: lazy FPU switching), and the resident size.

**More than 64 MB** (boot-test `bigmem`; §9.6 has 3.1's numbers: its WIN386 used all of 128 MB, DPMI 0500h's largest block 258,964 KB with the paging file). The loader (`win386/ldr/ldr.c`, `xms_call32` in `ldrasm.asm`) takes the largest block with XMS 3.0's 88h/89h when the driver is 3.0 or later, else with 08h/09h, then more blocks (88h/89h, then 08h/09h) as long as each starts where the window ends (or in the same 4 KB page: DOSBox-X gives whole pages, so 65,535 KB from 09h ends 1 KB before the next block), since the VMM's window is one physical run mapped at 80000000h. The window is at most 1 GB (`W386_WINDOW_MAX`): above it, from C0000000h, are the VMs' high linear windows (`vm.c` HIGH_LIN). The page tables for it (one per 4 MB, `pt_count`) are in the window's first pages; the client's linear memory runs from 4 MB to 80000000h. [w16] LargeMemory=0 uses 08h/09h only; with DOSBox-X's driver the blocks follow one another, so that too gives all of it, as 3.1. DPMI (`dpmi.c`): 0501h checks the page tables a block needs and the client's linear space as well as the free pages, and gives back what it took when it fails part-way (it used to keep the pages and leave next_lin where it was); 0500h's largest block is what 0501h can give (the free pages less a page table per 4 MB). Results with w16's WIN386 and KRNL386: 0500h's largest 127,648 KB at 128 MB and 258,464 KB at 256 MB, physical pages 129,600 and 260,672 KB, 127,744 and 258,496 KB allocated and written; with LargeMemory=0 at 128 MB the same as without.

**The interrupt flag** (VX6, `tests/dos/vx6.asm` as KRNL386, `tests/vxobs.sh vif`; Windows 3.1): V86 code runs with IOPL 3 (flags 3287h) and its CLI clears the real IF (PUSHF 3083h, no ticks). Protected-mode code runs with IOPL 0 (flags 0246h) and its IF is the VM's: after CLI PUSHF shows 0046h and no ticks come; POPF and IRET of an image with IF=1 do not enable interrupts again, nor does POPF with IF=0 disable them (the processor's rule at CPL > IOPL); an INT 21h made with interrupts off returns with them off; INT 31h 0900h-0902h give and set the same flag (0900h: AL=1 before, PUSHF 0046h after). A hardware interrupt's protected-mode handler runs with IF=0 and a frame showing 0246h (IOPL 0); a software interrupt's (INT 60h set with 0205h) runs with the caller's flags, IF included, in the handler and in its frame. w16 does the same since 2026-11-14: the client runs with IOPL 0, CLI and STI raise #GP and the VMM sets the client's IF (`pm_cli_sti` in vmm.c), every flags image the host builds for protected mode has IOPL 0, and a software interrupt to a client handler keeps IF; VX6's IF results are then equal to Windows 3.1's (the arithmetic flags some INT 31h calls return differ: UNVERIFIED which 3.1 sets). Port I/O from protected mode now goes through the TSS's I/O bitmap, as V86 code's always did.

**I/O trapping** (`win386/vmm/io.c`; VXI.386, `tests/vxd/vxp.asm` built with IOPROBE, and VX7, `tests/vxobs.sh io`). A device installs a handler for a port (VMM services Install_IO_Handler 96h: ESI the procedure, EDX the port, carry if the port has one already, as observed; Install_Mult_IO_Handlers 95h: EDI a table of a word count, then a word port and a dword procedure each, carry and EDX the port on failure; Enable/Disable_Global/Local_Trapping 97h-9Ah; Simulate_IO 94h); the port's bit in the I/O bitmap makes IN, OUT, INS and OUTS to it from V86 mode or a protected-mode client raise #GP, and the VMM decodes the instruction (prefixes 66h, 67h, REP, segment overrides) and calls the handler with EBX the VM, ECX the type, EDX the port, EAX the data, EBP the client registers. Windows 3.1's types (VXI): Byte_Input 0, Byte_Output 4, Word_Input 8, Word_Output 0Ch, Dword_Input 10h, Dword_Output 14h; INS/OUTS add String_IO 20h, REP adds Rep_IO 40h, and the high word holds the string's segment or selector (ES for INS, DS for OUTS); Client_ECX is the count and Client_EIP is already past the instruction; IN AL,imm8 is the same as IN AL,DX. Simulate_IO given a string calls the port's handler once per element with the plain type, after setting Client_ECX to 0, and moves DI or SI on. w16's types, handler registers, Client_EIP and Simulate_IO's string behaviour are the same (VX7's results and VXI's calls equal Windows 3.1's but for the program's segment). UNVERIFIED: Addr_32_IO (80h in w16) and Reverse_IO (100h), and Simulate_IO with a plain word or dword (w16: byte by byte through each byte port's handler or the hardware) or byte (the hardware). VPICD installs handlers for 20h, 21h, A0h and A1h at Sys_Critical_Init that, for now, pass the VM's accesses to the 8259s unchanged (the next step makes them the VM's virtual PIC); the heartbeat counts trapped accesses.

### 12.4 What is missing, and the plan

Known differences and gaps, roughly in the order they block the boot:
1. **VxD PM/V86 APIs** (12.5): entries, function 0 and the boot's calls (VMD 0100h, VDD 000Ch, Reboot 0201h/0202h) are done; whatever DOS boxes, MMSYSTEM and Control Panel call, Ctrl+Alt+Del (Reboot with VKD) and the V86 entries are next.
2. **The VxD loader and the VMM services** (first part done, 12.7). Load the LE VxDs (built-in from the W3 table and external `.386` by name) in init order, resolve their INT 20h dynamic links against service tables (docs/win386-services.md lists the 199 VMM services used and by whom), send Sys_Critical_Init/Device_Init/Init_Complete, honour the 1605h startup-info chain (TSR-loaded VxDs and instance data), give the "Cannot find a device file" and "Invalid VxD dynamic link" messages. Two routes stay open (docs/win386-vxds.md): Microsoft's VxDs on w16's VMM (all 199 services) or w16's own VxDs (the services they need, every number present for third-party VxDs).
3. **Interrupt virtualisation**: IOPL 0 for clients and virtual IF (Windows 3.1's flags), VPICD-style virtual PIC, the VTD timer, VKD keyboard: needed before a second VM exists.
4. **Memory**: demand paging and the swap file (WIN386.SWP / SPART.PAR as observed), XMS and EMS emulation in VMs (V86MMGR), DOS memory above 64 KB per selector, the 0303h real-mode callbacks, 0500h with Windows' numbers.
5. **More VMs**: DOS boxes (WINOA386.MOD, VDD windowed/full screen), the scheduler, DOSMGR instancing; then WfW 3.11's external VxDs (IFSMGR, VCACHE, ...) and third-party `.386`s.
6. **The loader**: GEMMIS/the V86 callback for EMM386-like managers, the device checks it does in real mode (12.2 step 4), WIN386.EXE's resident size (Windows 3.1: 349h paragraphs; w16's is about 6B0h).

w16's KRNL386.EXE (§9.10) runs in 386 Enhanced mode on w16's WIN386 and on Microsoft's (boot-test k386e: pixel-identical to Microsoft's KRNL386 on the same WIN386), with WF_ENHANCED, the Reboot device's 0201h and KRNL386's heap; what it does not do yet is in docs/notes/krnl386-handover.md.

ZOI: the VMM's limits are the architecture's: 8192 LDT descriptors (64 KB), the XMS block (64 MB with XMS 2.0), a 64 KB ring-0 stack bounding the nesting depth of nested calls; nothing else is fixed. UNVERIFIED items are marked in the text above and in the code.

### 12.5 The VxD API entries (INT 2Fh 1684h)

`vxdapi.c` hands out protected-mode entries for exactly Windows 3.1's set (VPICD, VTD, Reboot, VDD, VMD, VKD, VCD, SHELL, PageFile, VTDAPI; ARCHITECTURE 12.2) as HLT breakpoints in the host's ring-3 segment. VX3 (`tests/apps/vx3.c`, run as the shell with `tests/refrun.sh VX3.EXE /3`) recorded function 0 of each under Windows 3.1: AX=030Ah for VPICD, VTD, VDD, VCD, SHELL (BX=1000h); VMD 0300h, VKD 0200h, PageFile 010Ah, VTDAPI 0001h; Reboot has none (carry set). w16 returns these. During the boot to Program Manager only these come: VPICD 0 (version), VMD AX=0100h (MOUSE.DRV: BX=7404h CX=0071h DX=0500h), VDD AX=000Ch (VGA.DRV: BX=9B0Ah CX=3101h DX=0 SI=01F4h DI=0106h), Reboot AX=0201h (KERNEL). VX5 (`tests/apps/vx5.c`, `tests/refrun.sh VX5.EXE /3`) replays them with the same registers under Windows 3.1: VMD 0100h fails, carry set and every register unchanged, also when it is the first call (w16's MOUSE.DRV in place of Microsoft's); VDD 000Ch returns AX = BX, carry clear, the rest unchanged; Reboot 0202h returns in ES:DI exactly what KERNEL's 0201h set (0117h:A5ADh). Ralf Brown's Interrupt List 61 (INT 2Fh 1684h BX=0009h) documents Reboot's 0100h (warm boot), 0201h (set KERNEL's Ctrl+Alt+Del handler ES:DI and its sanity-check byte DS:SI), 0202h (get it), 0203h (the "application not responding" box), 0204h (set/reset the PM INT 01h handler). w16 answers VMD 0100h, VDD 000Ch and Reboot 0201h/0202h as observed; other functions are traced and fail with carry set. VX5 also showed that the entries' offsets (003Bh:0360h + 2n) are handed out in the order devices are first asked for, not in device order (they differ between a boot with Microsoft's MOUSE.DRV and one with w16's); w16 uses device order (UNVERIFIED that anything depends on it). No V86 API entries yet (Windows 3.1 has VPICD, VTD, VMD, DOSMGR).

### 12.6 The VxDs w16 needs

A complete replacement for WIN386.EXE provides the VMM and every VxD that the original bundles, under the same W3 module names and device IDs. SYSTEM.INI loads them by their W3 module names (`*vddvga`, `*wshell`); third-party `.386` files and programs reach them by device ID (VxD service calls, INT 2Fh AX=1684h) and through the services they export. The sources are `docs/win386-vxds.md` (the W3 and LE tables, read with readexe) and `docs/win386-services.md` (who calls which service).

**Windows 3.1's WIN386.EXE: the VMM plus 28 built-in VxDs**

| Group | VxDs (device ID) | Role |
|---|---|---|
| Core | VMM (0001h), VPICD (0003h), VTD (0005h), V86MMGR (0006h), DOSMGR (0015h), VDMAD (0004h) | VMs, interrupts, timer, V86 memory, DOS, DMA |
| Devices | VDD (000Ah, W3 module VDDVGA), VKD (000Dh), VMD (000Ch), VCD (000Eh), VFD (001Bh), EBIOS (0012h), PARITY (0008h), Reboot (0009h), BIOSXLAT (0013h) | display, keyboard, mouse, COM ports, floppy, BIOS |
| Disk and paging | BLOCKDEV (0010h), INT13 (0020h), WDCTRL (no ID), PageSwap (0007h), PageFile (0021h) | FastDisk, the swap file |
| Shell and DOS sessions | SHELL (0017h, W3 module WSHELL), VMPOLL (0018h), VMCPD (0011h) | DOS windows, idle detection, the coprocessor |
| Network and other | VNETBIOS (0014h), DOSNET (001Ah), COMBUFF, CDPSCSI, QEMMFix (no IDs) | NetBIOS, network redirector support, workarounds |

**Loaded by default from a file:** VTDAPI.386 (0442h, the timer API for MMSYSTEM).

**Windows for Workgroups 3.11 adds:**
- **Built in:** VPD (000Fh, printer) and VWC (no ID).
- **External `.386` files:**
  - IFSMGR (0484h), VFAT (0486h), VCACHE (048Bh), IOS (0010h; it takes BLOCKDEV's ID): 32-bit file and disk access;
  - VSHARE (0483h), VXDLDR (0027h), VPMTD (003Ah);
  - VCOMM (002Bh), SERIAL and LPT.
- **In a networked install** (not in our reference copy; a networked WfW 3.11 reference is in progress, not merged yet): the redirector, server, NDIS and protocol VxDs.

**The boot set.** The VMM itself calls into VPICD, VTD, V86MMGR, PageSwap, VMCPD, VNETBIOS, DOSMGR and SHELL. The VxDs that do work during Sys_Critical_Init, Device_Init and Init_Complete add VDD, VKD, BLOCKDEV, VDMAD and PageFile. Of the VMM's 242 services, 199 are called by some module.

**Two routes:**
1. **Our VMM loads Microsoft's VxDs.** It must then provide all 199 called services with exactly the documented behaviour.
2. **w16 also replaces the VxDs.** The service set narrows to what our own VxDs use, but every service number must still exist for third-party `.386` files.

Each piece's route is recorded here as it is built.

**Where w16 stands** (2026-11-13):
- **Built:** a VMM with paging, a V86 monitor, the observed INT 2Fh 16xxh answers and a 32-bit DPMI host.
- **API entries** for the ten VxDs Windows 3.1 exposes to programs (§12.5).
- **Answered as Windows 3.1 does** (VX5, §12.5): the VMD, VDD and Reboot API calls of the boot.
- **The VxD loader** (§12.7): SYSTEM.INI's devices loaded, the control messages in init order, INT 20h dynamic links; Microsoft's VTDAPI.386 loads and answers its API. w16's built-in devices are part of the VMM module so far (VTD's services, the API entries of §12.5).
- **w16's own VxD modules** (§12.8): VPICD, VMD, VKD, REBOOT so far.
- **Not yet:** most of the 242 VMM services, the other built-in devices as W3 modules, devices that run code in the VM at init (Simulate_Int, nested execution). The DDK's Virtual Device Adaptation Guide (`.tools/docs/vdag31`, from the same help-file archive as `ddag31`) documents the control messages, the services and the client register structure.

### 12.7 The VxD loader

**What Windows 3.1 gives a device** (VXP.386, `tests/vxd/vxp.asm`: a probe VxD of w16's own, test only, loaded with `device=vxp.386`; `tests/vxpprobe.sh ms|w16` runs a whole session, boot and Exit Windows, and prints what VXP wrote to COM1). The control messages arrive in this order, with EBX the System VM's handle (805E1000h) throughout: Sys_Critical_Init (EAX=0), Device_Init (1), Init_Complete (2), Sys_VM_Init (3), each with ESI pointing at WIN386's command tail (a length byte, then the text: `    /B` for `WIN /3 /B`, the switches blanked as KRNL386 gets them); Begin_PM_App (15h) when KRNL386 enters protected mode (EDX=0 for its 16-bit client, EDI an application control block); End_PM_App (16h, the same EDI) when it ends; then Sys_VM_Terminate (4), System_Exit (5), Sys_Critical_Exit (6). Services: Get_VMM_Version AX=030Ah, ECX=0; Get_Cur_VM_Handle and Get_Sys_VM_Handle the same handle; the control block starts with the status (0), the high linear address of the VM's memory (84000000h), the client register structure's address and the VM ID (1); VTD_Get_Version AX=030Ah, EBX=1, ECX=36h (periods 1 to 54 ms), VTD_Get_Interrupt_Period 36h. Sources for the meanings: the DDK's Virtual Device Adaptation Guide (`.tools/docs/vdag31`).

**w16's loader** (`win386/ldr/ldr.c`): after the VMM it loads every device that `[386Enh]` names in `device=`, `mouse=`, `display=`, `keyboard=`, `network=` (a comma-separated list) and `ebios=`, each once, in the file's order. `*name` is a module of WIN386.EXE's W3 table; w16's built-in devices are part of its VMM module so far, so Windows 3.1's 28 built-in names are accepted as provided. Anything else is a file in WIN386's directory (an MZ stub and an LE module; the fixups applied as for the VMM, with source lists). A name not found (or not a VxD: UNVERIFIED what Windows 3.1 says then) gives 12.2's "Cannot find a device file" message and waits for a key. The DDBs (each module's entry 1) go to the VMM as a list after the images.

**w16's VMM** (`win386/vmm/vxd.c`): the devices (the VMM's own DDB, w16's built-in VTD, the loaded ones) sorted by init order, ties in load order (UNVERIFIED tie-break); the messages as above, the exit ones in reverse init order (UNVERIFIED for several devices); a device that fails an init message is dropped. INT 20h in ring-0 code traps to the VMM, which rewrites the six bytes into CALL (or, with bit 15 of the service number, JMP) DWORD PTR [slot] and runs them: the slot is the device's service-table entry (a loaded device's from its DDB; the VMM's and w16's own devices' are stubs into `svc_dispatch` with the registers, flags included). A link to a device that is not loaded is skipped as if the call had returned, with a note in the trace: VXQ.386 (VXP built with BADLINK) linking to VPD (000Fh, not in Windows 3.1) or to an ID no device has (7FFFh) at Sys_Critical_Init went on under Windows 3.1 without a message, and Windows booted (UNVERIFIED: the registers 3.1 leaves, later calls). Windows 3.1's "Invalid VxD dynamic link" message (12.2) came from the VMM's own link to a missing VPICD; w16 keeps it for a VMM service number past 242 (UNVERIFIED that 3.1 refuses those so): before the System VM has run the VMM goes back to the loader (`fatal()`, pm_go returns W386_FATAL), which prints it with "w16" for "Windows" and exits. Services so far: VMM Get_VMM_Version, Get/Test_Cur_VM_Handle, Get/Test_Sys_VM_Handle, Validate_VM_Handle, the list services (List_Create to List_Remove_First); VTD Get_Version, Get_Interrupt_Period, Begin/End_Min_Int_Period (noted; the timer is not reprogrammed yet). Others stop with a "not implemented yet" panic naming the caller. INT 2Fh 1684h gives a loaded device with a PM API procedure an entry that calls it with EBX the VM handle and EBP its client registers (the DDK's Client_Reg_Struc).

**Result:** VXP.386 sees the same messages, registers and service results under w16's WIN386 as under Microsoft's (handles and addresses aside); Microsoft's VTDAPI.386 (the default `device=vtdapi.386`) loads and initialises (it uses VTD_Get_Version and the list services), and its own API answers VX3's version call with AX=0001h as under Windows 3.1.

**Next:** the rest of the VMM services by use (docs/win386-services.md), w16's VxDs as W3 modules of their own (`*vpicd`, `*vtd`, ...), the messages Windows 3.1 sends that VXP did not see in a plain session (Create_VM and the rest for DOS boxes), and the dynamic-link failure shown as Windows 3.1 shows it.

### 12.8 w16's own VxD modules; VPICD

**Modules.** w16's built-in devices become LE modules of their own in WIN386.EXE's W3 table, under Windows 3.1's module names, loaded by the `*name` SYSTEM.INI gives (12.7) like any other device; the first is VPICD. They are written in C (`win386/vxd/*.c`, wcc386 flat) with a small nasm part per module (`*ddb.asm`: the DDB with Windows 3.1's name, ID, version and init order, read from its file with readexe; the service table) and the shared run-time `win386/vxd/vxdrt.asm` and `win386/include/vxdrt.inc`: ENTRY (a control, API or callback procedure handing the registers to a C function as a `struct vregs`, flags included), SERVICES (the DDB's service table into one C dispatcher), VXDCALL (a dynamic link callable from C) and `vxd_call_proc` (calling another device's procedure with a register block). `win386/include/w16vxd.h` holds what they share with the VMM: `struct vregs`, the DDK's Client_Reg_Struc, VxD_Desc_Block and VPICD_IRQ_Descriptor. The loader's list of built-in names that need no module (12.7) shrinks as modules come.

**What Windows 3.1's VPICD answers** (VXP, at its Sys_Critical_Init unless said): VPICD_Get_Version AX=030Ah, EBX=1, ECX=0Fh; VPICD_Get_IRQ_Complete_Status (service 0Bh) carry set for IRQ 0 (VTD's; ECX=40h) and IRQ 1 (VKD's; ECX=0), clear for IRQ 5 (ECX=4: it is masked) and for IRQ 12 at init (ECX=0) but set at System_Exit (VMD takes IRQ 12 later, when the mouse driver registers); VPICD_Convert_IRQ_To_Int (0Dh) in the System VM 1 -> 9, 12 -> 74h, 16 carry set; VPICD_Convert_Int_To_IRQ (0Eh) carry set for 74h and 21h. Its PM API function 0 gives AX=030Ah (12.5). Its DDB: "VPICD", 0003h, 3.10, init order 0C000000h, 21 services, V86 and PM API procedures.

**w16's VPICD** (`win386/vxd/vpicd.c`, module VPICD): it owns the 8259s, moving their vectors to 50h/58h at Sys_Critical_Init (the VMM's IDT gates) and back at Sys_Critical_Exit, and every hardware interrupt the VMM takes goes to it through a private link (`struct vpicd_link`, found through the DDB's reference data; the VMM no longer reflects interrupts itself). An IRQ a device has virtualised goes to its VID_Hw_Int_Proc (EAX the IRQ handle, EBX the VM); one nobody has, and a request a device sets with VPICD_Set_Int_Request, is simulated into the System VM through the VMM (the paths of 12.3: the client's protected-mode handler on the host stack, or reflection through the IVT) where the VM was interrupted, or, if the VM has interrupts off, when it next takes them. The simulation goes through the System VM's virtual PIC ("The virtual PIC" below). All 21 services exist; Force_Default_Owner/Behavior and the instance page-swap services have nothing to do with one VM. A SYSTEM.INI without `*vpicd` ends Windows with Windows 3.1's message for exactly that case (device 0003h, service 0001h). VTD (still inside the VMM) takes IRQ 0 with VPICD_Virtualize_IRQ and passes every tick to the System VM with VPICD_Set_Int_Request. With w16's VPICD the VXP results above are the same as Windows 3.1's, and the boot, input and Exit Windows cases are unchanged.

**VMD** (`win386/vxd/vmd.c`, module VMD; DDB as Windows 3.1's: 000Ch, 3.00, init order 34000000h, 3 services, V86 and PM API). It takes IRQ 12 from VPICD at Device_Init (Windows 3.1's VMD had not at Sys_Critical_Init and had by System_Exit; UNVERIFIED which message) and passes every mouse interrupt to the System VM, the owner, where the BIOS and MOUSE.DRV's PS/2 handler (INT 15h C207h, bridged by the DPMI host, 12.3) take it. VMD_Get_Version AX=0300h; VMD_Get_Mouse_Owner 0 until it has the IRQ, then the System VM; VMD_Set_Mouse_Type converts a negative EAX (a vector) with VPICD_Convert_Int_To_IRQ and fails with EAX=1 when that or VPICD_Virtualize_IRQ fails, EAX=0 when it has the mouse already. Its PM API: function 0 AX=0300h, MOUSE.DRV's 0100h fails with every register unchanged. VXP and VX5 see Windows 3.1's results.

**VKD** (`win386/vxd/vkd.c`, module VKD; DDB as Windows 3.1's: 000Dh, 2.00, init order 38000000h, 15 services, a PM API). It takes IRQ 1 from VPICD at Sys_Critical_Init (Windows 3.1's VKD had it then), reads each byte from port 60h, keeps the global shift state (Shift, Ctrl, Alt, either side) and checks the hot keys defined with VKD_Define_Hot_Key (scan code, normal/extended/either, shift mask and compare, press/release callbacks with the DDK's registers): a hot key's press and release go to its callback, VPICD_Phys_EOI ends the interrupt and the VM never sees them; every other byte is passed to the System VM, whose handler (KEYBOARD.DRV's, w16's or Microsoft's, or the BIOS's) reads port 60h again (the controller keeps the byte until its next one; Windows 3.1 traps port 60h, which w16 cannot yet: the virtual PIC paragraph). VKD_Get_Version AX=0200h, VKD_Get_Kbd_Owner the System VM, VKD_Peek_Msg_Key/Get_Msg_Key nothing (no message mode yet), Remove_Hot_Key, the local enable/disable, cancel, paste mode and flush services accepted; Reflect_Hot_Key, Force_Keys and Start_Paste fail (they need port 60h trapped). The flag and shift-state values are w16's own (UNVERIFIED against Windows 3.1's). PM API function 0 AX=0200h. VXP and VX3 see Windows 3.1's results; the input runs pass with Microsoft's and with w16's KEYBOARD.DRV and MOUSE.DRV. **Port 60h and 64h** (2026-11-14): VKD traps both (Install_IO_Handler) and gives the VM a controller of its own: each byte it takes in its IRQ 1 Hw_Int_Proc (ended at once with VPICD_Phys_EOI) goes into the System VM's queue unless it is a hot key's; the queue's next byte goes into the VM's output buffer (status bit 0) with IRQ 1 requested while it is full (a read of port 60h clears both; the VM's EOI or a status read brings the next); writes go to the real controller, and a byte of the pointing device (status bit 5) is read from it as it is; a VM polling port 64h with interrupts off gets the controller's replies through the same queue. VKD_Force_Keys (into the queue, the shift state tracked, carry and ECX the rest when the queue is full) and VKD_Reflect_Hot_Key (the hot key's press, then its release as it comes; the DDK's shift-state keystrokes are not made: UNVERIFIED) work; pasting and message mode wait for DOS boxes. REBOOT gives the VM the Ctrl and Alt releases read while its screen was up (VKD_Force_Keys; UNVERIFIED how Windows 3.1 does it): before, the VM's keyboard driver kept them down, and Return in Program Manager did nothing afterwards (win386cad now checks that it opens File Manager).

**REBOOT** (`win386/vxd/reboot.c`, module REBOOT; DDB as Windows 3.1's: "Reboot", 0009h, 2.00, init order 24000000h, no services, a PM API). It defines Ctrl+Alt+Del (Del, normal or extended, with Ctrl and Alt) with VKD_Define_Hot_Key at Device_Init. Windows 3.1 with nothing hung shows a text screen (tests/scripts/input/win386cad.txt): 80x25, white on blue (1Fh), " Windows " centred on row 6 in blue on grey (71h), the explanation from row 8 at column 5, "Press any key to continue" on row 19 at column 27 with the cursor two columns after it; a key returns to Windows exactly as it was; Ctrl+Alt+Del again restarts. w16 shows the same ("w16" for "Windows" in the title and in "Press any key to return to w16"): it saves the VGA's registers, all four 64 KB planes and the DAC, sets text mode with INT 10h in the System VM (the VMM's nested-execution services below), writes the screen, reads keys from the keyboard controller until a new one is pressed (as under Windows 3.1, with the keys held and let go one by one: releases, the repeats of a key still held and Shift/Ctrl/Alt presses do not end the screen, nor do bytes from the pointing device, status bit 5), then sets the BIOS's old mode and restores everything; Ctrl+Alt+Del again pulses the controller's reset line. The PM API: 0201h/0202h as observed (12.5), others fail. In progress (handover note, step 5): Windows 3.1's two other screens, observed with VX8 (`tests/vx8run.sh`): "<MODULE>.EXE ... This Windows application has stopped responding to the system" (Esc back, Enter ends the task; KERNEL's handler, run as an interrupt of the VM when the sanity byte at 0201h's DS:SI stays 0, calls Reboot 0203h with ES:DI the module name) and "WARNING! The system is either busy or has become unstable"; w16's REBOOT has all three screens, 0203h, the VMM's Map_Flat and Build_Int_Stack_Frame and the sanity check (the byte zeroed, KERNEL's procedure run as an interrupt of the VM at its next idle or, after 750 ms, where it is; set again: screen A; a 0203h call: screen B, Enter ends the task; neither after another 750 ms: screen C; the delays UNVERIFIED). Ctrl+Alt+Del while a screen is up is not a new check (the keyboard repeats Del). Afterwards the VGA's index registers (Sequencer, Graphics Controller, CRTC, attribute) and the DAC's mask and write index are put back too: VGA.DRV keeps an index set and writes only the data port, and without them drew every bitmap from memory in black and white. Not yet: 0100h and 0204h. Boot-test cases `win386cad` (the screen equals Windows 3.1's but for the branded text, Windows and File Manager afterwards 0 pixels) and `win386hung` (VX8 hung: screen B, Escape, B, Enter; equal but for the branded text).

**Nested execution** (VMM, `vxd.c`): Begin_Nest_V86_Exec (82h), Begin_Nest_Exec (83h), Exec_Int (84h: the interrupt in EAX with the client registers of EBP's Client_Reg_Struc), Resume_Exec (85h), End_Nest_Exec (86h), Save_Client_State (8Dh), Restore_Client_State (8Eh), from the DDK's descriptions; w16 runs the code synchronously in V86 mode on the reflection stack (12.3), the client's DS and ES taken as V86 segments (UNVERIFIED: nested execution of protected-mode code).

**The virtual PIC** (2026-11-14). The System VM has a PIC of its own in VPICD: IMR, IRR (the devices' requests, level-triggered: a request stays until VPICD_Clear_Int_Request) and ISR, programmed and read through ports 20h, 21h, A0h and A1h, which the VMM traps (12.3) and hands to VPICD: ICW1-ICW4 (the VM's vector bases), OCW1 (the mask; VID_Mask_Change_Proc with ECX 1 for masking, 0 for unmasking), OCW2 (non-specific and specific EOI: the ISR bit cleared, then VID_EOI_Proc), OCW3 (a read gives the IRR or the ISR). A request is simulated, the highest priority first (0, 1, the cascade's 8-15, 3-7), when the VM has interrupts on, has not masked the IRQ and has nothing of equal or higher priority in service; the IRQ goes into the ISR (a slave's also sets the master's bit 2), VID_Virt_Int_Proc is called, and for a device with a VID_IRET_Proc the VMM reports the handler's IRET (a protected-mode handler runs to its return during the delivery; a V86 one gets a second frame whose IRET reaches a V86 breakpoint, W386_BP_IRQIRET) and VPICD sets a VMM global time-out of the descriptor's VID_IRET_Time_Out: whichever comes first calls VID_IRET_Proc (carry set for the time-out), the other is ignored. The VM's IRR reads show requests not in service. On the hardware side, as the DDK says of Windows 3.1's VPICD ("does not prioritize hardware interrupts"), a hardware interrupt is ended on the 8259 at once and its IRQ physically masked until VPICD_Phys_EOI; an IRQ nobody virtualised becomes a request of VPICD's own in the System VM, whose EOI clears it and ends it physically. Physical masks follow the VM's (auto-masking) unless the device used VPICD_Physically_Mask/Unmask; until Sys_VM_Init the 8259s keep DOS's masks and the VM's IMR is clear, from then on the VM has DOS's masks (VXP saw IRQ 5 physically but not virtually masked at Sys_Critical_Init under Windows 3.1; VX6/VX7 read DOS's masks back later). At exit the 8259s get the System VM's masks (UNVERIFIED). Status bits (Get_Complete_Status, observed with VXI): IRET_Pending 1, In_Service 2, Phys_Mask 4, Virt_Mask 10h, Virt_Req 20h, Virt_Dev_Req 80h; Phys_In_Serv 8 and Phys_Req 40h are UNVERIFIED. VXI.386 and VX7 (`tests/vxobs.sh io`: IRQ 10 virtualised by the probe, simulated in V86 and protected mode, once with the handler waiting a second) see exactly Windows 3.1's sequence of procedure calls and statuses and the same IRR/ISR/IMR reads. VTD (IRQ 0), VKD (IRQ 1) and VMD (IRQ 12) use the DDK's pattern: VPICD_Physically_Unmask after virtualising, VPICD_Set_Int_Request in the Hw_Int_Proc, VPICD_Clear_Int_Request (and, VKD and VMD, VPICD_Phys_EOI) in a VID_EOI_Proc; VTD ends each tick physically at once and drives the VMM's time-outs (Set_Global_Time_Out 3Ch, Cancel_Time_Out 3Eh, in 54.925 ms ticks). With VMD's physical unmask, IRQ 12's status at System_Exit is 0 as under Windows 3.1 (it was 4 while the VM's mask writes reached the 8259).

### 12.9 The 386 display grabber: VGA.3GR

WINOA386.MOD (module WINOLDAP) imports GRABBER, the DLL SYSTEM.INI `[boot] 386grabber=` names (VGA.3GR; KRNL386 loads it, §9.10). It shows a windowed DOS box and copies its screen: WINOLDAP calls its 23 functions (the DDK's "Enhanced-mode Grabber Functions" plus GrbGetTextColor and TranslateWinToFScreen, which the DDK lists without pages) with a pointer to its EXTPAINTSTRUC, and the grabber reads the DOS box's screen through the VDD's protected-mode API (INT 2Fh 1684h, device 000Ah: Get_Version, Get_Mem/Free_Mem, Get_State, Get_Mod/Clear_Mod, Unlock_App and the grab copies). Everything was observed black-box on 3.1 with the recorder GR3 (`tests/apps/gr3.asm`: a stand-in GRABBER in front of a renamed scratch copy of the real one, recording every call, the structure's changes and, through a protected-mode INT 2Fh hook, every VDD call; `tests/grabber3.sh -r`, with `-u` also the grabber's GDI/USER/KERNEL calls through USTRACE forwarders); the observed interface, the structure offsets that matter, the VDD's functions, registers and buffers: `docs/notes/grabber-handover.md` "VGA.3GR". The DDK is wrong in two places: UpdateScreen takes (lp, wParam, lParam), and SetPaintFnt's two words receive the full screen in pixels.

w16's is `drivers/grabber/vga3gr/` (C and nasm, `build/dos/VGA.3GR`, installed in `build/w16/SYSTEM`; `drivers/grabber/README.md`). Under Microsoft's WIN386, VDD and WINOA386 it draws the same pixels as 3.1's for windowed text (painting, updates, attributes, caret, fonts, scrolling, Mark/Copy) and the 4-plane and CGA graphics modes, and gives the same clipboard text (`tests/grabber3cmp.sh`, results in the README). What w16's own WIN386 must answer for it: `docs/notes/win386-handover.md` "VDD services the 386 grabber needs".

### 12.10 DOS boxes: VMs, SHELL, the display

**What Windows 3.1 does** (VXS.386, `tests/vxd/vxs.asm`, a probe VxD that hooks the protected-mode APIs of SHELL, VDD, VKD and VMD with Hook_Device_PM_API, VMM 0092h, logs the control messages and, built as VXSH.386, a few VMM services and SHELL's calls into the System VM; and a V86 INT 21h hook in the DOS VMs; `tests/vxsrun.sh NAME SCRIPT`, Windows 3.1 on MS-DOS 4.00 in DOSBox-X). WINOA386 asks SHELL (its API takes the function in DX) for the version (EAX 030Ah, EBX the System VM's handle), gives it its event procedure (function 9: a 48-bit pointer at ES:DI+8, 1547h:0000h; EDX back 007Bh, a flat ring-3 data selector), and creates the VM (function 3: ES:DI a block with three 48-bit pointers at +8, +0Eh, +14h to the program, `C:\COMMAND.COM`, its command tail and the start-up directory `C:`, which SHELL turns into linear addresses in place; the priorities 100 and 50 at +1Eh, +20h; the title `MS-DOS Prompt` at +2Eh; EAX back the VM's handle; Create_VM goes to the devices with EBX the new VM, ESI the block). Function 5 gives a VM the focus: Set_Device_Focus for VKD, VDD and VMD (ESI 0F20h, EDI the caller's EDI) and the execution focus; the new VM gets VM_Resume, VM_Critical_Init and VM_Init and runs. Its first DOS calls (in V86 mode): INT 21h 0C06h, 0Eh (C:), 3Bh (`C:`), 40h (the MS-DOS Prompt's box, written to standard output), 4Ah, 2901h twice, 4B00h `C:\COMMAND.COM`. When the program ends: VM_Terminate, VM_Suspend, VM_Not_Executeable, the three devices' focus back to the System VM, then SHELL calls WINOA386's event procedure in the System VM (a far call on the locked stack, 0087h:0FF2h after its arguments: hwnd, message 418h, wParam 0, lParam the linear address of an event record: +8 the VM, +0Ch the window, +10h a dword with wParam, +14h the message); WINOA386 then gives the System VM the focus (function 5), calls 6, 16h and 0Ch (EDI the record) and destroys the VM (function 4: Destroy_VM). Other events: 414h (a hot key in a full-screen VM: wParam 1 for Alt+Enter), 41Ah (a windowed VM's display changed). Function 6 sets the VM's flags (EAX bits, ECX mask; bit 0: windowed), function 7 a status word in WINOA386's block, 15h names WINOA386's window (ESI). The full lists: the WIN386 handover note, step 6.

**w16** (2026-11-19): `win386/vmm/vm.c` (the VMs, their memory, instance data, the scheduler, VM events), `shell.c` (SHELL's API as above), `vdd.c` (the display's owner), the loader's VM stub (`ldrasm.asm`), per-VM state in VPICD (a virtual PIC per VM in its control block, _Allocate_Device_CB_Area) and VKD (a keyboard queue per VM, the keyboard focus, hot keys disabled per VM). Each VM has its own ring-0 stack, so a VM can be left at the end of any trap and taken up later where it was; its first megabyte is a page table of its own (the pages from the free memory's first page to the top of conventional memory private, the rest shared), also mapped at C0000000h + ID x 4 MB (CB_High_Linear); the interrupt vectors, the BIOS data area, DOS's swappable data area and the partial pages around the private ones are instance data, copied out and in at each switch, and a new VM starts with them as they were when Windows started. The VM's first instruction is a breakpoint (VM_Critical_Init, VM_Init, the BIOS sets its video mode when it has the display), then the loader's stub takes 8 KB of DOS memory as a PSP of its own, does what 3.1's VM did (above) and EXECs the program; its end is another breakpoint. The scheduler gives each VM ticks (its priority / 50), takes it off when it idles (HLT, INT 2Fh 1680h) and not inside DOS's critical sections (INT 2Ah 80h/81h). The display: the VGA's registers, planes and DAC are saved and restored when it changes owner, and the display driver gets INT 2Fh 4001h before and 4002h after in the System VM (run on the host stack by its protected-mode handler).

**Windowed (in progress).** Alt+Enter is a hot key SHELL defines with VKD (disabled in the System VM); in a DOS VM it becomes event 414h (wParam 1). WINOA386 then gives the System VM the focus (the full-screen VM is suspended, its VGA state saved), sets the windowed flag (function 6), and gives the VM the keyboard focus only. A windowed VM's video memory (A0000h-BFFFFh) is RAM of its own, the text taken from its saved planes (odd/even: the character at an even address is plane 0's byte, the attribute plane 1's); the VDD answers the grabber: Get_Mem (the text at the VM's high linear address B8000h, separate buffers for plane 1 and the font, and a last pointer to a bitmap of all ones: the DDK calls it the "allocation bitmap", and with zeros there Microsoft's VGA.3GR painted the window black), Get_State (mode, rows, cursor and its shape from the VM's BIOS data, the palette registers), Get_Mod (as Windows 3.1's: flags 38h the first time, the changed cells in AL AH BL BH and as words at +4, CX 5, EDX 0Ch), Clear_Mod, Free_Mem, Unlock_App; a windowed VM whose text or cursor changed gets event 41Ah. Alt+Enter in the window goes back to full screen (the text into the planes, the VGA restored). Not yet: typing in the window (on DOSBox-X's own DOS the shell takes the keys but its echo does not reach the VM's video memory: UNVERIFIED why; its C++ shell waits for keys in a nested loop of the emulator), the window's size (w16's shows scroll bars), Alt+Tab, Alt+Esc and Ctrl+Esc (Windows 3.1: VM_Suspend and event 41Eh, then functions 10h and 14h), two VMs at once, Exit Windows with a DOS box open.

**Result:** with w16's WIN386.EXE and Microsoft's KRNL386, USER, GDI, WINOA386 and drivers on DOSBox-X's own DOS (`DOSVM_NOHDD=1 tests/dosvmrun.sh NAME tests/scripts/input/dosbox386v.txt w16`), the MS-DOS Prompt starts full screen with the shell's banner and prompt, VER answers, EXIT returns to Program Manager, which is then pixel-identical to Windows 3.1's (the session `dosbox386v.txt` with Microsoft's WIN386 on the same DOS). Windows 3.1's DOS box needs WIN's output not redirected (DROPIN_NOREDIR, as INPUTRUN_HDD does): it inherits the handles. Boot-test case `win386dos` (10 of 10 on installation-05 under load). w16's WIN386 does not boot yet on the MS-DOS 4.00 image (KRNL386 faults after reading the first .FOT; Microsoft's WIN386 boots there); INT 21h functions that return no pointer now keep the caller's DS and ES (MS-DOS 4.00 changes ES in some), which was one difference but not the fault.

### 12.11 WINOA386.MOD: DOS programs in 386 Enhanced mode

w16's WINOA386.MOD is `winoa386/` (C, OpenWatcom medium model, with `shell/start.asm`'s start-up and `winoa386/woa386s.asm` for the 32-bit VxD calls and SHELL's event procedure; `make` builds `build/dos/WINOA386.MOD`, `make install-tree` puts it in the tree's SYSTEM). Module name WINOLDAP, as KERNEL loads it (§9.10); it imports the grabber (module GRABBER, `[boot] 386grabber=`) by ordinal. Everything it does was observed black-box, never read out of Microsoft's file: VXS.386's records of what Microsoft's WINOA386 passes to and gets from SHELL, VKD, VMD and the VDD (§12.10, `tests/vxsrun.sh`), GR3's records of its grabber calls (§12.9), w16's VMM trace and screenshots. Working notes: `docs/notes/winoa386-handover.md`.

**What it does.** One task and one window (class `WOA386`, w16's name: UNVERIFIED what 3.1's is) per DOS VM. Start: the command tail and the PIF (below); SHELL's API entry (INT 2Fh 1684h BX=17h; then VMD's and VKD's, function 0 each, as 3.1's order); SHELL 0 (the System VM's handle); the grabber: GrbGetTextColor (FFFFFFh, 0), GetFontList and the four DOS-box fonts it names (AddFontResource, then a font of each cell size into AltFnt[0..3] and DefFont; a font file that is not there is skipped and the OEM font used: Windows 3.1's WINOA386 stops with KERNEL's "Cannot find EGA80WOA.FON" box, and w16's tree has none of these fonts), CheckGRBVersion; the window as an icon; SHELL 9 (the event procedure: `woa_event` posts SHELL's (hwnd, message, wParam, lParam) to the window), 3 (the VM: the block and registers below), 5 (the focus: full screen), 15h (the window). Events: 414h wParam 1 (Alt+Enter) to a window (SHELL 5 to the System VM, 6 (1, 1), 6 (1, FFFFh), 7 (4); the window shown, sized for the DOS screen as SetPaintFnt gives it: wider than the screen, it starts a frame's width in and is as wide as the screen less two frames, its height keeps the whole DOS screen above the horizontal scroll bar, as measured on 3.1's screen) or back to full screen (5, 6 (0, 1), 6 (10h, FFFFh), 7 (0), the icon, 5 to the VM); 41Ah (GetDisplayUpd, then SetPaintFnt + UpdateScreen + CursorPosit, or GrbUnLockApp); 41Eh (a switching key in a full-screen VM: SHELL 10h offering the windows in switching order with their titles, then 0Ch with EAX 1 and the chosen window activated); 418h (the program ended: SHELL 5, 6 (AX 418h), 16h, 0Ch, 4; the window destroyed; with Close Window on Exit off the inactive window instead, below). Every event record goes back with SHELL 0Ch. WM_PAINT: TranslateWinToFScreen, GetDisplayUpd(-1), SetPaintFnt, PaintScreen; scroll bars move ColOrg/RowOrg. Activating the icon (Alt+Tab, a double click) gives the VM the focus again (full screen); a windowed VM gets the keyboard while its window is active. WM_QUERYENDSESSION while the VM runs: "Application still active. / Quit the application before quitting w16." in a box titled with the program's title (3.1's text, "w16" for "Windows"), and Windows does not end.

**The PIF** (`winoldap/pifread.c`, shared with WINOLDAP; `tests/pifread.sh`, fixtures `tests/scripts/pifread/mkpifs.py`: DOSBox-X's file log of each read, VXS.386's record of SHELL function 3, WC1.COM's of what the program got, under Microsoft's WINOA386 and w16's). 3.1's WINOA386 seeks to the end; a file of more than 171h bytes has its header at 171h read, and if that is "MICROSOFT PIFEX" the chain is followed (each header's next offset, FFFFh the end) to "WINDOWS 386 3.0" with a length of exactly 68h (40h and 70h were refused); the checksum is not looked at. Found, it reads the 386 data at the header's data offset: +00h 16 bytes, +10h 4, +14h 4, +28h 64 (the parameters), +18h 16, each read over defaults (a read past the end leaves them: KB desired FFFFh, required 0, priorities 100/50, EMS and XMS 1024/0, flags 08 10 02, video 17h, no parameters). Not found (a 2.x PIF, a file of 171h bytes or less, an unknown first header, a next offset past the end, a 386 section of another length or name, a missing one) it reads EDh, 16Fh, 63h, 20h (4) and the base's parameters (A5h) instead: KB desired and required from 20h and 22h, video 17h, full screen with 16Fh bit 80h (else windowed), exclusive with 16Fh bit 10h, background with 63h bit 01h (63h FFh gave it). Then 63h, the program (24h), the directory (65h), the title (02h). A loop in the chain hung 3.1's WINOA386 (Windows never came back); w16 stops after 16 headers. The parameters come from the 386 section only (an empty one is empty, never the base's), to their first NUL with their blanks (all 64 bytes without a NUL), after one blank: "  ea  eb  " gave "   ea  eb  "; "?" shows the Parameters dialog (3.1's at 64,56, 371 x 107 pixels). A blank or NUL title gives the PIF's name in capitals ("PROBE"); a .BAT runs as COMSPEC with " /c FILE " before the parameters' blank. A missing program: "Cannot find file. / Check to ensure the path and filename are correct." titled with the program as the PIF has it; a missing start-up directory: "Start-up directory not found. ..." titled with the PIF's title; no directory: the current drive ("C:"), where the program found itself in C:\.

**SHELL function 3's block** from the PIF (3.1's WINOA386, VXS.386, every fixture): +00h the 386 flags (+10h) with 01h for Exclusive (the PIF's 04h), 02h Background (02h), 04h windowed (the PIF's 08h clear), E0h the reserved Alt+Tab, Alt+Esc, Alt+Space as they are (Allow Close When Active, 01h, does not reach SHELL); +01h the 386 data's +11h as it is; +02h its +12h, with 08h for a batch file; +03h 40h Close Window on Exit (63h 10h); +04h the video dword (+14h); +1Ah the 386 data's first 16 bytes (KB desired, required, the priorities, EMS and XMS limits and required); +2Eh the title. Registers: EAX 1902h, EBX F0h, ESI 8 for every PIF, ECX 07C7h with the 386 data's +12h in bits 16-23 (meanings UNVERIFIED). w16's WINOA386 gives the same block and registers. What Microsoft's WIN386 does with them, seen from the program: the top of conventional memory is the VM's free memory's start (0C00h there) plus KB desired (64 KB: 1C00h; 100 KB: 2500h; 0: 4 KB; -1 or more than there is: 640 KB's top), whatever KB required says. w16's SHELL acts on windowed (from the start), the priorities, KB desired (`vm_set_memory`: the free arena after the VM stub's 4 KB cut there by an MCB of the system's) and Exclusive (no other VM runs while it has the focus full screen); Background is kept but a full-screen VM without the focus is still suspended (its video memory is the VGA's then), and the rest (+01h-+03h, the video settings, KB required, EMS, XMS) is not acted on. With Close Window on Exit off, 3.1's WINOA386 keeps the ended VM: SHELL 6 (AX 418h), 16h, 0Ch as always, then 6 (AX 0, CX 47h), 6 (CX FFFFh), 7 (4), and the window "(Inactive TITLE)" stays until closed; w16's does the same.

**Results.** On w16's tree alone (WIN386, VGA.3GR, w16's WINOA386: boot-test `treealone`), the MS-DOS Prompt goes full screen, to a window and back, and ends with EXIT. With w16's WIN386 on Windows 3.1, the switching and Exit-Windows scenarios equal Microsoft's WINOA386's screen for screen but for the icon and the branded text (boot-test `win386woa`); the window itself equals Microsoft's WINOA386's to the pixel with the same fonts. Not done: the system menu's Edit (Mark, Copy, Paste, Scroll), Settings and Fonts dialogs, the "Inactive" title of a suspended VM, Alt+Space, the DOS-box fonts; of the PIF's 386 options w16's WIN386 does not yet act on Background and the rest named above. Under Microsoft's WIN386 a box starts full screen and ends, but Alt+Tab out of it does not come back to Windows: Microsoft's SHELL answers function 10h with EAX 1 every time (UNVERIFIED what 3.1's WINOA386 does between its two calls).

## 13. SHELL

`SHELL.DLL` (module SHELL) is the shell library of Windows 3.1: the registration database, drag and drop for receivers, `ShellExecute`/`FindExecutable`, icon extraction, the About box, environment substitution, and the shell hook. w16's is `shell/shelldll/` (`make shelldll`: `build/dos/SHELL.DLL`, also `build/w16/SYSTEM/SHELL.DLL`). It is a drop-in replacement: copied over `WINDOWS\SYSTEM\SHELL.DLL` it serves Microsoft's Program Manager, File Manager and applets. Windows 2.x has no SHELL.DLL (§8), so there is no 2.x build.

### 13.1 The interface

From the NE tables (`docs/exportdb/`, `tools/neimports.py SHELL`): 51 entries, 20 imported by 26 files of the 3.1 install (WfW 3.11: 48 entries, the same 20 imported). w16 exports the same ordinals with the same names and residency (WEP's name resident, the rest non-resident; 104-128 ordinal-only with the entry flag 0, as 3.1 has them: `tools/nenoname.py`), one moveable discardable code segment and a fixed DGROUP. The `sh*` probes (`tests/apps/sh*.c`, run by `tests/shellprobe.sh [-w] SHn` on the reference install, `-w` with w16's SHELL dropped in; reference tables `tests/apps/sh*ref.h` from `tools/obsref.py`) observed everything below black-box; SHELL.DLL's code was never looked at. Undocumented imports SHELL uses (Schulman et al., *Undocumented Windows*, ch. 2, 5, 6): DirectResAlloc (KERNEL.168), GetIconID (USER.455), LoadIconHandler (USER.456).

| Ord | Export | Observed on 3.1 (probe) | w16 |
|---|---|---|---|
| 1-7 | RegOpenKey, RegCreateKey, RegCloseKey, RegDeleteKey, RegSetValue, RegQueryValue, RegEnumKey | 13.2 (SH1, SH2, SH12) | all; REG.DAT byte-identical to 3.1's after every step of SH1 and SH2, from the stock file, from nothing, and from rewritten files |
| 8 | WEP | | returns 1 |
| 9, 11-13 | DragAcceptFiles, DragQueryFile, DragFinish, DragQueryPoint | 13.3 (SH3, SH13) | all; identical results |
| 20, 21 | ShellExecute, FindExecutable | 13.4 (SH4, SH11) | all but DDE (`ddeexec`, UNVERIFIED) |
| 22, 33 | ShellAbout, AboutDlgProc | 13.5 (SH8, SH10) | all; same layout; w16's own picture and two strings |
| 34, 36, 39 | ExtractIcon, ExtractAssociatedIcon, InternalExtractIcon | 13.6 (SH6) | all; icon blocks byte-identical |
| 37, 38 | DoEnvironmentSubst, FindEnvironmentString | 13.7 (SH7) | all |
| 101 | FindExeDlgProc | 13.4 (SH11) | the Program Not Found dialog |
| 102, 103 | RegisterShellHook, ShellHookProc | 13.8 (SH9) | all |
| 32, 100, 104-128 | WCI, HERETHARBETYGARS (the About box's hidden credits, per its name and Schulman), unnamed (107, 121, 127, 128 in data segments) | nothing in 3.1 imports them | stubs returning 0; data words. UNVERIFIED: their purpose |

### 13.2 The registration database and REG.DAT

**The API** (SH1, SH2; return codes are LONGs, the SDK's ERROR_*):
- HKEY_CLASSES_ROOT (1) is the key `.classes` under the database's top node (made when first written to). A handle is `MAKELONG(node, 1)`: the entry index of the key's node in REG.DAT; a handle whose high word is 0 means the top node (so a bad handle like 1234h reads and even writes the top node); a node index beyond the table gives ERROR_BADKEY (2). RegOpenKey on the same key gives the same handle; NULL or "" opens the key itself.
- Opening and closing only count: RegCloseKey accepts any handle, and closing more often than opening gives ERROR_INVALID_PARAMETER (7). Handles stay usable after closing and after their key is deleted.
- Key names are matched without regard to case; a path with an empty component (leading, trailing or doubled `\`) gives ERROR_BADKEY; RegCreateKey accepts components of 1-63 characters from 21h-7Fh only (64 or more, a space, a control character or one of 80h-FFh: ERROR_BADKEY). 40 levels were accepted. These are 3.1's rules, kept as observed compatibility, not buffer sizes.
- RegSetValue ignores cbData (the value is the string), accepts only REG_SZ (else 7), creates missing keys, and "" removes the value. RegQueryValue copies at most *lpcb - 1 characters and a NUL, and sets *lpcb to the length of what is then in the buffer plus 1 (with *lpcb = 0: nothing is copied and *lpcb is the old buffer's length + 1); a key without a value gives "". RegEnumKey lists the newest subkey first; an index past the end (also above 64K) gives ERROR_BADKEY. RegDeleteKey deletes the whole subtree; a NULL or "" subkey gives ERROR_BADKEY.
- REG.DAT is written whenever a change is made while no key is open, or when the last open key is closed; a missing REG.DAT is written at once, empty, when SHELL loads. An existing REG.DAT is read at the first registration call, not when SHELL loads, and not again while SHELL stays loaded (RE13, 15.9; w16 read it in LibMain until 2026-11-21). Such a new database gets WIN.INI's `[embedding]` OLE servers (`class = description` and `class\protocol\StdFileEditing\server = program`, in the section's order) in the first registration call made about a second after the shell started (SH12; UNVERIFIED: the exact delay and trigger).

**The file format** (written and read by `shell/shelldll/reg.c`; `tools/regdat.py` dumps it):
- Header, 32 bytes: `"SHCC3.10"`; DWORD 20h (the entry table's offset); DWORD 20h (UNVERIFIED: meaning); DWORD entry count; DWORD offset of the text area (20h + 8 × count); DWORD size of the text area; WORD the number of hash buckets (37); WORD the first free entry.
- Entries, 8 bytes (4 WORDs), by index: 0 the top node {0, first child, 0, 0}; 1-37 the buckets {first text entry of the chain}; a key node {next sibling, first child, name text, value text}; a text entry {next in its chain, references, length, offset of the string in the text area}; a free entry {next free, 0, and the two words it last held}. A chain is circular: its last entry points back at its bucket.
- Text area: each string (no NUL) is preceded by the WORD 2 × entry + 1. Strings are shared by reference count between key names and values: a key name takes an existing string regardless of case (a key created as `txtfile` displays as `TXTFILE` if that value string exists), a value only an exact one. A string's bucket is 1 + (sum of its first 39 characters, upper-cased) mod 37.
- How 3.1 changes it, which w16 reproduces byte for byte: a new string goes to the end of its chain and of the text area, its text entry before the key node that uses it; a new key goes first in its parent's list; entries come from the free list, which grows by 16 entries in order when empty; freed entries go to its head; deleted keys leave the tree at once but are freed when the file is next written, in deletion order, each subtree children first and each key's name, value, then node; dead strings stay in place until the file is written, when the live ones are packed down (a freed entry keeps the offset it had). On loading, 3.1 keeps only what the tree reaches (not even the top node's own value), relinks the free entries in order, ends the table 16 entries after the last one in use, and drops dead chain entries and strings. An empty database is the top node and 37 empty buckets, its text area size 1 with 16 zero bytes written.
- Limits are the format's: WORD entry indices and text offsets (65535 entries, 64 KB of text); w16's entry table becomes a huge block past 64 KB.

### 13.3 Drag and drop (receiving)

DragAcceptFiles only sets or clears the window's WS_EX_ACCEPTFILES (10h) (SH3: no subclassing, no properties). The drop block (SH13, a journal-playback drag from File Manager onto a window; Schulman's DROPINFO, ch. 6, corrected): WORD offset of the names (8), POINT the drop point in the target's client coordinates, WORD nonzero when dropped outside the client area (Schulman calls it bClient; DragQueryPoint returns 1 only when it is 0), then full upper-case paths, NUL-terminated, ending with an empty one; a moveable block (flags 0). DragQueryFile(-1) counts the names; another index past the end gives 0 with the buffer untouched; otherwise the full length is returned and as much copied as cb allows; DragFinish frees the block. **For the USER agent:** SHELL takes no part in building or sending the block: with w16's SHELL dropped in, File Manager's drop arrived byte-identical. The target window receives, all sent: WM_QUERYDROPOBJECT (22Bh) wParam 1 then WM_DRAGSELECT (22Eh) wParam 1 on entering, then 22Bh/WM_DRAGMOVE (22Fh) pairs with wParam 0 while moving, WM_DROPOBJECT (22Ah) wParam = the source window, lParam = the DRAGINFO block throughout, and then WM_DROPFILES (233h) posted with the block; the target passed all of them to DefWindowProc.

### 13.4 ShellExecute and FindExecutable

(SH4 with SH5, a program that logs its command line and directory; SH11.) A file without an extension is a program with `.EXE` added; one whose extension WIN.INI `[windows] Programs=` lists is its own program; others are documents. The file is looked for as OpenFile does (in `lpDirectory` when given, which is also the started program's current directory); not found: OpenFile's error (2, or 3 for a missing directory). A document's command comes from the registration database first (`.ext` names a class; `class\shell\verb\command`, verb "open" by default, any case; `%1` the document's full path, `%2` the parameters (UNVERIFIED: %3-%9), `%%` a %, `%x` x; without `%1` nothing is added), then for the open verb only from WIN.INI `[extensions]` (`program args ^.ext`, `^` the path without its extension; parameters ignored). A class without the command is "incomplete" (27); nothing at all is "no association" (31); a program with any verb but open: 31. FindExecutable returns 1000 on success with the command's first word (not searched for) or the program's full path. When the program of a registered class cannot be started, SHELL looks in WIN.INI `[programs]` (NAME=path) and otherwise asks with its Program Not Found dialog (template 200, FindExeDlgProc): OK with the directory holding the program writes `[programs]` and starts it; a wrong directory shows "Cannot find file '...'." in a message box; Cancel returns 15; Locate... uses COMMDLG with SHELL's template 300 (UNVERIFIED: its flags; Help: UNVERIFIED). A WIN.INI-only association returns WinExec's error instead.

**Every kind of name** (SH14, `tests/shellprobe.sh SH14`: FindExecutable, ShellExecute, WinExec and LoadModule each called with a Windows program with and without its extension and with parameters, a .COM, a .BAT, a DOS .EXE, a file without an extension, registered documents (.TXT, .WRI), unregistered extensions (.ABC, .XYZ, with and without parameters), an extension WIN.INI maps to a missing program, a registered class whose program is missing, missing files, a missing directory and directories; every document's contents a tiny .COM program that leaves a marker file if DOS runs it). ShellExecute and FindExecutable: 31 for an unregistered extension, nothing started; 2 for a missing file, a directory (C:\RA), a file without an extension (".EXE" added) and an association to a missing program from WIN.INI; 5 for a directory with an extension; 3 for a missing directory; 15 after the Program Not Found dialog. **WinExec starts anything that is not a Windows module through WINOLDAP:** an unregistered or registered document (U.ABC, A.TXT, W.WRI) returns an instance and DOS runs the file as a .COM program (a real document then crashes); only a file without an extension fails (2: ".EXE" is added). LoadModule of anything that is no Windows module returns 11 and starts nothing (a 32-bit program, a PE file: 21; WinExec and ShellExecute run its MS-DOS stub, §9.8). So a shell must not fall back to WinExec when ShellExecute says 31. **Program Manager** (File > Run, boot-test `runassoc`): ShellExecute's 31 gives its "Application Execution Error" box "No application is associated with the specified file. Create an association by using File Manager." (exclamation, OK) and nothing starts; 2 gives the "Cannot find file ... (or one of its components)" box (also for a directory, a file without an extension and a missing associated program), 5 (a directory with an extension) "Access to the specified device, path, or file is denied."; the boxes are system modal (MB_SYSTEMMODAL: Microsoft's USER then draws them with a thin frame, pixel for pixel 3.1's), the Run dialog is hidden while one is up and comes back afterwards with the command line as typed. Until 2026-11-16 w16's Program Manager ran the document with WinExec on 31 (w16's install tree, before WIN.INI had `wri=`: a .WRI started as a DOS program and crashed); w16's KERNEL also started WINOLDAP from LoadModule. SH14 gives 3.1's results with w16's SHELL on w16's KRNL286 and KRNL386 (Standard mode; 386 Enhanced mode's reference agreed as far as it ran, up to WinExec of a .TXT, where it was too slow for the run's time limit in DOSBox). **File > Run's directory** (SH5 run five ways, boot-test `runassoc@dir`): a program named by a full path starts in its own directory (`C:\TMP\SH5.EXE a b` in C:\TMP, `c:\tmp\sub\sh5 f` in C:\TMP\SUB), a bare name in Program Manager's own (C:\WINDOWS), also right after the others, so Program Manager's directory does not change; w16's passes the directory to ShellExecute (UNVERIFIED: a relative path with a directory, a document). Until 2026-11-18 w16's started everything in its own directory, and the Movie Player that QuickTime's installer (`C:\TMP\QTINSTAL.EXE`) starts opened its Open box in C:\WINDOWS instead of C:\TMP.

### 13.5 ShellAbout

(SH8 dumps each control of the running dialog and screenshots it; SH10.) `"Title#Text"`: the caption is Title and the first line ends with Text; without `#` the caption is "About szApp". Then "Version 3.1 " (`Version %s %s`, USER's string 516 and "(Debug)" on a debug system), szOtherStuff, "This product is licensed to:", the user's and organisation's names and the serial-number note (USER's strings 514, 515, 517, which Setup writes), the mode, "Memory:" GetFreeSpace(0)/1024 KB with comma thousands, "System Resources:" GetFreeSystemResources(0) %, the SMARTDrive label hidden (UNVERIFIED: when 3.1 shows it). hIcon goes top left; without it the icon control is hidden and a 64x64 picture is drawn. Returns 1. The template's layout was taken from the running dialog (base units 7 × 13, `tools/dlgunits.py`) and matches to the pixel. **Decision needed (branding):** SHELL's own strings "Microsoft Windows %s" (the first line's prefix) and "Copyright © 1985-1992 Microsoft Corp." are Microsoft's branding; w16 shows "w16 %s" and "Copyright © the w16 contributors" (marked BRANDING in `shell.rc`) until the coordinator decides. The picture (Windows' logo on 3.1) is w16's own art (`tools/mkaboutlogo.py`). Everything the caller passes and USER's strings are shown exactly.

### 13.6 Icons

(SH6.) ExtractIcon: count with index -1 (the RT_GROUP_ICON resources of an NE file; 1 for an .ICO file whatever its images); an icon block exactly as LoadIcon makes it (the image chosen by GetIconID, converted by LoadIconHandler, in a DirectResAlloc block owned by hInst); NULL past the end, for a missing file, a directory, a name without its extension; 1 at any index for a file that is neither (text, DOS program, PE). ExtractAssociatedIcon: the file's icon; for a non-icon file its association's program (the path becomes the program's name, the index 0); failing that the calling program's own icon 1 (a DOS program by extension) or 6 (anything else), the path becoming its file name: Program Manager's stock icons. InternalExtractIcon(hInst, file, first, n): a moveable block of n HICONs (0 past the end; 1 first for a non-icon file), NULL for no file or n = 0. On w16's USER, which does not have GetIconID/LoadIconHandler yet, w16's SHELL chooses the image itself (UNVERIFIED rule) and icons fail until USER has LoadIconHandler.

### 13.7 Environment

(SH7.) DoEnvironmentSubst(sz, cb): `%NAME%` (any case) becomes the value; `%%` a %; unknown names, a lone % and an unterminated `%NAME` stay. If the result and its NUL fit in cb, it replaces sz and the result is MAKELONG(length, 1), else sz is unchanged and the result is MAKELONG(length of sz, 0). FindEnvironmentString(name): a pointer to the value in the DOS environment (past `=`), the name in any case; NULL for a prefix, a name with `=` or "".

### 13.8 The shell hook

(SH9.) RegisterShellHook(hwnd, n): n != 0 adds a registration (again on each call: a window registered twice gets each message twice); n = 0 removes one; it returns 1, also for NULL. SHELL installs its WH_SHELL hook and registers "OTHERWINDOWCREATED", "OTHERWINDOWDESTROYED" and "ACTIVATESHELLWINDOW" at the first call. HSHELL_WINDOWCREATED (1) and HSHELL_WINDOWDESTROYED (2) post the first two messages, wParam the top-level window, lParam 0, to every registration; child windows, activation, hiding and showing send nothing. HSHELL_ACTIVATESHELLWINDOW (3) sent nothing and activated nothing (UNVERIFIED: its purpose). ShellHookProc returns 0.

### 13.9 Tests and results

- `tests/shellprobe.sh -w SHn`: SH1-SH4, SH6-SH9, SH11, SH13 give the same observations with w16's SHELL as with Microsoft's in both modes (the OBS tables check them: 0 FAIL); REG.DAT snapshots are byte-identical. Known differences: registered message numbers (atoms) and global handles, which depend on the order of other allocations; GetFreeSpace in the About box.
- `tests/shellscen.sh` (SNAP scripts in `tests/scripts/shell/`; SNAP now plays back mouse drags and clicks with a WH_JOURNALPLAYBACK hook): About boxes (Notepad, Calculator, File Manager, Program Manager), Change Icon, File Manager drag and drop onto Program Manager (two files become items), opening documents from File Manager (Notepad with BOOTLOG.TXT; a file without an association), REGEDIT in both views; screenshots compared with `tests/scripts/shell.baseline`, REG.DAT, WIN.INI, PROGMAN.INI and the group files byte for byte. Result: every screen identical (0 pixels) except the About boxes (1160-3031 pixels: the picture, the two branding lines, and the free-memory figure, a few KB lower with w16's SHELL); every file identical.
- `tests/shellinput.sh` (real input, `tests/inputrun.sh`: xdotool through DOSBox's mouse and keyboard): Program Manager's and Notepad's About boxes opened and closed by keyboard (only the expected About-box pixels differ, the screens after closing are identical), and a file dragged with the mouse from a narrowed File Manager into Program Manager's Main group (identical, 0 pixels, at every step). `-b` sets the boot wait: on a busy machine 25 seconds was too short (black shots).
- `tests/boottest.sh runassoc` (parts probe, pm, pmk286, pmk386, tree): SH14 with w16's SHELL on w16's KRNL286 and KRNL386 against 3.1's table and observations; Program Manager's File > Run of every kind of name (`tests/scripts/input/runassoc.txt`) with w16's PROGMAN and SHELL on Microsoft's KERNEL and on w16's two, every screen 0 pixels against all of Microsoft's (the group icons' pictures and the carets left out) and no document run as a DOS program; the install tree alone under both kernels (`runassoctree.txt`).
- `tests/boottest.sh runpe` (parts k286, k386, on286, on386): Program Manager's File > Run of a 32-bit program (`tests/scripts/input/runpe.txt`, §9.8) with w16's PROGMAN and SHELL on w16's two kernels: `[w16] PEError=0` every screen 0 pixels against all of Microsoft's and the MS-DOS stub run in both; `PEError=1` w16's box (the same on both kernels), nothing run, the Run box back as typed, then the screen after the boot. Microsoft's USER leaves a stray repaint on Program Manager's caption after the DOS program returns in about half the runs, all-Microsoft reference runs included (2 of 4; w16's kernels 4 of 10): rows 51-52 from x 68 to 579 (the frame's inner line and the caption's first row, 352 or 374 pixels; case winoldap's check meets it too); the stock parts leave those two rows out.
- `tests/boottest.sh shell shellk`: Windows 3.1 booting with w16's SHELL.DLL, on Microsoft's and on w16's KRNL286: the screen pixel-identical to the reference.

## 14. Control Panel

CONTROL.EXE (`shell/control`) and the applet DLLs (`shell/cpl`), as drop-in replacements for Windows 3.1's: with ours copied over Microsoft's in a scratch copy of the reference install, the windows must look the same pixel for pixel (apart from w16's own icon art, `tools/cplicons.py`) and every INI file must come out byte-identical. Microsoft's files are only run and observed: SNAP.EXE scripts (`tests/cpobs.sh` for one-off probes), the applet probe CP1 (`tests/apps/cp1.c`: a CPL that logs every CPlApplet call, its parameters, the window it was given, and answers as `C:\CP1.INI` says), dialog templates, menus and strings read as data with `tools/resdump.py`. `tests/cplscenarios.sh` runs each scenario of `tests/scripts/cpl/` on the original and on ours and compares CONTROL.INI, WIN.INI, SYSTEM.INI, the CP1 log and every screenshot (`tests/scripts/cpl.baseline`).

### 14.1 Who is where (Windows 3.1, Standard mode)

| Applet | File | Notes |
|---|---|---|
| Color, Fonts, Ports, Mouse, Desktop, Keyboard, Printers, International, Date/Time | SYSTEM\MAIN.CPL (module MAINCPL) | 9 applets, in that order |
| Drivers | SYSTEM\DRIVERS.CPL | |
| Sound | SYSTEM\SND.CPL | |
| 386 Enhanced | SYSTEM\CPWIN386.CPL | not shown in Standard mode (its CPL_INIT declines, UNVERIFIED which call); w16's is a file of its own too (`shell/cpl/win386`, 14.6), so that w16's MAIN.CPL laid over 3.1 does not show the icon twice |
| MIDI Mapper | MIDIMAP.DRV (installable driver, exports CPLAPPLET) | not shown in the reference install's Standard mode |

MOUSE.DRV exports CPLAPPLET as ordinal 6: the Mouse applet calls it with 5 first and shows its own dialog only when it returns FALSE (DDAG, "CplApplet").

### 14.2 CONTROL.EXE (observed)

- **Windows.** Frame class `CtlPanelClass`, caption "Control Panel", WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN. Children: the icon list, class `lb`, ID 14h, WS_BORDER | 0711h (+ WS_VSCROLL when needed); the status line, class `Text`, ID 15h, WS_BORDER. Both hang one pixel over the frame's client edge (at client -1,-1, so their borders fall on the frame's inner line); the list's bottom border is the status line's top border. The status line is the 10-point font's height + 6 (+ borders: 24 on VGA), a sunken box 8 pixels in from the sides and 2 from top and bottom, text 3 pixels in, 1 down.
- **Icons.** Names in 8-point MS Sans Serif. Cell width = the widest name; height = SM_CYICON + 6 + font height (51 on VGA); icon 6 pixels below the cell top, centred; the name below it, centred, the selected one on COLOR_ACTIVECAPTION in COLOR_CAPTIONTEXT (not the highlight colours; also while the panel is inactive) exactly as wide as its text. Row by row; (list width - SM_CXBORDER - SM_CXVSCROLL) / cell width columns, whether or not the scroll bar shows.
- **Loading order**: SYSTEM\MAIN.CPL, then each [MMCPL] entry of CONTROL.INI (other than NumApps, X, Y, W, H; the value is a path), then the other SYSTEM\*.CPL in directory order. Calls: CPL_INIT (the whole LONG; 0 = unload), CPL_GETCOUNT (low word only), per applet CPL_NEWINQUIRE into a buffer whose dwSize is cleared first (the rest is left over from the previous applet): if the applet set dwSize its answer is used whatever it returned, else CPL_INQUIRE follows (MAIN.CPL takes that path). The window exists, visible at its old or default size, during CPL_INIT.
- **Choosing an applet** (Enter, double click, the Settings menu): the list selects it, then CPL_SELECT and CPL_DBLCLK (applet, lData). Moving the selection sends nothing. **Closing**: X/Y/W/H written, the window hidden, CPL_STOP for each applet (last first), CPL_EXIT.
- **Settings menu**: "<name>...", inserted before the separator and Exit; a name without '&' gets its first letter as mnemonic. Menu IDs of the Help items and F1 come from the original's menu resource.
- **CONTROL.INI [MMCPL]**: NumApps=<applets> when loaded; X=, Y=, W=, H= (the normal rectangle) on close. At start: X/Y/W/H (default 0, 0, 430, 240 on 640x480; UNVERIFIED how it scales). If NumApps differs from the saved value and the icons do not fit, the window becomes 6 columns wide (fewer if the screen is too narrow) and as tall as the rows need (at most the screen); width = columns x cell + SM_CXVSCROLL - 1 + 2 SM_CXFRAME, height = rows x cell + status line - 1 + 2 SM_CYFRAME + SM_CYCAPTION + SM_CYMENU (one more than AdjustWindowRect gives).
- **Command line**: `CONTROL <name>` (any case, without '&'): the panel is shown, disabled during the applet, and closes afterwards. `CONTROL <file>.CPL`: only that file, no window (hwnd NULL in every call), CPL_DBLCLK on its first applet, no CPL_STOP/CPL_EXIT. Anything else: the normal panel.
- **Help**: Contents/Search/How to Use Help on CONTROL.HLP; About is SHELL's ShellAbout. F1 on an applet: its NEWCPLINFO help file and context, else CONTROL.HLP (UNVERIFIED: the original's choice here).

### 14.3 MAIN.CPL applets (observed; w16 status)

- **Keyboard** (done). WIN.INI [windows] KeyboardDelay (0..3; the bar runs Long to Short, position 3 - delay) and KeyboardSpeed (0..31). OK writes both; Cancel nothing.
- **Mouse** (done). MouseThreshold1/MouseThreshold2/MouseSpeed from seven settings 0 0 0, 10 0 1, 7 0 1, 4 0 1, 4 12 2, 4 9 2, 4 6 2, always written on OK (the current values when the bar did not move); the bar starts at the slowest setting not faster than the current one (UNVERIFIED beyond the points tried). DoubleClickSpeed 100..900, 16 per step. Home is the Fast end of both bars. SwapMouseButtons=yes/no when changed (a plain BS_CHECKBOX; the L and R labels swap). MouseTrails written as the box is clicked (7, or -7 when off), put back by Cancel.
- **Color** (done; `color.c`). OK sets the 21 system colours and writes all 21 WIN.INI [colors] keys in the order Background, AppWorkspace, Window, WindowText, Menu, MenuText, ActiveTitle, InactiveTitle, TitleText, ActiveBorder, InactiveBorder, WindowFrame, Scrollbar, ButtonFace, ButtonShadow, ButtonText, GrayText, Hilight, HilightText, InactiveTitleText, ButtonHilight, as "R G B" (also when nothing changed). CONTROL.INI [color schemes] values list the same 21 colours as hex BBGGRR in that order (Black Leather Jacket's 10E0E0E0 keeps its high byte when drawn, as 3.1 draws it); [current] color schemes=<name>, empty after a colour was changed by hand (the combo box keeps showing the scheme). The scheme combo box is owner-drawn, unsorted, "Windows Default" first (built in: C0C0C0,FFFFFF,FFFFFF,0,FFFFFF,0,800000,FFFFFF,FFFFFF,C0C0C0,C0C0C0,0,C0C0C0,C0C0C0,808080,0,C0C0C0,800000,FFFFFF,0,FFFFFF), items "name=values"; its focused item is highlighted whole with the focus rectangle one pixel outside, an unfocused one one pixel inside. Remove Scheme asks (Control Panel, exclamation, Yes/No), deletes the key and selects the next scheme. Save Scheme and Remove Scheme start disabled; Color Palette >> widens the dialog (collapsed: 151 dialog units), enables Save Scheme and disables itself. Save Scheme's box starts with the selected scheme's name. The 48 basic colours, row by row (read from the selector after clicking each): FF8080 FFFFE8 80FF80 00FF80 80FFFF 0080FF FF80C0 FF80FF / FF0000 FFFF80 80FF00 00FF40 00FFFF 0080C0 8080C0 FF00FF / 804040 FFFF00 00FF00 008080 004080 8080FF 800040 FF0080 / 800000 FF8000 008000 008040 0000FF 0000A0 800080 8000FF / 400000 804000 004000 004040 000080 000040 400040 400080 / 000000 808000 808040 808080 408080 C0C0C0 828282 FFFFFF (RRGGBB), the same as COMMDLG's. The sample is drawn from rectangles measured on VGA: an inactive and an active window (frames, notches, captions in the system font), the File menu dropped down (Disabled, Highlighted), the client in AppWorkspace, a child window (Window Text, OK button), a scroll bar. Grids and the Custom Color Selector draw as w16's COMMDLG colour dialog does (field, bar, arrow 6 rows each way here, cross, sample); Add Color writes [Custom Colors] ColorX at once and leaves the screen element alone; Define Custom Colors is disabled while the selector is up. cpArrow controls have WS_BORDER and are one pixel wider than their template (13 pixels for 7 units). UNVERIFIED: the luminosity bar's block rows for this bar's height (a few rows differ), keyboard use of the grids.
- **Desktop** (done; `desktop.c`). WIN.INI [windows] CoolSwitch, ScreenSaveActive, ScreenSaveTimeOut (seconds), BorderWidth, CursorBlinkRate; WIN.INI [Desktop] Pattern (the bits, "170 85 ...", or "(None)"), Wallpaper (the file name as listed, lower case), IconSpacing, IconTitleWrap, TileWallPaper, GridGranularity; SYSTEM.INI [boot] SCRNSAVE.EXE (the saver's path, or "(None)"). OK with nothing changed still writes CoolSwitch=1, IconSpacing=75, TileWallPaper=1 (appended in that order when absent) and SCRNSAVE.EXE=(None); IconTitleWrap only when changed. Ranges (checked on OK, "Control Panel" message with the information icon): border width 1-50, icon spacing 32-512, granularity 0-49; a 0-minute delay is taken. Cursor blink 200-1200 ms, 20 per step, Home the Fast end. Patterns from CONTROL.INI [Patterns] ("(None)" first, sorted), savers from the *.SCR files' module descriptions ("SCRNSAVE : Blank Screen"), wallpapers from *.BMP. Test and Setup... run the saver (scenario `tests/scripts/ss/desktop.txt`, 15.8). UNVERIFIED: the Edit Pattern dialog's details, Test/Setup's exact command lines (w16: /s, /c), the blink sample (w16: the static shown and hidden at the rate).
- **Fonts** (done; `fonts.c`; layouts from 3.1's window dumps, not its resources: the scripts of `tests/scripts/cpl/fn*.txt` and docs/notes/cpl-fonts-ports-handover.md). The list is WIN.INI [fonts]'s keys (sorted, extended selection, none selected at first); the sample an owner-drawn list: "AaBbCcXxYyZz 123" once per size of a raster font file (each row its font's tmHeight), once at 24 points for a TrueType (.FOT) font, once at its font directory's nominal height and average width for a vector font (both 0 pixels from 3.1); below it "No fonts are selected.", "This is a screen or plotter font.", the TrueType text, or "... cannot display samples of more than one font at a time.", and "The size of the font on the disk is:  N KB." (each file's size in KB rounded up, a .FOT and its .TTF both counted). The default button (IDOK) reads Cancel, then Close once Remove was pressed, a font added or a TrueType option clicked. Add is COMMDLG's open dialog with a template and hook (List of Fonts named by each .FON's description after "FONTRES ...:" and each .TTF's full name + " (TrueType)", Select All, Copy Fonts to Windows Directory, checked): after it closes, a name [fonts] has already gets "The <name> font is already installed. To install a new version, first remove the old version." (Control Panel, OK/Cancel, exclamation); copied, the file goes to the System directory, its last letter before the dot replaced by 0, 1 ... when the name is taken (EGA40WOA.FON as EGA40WO0.FON), and the key's value is the file name; not copied, the full path; a .TTF gets a .FOT of its name in SYSTEM (CreateScalableFontResource), whose name or path is the value; a copy also writes CONTROL.INI [installed] 3.1=yes. Then AddFontResource, the key appended, WM_FONTCHANGE broadcast, the new names selected. Remove empties the sample and asks per font ("Remove Font": Yes, Yes to All, No, Cancel, Delete Font File From Disk), then RemoveFontResource, the key removed, the file (and a .FOT's .TTF) deleted when asked; the caret moves to the font before. TrueType: WIN.INI [TrueType] TTEnable and TTOnly, both written on OK; only a TTEnable change asks to restart ("System Setting Change": Don't Restart Now, Restart Now = ExitWindows EW_RESTARTWINDOWS). w16's GDI has no TrueType rasterizer: the keys are kept as 3.1 keeps them and act only with a GDI that has one (Microsoft's). UNVERIFIED: dropped files (the dialog has WS_EX_ACCEPTFILES; w16 installs them where they are), compressed *.FO_/*.TT_ (listed by 3.1; skipped by w16), 3.1's message when AddFontResource fails.
- **Ports** (done; `ports.c`). Four owner-drawn buttons (the applet's icon centred, COLOR_HIGHLIGHT frame 2 pixels around the current port's), "COM1:" ... below; Settings for COMn: Baud Rate (editable: 110 ... 19200), Data Bits 4-8, Parity, Stop Bits 1/1.5/2, Flow Control; OK writes WIN.INI [ports] "COMn:=baud,parity,data,stop[,x|,p]" (e/o/n/m/s; x Xon / Xoff, p Hardware, nothing for None) and the Ports dialog's Cancel becomes Close; a port without a key shows 9600,n,8,1,x. Advanced (also in Standard mode): Base I/O Port Address (editable: Default, 03F8, 02F8, 03E8, 02E8, 02E0; the address shown as text) and Interrupt Request Line (Default, 15 ... 2): OK writes SYSTEM.INI [386Enh] COMnBase (the text, unless Default) and COMnIrq (a Default choice removes it), and asks to restart when they changed. UNVERIFIED: where 3.1 takes the default address and IRQ when SYSTEM.INI has none (w16: 3F8/4, 2F8/3, 3E8/4, 2E8/3).
- **International, Date/Time, Printers**: not yet.
- **Branding**: CONTROL.EXE's and MAIN.CPL's user-facing "Windows" reads "w16" ("Changes the w16 screen colors", "your w16 SYSTEM directory"); the scheme name "Windows Default" is kept, being a value CONTROL.INI stores and 3.1 matches (decision pending, the coordinator's).

### 14.4 Results

`tests/cplscenarios.sh` (32 scenarios; run on installation-05): CONTROL.EXE over Microsoft's, with MAIN.CPL and the CP1 probe in every configuration tried (11 applets; 14 with a very long name; 31, 51 with scroll bar, saved and changed geometry, [MMCPL], both command-line forms, the CPL_NEWINQUIRE variants): 0 pixels and identical INI files and CP1 logs. w16's MAIN.CPL (Color, Desktop, Keyboard, Mouse): identical INI files in every scenario; the screens differ only by w16's icon art behind the dialogs (and the rebranded status text), blinking (scroll bar thumbs, the blink sample) and a few rows of the selector's luminosity bar. `make boot-test` case `control` (parts ms, k286, k386, tree): w16's CONTROL.EXE under real keys against Microsoft's on all-Microsoft 3.1, on w16's KRNL286 and on w16's KRNL386, and the install tree alone under KRNL386 against KRNL286: every screen and CONTROL.INI identical. Program Manager's Main group has a Control Panel item; the install tree has CONTROL.EXE and MAIN.CPL. Repainting (2026-11-15, a report of "issues repainting at times" on the install tree): the panel sized smaller, scrolled, maximized/restored, applet dialogs moved over it and closed, Alt+Tab, minimized and restored (`tests/scripts/input/cprepaint.txt`) showed nothing wrong on the tree (KRNL386 and KRNL286 alike, w16's USER repainting resized windows whole), but on 3.1's USER, which keeps a resized window's bits, w16's CONTROL.EXE kept the old icon layout and the status box without its right edge after the panel was narrowed: the list and status classes now have CS_HREDRAW | CS_VREDRAW (UNVERIFIED whether 3.1's do, or it invalidates). Part `control@resize`: that script with w16's CONTROL.EXE against Microsoft's on all-Microsoft 3.1, every screen identical but for the minimized panel's icon. A second report (2026-11-16: the install tree under KRNL386, the Ocean scheme applied, the Color dialog closed) showed the panel's status line covered by the dialog's sample (Highlighted, Window Text, a piece of the scroll bar). Reproduced on the tree under both KERNELs whenever a scheme was picked from the drop-down list with the mouse, OK or Cancel alike, and with Microsoft's CONTROL.EXE and MAIN.CPL on w16's USER, but not with w16's on Microsoft's USER; picking with the keys showed nothing. **Cause** (USER, `paint.c`): finding nothing that shows with anything to paint, paint_message set the queue's paint count to 0 but left the hidden windows' WF_INPAINT flags (here the dropped list, invalidated by the selection and hidden before it painted); destroyed with the dialog, they took their counts from windows still waiting, and the status line, the panel's last, never got WM_PAINT. **Fix**: the hidden windows' flags are cleared with the count (they keep their update regions; showing a window invalidates it again), so the count stays one per window waiting to paint (Pietrek, *Windows Internals*, ch. 7). The same miscount made boot-test part `cplclick@co-tree` fail on 02-palette once master's tree had MAIN.CPL (three runs in a row): after Color Palette >> widened the dialog, the dialog painted its grids but none of its controls (Screen Element, the labels, Define Custom Colors, OK, Cancel, Help), whichever KERNEL; reproduced with the fix taken out, gone with it. Found on the way: a fresh build's tree had no MAIN.CPL (a merge had dropped the Makefile rule's recipe), so the parts `control@tree` and `cplclick@*-tree` had compared "Cannot find the Control Panel components" on both KERNELs. Boot-test case `cpclose` (`tests/scripts/input/cpclose.src`, expanded by `tools/inputgoto.py`): Color with a scheme picked from its list (OK, then Cancel), Color OK with nothing changed, Color from the Settings menu, Keyboard and Mouse (OK, Cancel), Desktop with a pattern picked and Cancel, then OK with none (w16's USER draws no desktop pattern or wallpaper yet: WIN.INI [Desktop] Pattern and Wallpaper are ignored), every screen after a close: w16's USER with Microsoft's applets against all-Microsoft 3.1 (@us), w16's USER against Microsoft's with w16's applets (@usw), w16's CONTROL.EXE against Microsoft's (@ctl), the tree under KRNL386 against KRNL286 (@tree). Found by it and fixed: 3.1's CONTROL.EXE draws the selected name on COLOR_ACTIVECAPTION in COLOR_CAPTIONTEXT, not the highlight colours (the same in the default scheme only); w16's USER leaked GDI's heap on 3.1's GDI: DeleteObject of an object USER had made private (MakeObjectPrivate: update regions, system brushes, save-bits images) returned TRUE and freed nothing (measured with GetHeapSpaces), so every update region stayed; w16's Desktop applet, whose blink sample is shown and hidden every 1.2 s, used up the heap in a few minutes, CreateRectRgn failed and USER hung (part usw). Such objects are now made public before DeleteObject (`init.c` delete_private). w16's USER painted a dithered desktop (Arizona's Background) with the checkerboard's phase changing from one repaint to the next (a cache DC came with another user's brush origin: the desktop's WM_ERASEBKGND now sets it to 0, as the class-brush erase did); w16's USER showed DRIVERS.CPL's and SND.CPL's icons from their 8-colour images where 3.1 takes the 16-colour ones (both 4-bit; `icon.c` pick() now prefers the most colours the display has). Not fixed, for USER's maintainers (seen with the Color dialog, kept out of the case): an owner-drawn combo box's dropped list prints its items' strings ("name=values") where 3.1 has the owner draw them; with a scheme other than the first selected, a click at the same place in the dropped list picks a different scheme on 3.1 (which appears to scroll the list to the selection) than on w16; on w16's USER with Microsoft's MAIN.CPL the Color dialog opened a second time by double click had no focus on its scheme box (Home did nothing), and on the install tree Alt+C there moved the focus to the dropped list's window.

### 14.5 Mouse input in the applets (2026-11-15)

A hand test of the install tree found the applets dead to the mouse: only OK and Cancel worked. Reproduced with real clicks (`tests/inputrun.sh`) on the tree under both KERNELs and on Microsoft's 3.1 with only w16's USER; with Microsoft's USER, w16's MAIN.CPL worked. **Cause** (USER): the dialog templates put each group box before the controls it frames, so it is above them in z-order; a group box answered WM_NCHITTEST with HTCLIENT and the mouse routing (`input.c` mouse_target) took the topmost window under the pointer whatever it answered, and the group box dropped the click. **Fix**: a group box answers HTTRANSPARENT, and a window of the reading task that answers HTTRANSPARENT passes the event on to the window under it (`win.c` window_below: the next visible sibling containing the point, descended into, else the parent), as the SDK describes. Found on the way, also USER: a focused scroll bar control shows a blinking gray caret over its thumb (2 pixels in; CreateCaret's (HBITMAP)1, a 50% pattern aligned with the screen); an owner-drawn drop-down list's selection box is drawn by the owner (WM_DRAWITEM, as observed on 3.1) instead of USER printing the item string; the Keyboard applet's bars page by a quarter of their range and the Mouse applet's double-click bar by 160 ms (observed).

Boot-test case `cplclick` (default set): every control of Keyboard, Mouse, Desktop and Color clicked with real input (`tests/scripts/input/cplclick-*.txt`, generated; WIN.INI MouseSpeed=0 and pointer moves in small steps, since larger or accelerated moves land differently on a loaded host; Windows exited at the end so KERNEL writes the INI files): w16's applets against Microsoft's on 3.1 (INI files identical) and the install tree under KRNL386 against KRNL286 (screens and INI files identical). `cplclickus` (run by name) compares w16's USER with Microsoft's under the same clicks and still fails on USER's remaining differences, for USER's maintainers: a scroll bar control taller than the arrows (w16 keeps 17-pixel arrows with black beside them, 3.1 draws them otherwise), the owner-drawn combo box's focus rectangle (drawn in 3.1's colours since US34, not rechecked here), and an item chosen with the arrow keys in an open drop-down list that a click on the box then closes (3.1 keeps the choice, w16 drops it). Since US34 (2026-11-21) Desktop's combo boxes are laid out as 3.1's (dk-us: 394 pixels differ, all in the Cursor Blink Rate box); each part writes its own WIN.INI (`cplclick-AP-WIN.INI`): the parts shared one, rewritten by each as the others copied it, and a part could boot with a WIN.INI of one line (no [fonts], every font the system's).

The Mouse applet under real input (a second hand test; observed on 3.1 with held buttons and double clicks, `cplclick-ms`): a button held down over the dialog inverts its box of the L/R picture (the right box for the right button, also when swapped; the labels swap with the buttons); a double click on TEST inverts it at the double-click speed the bar has just set; Swap Left/Right Buttons takes effect at once. Mouse Trails is the display driver's (Windows 3.1 DDK, MOUSETRAILS escape 39: the driver draws the trail and keeps WIN.INI MouseTrails); the applet asks QUERYESCSUPPORT for the state and calls the escape with -1 (on) or 0 (off). w16's VGA.DRV implements the escape and draws the trail as 3.1's VGA.DRV does (2026-11-15; observed with the probe VG9, `drivers/vga/README.md` "Mouse trails": 3.1's driver reads MouseTrails only at Enable, and -1 turns trails on at the driver's own size without reading WIN.INI, unlike the DDK's text; each MoveCursor adds the cursor's last position, each CheckCursor drops the oldest, drawing that meets the trail takes it off until the next tick); boot-test case `trails`, including these Mouse applet clicks against Microsoft's driver. A later hand test on the tree found the trail janky: under real motion a mouse interrupt during the timer's CheckCursor drawing kept only its last position (the stack's calls were the same as 3.1's, measured with the CURREC.DRV stand-in); the driver now queues positions and ticks, and draws an image plane by plane; w16's MOUSE.DRV lost the motion of packets that came while that call ran and now keeps it (parts `trails@timed`, `trails@timednl`). A still trail runs down an image every second tick (about 600 ms for seven): set from the user's hand comparison with 3.1 booted interactively, not measured by a probe (the probes see 3.1's driver drop one a tick); SYSTEM.INI [w16] MouseTrailDecay sets the ticks per image. Fixed in USER: SwapMouseButton now swaps the buttons of real input (it only set the flag); SPI_GETMOUSE and SPI_SETMOUSE are the acceleration USER applies (SPI_GETMOUSE answered 2, 0, 1 whatever was set and SPI_SETMOUSE was ignored: the tracking bar did nothing). Boot-test case `mousespeed`: a fixed motion of the pointer at acceleration 0 0 0 and 4 6 2 ends at the same place with w16's USER (and with w16's MOUSE.DRV as well) as on all-Microsoft 3.1, so USER applies 3.1's acceleration to what the driver reports. UNVERIFIED: DOSBox with the mouse not captured (autolock=false) was not measured; the tests capture it.

ZOI: no fixed limit on applets, applet DLLs or [MMCPL] entries (arrays grow); the Settings menu takes as many items as USER allows.

### 14.6 386 Enhanced (CPWIN386.CPL, 2026-11-21)

**Observed on 3.1** (`WIN /3`, its own WIN386; `tests/scripts/cpl/en*.txt`): Control Panel shows "386 Enhanced" only in 386 Enhanced mode, from SYSTEM\CPWIN386.CPL. Device Contention lists the serial ports the BIOS reports ("Com1", "Com2" in DOSBox) with Always Warn, Never Warn, Idle (in sec.) (2 when absent); Scheduling: Windows in Foreground and Background (WinTimeslice, 100,50), Exclusive in Foreground (WinExclusive), Minimum Timeslice (MinTimeslice, 20). OK checks the minimum timeslice first ("Invalid minimum timeslice value; it must be a value between 1 and 10000."), then the foreground and background values ("Invalid foreground value.", "Invalid background value.") in "Control Panel - 386 Enhanced" boxes, then writes MinTimeslice, WinTimeslice=fg,bg, WinExclusive (0/1) and ComNAutoAssign for every listed port (0 Never Warn, -1 Always Warn, else the seconds), changed or not, and closes without a restart prompt. Virtual Memory: Current Settings (drive, size, "Temporary (using MS-DOS)"); Change>> widens the dialog to New Settings (drive with a picture, type Permanent/Temporary/None, Space Available, Recommended Maximum Size, New Size) and removes the disabled Use 32-Bit Disk Access; OK after Change asks "Are you sure you want to make changes to virtual-memory settings?"; Yes writes [386Enh]: a temporary file's new size as MaxPagingFileSize (no restart), None as Paging=0 with PagingFile and MaxPagingFileSize removed, followed by a restart dialog (Restart Windows, Continue) over the Virtual Memory dialog, which closes after it; None keeps the temporary file's figures. A permanent file on DOSBox's drive is refused ("A permanent swap file has been found on a drive where one cannot be created ..."), so what 3.1 writes for one was not seen.

**w16** (`shell/cpl/win386`): the same dialogs (layouts, IDs and styles from the window dumps; the cpArrow class shared with MAIN.CPL, `shell/cpl/arrow.c`; the icon w16's own, `tools/cplicons.py`: a chip marked "32", where 3.1's, as seen on screen, is a chip marked "386"). Differences: the swap file's figures (3.1 takes them from its WIN386, presumably the PageFile device's API, not observed; w16 computes Space Available as the drive's free space plus the current temporary file, the recommended maximum as half of it at most MaxPagingFileSize: 61,440 KB where 3.1 shows 59,376); a permanent file is written as PermSwapDOSDrive/PermSwapSizeK (the keys 3.1's Setup documents) without making 386SPART.PAR. **What w16's WIN386 honours** (`win386/ldr` reads them, `vm.c` schedules): WinTimeslice is the System VM's foreground and background priority, MinTimeslice scales every VM's slice (priority / 50 ticks at 20 ms; w16's slices at the defaults are what they were), WinExclusive keeps the other VMs from running while Windows has the focus; all three at Windows' start (UNVERIFIED whether 3.1's applet changes the running VMM at once: it asks for no restart; w16's does not). ComNAutoAssign, COMnBase/COMnIrq and the swap file keys are saved only: w16's WIN386 has no virtual COM device (VCD) and does no paging yet (12.4).

**Results** (`tests/cplscenarios.sh`, installation-05, 2026-11-21): the scenarios `fnsample`, `fnadd`, `fnaddnc`, `fnremove`, `fntt`, `fnttonly` (Fonts), `ptset`, `ptadv` (Ports), `en386`, `envalid`, `envm` (386 Enhanced in `WIN /3` on 3.1's WIN386) and `en386w`, `envmw` (the w16 run on w16's WIN386 as well): CONTROL.INI, WIN.INI and SYSTEM.INI byte-identical to 3.1's in all 13; every Fonts sample (TrueType, raster, vector), the TrueType dialog, the Remove dialog and the 386 Enhanced dialog and its error boxes 0 pixels; what differs is w16's icon art (the panel, the Ports buttons), "w16" for "Windows" in the restart prompts and the swap file's figures (`tests/scripts/cpl.baseline`).

## 15. Applets

### 15.1 Notepad (`shell/notepad/`, probes NP1-NP9)

w16's `NOTEPAD.EXE` (`build/dos/NOTEPAD.EXE`) is a 1:1 replacement for Windows 3.1's Notepad, for Windows 3.1 only (COMMDLG, WinHelp, ShellAbout, drag and drop). `make install-tree` puts it in the Windows directory. Everything below was observed black-box on the reference 3.1 (SNAP scripts with the `info` and `text` commands added for this work; the original's resources read as data with `tools/nerdump.py`); nothing was disassembled.

**Window.** Class `Notepad` (CS_BYTEALIGNCLIENT, COLOR_WINDOW+1, no extra bytes), WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL at CW_USEDEFAULT, WS_EX_ACCEPTFILES. The text is an edit control, ID 15, WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL (+ ES_AUTOHSCROLL without Word Wrap), no scroll bars of its own, Fixedsys (the stock fixed font), at (8, 2) in the client area, 15 pixels narrower and 4 shorter than it. The scroll bars are the frame's, ranges 0..100; their positions are what the edit control answers to WM_VSCROLL / WM_HSCROLL with the code EM_GETTHUMB (WM_USER+14, "internal" in the 3.1 DDK's WINDOWS.H per Schulman, *Undocumented Windows* ch. 7): equal at every step observed (PgDn, PgUp, End on long lines; vertically the value is round(first visible line × 100 / lines)). They are taken when the edit control reports a scroll (EN_HSCROLL/EN_VSCROLL, which 3.1 sends for scrolling by keys and typing too), not after every change: typing on past the last horizontal scroll leaves the original's bar at 60 while the edit control by then says 59 (found with real typing through xdotool; scripted SNAP input showed it too once looked for). EM_GETTHUMB sent as a message returns 0. w16's USER answers that scroll code (`user/edit.c`), so Microsoft's Notepad's bars move on w16's USER too (boot-test case `sbdrag`). Word Wrap re-creates the edit control (selection back to the start) and hides the horizontal bar (range 0..0). Title `Notepad - (Untitled)` or `Notepad - ` + the file-name part in capitals (`c:\small.txt` gives `SMALL.TXT`).

**Menus and keys** are the original's resource (IDs included): Edit's Undo/Cut/Copy/Delete/Paste and File's Print (grayed without a `device=` printer) and the Word Wrap check are all updated whenever any menu opens. Accelerators (`MAINACC`): Ctrl+Ins, F1 (Help Contents), F3, F5, Alt+Backspace, ^Z ^X ^C ^V. The original also carries an unused-looking `SLIPUPACC` table and its pre-COMMDLG File Open/Save As/Find templates.

**[w16] NotepadAccelerators** (SYSTEM.INI; read at Notepad's start-up; **default 1**; **0 is stock Windows 3.1**): a compatible enhancement (6.8), the keys users expect from later Windows' Notepad. With 1 the window gets w16's menu `IDM_MENU_W16` (resource 2; passed to CreateWindow, the class keeps the original's menu 1) and accelerator table `W16ACC`: the original's entries unchanged, plus File New Ctrl+N, Open Ctrl+O, Save Ctrl+S, Print Ctrl+P, Edit Select All Ctrl+A, Search Find Ctrl+F (VIRTKEY CONTROL, so only on the key-down: no control character reaches the edit control, which gives Ctrl+letters no meaning of its own), each shown in its menu text after a tab as 3.1 shows Undo's Ctrl+Z, and Help Contents shows `F1`, which 3.1's table already has but its menu does not show. No key for Save As (later Notepads had none until Windows 11's Ctrl+Shift+S), Page Setup, Print Setup (none in any Notepad), Exit (Alt+F4, the system menu's Close), Word Wrap (later Notepads have none; Ctrl+W is "close" in many programs), Search for Help on, How to Use Help or About. Ctrl+P while Print is grayed does nothing (TranslateAccelerator sends no WM_COMMAND for a grayed item); Print's state is set when a menu opens, so before that, with no printer, Ctrl+P gives the "Cannot print" message, as `/P` does. With `NotepadAccelerators=0` Notepad loads the original's menu and `MAINACC` and nothing else differs. Not yet covered by a test of its own.

**Files.** A file is read whole; NUL bytes show as blanks; saving writes the text exactly (no newline added, no Word Wrap breaks). Untitled Save goes to Save As: COMMDLG, title `Save As`/`Open`, filters `Text Files (*.TXT)` `*.txt` / `All Files (*.*)`, Read Only and Help hidden, COMMDLG's own "already exists ... Replace existing file?". Messages (MB_ICONEXCLAMATION, caption `Notepad`; the original's strings, `%%` its placeholder): changed text (Yes/No/Cancel; untitled reads `(Untitled)`), "Cannot find the %% file. Do you want to create a new file?" for a command-line file (Yes/No; Yes makes it empty; even a name DOS cannot create, `C:\A*B.TXT`, becomes the title), "too large", "You have not entered any text to be saved" (untitled, empty), "%%\nThis file is empty and will be deleted..." (named, empty; OK deletes the file). "Too large" and "Cannot find" appear before the window shows. `/P file`: the window shows the file, prints, exits; with no printer "Cannot print the %% file..." first.

**The size limit** is USER's edit control running out of room in Notepad's data segment (the edit window's instance is Notepad's): with 66-byte lines 51190 bytes open and 51200 do not, with 1002-byte lines 52000 open, with 10-byte lines 40000 do not; typing past the load limit works. w16's Notepad gives the edit control a 64K segment of its own (GlobalAlloc + LocalInit, its selector as the edit window's instance), so it opens every file the original does and more (a 60000-byte file opens); files of 64K or more are "too large". Typing and pasting past USER's default 30000-character limit work in the original (a 40000-byte paste; typing into a 51190-byte file), so w16's Notepad lifts the limit with EM_LIMITTEXT 0 (on 3.1: no limit but the segment; a 60000-byte file takes more typing).

**.LOG**: a file whose text starts `.LOG` gets CR LF, the time and date, CR LF appended when opened (caret after it, text marked changed). **Time/Date** (F5, also .LOG; DOSBox-X with TIME set, [intl] varied): hour (leading zero only with iTLZero; 24-hour with iTime=1, 12 for 0 and 12 otherwise), sTime, two-digit minutes, then s1159/s2359 **even with iTime=1** (`21:05PM`); two blanks; month, day, two-digit year without leading zeros in iDate's order (0 MDY, 1 DMY, 2 YMD) joined by sDate; sShortDate is not used. Examples: `9:05AM  11/8/26`, `09.05am  11/8/26`, `21:05PM  8-11-26`, `12:05AM  11/8/26`, `01:07  11/8/26`.

**Find** is COMMDLG's modeless FindText: Whole Word and Help hidden, Down. Search from the selection's end (Up: before its start); none: "Cannot find "%%"" with MB_ICONINFORMATION. **Page Setup** is Notepad's own dialog (template 14, font Helv): Header `&f`, Footer `Page &p`, margins .75 .75 1 1; a non-number gives the "Margin values are not correct" message; nothing goes to WIN.INI. **Print Setup**: COMMDLG PrintDlg(PD_PRINTSETUP) ("No default printer" is COMMDLG's). **About**: SHELL's ShellAbout(`Notepad`) (the text and branding are SHELL.DLL's). **Help** (stand-in WINHELP, tests/apps/thelp.c): Contents and F1 HELP_INDEX `notepad.hlp`, Search HELP_PARTIALKEY "", How to Use Help HELP_HELPONHELP with no file, HELP_QUIT on exit; F1 in menus or dialogs asks nothing.

**Tests**: `tests/npscenarios.sh` runs nine scenarios (`tests/scripts/np/`: menus NP1, dialogs NP2, files NP3, logfile NP4, wrap NP5, find NP6, limits NP7, help NP8, scroll NP9) with the original and with w16's Notepad on the reference 3.1 and compares screenshots (baseline `tests/scripts/np.baseline`: the About box's icon), saved files byte for byte, SNAP's logged window state, and WinHelp requests. Both runs get a WIN.INI whose caret blinks once a minute, so the caret's state is the same in every screenshot.

**UNVERIFIED** (marked in the code): the printed page (no printer driver in the reference copy: font, header/footer placement, default alignment centred, long lines cut), which margins the original accepts beyond plain numbers, F3 with nothing searched yet (opens Find), the year's digits before 2010, what `SLIPUPACC` is for, the original's handling of the Find dialog during Windows exit (string 48).

### 15.2 Clock (`shell/clock/`, CLOCK.EXE; scenarios CK1–CK16)

w16's Clock matches Windows 3.1's (`make shell` → `build/dos/CLOCK.EXE`; `make install-tree` puts it in the Windows directory). It is a Windows 3.1 program only. Everything below was observed black-box on the reference install: screenshots and window/menu dumps (SNAP), the CLOCK.INI it writes, its resources read as data (`tools/neres.py`, now master's: the Settings menu and its IDs, the ChooseFont template, the string table), and the GDI calls it makes (`tests/refrun.sh` with `REFRUN_GDLOG=1` puts GD2 in front of GDI; `tools/gdcalls.py` prints one program's calls without handles, so ours and the original's can be diffed).

**Observation tools added** (`tests/apps/snap.c`): `settime`/`setdate`, `freeze hh:mm:ss ms` (re-sets the DOS time every 50 ms, so a clock shows a known second; needs DOSBox-X, `REFRUN_X=1`, whose DOS sets the time; DOSBox 0.74 ignores INT 21h AH=2Dh), `syscmd`, `movewin`, `post`/`send` (logs the result), `dumpmenu`, `timers`/`freetimers`.

**Observed behaviour** (code comments in `shell/clock/*.c` give the details):
- Window class "Clock", WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS, no class icon (it paints its own 36×36 icon: the face or "hh:mm", inside a black/red/black frame). No title: WS_BORDER | WS_THICKFRAME, no menu, same window rectangle; double-click switches; WM_NCHITTEST answers HTCAPTION for the client area. First start: digital, at 0,0,208,226 (a 200×180 client area). A second Clock activates the first.
- Settings: Analog, Digital (checked), Set Font... (grayed, MF_GRAYED|MF_DISABLED, in analog), No Title, Seconds, Date, About Clock... (ShellAbout). The system menu gains "Always on Top" (command 5; WS_EX_TOPMOST). No Help.
- CLOCK.INI `[Clock]`, written on exit: `Maximized=`, `Options=analog,minimized,noseconds,notitle,topmost,nodate`, `Position=l,t,r,b` (restored rectangle); `sFont=` when a font is chosen. Ours writes byte-identical files in every scenario.
- Colours: face COLOR_BTNFACE, dots' shadow COLOR_BTNSHADOW, hands COLOR_WINDOWTEXT, digits COLOR_BTNTEXT, embossed digits COLOR_BTNHIGHLIGHT/BTNSHADOW/BTNFACE; teal marks, cyan highlights, the icon's red ring and black are fixed.
- Analog geometry: m = min(client width, height); mark size s = MulDiv(25, m, 640) (2 at least across); radius m/2 − s/2 − 4; centre (cx − m)/2 + m/2 − 1; hour and minute hands are four lines (7/65/15 and 5/80/20 % of the radius: half-width, tip, tail), erased with the face pen and redrawn when the minute changes; the second hand is an R2_NOT line (80 %); the 48 minute dots only when s > 2.
- The circle table (positions of marks, dots and hands): 60 points on a circle of radius 8000 at 6° steps, computed by `tools/clocktable.py` (sin/cos, rounded) into `build/shell/clock/circle.h`. Fifteen entries are corrected by observation: in the original's paint of the face (GD2 log at 10:09:30, every CK2 window size) the MulDiv calls placing the marks and dots pass values one smaller in magnitude than rounding gives — y of 0 (−7999), 11, 19, 30, 40, 41, 49 and x of 4, 5, 25, 26, 34, 45, 55, 56 (±3253, ±3999, ±7999). `tools/clocktable.py --check CALLS` compares the table with such a log (0 differences); the CK screenshots are pixel-identical with it.
- Digital layout: fonts sized from "88:88:88 MM" measured at height −1000 (Arial unless sFont): width 7/8 of the client, height at most 3/8 of it (7/8 without the date), −2 until it fits; the date 3/4 of that at most; embossed from height 54. Only changed parts are redrawn. A one-digit hour is drawn right-aligned in its place without clearing the rest (as the original does).
- WIN.INI [intl]: iTime, iTLZero, s1159/s2359 (shown in 24-hour mode too), sTime, sShortDate (its own separators; sDate unused). The minimized title drops the year.
- No timer left: "Not enough timing resources for Clock. Close other applications and try again." in a system-modal box titled "Clock" (the hard-error style, MB_ICONHAND | MB_SYSTEMMODAL), and Clock does not start. SNAP can't capture it (the box locks every other task, Schulman ch. 5 LockCurrentTask), so CK16 takes the whole screen under xvfb.

**Results** (`tests/clockscenarios.sh`, CK1–CK16 on the reference 3.1 at frozen times; `tests/scripts/clock.baseline`): every screenshot pixel-identical to the original's, CLOCK.INI byte-identical, window and menu dumps identical, except the About box (our icon art; the free-memory figure varies by run) and a caret blink in the Font dialog. Under w16's GDI the digital face would differ (no TrueType yet; compared here on Microsoft's GDI).

**Real input** (`tests/clockinput.sh`, `tests/scripts/input/clock.txt` through the USER work's `tests/inputrun.sh`: xdotool into DOSBox on Xvfb, Clock as the shell): double-click to no title, drag by the face, double-click back, Settings by keyboard, Always on Top and minimize/restore from the system menu behave as the original's, and CLOCK.INI (moved position, analog, on top) is byte-identical; the screenshots differ only in the time shown (DOSBox 0.74 cannot hold it).

UNVERIFIED (marked in the code): the 60 s icon timer; how the original activates its first instance; the exact emboss threshold (53 flat, 54 embossed); dots at s > 2 vs m ≥ 64 (the same at 640×480); the default size formula off VGA; AM/PM-less measure strings; whether 640 in the mark size is HORZRES.

### 15.3 Calculator (`shell/calc/`, CALC.EXE)

w16's Calculator is a 1:1 replacement for Windows 3.1's (`make shell` builds `build/dos/CALC.EXE`; `make install-tree` puts it in `WINDOWS\CALC.EXE`). It is a Windows 3.1 program only. Every behaviour was observed black-box: CA1 (`tests/apps/ca1.c`) runs as the shell, starts Calculator, sends button commands, keys, clicks and pastes, and logs both displays, the clipboard after Edit > Copy, the indicators and the display after the Copy; the resources (menu, dialogs, accelerators, strings) were read as data (`tools/resdump.py`), the help text with `tools/hlptext.py`.

**Where to observe.** Plain DOSBox's coprocessor emulation computes in double precision, so typed numbers, formatting and functions differ in the last bits (0.01 showed as `0.00` there). DOSBox-X with a coprocessor and DOSBox-X without one (Microsoft's emulator in WIN87EM) agree; `tests/calcscenarios.sh` uses DOSBox-X.

**Structure.** `engine.c` (keys and state), `fmt.c` (display and typed-number parsing), `paste.c`, `fpu.c` (x87 helpers in extended precision, emulator fixups, WIN87EM initialised with `__fpMath` 0 and every exception masked) are portable: `tools/calchost` builds them for the host (GCC, x87 `long double`) and replays CA1 scripts; `tools/calccmp.py` compares logs. `main.c`, `draw.c`, `stat.c`, `clip.c`, `help.c` are the Windows side.

**Observed rules (the details are in the sources' comments):**
- Window: the dialog template `SC` of class `SciCalc` (caption, system menu, minimize box; system font), class menu `SM`; controls are only the radio buttons, Inv/Hyp, and statics 413 (scientific display), 414 (standard display), 400 `stat`, 401/402 ` M`, 403 `(=n`, plus a hidden Edit 495 that Copy goes through (the original imports no `SetClipboardData`). Calculator draws the keys itself (RoundRect with a 10 x 18 ellipse, outlined in red, blue, magenta, dark cyan or black; labels from the string table, in the outline's colour for digits, memory keys and C/CE/Back, black otherwise). The Statistics Box has no owner (it sits at its template position on the screen); the focus stays on the window itself; Paste is grayed without CF_TEXT on the clipboard. Client 480x297 scientific, 259x229 standard. WIN.INI `[SciCalc] layout=` 0 scientific, 1 standard (default).
- Display: 13 significant digits; fixed notation when -13 < (int)log10|x| < 13 (the logarithm rounded to double, then chopped), otherwise `d.ddde+NNN`, exponent 0 as `e+0`; fixed results keep the point (`123.`) except 13-digit integers; when rounding below 0.01 carries into a new digit the original drops it (0.0099999999999999 shows `0.00`). A typed number shows its value plus one `0` when the point and then a 0 were typed (`1.50`). In the standard view a text wider than the display is not shown and is an error state with the old text kept (`2/3/100000000=`). Hex/octal/binary show the low 32/16/8 bits unsigned.
- Typed numbers: integer digits x = x*10 + d in double; fraction digits add d*s, s = 0.1, 0.01, ... in extended precision, rounded to double each time (257 probed values agree). Exp: mantissa (1 if 0), exponent up to 307.
- Operators: standard view left to right; scientific x^y > * / Mod Lsh > + - > And > Or Xor; a new operator evaluates the pending ones only if its precedence is not above any of them. `=` with nothing entered uses the level's first operand (`2+=` is 4); repeated `=` repeats the last operation until a number is typed. An operator right after another replaces it. Copy shows the current number afterwards (0 after an operator).
- Functions: degrees and grads convert with 57.2957795130823 and 63.6619772367581 (sin 180 deg is -1.209806994168e-015 exactly so); the tangent/sine reduction behaves like the 387's 66-bit pi; cos below 2^-48 is 0 (cos 90 deg = 0, tan 90 deg undefined); n! only for integers up to 170; dms and Inv dms; Lsh takes the count's low byte, Inv Lsh is arithmetic; Back drops a typed fraction digit, otherwise divides by the radix and chops.
- Errors: `Cannot divide by zero.`, `Invalid value for this function.`, `Result of function is undefined.`, `Result is too large to store.`; then only C and CE work. Hex values beyond 32 bits are too large; a ninth hex digit is too large whatever the word size.
- Statistics Box: Dat adds the display's text; s is 0 for fewer than two values and `Invalid value` when the variance is not above 0.
- Paste: the whole CF_TEXT block (GlobalSize bytes, NULs skipped), each character as a key; `:` and a digit n is Fn, `:c :m :p :r` MC MS M+ MR, `:e` Exp (decimal scientific), `:q` nothing, `\` Dat; the standard view ignores keys it has no button for.
- Help: calc.hlp; F1 and Contents HELP_INDEX, Search HELP_PARTIALKEY "", How to Use Help HELP_HELPONHELP without a file name, nothing for F1 on menus, HELP_QUIT on exit.

**Not matched (UNVERIFIED or known):** `exp` is about 1.7e-15*x relative above the true value in the original (e^709 shows `...565`, ours `...555`); a few last-bit differences of sin, cos, tan near multiples of pi and of the rounding of numbers within 1e-13 of a power of ten; parentheses after a typed number (`2+3*4(5=`); the standard display's width limit between 176 and 184 pixels; the grad constant; the key flash on mouse clicks. The About text is w16's own (the original names its developer).

**Microsoft's MIT-licensed Calculator** (https://github.com/microsoft/calculator, commit 79e1ac5, `src/CalcManager/CEngine`) was read as evidence, every point checked on 3.1 with CA1 (`tests/scripts/calcca/inv-hyp-dms.txt`, `parse-int.txt`, `mod-ce-depth.txt`). Its scifunc.cpp still carries "SCICALC Scientific Calculator for Windows 3.00.12, (c)1989", and scicomm.cpp keeps the state machine's names (m_bRecord, m_bChangeOp, m_nLastCom, a precedence stack and paren arrays of 25, MAXPRECDEPTH). Shared with 3.1: the precedence levels (Or Xor < And < + - < * / Mod Lsh < x^y); a function right after an operator works on the operator's left operand ("5+=" is 10); Back on a finished number divides by the radix and chops (its comment says so; its code now only edits typed input); Not in decimal is -(Int x + 1); Inv Lsh keeps the sign bit; x^y's Inv is a root. The dms order of operations (minutes, then seconds, then (m + s/100)/100) gives 3.1's last bits, so engine.c adapts it (Microsoft's notice and the MIT text: `shell/calc/LICENSE-mscalc.txt`). Not shared (Win95 and later; 3.1 measured otherwise): when pending operators are evaluated (the modern engine evaluates down to the first higher-precedence one, 3.1 all or nothing, so "2+3*4*" shows 4), `=` closing every parenthesis, `(` after a number as an implied multiplication (3.1 appends digits), CE in an error state acting as C (3.1 keeps the pending "2+"), Mod in decimal (3.1 takes the dividend's sign, fmod), % (3.1: first operand * x / 100 for every operator), Lsh beyond the word size (3.1: 0, no error), Programmer mode masking the value itself (3.1 masks only the display), n! of non-integers, Ratpack's arbitrary precision and display, the expression-parsing paste (3.1's key codes are not in it) and the missing Statistics Box. Found from it and confirmed: Inv is used up only by the functions that have an Inv meaning (sin cos tan ln log x^2 x^3 Int dms PI, x^y and Lsh when done); after n!, 1/x, +/-, Not, sqrt and % it is still set; CE clears it; Hyp goes only with sin, cos, tan.

**Real input** (`tests/inputrun.sh tests/scripts/input/calc.txt OUT -f build/dos/CALC.EXE=WINDOWS/CALC.EXE`; add `-r` for Microsoft's KERNEL): with Microsoft's KERNEL, Calculator started from Program Manager's Run dialog and driven by keys behaves like the original. With w16's KERNEL its window is created (SNAP dump) but stays behind Program Manager, while the original comes to the front; started by `load=`, as the shell or by SNAP's WinExec it is fine. UNVERIFIED: presumably the order in which the new task and Program Manager's closing Run dialog activate their windows.

**Tests:** `tests/calcscenarios.sh` (screens: SNAP screenshots of both calculators, pixel counts against `tests/scripts/calc.baseline`; results: CA1 with `tests/scripts/calcca/*.txt` and `tools/calcgen.py`'s random scripts; help: the WinHelp requests).

### 15.4 Cardfile (`shell/cardfile/`, CARDFILE.EXE; probes CF1-CF14)

w16's Cardfile is a 1:1 replacement for Windows 3.1's (`make shell` builds `build/dos/CARDFILE.EXE`, marked 3.10 as the original is; `make install-tree` puts it in the Windows directory). It is a Windows 3.1 program only (no 2.x build: those are for Program Manager alone). Everything below was observed black-box on the reference install: SNAP scripts (`tests/cfrun.sh SCRIPT OUTDIR`, `CFRUN_OURS=1` for ours; the probes in `tests/scripts/cfprobe/`), the card files it writes, its resources read as data (`tools/nerc.py`), the stand-in WINHELP. The file format is in `docs/crd-format.md`; `tools/crddump.py` prints a card file's structure.

**Window.** Class `Cardfile` (100Bh: CS_BYTEALIGNCLIENT, CS_DBLCLKS, CS_HREDRAW, CS_VREDRAW), WS_OVERLAPPEDWINDOW, WS_EX_ACCEPTFILES, at 4,0 sized 430 x 323. Children in Z order: a ListBox (ID 204, LBS_NOTIFY, List view), the `Card` window (205, same class style, WS_CLIPCHILDREN) holding the card's Edit control (200, ES_MULTILINE | ES_NOHIDESEL, WS_EX_ACCEPTFILES, Fixedsys), a centred Static with "Card View"/"List View" (201), a centred Static with the count (202: " 1 Card", else the number, a blank and " Cards"), between them a ScrollBar of arrows only (203, WS_HSCROLL style, 36 pixels) that turns the cards; a line under them. The Card window and the list are a pixel larger than the area below the line; in List view the window shrinks to the list's whole lines (3 pixels at the start size) and stays so.

**The stack.** A card is 40 cx + 3 by 12 cy + 5 (Fixedsys cx 8, cy 15): border, index line, a double line, eleven 40-character lines (the Edit control, which does not scroll, so the card's size bounds its text), border. The front card sits cx from the left and cy/2 above the bottom; each card behind is 2 cx right and cy + 1 up, in index order after the front card, with its index text but no lines; cards are drawn while any part can show. Pictures are drawn over the text, the caret over the picture. Keys: PgDn/PgUp turn, Ctrl+Home/End first and last, Ctrl+Shift+letter the next card whose index starts with it (also in List view); a click on a card behind brings it to the front. In Picture mode the arrow keys move the picture a character cell (8 or 15 pixels) at a time, and a drag moves it only when it starts on the picture.

**Menus, dialogs, messages** are the original's resources (IDs, templates in dialog units with the Helv font, strings with `%%` as the placeholder), "w16" for "Windows" in two messages. States when a menu opens (observed): Cut/Copy need a selection (a picture in Picture mode), Paste text on the clipboard (a picture), Find Next a previous search, Print a non-empty card (UNVERIFIED rule; Print All always), Insert Object Picture mode, Paste Special something on the clipboard, Link a linked object, Object the object's verb ("Edit Paintbrush Picture Object" for an embedded Paintbrush picture). F1 in a menu asks for no help (the original imports no hook functions). The index line is at most 40 characters and must fit the box. Index writes the line and its NUL into the card's own 40-byte field and leaves the rest; Add starts from a copy of the front card's field; Duplicate copies it, the copy going behind the card (all observed in the saved files). Delete asks `Delete "%%"?` (OK/Cancel); the save prompt names the file in capitals ("(UNTITLED)"). Go To finds the next card whose index line contains the text (UNVERIFIED: substring); not found, Go To and Find say `Cannot find "zzz".` (the text quoted, MB_ICONEXCLAMATION). Find is COMMDLG's FindText with the original's template (Whole Word placed outside, Help hidden).

**Files.** Open, Save As and Merge are COMMDLG with the types "Card File (*.CRD)" and "3.0 Card File (*.CRD)", "*.crd" in the current directory; Save As has COMMDLG's read-only box renamed "Validate" (checked; UNVERIFIED what it does) and remembers its type for the session; the 3.0 type always asks first (string 521, Yes/No). Without pictures a file is written as MGC, with one as RRG. Bodies stay in the open file or a scratch file (GetTempFileName); only the front card is in memory, so the number of cards is bounded only by the format's count (a WORD, 65535) and memory.

**Pictures and OLE** (OLECLI, Microsoft's for now): every picture is an OLE object: a pasted bitmap a static `BITMAP` object, a 3.0 file's bitmap too when saved as 3.1 (numbered from P + 1 in card order, P the file's pictures: one picture object 2, two objects 3 and 4; UNVERIFIED why), a 3.0 file placing pictures in eighths of a character cell (15 pixels down is 8), objects from Insert Object (the registered StdFileEditing classes: Package, Paintbrush Picture, Sound; the focus on the list) embedded: OleCreate, OleSetHostNames("Cardfile", the file's name; the server then shows "Paintbrush Picture in (Untitled)"), the object made to fit the card body keeping its shape (640 x 442 became 237 x 164). OLECLI hands Cardfile's stream blocks above 64K (Paintbrush's native data), read and written a piece at a time. Saved as 3.0, an object becomes a monochrome bitmap a pixel wider and higher than its rectangle; the original leaves that column and row undrawn (uninitialised), so `tools/crdcmp.py` ignores those bits. The OLECLI calls are listed in `shell/cardfile/ole.c` for w16's OLECLI (16).

**Autodial**: the number from the selection, else from the first digit or "(" on the card, blanks dropped ("Ann (555) 123-4567 home" gives "(555)123-4567"); Setup >> opens the rest (focus to the dial type) each time anew; only the checked radio button of a group keeps WS_TABSTOP; on OK it writes WIN.INI [windows] Modem=COM1,T,2 (port, T/P, baud button index: UNVERIFIED beyond 1200), Prefix=, UsePrefix=, sends ATDT/ATDP to the port and says "Pick up the phone."

**Help** (`tests/cardhelp.sh`, stand-in WINHELP): cardfile.hlp; F1 and Contents HELP_INDEX, Search HELP_PARTIALKEY "", How to Use Help HELP_HELPONHELP; nothing for F1 in menus or the Find box's (hidden) Help. **About**: SHELL's ShellAbout("Cardfile") with our icon.

**Printing** (UNVERIFIED: no printer driver in the reference copy, so only the messages were seen: "Cannot print." with none): cards one under another as on screen at the printer's resolution, Page Setup's header and footer (`&f`, `Page &p`, codes `&f &p &d &t &l &c &r`) and margins (.75 .75 1.00 1.00 inches).

**Tests.** `tests/cardscenarios.sh` runs the scenarios in `tests/scripts/cf/` (basic, files, stack, dialogs, picture, object, convert, merge, autodial) with the original and with ours on the reference 3.1 at the same time; every saved card file must be identical (crdcmp), WIN.INI too, and screenshots may differ only as `tests/scripts/cf.baseline` allows (the About box's icon, and Paintbrush's window, which opens maximized for ours and normal for the original: UNVERIFIED why). `CF_COMMDLG=w16` runs ours on w16's COMMDLG.DLL (17; baseline `tests/scripts/cf-commdlg.baseline`): the dialogs match but for COMMDLG's own icon art (files and convert once failed after a Save As type change: w16's COMMDLG left the File Name box unselected; fixed, 17). Carets are kept from blinking (SNAP `blink 60000`). The boot-test case `cardfile` plays real keys into both Cardfiles on all-Microsoft 3.1 (`tests/scripts/input/cardfile.txt`): every screen identical. `tests/cardhelp.sh` compares the WinHelp requests.

### 15.5 Task List (`shell/taskman/`, TASKMAN.EXE; probes TM1-TM4)

w16's TASKMAN.EXE replaces Windows 3.1's Task List (`make shell` builds `build/dos/TASKMAN.EXE`, marked 3.10; the install tree has it in the Windows directory, as 3.1). Observed black-box on Windows 3.1: screens; **TM1** (`tests/apps/tm1.c`) put in as SYSTEM.INI [boot] `taskman.exe=TM1.EXE` logs its command line, nCmdShow, the pointer and every top-level window; **TM2** (`tm2.c`) makes top-level windows of every kind (hidden, owned, untitled, a popup without caption, disabled, minimised, one with a modal popup, one that ignores WM_CLOSE) and logs the messages they get and, each second, the windows' places; Microsoft's TASKMAN's calls into USER and KERNEL recorded by USTRACE forwarders (US1's `tools/mkinterpose.py`, its imports renamed by `tools/nerename.py` in a scratch copy, values only); **TM3** (`tm3.c`, run as the shell) calls TileChildWindows, CascadeChildWindows and ArrangeIconicWindows on the desktop and logs the windows' rectangles. Microsoft's TASKMAN was never disassembled.

**Start.** USER reads `[boot] taskman.exe=` (default `taskman.exe`; Pietrek ch. 2) and runs it with WinExec, SW_SHOWNORMAL, command line `X Y`: the screen point of a double-click on the desktop, `0 0` for Ctrl+Esc and the system menu's Switch To; WINOLDAP's Ctrl+Esc from a full-screen DOS program runs the same with `0 0`. Setup does not write `taskman.exe=` (it is not in 3.1's SYSTEM.INI, nor in w16's). Ctrl+Esc reaches the active window as WM_SYSCOMMAND SC_TASKLIST, from any focus window; in the Task List itself it does nothing (its dialog procedure ignores SC_TASKLIST; UNVERIFIED that 3.1's does it so). There is no single-instance check: the Task List ends whenever it loses activation, so only one is up.

**The dialog** is TASKMAN's template 10 (`taskman.rc`: 160 x 107 units, Helv 8, "Task List"; a list box 100, Switch To 1, End Task 101, Cancel 2, a black line 99, Cascade 102, Tile 103, Arrange Icons 104), a modal dialog without owner, 290 x 203 pixels. WM_INITDIALOG: for `0 0` (or no numbers) it is centred, ((screen - size) / 2: 175,138); otherwise its corner goes to (X - 101, Y - 40), kept on the screen (X 300, Y 200: 199,160; 600,100: 350,60; 1,1: 0,0). UNVERIFIED how 3.1 gets 101 and 40 (only GetWindowRect and the screen size are asked; the dialog is made at 48,12). The numbers are read a character at a time up to a blank, each taken as a digit ("300,200" put it at 0,0). Then the list: the desktop's children in Z order from the top, less the Task List, the hidden ones, the owned ones (icon titles, dialogs) and those whose GetWindowText is empty (WINOLDAP's window: a DOS program swapped out is not listed in Standard mode), each its title with its handle as item data, unsorted; the first selected; SetWindowPos HWND_TOPMOST; the focus in the list.

**Buttons.** Switch To (Enter, a double-click on an item): EndDialog, then the item's GetLastActivePopup, if enabled, gets SwitchToThisWindow(popup, TRUE), else MessageBeep(0). End Task: a disabled window (one with a modal dialog up) is not closed: EndDialog, MessageBeep(0) and the switch to its dialog; otherwise EndDialog, IsWinOldApTask on its task, the switch, and PostMessage WM_CLOSE. UNVERIFIED for a DOS program (not listed in Standard mode; w16 switches to it without WM_CLOSE). Cascade, Tile, Arrange Icons: the dialog hides itself, then CascadeChildWindows(desktop, 0), TileChildWindows(desktop, Shift down ? MDITILE_HORIZONTAL : MDITILE_VERTICAL), ArrangeIconicWindows(desktop), and ends. Cancel, Escape, Alt+F4 end it. WM_ACTIVATE inactive posts it WM_USER+100, which ends it (a click on another window takes it away). Pen Windows: RegisterPenApp from GetSystemMetrics(SM_PENWINDOWS)'s module when there is one.

**USER's side** (`user/defwnd.c`, `user/mdi.c`, `user/pos.c`, `user/win.c`): `start_taskman` runs `taskman.exe=` (read in FinalUserInit) with the command line above; **SwitchToThisWindow** activates the window and gives an icon WM_SYSCOMMAND SC_RESTORE (TM2's log: activation, then SC_RESTORE, WM_QUERYOPEN; UNVERIFIED fRestore FALSE); **HWND_TOPMOST/HWND_NOTOPMOST** set and clear WS_EX_TOPMOST, and a top-level window that is not topmost is put under the topmost ones. **Tile/Cascade on the desktop** (TM3, every case to the pixel): the windows are the visible, unowned, not minimised children (MDITILE_SKIPDISABLED leaves out disabled ones), a maximised one restored first, one without WS_THICKFRAME only moved, Z order kept; the area is the screen less an icon row when an icon is up. Tile: s = the integer square root of the count; s columns for MDITILE_HORIZONTAL, count / s for vertical; each column count / columns windows, the last (count % columns) columns one more; columns filled top to bottom from the top of the Z order (9 windows: 3 x 3 both ways; 6: 3 x 2 vertical, 2 x 3 horizontal). Cascade: from the bottom of the Z order up, at (i mod k) * 22, k - 1 = height / 3 / 22 (8 places on 480 lines, 7 with icons), every window (area - (k - 1) * 22) in size (486 x 326) whatever the count. Both return 1. **ArrangeIconicWindows(desktop)**: icons to slots 75 apart from the left, rows 72 high from the bottom (21,408 first), ordered by the slot row nearest each icon's top (bottom first), then left to right; returns the count. Found on the way (the case's `us` part): Ctrl+Esc is taken from the input itself and sent to the active window as WM_SYSCOMMAND SC_TASKLIST, whatever has the focus (it opened nothing from Notepad's text or from Character Map, a dialog, on w16's USER; on 3.1 it does; the Task List itself ignores SC_TASKLIST, so Ctrl+Esc there does nothing, as on 3.1); CW_USEDEFAULT windows are staggered by SM_CYCAPTION + 2 (22: Clock second after Notepad came at 22,22 on 3.1; w16 had 24); a CS_BYTEALIGNCLIENT window being made has its client rounded up to a multiple of 8 (Notepad made at 22 came at 28, client 32; when moved the nearest multiple stays), but Tile and Cascade place windows exactly (tiled Notepad at x 0); a dialog template's WS_CAPTION (WS_BORDER | WS_DLGFRAME) gives a border, not a dialog frame, and a dialog is kept SM_CXDLGFRAME short of the screen's right edge (**TM4**, `tests/apps/tm4.c`: CreateDialogIndirect of seven frame styles, the rectangles identical to 3.1's; Microsoft's Calculator, a dialog of class SciCalc, came 4 pixels wider and taller and at 150,106 instead of 154,110 on w16's USER). TM4 shows 3.1 doing the same for a template with DS_SETFONT (Character Map's: 16,34, 611 x 213); w16 does so since 2026-11-20 (US33, QuickTime's Control Panel, 10.4). It had kept the old rule because with Character Map's window where 3.1 makes it a stray glyph stayed on the desktop left of it (boot-test case icons, 3 x 9 pixels at 12,175): Character Map sets the focus to its character grid in WM_INITDIALOG, and that SetFocus activated the hidden dialog, whose WM_ACTIVATE gave its first tab stop, the font box, the focus; Character Map then drew its status line for the font box ("Fonts available...") through a screen DC before its window was up, and the glyph was that line's "F". A dialog activated by a SetFocus to one of its controls now keeps that control (input.c hwndFocusPending); a list box's focus rectangle on a selected item is drawn with white text and black background colours (3.1: dotted yellow and blue on the blue highlight; w16's was a solid line, the dots drawn in the item's white on the list's white). KERNEL's **IsWinOldApTask** is TRUE for a task of module WINOLDAP (Schulman's listing shows 3.1 reading a PDB flag the loader sets for it). w16's WINOLDAP runs `taskman.exe=` with `0 0`.

**Not as 3.1** (UNVERIFIED or known): ShowWindow(SW_MINIMIZE) of a top-level window that is not the active one: on 3.1 it goes to the bottom of the Z order and the window under the active one is activated (TM3 run against a non-active window); w16's keeps its place (TM3 minimises the active one). A program started minimised from WIN.INI `load=` (Calculator) shows a white square for its icon on w16's USER (Microsoft's Calculator; 3.1 draws the icon on the desktop's colour): not yet understood, so the case starts Calculator from `run=` and minimises it with Alt+Space N. Open: late in the case (windows tiled, Calculator minimised), a double-click on a Task List item ended the list on w16's USER but left no window drawn active (USER's own record shows the item's window activated); the case double-clicks an item at its start, where w16 and 3.1 agree. With the pointer parked on Program Manager's sizing corner, w16's USER shows the sizing arrow once the Task List has gone where 3.1 keeps the arrow until the mouse moves (left out in the case's `us` parts).

**Tests.** TM3 and TM4 on w16's USER (`DROPIN_FIXED=1 tests/userboot.sh -n -t 60 -s build/dos/TM3.EXE`) give TK.LOGs identical to 3.1's (`tests/refrun.sh build/dos/TM3.EXE`). The boot-test case `taskman` plays `tests/scripts/input/taskman.txt` (Clock and Calculator, Notepad over them; Ctrl+Esc, a double-click on an item (Calculator), Down, Escape, a double-click on the desktop's corner, Calculator minimised and Switch To its icon, Cascade, Tile, End Task on Clock, Tile with Shift, minimise, Arrange Icons, Program Manager, a full-screen MS-DOS Prompt and Ctrl+Esc there) against Microsoft's TASKMAN on all-Microsoft 3.1, every screen to the pixel but Clock's ticking parts: w16's TASKMAN on all-Microsoft 3.1 (part ms) and on w16's KRNL286 (k286), w16's USER with Microsoft's TASKMAN (us; Microsoft's GDI: w16's draws Clock's digits in another face) and with w16's (usall), and w16's install tree alone under its KRNL386 against its KRNL286 (tree).

### 15.6 Write (`shell/write/`, WRITE.EXE; probes WR1-WR29)

`shell/write/` is a clean-room Write for Windows 3.1, `build/dos/WRITE.EXE` (marked 3.10), installed as `WINDOWS\WRITE.EXE` by `make install-tree`. Everything it does comes from black-box observation of the original on the reference install (probes WR1-WR11): scripted input with SNAP (`tests/wrobs.sh`), the saved files decoded with `tools/wri.py`, the original's resource templates read as data (`tools/nerc.py`), its imports read from the NE tables (`tools/neimports.py`). The file format is written down on its own in `docs/wri-format.md`, checked against the Windows 3.1 SDK's description (Programmer's Reference vol. 4, ch. 8 "Write File Format", facts only; where Write's own files differ from it, the document says so). Write is in the install tree's Accessories group (first, as 3.1 Setup lists it).

**The window** (WR1, WR6): class `MSWRITE_MENU` with `CS_BYTEALIGNCLIENT` (the first window lands at (4, 0)-(636, 408), the client on an 8-pixel boundary), WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS, WS_EX_ACCEPTFILES, background `COLOR_SCROLLBAR` (the corner between the bars). Children, created so that the window list reads horizontal bar, vertical bar, page box, text: the bars overlap the frame border by a pixel; `MSWRITE_PAGEINFO` is 120 pixels wide, white "Page n" in the System font on `COLOR_WINDOWFRAME`; `MSWRITE_DOC` holds the text. Write draws its own caret (it imports no caret calls): 2 pixels wide, the line's height, blinking at `GetCaretBlinkTime`; after typing, and when the text window gets the focus back from a dialog, it stays off until the next blink (WR1, WR4); entering a menu shows it (WR3, w16write). Tests set `CursorBlinkRate=65000` so screenshots do not depend on the blink.

**Layout** (WR1, WR3, WR7, WR8, WRMET for the metrics): indent 0 is 20 pixels into the text window, the first line 4 below its top. A line is as tall as its highest ascent plus external leading (the leading goes above) and its deepest descent; super- and subscripts move 4 pixels (hpsPos 4; formula UNVERIFIED); 1 1/2 and double spacing make the line 24 or 32 pixels with the space above the text; the end mark's line is at least 13 + 3. Fonts are made with `lfHeight = -MulDiv(hps, dpi, 144)`; widths from `GetCharWidth`. Pictures start a pixel left of text, sizes rounded from twips. Page Down makes the partly visible last line the top one (WR2).

**Scrolling** (WR16-WR20): a picture taller than a line is laid out in bands of 16 pixels (the last one the rest), so the window may start inside a picture; the scroll bar's arrows move by one band (WR17). When an edit or a paste leaves the caret below the window, the caret's line goes to the middle: lines above it are taken until they are at least (h / 16) / 2 * 16 pixels tall, h the text window's height (346: 160; 298: 144; 238: 112; probe WR19 with pictures of every height modulo 16 in three window sizes). The vertical scroll bar's range is 0-255, its position the top line's cp in 255ths of the document, rounded (WR20: 80 positions), where a picture paragraph counts as 40 characters, its header (WR10, WR16, WR17); it does not move while the top is inside a picture; during a run of typing the document counts 32 characters more (WR18; UNVERIFIED cause). After a pasted picture the caret stays off until it blinks unless the paste scrolled the window (WR16, WR17); after typing the caret goes off a moment later until it blinks, wherever the typing was: a menu or a key that comes at once finds it on and it stays on (WR23 T1, WR25 A1), half a second later it is off (WR1, WR18, WR24 R4; w16 waits 450 ms, UNVERIFIED: between SNAP's 200 and 700); the arrow keys keep the caret's state unless the window scrolls (WR24 R5, WR18 S5); other keys show it.

**Centred and right-aligned lines mixing regular and bold** match 3.1 (WR37: regular first, bold first, right-aligned; the 2-pixel offset of w16write's first version is gone). Up and Down keep the column they started from, also across a paragraph format that moved the line (WR37).

**Page breaks** (WR21): Ctrl+Enter inserts character 12, which ends its paragraph (the PAP's fcLim is right after it; no CR LF) and has a line of its own, as tall as the text's line, drawn as a dotted line (2 rows, 2 pixels on and 2 off, from a pixel right of the text's left edge to the window's right edge, 2 and 1 rows above the baseline); text typed after "one" Ctrl+Enter "two" is saved identically. With the caret before a page break 3.1 shows no caret. Printing starts a new page after it (UNVERIFIED: no printer in the reference install).

**Formats** (WR3, WR8): Bold, Italic, Underline toggle; Regular clears them (CHP byte 0 = 1); Reduce/Enlarge step through 8, 10, 12, 14, 18, 24 points (other sizes UNVERIFIED); super/subscript take the next smaller size. Paragraph formats on the last paragraph first add a CR LF (the caret stays before it); Tabs go to every paragraph without one (WR4). A posted Tab character is a tab, not Ctrl+I (the original checks for Ctrl).

**Save As with pictures** (WR35): a document with objects (identifier BE32) has "3.0 Write (*.WRI)" second in the type list (WR23's, without, has not); saving in any type but Write's asks first, "If you save this document in the Write 3.0 format, the linked or embedded objects will be deleted..." (string 8233) for 3.0 Write, "...no pictures, including linked or embedded objects, will be saved..." (8234) for the others, and No saves nothing; the file is written without the picture paragraphs, 3.0 Write as a .WRI with identifier BE31 (files identical). The document itself keeps them.

**Word for DOS** (WR23, WR36: with Page Layout set the section and its table are written too, pnMac still 0; after a first save the section table's file number is 2; Page Layout moves the running heads' indents with the margins): Save As "Word for DOS" writes the .WRI pages up to the PAP FKPs with pnMac 0 and no section, page or font table; "Word for DOS/Txt Only" writes the text alone (`docs/wri-format.md`). Entering a menu shows the caret; a dialog reached from it gives back, on return, the caret's state from before the menu (WR1 S4 off after typing at the end; WR23 shown after typing before a paragraph mark); Page Layout, which lays the document out again, leaves it off (WR4 L2).

**Saving**: byte-identical to the original's saves outside the bytes the original leaves as stale memory (WR1-WR5: new documents, formats, dialogs; README.WRI, WININI.WRI and SYSINI.WRI re-saved). The SETB file number (1 new, 2 loaded) is the original's. Save As uses COMMDLG with a hook that turns Read Only into "&Backup" and keeps an untitled document's name field empty; `[MSWrite] Backup=` goes to WIN.INI.

**OLE** (for the OLECLI work, ARCHITECTURE.md 16): the original imports OLECLI 2-4, 6, 9-16, 18-22, 24-29, 32, 34-45, 47, 56 (OleDelete, OleSaveToStream, OleLoadFromStream, OleClone, OleQueryLinkFromClip, OleQueryCreateFromClip, OleCreateLinkFromClip, OleCreateFromClip, OleCopyToClipboard, OleQueryType, OleSetHostNames, OleSetTargetDevice, OleQueryBounds, OleDraw, OleQueryOpen, OleActivate, OleUpdate, OleGetLinkUpdateOptions, OleSetLinkUpdateOptions, OleEnumFormats, OleClose, OleGetData, OleSetData, OleObjectConvert, OleCreate, OleQueryReleaseStatus, OleQueryReleaseError, OleQueryReleaseMethod, OleCreateFromFile, OleCreateLinkFromFile, OleRelease, OleRegisterClientDoc, OleRevokeClientDoc, OleRenameClientDoc, OleRevertClientDoc, OleSavedClientDoc, OleEnumObjects, OleQuerySize). A pasted picture is stored as an OLE object even without a server (WR7): a picture paragraph (PAP byte 16 bit 10h, byte 0 3Dh) whose 40-byte header has mm E4h, the shown size in twips, the stream's size at 16 and a per-session object number at 24, followed by OLECLI's stream for a static object (version 0501h, format 3, class "DIB" or "METAFILEPICT", HIMETRIC size, data); the file's identifier becomes BE32h. w16's Write draws such objects itself; creating and activating objects through OLECLI is the next stage, and must work with w16's OLECLI.


**The ruler** (`ruler.c`, WR24-WR27): Document > Ruler On makes the child window `MSWRITE_RULER` (first in the frame's window list, 39 pixels tall, the text window's width; the text window then starts 36 pixels down, under the ruler's last 3 rows) and Ruler Off destroys it; the menu item reads "&Ruler On"/"&Ruler Off" (strings 5, 6). In ruler pixels, x 19 is indent 0: rows 1-15 hold nine 22 x 15 buttons at x 91, 125 (left and decimal tab: the kind a click sets), 195, 229, 263 (single, 1 1/2, double), 333, 367, 401, 435 (left, centered, right, justified), the current ones down; rows 17-25 the scale, eighths of an inch with ticks 9, 7, 5 and 3 rows tall and each inch's number in MS Sans Serif 8 (`ANSI_VAR_FONT`) a pixel right of its tick, or with Page Layout in cm half centimetres of 19 pixels (ticks 9 and 5 rows); lines at rows 26 and 38 from x 19 to the edge; rows 27-37 the marks of the caret's paragraph, 12 x 11 cells whose column 6 is the position, their ink inverting the ruler (a first-line indent on the left indent shows white). Buttons apply to the selected paragraphs and, unlike the menu, end the document's last paragraph first (WR25 A1); tabs belong to every paragraph (the last gets its paragraph mark, PAP byte 0 kept: WR27); a mark is dragged with its cell inverted and the text laid out on release; a click in the marks' row elsewhere sets a tab; a tab dragged off the ruler is gone at once; positions go to the nearest 1/16 inch (WR26). Where the first-line and left indents meet, a press takes the first-line one; moving the left indent keeps the first line in place (WR25: dxaLeft 1440, dxaLeft1 -720). The art is w16's own (`tools/mkwriteruler.py`) in the cell layout of the original's resources 102 (18 buttons, up then down) and 202 (first-line, left, right indent, left tab, decimal tab); `tests/wrmsres.sh` builds, for tests only, a copy with the reference's bitmaps so that the scenarios compare the drawing code (ARCHITECTURE.md 3, test-only combinations). Also found with the ruler: with a tab in a centered or right-aligned line only what follows the last tab is placed, between its tab stop and the right indent (WR24 R3); the caret leaves out the rows 1 1/2 and double spacing add (WR24 R3); Page Layout's cm button shows the margins in cm at once ("3.17 cm", the original's string 28687), and cm figures go up to the next twip (WR26: 3.17 cm 1798); Page Layout's OK always stores the first page number. In the Header/Footer views the ruler shows the head's indents from the margins, and a tab set there goes, back in the document, to every paragraph (WR38). On the cm scale a centimetre is 38 pixels for the marks too, and positions go to the nearest 1/8 cm with the twips cut (WR38: 779, 2338). A mark pressed and released without moving stays put. Ruler tabs end the last paragraph only when the document is one paragraph (WR27; WR38 leaves it: UNVERIFIED rule). Not done: horizontal scrolling.

**Move Picture and Size Picture** (WR22, WR28; `view.c`): enabled when a picture is selected alone; arrows step over a picture as one unit, so Shift+Left after a pasted picture selects it. In either mode the picture is drawn plain (not inverted) in a black frame a row taller than it, and a gray outline (XOR colour 8) of the same size follows the keys or the mouse; Enter or a click sets it, Escape leaves it. Move: 8 pixels a Left/Right, or centred on the mouse's x; the offset is the outline's place (dxaOffset 1845 for 123 pixels). Size: the first arrow takes the right or bottom edge and later ones move it (8 pixels across, 6 down: UNVERIFIED steps); with the mouse the edges on its side of the centre follow it; the page box shows "1.2X/1.1Y"; mx and my are the outline's width and height (with its extra row) in thousandths of the picture's at scale 1000 (146 / 80 = 1825, 134 / 41 = 3268). Pasting a picture can be undone ("Undo Editing"), and Paste Special is enabled for a picture on the clipboard (its dialog is not done).

**Undo** keeps one copy of the whole document, taken before a change (typing: before a run of typing); Undo swaps it in, so a second Undo redoes. Edit > Undo reads "&Undo Typing", "Editing" or "Formatting" (the original's strings; which commands are which beyond typing UNVERIFIED).

**Headers and footers** (WR12, WR13): Document > Header/Footer shows only that running head (title HEADER/FOOTER, the end mark after it) with the modeless Page Header/Page Footer box; the head is edited as a copy of the document holding only it, and Return to Document puts it back before the body as 3.1 stores it (rhc 6/7, the margins as its indents, the page number as character 1 with fSpecial, shown "(page)"). Running heads are not shown in the document itself.

**OLE through OLECLI** (`ole.c`, WR6, WR10): the document is registered as `WINWRITE` with its name; each object is loaded from its paragraph's stream (OleLoadFromStream, "Static" for static objects) when first drawn and drawn with OleDraw (an embedded Paintbrush picture has no picture of its own in the stream); a double click opens it (OleActivate); OLE_CHANGED and OLE_SAVED store the stream again (OleSaveToStream); Insert Object lists the registration database's classes with a StdFileEditing server and creates the object (OleCreate) with the host names set (Paintbrush titles itself "Paintbrush Picture in (Untitled)", as with 3.1's Write). WR10's saved file equals the original's but for the object number (the clock). The shown size in twips comes from OleQueryBounds' HIMETRIC by way of whole screen pixels, as the MM_HIMETRIC and MM_TWIPS mapping modes convert (HORZSIZE/HORZRES, VERTSIZE/VERTRES: on VGA 16931 x 11693 HIMETRIC is 521 x 360 pixels, 9600 x 6633 twips; confirmed with five DIB sizes in WR16 and the metafile of WR15; `docs/wri-format.md`). **Links** (`ole.c`, WR30): Paste Link is enabled when the clipboard offers a link (OleQueryLinkFromClip) and makes a linked object (header type 3, OLECLI's stream format 1, the presentation after the names) with OleCreateLinkFromClip; the caret is off until it blinks, as after Paste. Edit > Links is the original's DTPROP box: one line per linked object, "class name<tab>document<tab>item<tab>update" (the class's name from the registration database, the document without its path, "Automatic", "Manual" or "Unavailable"), every line selected at first; Automatic/Manual set the selected links' option (OleSetLinkUpdateOptions; the stream's update word becomes 2 for Manual), Update Now updates them (a failure: "The source document cannot be found...", string 8470, and "Unavailable"), Cancel Link makes them static (OleObjectConvert; the type word 1 and the characters' properties cleared, FOD FFFFh), Change Link names another document (COMMDLG Open, then OleSetData of ObjectLink and OleUpdate: UNVERIFIED), Activate and Edit open them; with no link selected the radios and those buttons are grayed. Enter in the box presses Edit, its default button. Saved files are identical to 3.1's (`tools/wri.py cmp` skips the presentation DIB's stale tail of linked objects too). With w16's OLECLI too (boot part `w16write@wr30o`): a link pasted with a picture on the clipboard takes that picture as its presentation and starts no server (connecting only to one that has the document open), and a clipboard DIB without a resolution gets the screen's (3780 pixels a metre, as 3.1's OLECLI stores it). **Paste Special** (WR32): the original's PASTESPECIAL box; with OwnerLink or ObjectLink on the clipboard "Source:" shows the class's name and "document item" below it, otherwise "Source:" is hidden; "Data Type:" lists "<class name> Object", "Picture", "Device Independent Bitmap", "Bitmap", "Text" as available (strings 112, 108, 109, 107, 110), the first selected; Paste Link is enabled for a link; Paste pastes the chosen type (UNVERIFIED for Text, Picture or Bitmap beside a DIB). **Opening a document with links** (WR33): with automatic links Write asks, before its window shows, "This document contains links to other documents. Do you want to update links now?" (string 8473, Yes/No, exclamation icon); Yes shows the window without drawing the text, updates them, and when one cannot be updated shows DTINVALIDLINK ("Linked document is unavailable.", System font, Links... the default button; several links: DTINVALIDLINKS), Links... opening the Links box with them "Unavailable". 3.1's OLECLI reports the failure only during the OLE_RELEASE notification (OleQueryReleaseError reads OLE_OK afterwards), so Write records it there. **The object item** of the Edit menu: with a linked or embedded object selected it reads "<verb> <class name> &Object" ("Edit Paintbrush Picture &Object", command 5122) and does the verb (a link that cannot open: DTINVALIDLINK); several verbs make a popup of them (UNVERIFIED); else "&Object", grayed. Known difference: WR33 O4, 120 pixels at the selected picture's right edge after the failed verb.

**Confirm Page Breaks** (`print.c`, WR31): Repaginate with the box checked shows each break before keeping it: the coming page's first line selected and scrolled into view (from above with 128 pixels of lines over it, from below by the caret's middle rule: UNVERIFIED rule), the page mark beside it, the page box counting only the pages so far, and dialog 9; Up and Down move the break a line within its page; Confirm keeps it, a moved one as a page break character of its own paragraph before the line (WR31: identical file); Cancel stops. A page break already in the text is selected (its whole line inverted, from a pixel left of the text to the window's edge) and dialog 10 asks: Keep, Remove (deleted), Cancel. Afterwards the selection is as before. Known difference: the proposed line's highlight is 3 pixels wider in 3.1 (139 pixels in WR31 C1).

**Printing** (`print.c`; WR29): Print (COMMDLG PrintDlg with Pages enabled, From 1 To 1; the STARTDOC/NEWFRAME/ENDDOC escapes, the original's Now Printing box with an abort procedure), Print Setup, Repaginate (the page table; the printer's metrics when one is set up, else the screen's). Checked against 3.1 with the Generic / Text Only driver (TTY.DRV, UNIDRV.DLL expanded from the Setup disks by the scenario) on a file port C:\WROUT.PRN (a port named PRN.* would be the DOS printer device): the bytes written are identical. Pages are Page Layout's text area; a page break (character 12) starts a page and prints nothing; running heads print on every page but the first unless "Print on First Page" (rhc bit 3), indents counted from the page's edges, the page number as its number; the header a line lower than yaHeader from the top, the footer's bottom a line lower than yaFooter (counted from the page's top) (on TTY: lines 5 and 61; UNVERIFIED on other printers). Printing fills the page table, page 1 at the body's first character, the table's room word 10 for 2 pages (`docs/wri-format.md`). With a printer set up, a new document's font is Arial as the printer realizes it: Generic / Text Only gives "Roman 10cpi" (family 30h) in the font table, and the screen shows that face. With a printer set up the screen shows each font as the printer has it, its face and its size (Generic / Text Only's "Roman 10cpi" at 12 points for a 10-point request: on the screen a 12-point fixed-pitch modern face, glyph for glyph 3.1's). Not done: 3.1's line heights then (WR29 P1: the line after a page break sits 2 pixels higher; 706 pixels). A page range (Pages From 2 To 3) prints just those pages, their running heads numbered 2 and 3, and leaves the page table as it was (WR34: identical output).

**Tests**: `tests/writescenarios.sh` (scenarios `tests/scripts/write/WR*.txt`, results in `build/ref/wrscen/<date-time>/`, pixel baseline `tests/scripts/write.baseline`), `tests/writehelp.sh` (WinHelp requests against `tests/apps/thelp.c`). Boot-test case **w16write**: w16's Write under real keys against Microsoft's on all-Microsoft 3.1, on w16's KRNL286 and KRNL386, and the install tree alone under both kernels. Feature scenarios run as boot-test parts too: `w16write@wr24` ... (the list `WRSCEN_BOOT` in `tests/boottest.sh`), each `tests/writescenarios.sh` on one scenario. Scenario directives (`tests/wrobs.sh`, `tests/writescenarios.sh`): `; files:`, `; prep:`, `; mkpic:`, `; expand: FILE DEST` (a Setup-disk file expanded into the scratch install), `; ini: section:key=value`, `; get: FILE` (fetched and compared byte for byte). Not yet done (plan): the line heights with a printer set up (WR29, WR34),.

### 15.7 File Manager (`shell/winfile/`, WINFILE.EXE; probes WF1-WF20, scenarios WF01-WF14, WF20, WF21)

`shell/winfile/` is a clean-room File Manager for Windows 3.1, `build/dos/WINFILE.EXE` (marked 3.10). By the user's decision it is a clean-room clone: Microsoft's published File Manager source (and any other File Manager source) is never looked at; every fact comes from black-box observation of 3.1's WINFILE.EXE on the reference install (SNAP scripts in `tests/scripts/winfile/obs/`, run with `tests/wfobs.sh`: window classes, styles and rectangles, the menu with its IDs, dialogs' controls, screenshots, the WINFILE.INI it writes; `tests/apps/thelp.c` for Help) and from the SDK. Its resources were not read with a dumper: the menu and the dialogs are reproduced from the running program (`tools/snapdlg.py` turns a SNAP dump of a dialog into a template). Its art is w16's own (`tools/mkwfbmp.py`, the strip of small pictures in the original's cell layout; `tools/mkwfileicon.py`); `tests/wfmsres.sh` links the original's strip and icons into a scratch test copy only, so screens compare drawing code (ARCHITECTURE.md 3).

**Windows** (WF1): the frame `WFS_Frame` (title "File Manager", first start at 0,0 the screen's size, not maximised) holds an `MDIClient` (-1,-1, two pixels wider than the client, down to a pixel into the status bar; window list from ID 1006). A directory window is an MDI child `WFS_Tree` titled with its directory and pattern; its children: the drive bar `WFS_Drives` (ID 1), the tree `DirTree` (5) holding an owner-drawn ListBox (3), the file list `WFS_Dir` (2) holding an owner-drawn, sorted (WM_COMPAREITEM) ListBox (6) that is multicolumn in the Name view; between them a 3-pixel split, white but black on the panes' top row and beside their horizontal scroll bars. Measured details (VGA, MS Sans Serif 8): lines 16 high; tree levels 18 pixels apart; name-view columns the widest name plus 21; details columns right-aligned at fixed distances after the widest name (size 59, date 55, time 67, attributes 33 further); drives every 44 pixels, the current one framed (filled with the focus); the volume label centred right of the drives. The status bar (MS Sans Serif 10, 22 high, two sunken boxes 276 wide) says "C: n KB free,  n KB total" or "Selected n file(s) (n bytes)" on the left (which one follows events, not the focus: winfile.h ST_*), "Total n file(s) (n bytes)" on the right, counting directories.

**WINFILE.INI** `[Settings]`: `Window=x,y,width,height, , ,show`; `dirN=left,top,right,bottom,iconx,icony,show,view,sort,attribs,split,path\pattern`, dir1 the bottom of the MDI Z order (written top first); view bits 2 size, 4 date, 8 time, 16 attributes, 32 Indicate Expandable Branches (per window); sort 201-204; attribs 0x10 directories, 0x100 programs, 0x200 documents, 0x400 other (with 0x40, 0x20, 0x01 at first), 0x06 hidden/system; option keys written when changed (StatusBar, MinOnRun, Save Settings, Confirm*, Face, Size, LowerCase bit 0 lower case and bit 2 italic, FaceWeight).

**Behaviour found** (each in the code's comments): the year shows as '0' + (year - 1900) / 10, so 2026 is "<6"; a directory's size as DOS reports it is added to the total; window numbers ":n" are one more than the others' showing the same directory and change on F5; documents are files whose extension WIN.INI [Extensions] or REG.DAT associates; the details view starts at the caret's line when nothing is selected; Tree Only straight to Directory Only rebuilds the tree, clears Indicate Expandable Branches and moves the focus to the drive bar; Help asks HELP_INDEX, HELP_PARTIALKEY "", HELP_HELPONHELP, and F1 on a menu item HELP_CONTEXT 5000 + its command ID (a WH_MSGFILTER hook).

**File and disk commands** (`fileops.c`, `assoc.c`, `search.c`, `dnd.c`, `disk.c`): the dialogs as observed; the "From:" list names files and gives directories their full paths; the operation's box stays up, disabled, under the confirmations (Confirm File Replace, Confirm File Delete, Confirm Directory Delete for the directory and then each file in it, with Yes to All when more can come) and shows "Deleting..." and the name. Search fills an MDI child `WFS_Search` ("Search Results: C:\*.TXT", full paths, "n file(s) found"). Drag and drop is USER's DragObject with File Manager's list ("NAME " for files, full paths for directories); our panes, drive bar and icons take drops (Confirm Mouse Operation, "Are you sure you want to move the selected files or directories to C:\WFTEST\ALPHA?"), other programs get WM_DROPFILES (Program Manager makes an item: identical to 3.1's). The Disk menu's boxes and error paths are 3.1's (no floppy in DOSBox); the floppy work goes through the BIOS (INT 13h via DPMI), UNVERIFIED. WINFILE.EXE is in the install tree and the Main group. Changes other programs make arrive through KERNEL's FileCdr (9.7): a window showing the directory is reread when a name appears or goes, and, with Search Results up, "The contents of this drive have changed..." is asked, as 3.1 does (obs WF20, scenarios WF20 and WF21; the status bar then shows the selection's line; Microsoft's File Manager on w16's KERNEL gives the same screens).

**Extensions** (`ext.c`, layer 5; probe FMX.DLL `tests/apps/fmx.c`, obs WF15-WF18, scenario WF14): WINFILE.INI `[AddOns]` read in the file's order after the frame is made (hidden, no directory window yet): LoadLibrary (a missing file: silently skipped), GetProcAddress "FMExtensionProc" (3.1's KERNEL upper-cases the name asked for: an extension must export it in capitals), FMEVENT_LOAD (100) with the frame and an FMS_LOAD (MENU_TEXT_LEN 40) whose only member set is wMenuDelta: 600 for the first extension loaded, 100 more for each next. An answer of 0, hMenu 0 or a dwSize other than 48 gets FreeLibrary and no FMEVENT_UNLOAD, and does not use up a delta; at most 5 load, after the fifth no more keys are read. Every item ID of the extension's menu (submenus too) gets the delta added and its state dropped (a grayed item shows enabled); the popup goes before Window. A command ID is File Manager's own first (1003 is Refresh even with a fifth extension), then an extension's, then MDI's (1006 is the fifth extension's item 6, not the first window). Events: FMEVENT_INITMENU (102, lParam delta and hMenu) when its popup opens; a command as its own ID (1..99), lParam 0; FMEVENT_USER_REFRESH (103) on Window > Refresh; FMEVENT_SELCHANGE (104) when the user changes the selection of the file list or Search Results (not a new directory, not the tree); FMEVENT_UNLOAD (101) with hwnd 0 at exit and on FM_RELOAD_EXTENSIONS, each followed by its FreeLibrary; no help strings (3.1 has no FMEVENT_HELPSTRING). Messages, WM_USER + 0x200 onwards: FM_GETFOCUS (1 file list, 2 tree, 3 drive bar, 4 Search Results), FM_GETDRIVEINFO (always 1: total and free bytes, the window's directory, the volume label, share ""; Search Results: the directory it started from; FFFFFFFFh free after a file is made or changed in a directory no window shows, until the drive is read again: scenario WF21, through KERNEL's FileCdr; an automatic reread sends no event), FM_GETSELCOUNT and ...LFN (the file list's selection without "..", whatever pane has the focus), FM_GETFILESEL and ...LFN (time, date, size, attributes, the full path in capitals; an index past the end gives the last one and returns the count; none selected: nothing written, 0), FM_REFRESH_WINDOWS (+0x206: wParam 0 the active window, else all; no FMEVENT_USER_REFRESH), FM_RELOAD_EXTENSIONS (+0x207).

**Tests**: `tests/wfscenarios.sh` (scenarios `tests/scripts/winfile/WF*.txt` on the original and on w16's, screens against `tests/scripts/winfile.baseline`, the WINFILE.INI each leaves compared; a test tree with fixed dates, `tests/scripts/winfile/mktree.py`, as C:\WFTEST), `tests/wfhelp.sh` (WinHelp requests, `tests/scripts/winfilehelp/`). Boot-test case **w16winfile**, one part per scenario. State, differences and the plan: `docs/notes/winfile-handover.md`.

### 15.8 Screen savers (`shell/scrnsave/`, SSSTARS.SCR; scenarios `tests/scripts/ss/`)

A screen saver is a Windows program named `*.SCR` whose module description is `SCRNSAVE : <name>` (Control Panel's Desktop lists the Windows directory's by it). `/s` (or `-s`) runs it full screen; `/c`, `-c` or nothing shows its Setup dialog (3.1 SDK, Programmer's Reference vol. 1 ch. 14, the SCRNSAVE.LIB model). w16's library is `shell/scrnsave/scrnsave.c` + `pwenc.c` + `scrnsave.dlg`, written from the SDK's description and black-box observation of 3.1's SSSTARS.SCR (SNAP dumps and screenshots, the INI files it wrote; nothing of Microsoft's saver files was read): a saver provides `ScreenSaverProc`, `ScreenSaverConfigureDialog` and `RegisterDialogClasses`, the strings `IDS_DESCRIPTION` (1) and `idsAppName` ("Screen Saver.Stars", its CONTROL.INI section) and its Setup template, and links the library; the other four 3.1 savers can be added the same way (`SAVERS` in the Makefile). The library's behaviour, as observed on 3.1:

- **The saver window**: class `WindowsScreenSaverClass`, style 94000000h (popup, visible, clip siblings), WS_EX_TOPMOST, the whole screen, black, no cursor. A second `/s` while one runs does nothing. A key, a mouse button or the mouse moving ends it (USER-started or Test). **Password protected** (`[Screen Saver.X] PWProtected=1`): the dialog captioned "X" (the part of `idsAppName` after the dot), centred, exclamation icon, "The screen saver you are using is password protected.  You must type in the screen saver password to turn off the screen saver."; Cancel goes back to the saver; a wrong password shows "Incorrect password;\n\nCheck your screen saver password and try again." (stop icon, placed from the dialog) and then goes back to the saver; the right one ends it.
- **Change Password** (the SDK's template, IDs 100-109): Old Password disabled while there is none; OK disabled and Cancel the default button until a field has text, then OK the default; edits limited to 20 characters; a wrong old password: "The value for Old Password is not correct.\n\nCheck your screen-saver password, and then try again." (that field cleared); New and Retype differing: "The values for New Password and Retype New Password do not match.\n\nTry again." (both cleared); both message boxes "Change Password", stop icon. OK writes CONTROL.INI `[ScreenSaver] Password=` at once (Cancel on Setup keeps it); an empty new password writes it empty. Help: CONTROL.HLP topic 9000 (its Help button and F1), HELP_QUIT when a dialog closes.
- **The stored password** (`pwenc.c`, derived from 293 passwords set on 3.1 and the values written, `tests/scripts/ss/pwdata.json`; `make test` checks every one, `tests/scripts/ss/pwtest.py`): uppercased, then XOR steps each kept only if the result is 21h-7Eh and not `=`, `[` or `]`: forward over the characters with the length, the index and the previous character (2Ah for the first); then, for two or more characters, backward with the length, the index (not for the last) and the next character (2Ah for the last). A password set on 3.1 works on w16 and the other way round.

**SSSTARS.SCR** (`ssstars.c`; icon `tools/starsicon.py`): stars are white squares flying out of the middle on black. They start together at the far depth, spread evenly over the screen; a star leaving the screen comes back at the far depth at a random place; positions scale as 1/depth, the depth falling steadily (about 5% of the far depth per 150 ms at the default speed); sizes 1, 2, 3, 4 pixels below 100%, 77%, 52%, 26% of the far depth; all white on 16 colours. Setup "Starfield Simulation Setup": Warp Speed 0-10 (a line or page a step), Starfield Density 10-200 (edit and `cpArrow` spin control; OK disabled while out of range), Password Protected and Set Password...; OK writes `Density`, `WarpSpeed`, `PWProtected` in that order (defaults 25, 5, 0), Cancel nothing; Help CONTROL.HLP topic 9003.

**USER** (`user/misc.c`, `defwnd.c`, `input.c`; the SDK, Pietrek ch. 2, 3 and 6 as facts, confirmed on 3.1): WIN.INI `ScreenSaveActive`/`ScreenSaveTimeOut` are read once; `SystemParametersInfo` SPI_GET/SETSCREENSAVEACTIVE and ...TIMEOUT read and change them (SPIF_UPDATEINIFILE writes WIN.INI). The first task's InitApp once Windows is up starts a 10-second system timer: past the time-out since the last key or mouse event, no button held, no system-modal window, an active window that is not a DOS program's in 386 Enhanced mode, it posts the active window WM_SYSCOMMAND SC_SCREENSAVE and waits for new input; DefWindowProc's SC_SCREENSAVE runs SYSTEM.INI `[boot] SCRNSAVE.EXE` with ` /s` (nothing for "(None)"). Observed on 3.1: a 15-second time-out started the saver 15-17.5 s after the last key; SC_SCREENSAVE posted to Notepad started it. The default stays 3.1's: `ScreenSaveActive=0`, no SCRNSAVE.EXE (assets/).

**Tests**: `tests/ssscenarios.sh` (boot-test case `scrnsave`, a part per scenario): each `tests/scripts/ss/*.txt` on the reference install with Microsoft's saver and with w16's (`setup`, `setpw`, `pwrules`, `settings`, `run`, `pwrun`, `desktop` with w16's CONTROL.EXE and MAIN.CPL, `timeout` with w16's USER); INI files and window dumps identical, screenshots within `tests/scripts/ss.baseline`, the running saver's screens checked by `tests/scripts/ss/stars.py` (stars are random); `tests/sstree.sh` (parts `tree286`, `tree386`): w16's install tree alone, USER starting the saver after the time-out and on SC_SCREENSAVE, Desktop listing it and its Test.

UNVERIFIED (w16's choice in brackets): the frame rate and the speed of warp settings other than 5 [proportional to the setting + 1]; how far the mouse must move [any move]; whether a key's release ends the saver [no]; which check comes first when both the old and the new password are wrong [the old].

### 15.9 Registration Info Editor (`shell/regedit/`, REGEDIT.EXE; probes RE1-RE19, scenarios RG01-RG19, New Style NS01-NS02)

`shell/regedit/` is a clean-room Registration Info Editor for Windows 3.1, `build/dos/REGEDIT.EXE` (marked 3.10), in the install tree's Windows directory. Every fact comes from black-box observation of 3.1's REGEDIT.EXE on the reference install and from the SDK: SNAP scripts in `tests/scripts/regedit/obs/` run by `tests/regobs.sh` (window classes, styles and rectangles, menus with their IDs, every dialog's controls, message texts, screenshots, the REG.DAT and WIN.INI each session leaves, the .REG files it writes), and its calls into SHELL logged by XHELL.DLL (`tests/apps/xhell.c`: `REGOBS_TRACE=1` gives a scratch copy of the program whose SHELL imports `tools/nerename.py` points at XHELL, which logs each RegOpenKey ... RegEnumKey with its arguments and result and passes it on). The program itself was never disassembled or resource-dumped; dialogs are `tools/snapdlg.py` templates from the dumps; its icon is w16's own (`tools/mkregicon.py`). It needs nothing SHELL does not export: 3.1's imports from SHELL are the seven registration functions, the four drag-and-drop ones and ShellAbout, and it parses and writes .REG files itself.

**Windows** (RE1, RE2): the frame, class "Registration Editor", title "Registration Info Editor", WS_OVERLAPPEDWINDOW (no WS_CLIPCHILDREN), accepting dropped files (a dropped .REG file is merged), holds one child dialog, the view, at its client's origin. Its size is the view template's (280 x 156 basic, 280 x 208 /V) or WIN.INI `[RegEdit] width=`, `height=` (the view's client size, written at exit). The list fills the view below its labels, one pixel past the sides and bottom, its height whole lines; when the frame is sized to anything but the template's size, the frame shrinks to the list's whole lines (a frame moved to 260 pixels became 254; the template's 208 for /V stays). A second start brings the first up (one at a time). Help (`tests/rehelp.sh`, REH1-REH8, THELP standing in for WinHelp): `regedit.hlp` (basic) and `regeditv.hlp` (/V), every request followed by HELP_SETCONTENTS with the view's contents topic 2801h / 2881h; Contents is HELP_CONTEXT of that topic, as is F1 in the window; F1 on a menu item 2001h (basic) or 2081h (/V) plus its ID; the dialogs' Help and F1: Modify 4000h, Add 4001h, Copy 4002h File Type, Add 3FFFh, Copy 3FFEh, Delete 3FFDh, Find 3FFCh Key, Merge's Open box 3000h, Save Registration File's 3002h (Browse's 3001h UNVERIFIED); About: ShellAbout("Registration Editor", "An editor for registration information for applications.", its icon).

**Basic view.** "Registered File Types:", a sorted list of every key of HKEY_CLASSES_ROOT not starting with `.` by its value ("" sorts first; WordDocument, which has none, is the blank first line of 3.1's stock database) and a hidden list of identifiers off the window (ID 431h); among equal names the newer key comes first. Menus File (Merge Registration File..., Exit), Edit (Add File Type..., Copy File Type..., Modify File Type..., Delete File Type), Help. Enter and a double-click are Modify. Modify, Add and Copy are one dialog (Modify's Identifier a static): File Type, Action Open/Print each with its own Command and DDE settings (Uses DDE, DDE Message, "(optional) DDE Application Not Running", Application, Topic), Browse... (COMMDLG Open "Browse", filter Programs `*.com;*.exe;*.bat;*.pif`), Help. Add starts with Uses DDE checked for both actions; the DDE fields are enabled with Uses DDE, the not-running message only while the DDE Message is not empty; checking Uses DDE with a command fills an empty Application with the program's name (first letter capital, "Pbrush") and an empty Topic with "System". OK checks a File Type ("There is no File Type specified."), then the identifier (1-63 characters 21h-7Fh, no backslash: "The specified identifier is not valid. ... or backslashes (." — sic, 3.1's text ends so), then for Add and Copy that it is new ("The identifier X already exists."). The writes, in 3.1's call order (RE17): a new identifier RegCreateKey'd and closed; then opened; `shell` created; the value set; for open, then print, the verb created, `command` set (deleted when empty), and with Uses DDE and a message `ddeexec`, `ifexec`, `application`, `topic` set (each deleted when empty, application when it names the command's program, topic when "System"), else all four deleted. Opening Modify or Copy reads the type with RegCreateKey of `shell`, `shell\open` and `shell\print`, so it makes them when missing, as 3.1 does. Delete: "Are you sure you want to delete "X" File Type?" (Yes/No, exclamation), RegDeleteKey of the identifier only. After a dialog the type is selected by its name, scrolled to from the top; after Merge (even cancelled) the first line.

**Advanced view (/V).** "Full Path:", "Value:" (an edit) and an owner-drawn list of every key, always expanded, lines 16 pixels high, "name = value", the drawing measured (advanced.c's header). Menus File (Save Changes, Restore To Last Save, Merge Registration File..., Save Registration File..., Exit), Edit (Add Key..., Copy Key..., Delete Key...; Del), Search (Find Key..., Find Next), Help. The view edits its own copy of the database (RE15, RE16, RE19: no SHELL call is made while editing): read at the start with RegCreateKey of the root; Save Changes deletes the deleted keys (RegOpenKey of the root, RegDeleteKey each, close) and then sets every changed or new line from the bottom of the tree up (RegCreateKey of the root, RegSetValue each, close) and reads the database again, the root selected; Restore To Last Save ("Are you sure you want to lose all changes?", OK/Cancel) reads it again; Exit with changes asks "Changes have been made to the registration database. Do you want to save them?" (Yes/No/Cancel, no icon) and No writes nothing. A value typed shows in its line only when the list is drawn again. Add Key puts the key first under the selection; Copy Key (From: editable, To:) copies a subtree first under To's parent; Delete Key removes a subtree, the line in its place selected; a path not in the tree: "The key 'X' does not exist." Merge in /V goes into the copy: missing keys made first under their parents, the last key merged selected, the list redrawn after the message. Find Key: a key name containing the text, any case, from the next line down; nothing found leaves the selection. Save Registration File writes the selection's subtree from the copy.

**.REG files** (RE6, RE10, RE18): the first line must be `REGEDIT` (the case counts: `regedit` is "This is not a registration file. Select a registration file (.REG), and then try again."); each line `HKEY_CLASSES_ROOT\path` (a backslash must follow: `HKEY_CLASSES_ROOT = x` sets nothing) names a key; the key ends at the first blank; after blanks an optional `=` and one blank, the rest of the line is the value (trailing blanks kept; `a = b` stays whole; `k\a b = c` gives `k\a` the value `b = c`); a line without a value sets "" (the key made, no value). Other lines are skipped. LF or CR LF; the last line may have none. Merged into SHELL: RegCreateKey of the root held, one RegSetValue on HKEY_CLASSES_ROOT per line, close (one REG.DAT write). Written: `REGEDIT`, then in RegEnumKey's order, each key before its subkeys, `HKEY_CLASSES_ROOT\path = value` for every key with a value or without subkeys (`= ` and nothing), CR LF. Command line: `REGEDIT file` merges and says "Information in FILE has been successfully registered in the registration database." (exclamation icon), `/s` says nothing; a missing file "Cannot open file: FILE. Make sure the specified path and filename are correct."; no window is made.

**Not in 3.1's REGEDIT** (the brief named them; observation found none): Save Registration File in the basic view, Show Details, a View menu with Tree and Data (those are later Windows' editors). The title is "Registration Info Editor" in both views; "Registration Editor" is the class and the About box's name.

**SHELL** (13.2): 3.1's SHELL reads an existing REG.DAT at the first registration call, not when it loads (RE13: Program Manager keeping SHELL loaded, a REG.DAT copied over the file before REGEDIT's first call was the one it showed, one copied later was not). w16's read it in LibMain; it now reads at the first call (`pending_load`), its tables allocated GMEM_DDESHARE so that they belong to SHELL and not to the program that happened to make the first call (without that the second REGEDIT hung once the first had ended). SH1, SH2, SH4, SH11, SH12 still give 0 FAIL in both modes.

**New Style** (w16's own; `[w16] RegeditNewStyle`, docs/w16-settings.md; newstyle.c): 1 starts in a Windows 95 Registry Editor-like view, 2 in 3.1's views; either adds View > New Style to every view and writes the choice back; 0 (the default) is 3.1's program exactly. The view, drawn by REGEDIT itself with GDI (no common controls, no art from any Microsoft program): a key tree (an owner-drawn list box whose lines are inserted and removed as keys open and close: "My Computer", HKEY_CLASSES_ROOT, the keys, with w16's own 16 x 16 pictures of a computer and closed and open folders, dotted lines, +/- boxes; Right/Left/Enter/+/- and clicks on the boxes), a splitter (drag, or View > Split and the mouse or arrows), a Name/Data header with a draggable divider over the key's one value ("(Default)", its data in quotes or "(value not set)", a string picture), and a status bar with "My Computer\HKEY_CLASSES_ROOT\path". Menus Registry (Import/Export Registry File..., Connect/Disconnect Network Registry and Print grayed, Exit), Edit (Modify, New > Key / String Value, Delete, Rename, Find..., Find Next), View (Status Bar, Split, Refresh, New Style), Help (Help Topics, About Registry Editor); Del, F2, F3, F5, Ctrl+F. New > Key makes "NewKey1" (3.1 key names allow no blanks) and starts the in-place rename (an edit over the line: Enter keeps, Esc drops; a rename is a copy and a delete, the API having no rename); Modify and New > String Value open Edit String for the key's value; Find looks in key names and/or data, whole or part. It shows only what the 3.1 database holds: HKEY_CLASSES_ROOT, one unnamed string per key. Changes are written at once. Switching views keeps the selected key; the /V view's unsaved changes are asked about first. Its placement, split, column and status bar are WIN.INI `[RegEdit] NSWindow=`, `NSSplit=`, `NSColumn=`, `NSStatusBar=`.

**Tests**: `tests/rescenarios.sh` (RG01-RG19 run on the reference 3.1 with Microsoft's REGEDIT and with w16's: REG.DAT, WIN.INI and saved .REG files byte-identical in all; 89 of 99 screens 0 pixels, `tests/scripts/regedit.baseline`; NS01-NS02 run w16's New Style against w16's approved screens in `tests/scripts/regedit/ns-ref/`, `RE_NSREF=update` to approve new ones), `tests/rehelp.sh` (the WinHelp requests, `tests/scripts/regedithelp/`). Known differences: a /V dialog opened from a second instance (frame at 22,22) sits 8 pixels further right (RG11 F5: the dialog's x as 3.1 places it relative to the frame is not known); the /V selection's focus rectangle while Merge's Open box is up and after it is cancelled (RG06 BA, RG04 S6, about 40 pixels), a pixel column of the /V list's border when resized from WIN.INI (RG10, 18 pixels), and the scroll bar's thumb after Delete Key (RG16 I1, 74 pixels). The About box's picture and free-memory line are masked (w16's icon).

### 15.10 Windows Help (`shell/winhelp/`, WINHELP.EXE; probes WH1-WH5, scenarios WS01-WS10)

`shell/winhelp/` is a clean-room Windows Help for Windows 3.1, `build/dos/WINHELP.EXE` (marked 3.10), in the install tree's Windows directory, where USER's `WinHelp` starts it (10, `user/misc.c`). It reads Windows 3.x `.HLP` files; the help files themselves are not w16's and are not in the tree.

**Sources.** The file format: Manfred Winterhoff's format description *Windows Help File Format / Annotation File Format / SHG and MRB File Format* (`helpfile.txt`, revision 10, distributed with helpdeco; GPL'd text read as a description of facts only, helpdeco's code never read), Pete Davis's Dr. Dobb's articles (September and October 1993) as credited there, and the SDK's Programmer's Reference Vol. 4 ch. 15 (the Help statements and macros). Every structure was checked against the reference install's 26 help files with w16's own host build of the reader, `tests/winhelp/hlpdump.c` (`make build/hlpdump`; `-c` hashes context strings and looks them up, `-k` lists keywords, `-m` the [MAP] numbers; all 26 files parse with no bytes left over). How 3.1's WINHELP looks and behaves comes only from black-box observation: SNAP scripts in `tests/scripts/winhelp/obs/` run by `tests/whobs.sh` (window classes, styles, rectangles, menus and IDs, dialogs' controls, message texts, screenshots, WIN.INI, WINHELP.BMK and .ANN files). Microsoft's WINHELP.EXE was never disassembled, hex-read or resource-dumped; its icon, cursor and the no-file logo are w16's own (`tools/mkhelpart.py`).

**The reader** (`hlp.c`, `para.c`, portable C also built on the host): the internal file system and its B+ trees; `|SYSTEM` (title, copyright, contents topic, [CONFIG] macros, [WINDOWS] definitions); `|Phrases` (LZ77 and 3.0's plain form); `|FONT` (3.x descriptors); `|TOPIC` (LZ77 blocks of 4K decompressed to 16K, TOPICPOS and TOPICOFFSET, topic headers, display records 20h and tables 23h with their paragraph formats and commands, 3.0's records with less care); `|CONTEXT` (the hash: h = 43h + v(c), letters case-blind; checked against every file's JumpId targets), `|CTXOMAP`, `|KWBTREE`/`|KWDATA` and the other keyword tables, pictures (`|bm<n>` and embedded: DDB, DIB, all four packings, SHG hot spots, the MRB resolution matching the display). At open the reader walks every link once to index the topics by TOPICOFFSET. In 3.1's files the jumps' "topic offsets" are context hashes, looked up in `|CONTEXT`.

**Observed and done** (WH1-WH4, scenarios against 3.1's WINHELP on the reference install's own files):
- Classes: the main window is MS_WINHELP when started by WinHelp (`-x`), MS_WINDOC when started by hand, so a Help window opened by hand is never taken over (How to Use Help from a hand-started one starts a second instance, 24 pixels right and down); children MS_WINICON (the button bar), MS_WINNSR, MS_WINTOPIC; popups MS_WINNOTE (WS_POPUP, topmost); secondary windows MS_WINTOPIC_SECONDARY; History MS_WIN_PATH.
- Place: x a third of the screen in, y 2, the rest of the width less 2, the height less 4 (VGA 213, 2, 425, 476). WIN.INI `[Windows Help]`: `M_WindowPosition=[x,y,cx,cy,max]` is written at exit only when the window is not where it started; `H_WindowPosition` (History, a third of the screen each way) always, unless no file was ever opened.
- Menus (IDs as 3.1's): File (Open..., Print Topic, Print Setup..., Exit), Edit (Copy..., Annotate...), Bookmark (Define...), Help (How to Use Help, Always on Top, About Help...: ShellAbout "Help"). Buttons: standard push buttons 59 x 21 side by side, bold 8-point Helv, on dark grey with a black line along the bar's bottom; Contents, Search, Back (grey with nothing to go back to: a jump pushes, Back pops), History (every topic shown, the latest first, the previous one selected), and the file's CreateButton buttons (Glossary in 3.1's files); << >> when BrowseButtons() runs. A button's letter works with or without Alt.
- Layout: half points to pixels at LOGPIXELSY/144, truncated; text 8 pixels in plus the left indent; a word fits when it and the space after it end within the width less the right indent; line height the tallest cell, line spacing negative exact or positive a minimum; spacing above/below; tabs (default every 36 points); left/centre/right alignment; borders as lines. Jumps green and underlined a pixel under the baseline, popups green with a 2-on 2-off dotted underline, "without font change" hot spots as plain text; the hand cursor over hot spots; Tab/Shift+Tab and Enter select and follow them. A character's advance is the extent of two of it less that of one (synthesized bold).
- Popups: laid out as wide as the source window's client area less 4, narrowed to the widest line; a 2-pixel black frame, the text one pixel in, five pixels below; a 12-pixel shadow (black on every pixel whose screen x + y is odd); the window centred on the click, 10 pixels below it, kept 2 pixels inside the screen; it takes the activation; any click or key closes it.
- Secondary windows: the [WINDOWS] definition's place and size are 1024ths of the screen (GLOSSARY.HLP's glossary window: 76, 57, 250 x 234 on VGA).
- Errors (information icon, the program's name as caption): "Cannot open Help file.", "This file is not a Windows Help file." (the format's name, kept), "Help topic does not exist.".
- Search: "Search" with 3.1's controls and IDs (WH2); typing selects the first keyword starting with the text, else the last one before it, the one above it shown at the top; Show Topics fills the topics list, moves the focus there, makes Go To the default and Cancel "Close"; a keyword with one topic (HELP_KEY) goes straight to it; with the window hidden (HELP_PARTIALKEY "") the dialog comes up alone over the application's window.

**OpenWatcom's help files and compiler.** OpenWatcom (open source) ships 41 help files made by its own compiler `whc` (`$WATCOM/binw/*.hlp`, up to 2.7 MB: clib.hlp has 910 topics) and `whc` itself. whc's files differ from Microsoft's in one way the reader had to learn: a topic block may decompress past 16K (to 19703 bytes in fmix.hlp) with the whole of a link that Microsoft's compiler would carry into the next block, and the next link's position is then written as if it were in the next block (block * 16K + offset). `hlp_nextlink` takes such a position as the overflow of the block it follows when the link before ends exactly there and that block's data reaches it (marked `HLP_OVERFLOW` internally; bookmark and annotation keys are written without the mark). `tests/winhelp/hlpcheck.sh` (in `make test`; skipped without OpenWatcom or the reference install) walks all 41 and the reference's 26 with `build/hlpdump -q`. They use macro hot spots (JumpKeyword into another file), BrowseButtons, CreateButton for Index and Up, non-breaking spaces (whc writes A0h), \keepn regions and wide tables of contents: w16 lays a topic out wider than the window when an unbroken run does not fit and gives it a horizontal scroll bar while a line in view is wider than the window (3.1 does the same: clib.hlp's contents, whose wide lines are further down, starts without one, WS09); topics may be tens of thousands of pixels tall (cmix.hlp's contents: 3858 lines), so the views' lists grow in global blocks past 64K, reached through __AHINCR, and positions are longs (scroll bars scaled to 32767), and puts buttons that do not fit on a second row of the bar (3.1: Up alone on the second row of a 417-pixel window).

w16's own test help file, `tests/winhelp/w16test.rtf` and `.hpj`, is built by whc at test time (`tests/winhelp/mkhlp.sh`, pictures drawn by `tests/winhelp/mkpics.py`; `; hlp: w16test` in a scenario) to cover what no available file has. What whc builds (checked by building and reading back with hlpdump): fonts and their attributes (bold, italic, underline, strike-out, small capitals, double underline for jumps), \qc, \qr, \li, \ri, \fi, \sa, \sb, \sl, tab stops with \tqr and \tqc, \box, \keepn non-scrolling regions, \page, the # $ K + ! footnotes, [WINDOWS] secondary windows (jumps into them by window number), [CONFIG] and [MAP], \{bmc/bml/bmr\} with .BMP files (recompressed) and SHG and MRB files (passed through as they are, hot spots and all). What it does not: colours (\cf is an error), tables (\trowd), embedded \pict pictures, .WMF files ("unknown format"), \ewc windows, the other \brdr borders. So w16test's metafile picture is an SHG file holding a type-8 (metafile) picture, which whc passes through; its SHG and MRB are made by mkpics.py; tables remain untested (no file at hand has one). Observed on 3.1 with it (WS07, WS08): visible SHG hot spots are framed (jumps and macros solid, popups dotted, PS_DOT); a metafile picture's size and hot spots are in 0.01 mm (5080 x 2540: 192 x 96 pixels); of an MRB, the 96-dpi 16-colour picture is shown; \box draws a frame from a pixel inside the left indent to two inside the right, a pixel above and two below the text, the text 5 pixels further in; \bml puts the picture at the margin and the text 10 pixels beside it while lines start within its height or right under it; small capitals are drawn in a font 4/5 the size; italic leaves its overhang before the next run; right and centre tabs; jump hot spots into a window (EBh) are green, and EAh/EEh are the popups (visible, without font change) as the hot-spot table in the format description has it; a topic's CreateButton run again says "Unable to add button."; Search's topic list is sorted (LBS_SORT); popups are placed by what opened them: under the click (10 pixels) for text jumps and popups, right under a picture's hot spot, centred right under a macro hot spot's run; the widest line's trailing space counts in a popup's width (box = widest + 10). Scrolling (WS10): a topic scrolls until its end is half way up the window, Page Down moves the window's height less 15, an arrow 15 pixels.

**Bookmarks, annotations, Copy, Print** (`marks.c`, `hfs.c`; WH3, WH4, WS06). Both files are help file systems (16-byte header, FILEHEADERs, a "z4" B+ tree directory); `hfs.c` writes them as 3.1 was seen to: a file is written at the end when it is closed, the directory after it (in place while it fits), a file that outgrows its block moves to the end and its old block heads the free list (its first 8 bytes the block's size and -1, the rest left). `tests/winhelp/hfstest.c` replays the sessions and its output is byte for byte the files 3.1 left.
- WINHELP.BMK (Windows directory, pages of 1024): one file per help file, named its name without extension and its |SYSTEM date in 8 lowercase hex digits ("NOTEPAD29b20284"): the format number (15h), the date, the count, the size, then per bookmark the TOPICPOS of the first link after the topic's header, a long 0 and the name. Bookmark Define ("Bookmark Name:", the topic's title selected, the list, OK, Cancel, Delete); the menu "&1 name"... under Define... (More... after nine: w16's, UNVERIFIED).
- `<name>.ANN` (w16: the Windows directory; pages of 128): @VERSION (08 62 6D 66 01 00), @LINK (count; per annotation that TOPICPOS and two longs 0) and "<TOPICPOS>!0" with the text; made, empty, when Annotate opens. Annotate (sizeable; Save once there is text, Delete with an annotation, Copy, Paste with text on the clipboard; Esc in the text does nothing). An annotated topic starts with the annotation mark (w16's own green paper clip, 14 x 16), a hot spot that opens Annotate.
- WIN.INI `A_WindowPosition` and `C_WindowPosition`: the Annotate and Copy dialogs' places, written as they close.
- Edit > Copy...: "Copy", sizeable, the topic's text as shown (each screen line with its trailing space, CR LF; an empty line after each paragraph record), Copy (the selection, or all) and Cancel. CopyTopic copies it all.
- Print Setup: COMMDLG's; when it fails, "Unable to setup printer.". Print Topic lays the topic out again for the printer (its resolution, half-inch margins) and prints it in pages (w16's: 3.1's printout not observed, no printer in the reference install).

**The WinHelp API** (`api.c`; the request block as USER builds it): HELP_CONTEXT and HELP_CONTEXTPOPUP ([MAP] numbers through `|CTXOMAP`), HELP_KEY, HELP_PARTIALKEY, HELP_MULTIKEY (`|xKWBTREE`), HELP_COMMAND (macros), HELP_CONTENTS, HELP_SETCONTENTS, HELP_HELPONHELP (WINHELP.HLP), HELP_FORCEFILE, HELP_SETWINPOS, HELP_QUIT. One instance serves every application; it ends at HELP_QUIT when no other application that asked is still running. The Registration Info Editor's and File Manager's Help menus and F1 open the same topics as with 3.1's WINHELP (WS05).

**Macros** (`macro.c`; SDK names and short forms): the jumps and popups (JumpId, JumpContext, JumpKeyword, JumpContents, JumpHelpOn, PopupId, PopupContext), buttons (CreateButton, DestroyButton, ChangeButtonBinding, Enable/DisableButton, BrowseButtons), the menu commands (Back, Contents, History, Search, Next, Prev, Exit, FileOpen, Print, PrinterSetup, CopyDialog, CopyTopic, Annotate, BookmarkDefine, BookmarkMore, HelpOn, HelpOnTop, About, SetContents, SetHelpOnFile), menus (InsertMenu, AppendItem, InsertItem, DeleteItem, ChangeItemBinding, Check/UncheckItem, Enable/DisableItem, ResetMenu), marks (SaveMark, GotoMark, DeleteMark, IsMark, Not, IfThen, IfThenElse), windows (CloseWindow, FocusWindow, PositionWindow), ExecProgram, RegisterRoutine (DLL routines called Pascal-style from their prototype letters).

**w16's own:** "w16 Help" and "w16 Help History" where 3.1 says "Windows Help" (3); the screen shown with no file is w16's own design, drawn for the window's size and redrawn whole when it changes (maximized, restored, very small): a navy-to-black 4 x 4 ordered dither across the whole window and the name, a small monochrome picture, centred with its shadow, built in a memory bitmap and copied in one go (3.1 shows its own picture, centred over a fixed dither: WH5; `WHTREE_SCRIPT=nofile tests/whtree.sh` shows w16's at several sizes on w16's tree).

**Not done / UNVERIFIED:** tables (whc cannot make one and no file at hand has one: w16 lays cells out one under another); the non-scrolling region's black line under it matches 3.1 (WS07, WS09); popup widths from narrow windows are within a few pixels of 3.1's (WS02 S6); 3.0 (HC30) files are read but untested; WIN.INI keys only as above.

**Tests:** `tests/whscenarios.sh` (WS01-WS05: each run with Microsoft's WINHELP and w16's on the reference 3.1, WIN.INI and WINHELP.BMK compared byte for byte, screens against `tests/scripts/winhelp.baseline`): NOTEPAD.HLP contents, a jump, Back, a glossary popup, History and Search (WS01); WINHELP.HLP with its bullet pictures, a jump, Page Down, the Glossary secondary window and a popup in it (WS02); CONTROL.HLP and PROGMAN.HLP (WS03); the error messages (WS04, w16's logo and name masked); the Registration Info Editor's and File Manager's help requests (WS05); bookmarks, an annotation, Copy and Print Setup (WS06: WINHELP.BMK, NOTEPAD.ANN and WIN.INI byte-identical). `tests/whtree.sh` runs it on w16's install tree alone (w16's USER, GDI, SHELL; NOTEPAD.HLP, GLOSSARY.HLP and REGEDIT.HLP copied in as input): a file opened by hand, a jump, a glossary popup, Search, and w16's Registration Info Editor's Help > Contents through USER's WinHelp, all passing under KRNL286. With OpenWatcom present: W16TEST.HLP's formatting, pictures, a secondary window, SHG jump and popup, browsing (WS07) and its macros (WS08: all 0 pixels); OpenWatcom's cmix.hlp and clib.hlp, the Search for strcpy and its topic (WS09); scrolling (WS10). Most screens are 0 pixels off; the rest (`tests/scripts/winhelp.baseline`): the Search dialog's default button after Show Topics (WS01 S8), the glossary popup's width (WS02 S6), the error box over the masked logo (WS04), the annotation mark (w16's picture) and a few dialog pixels (WS06).

### 15.11 PIF Editor (`shell/pifedit/`, PIFEDIT.EXE; probes PE1-PE21, PH01-PH19, scenarios PF01-PF13)

`shell/pifedit/` is a clean-room PIF Editor for Windows 3.1, `build/dos/PIFEDIT.EXE` (marked 3.10), in the install tree's Windows directory and Program Manager's Main group ("PIF Editor", after the Registry Editor; 3.1's Setup puts it seventh in Main, after Windows Setup). How 3.1's PIF Editor looks and behaves comes only from black-box observation: SNAP scripts in `tests/scripts/pifedit/obs/` run by `tests/pifobs.sh` (window classes, styles, rectangles, menus and IDs, every control, message texts with their raw bytes from TK.LOG, screenshots, the status line read off the screen) and its WinHelp requests logged by THELP (`tests/pifhelp.sh`, `tests/scripts/pifedithelp/`). PIFEDIT.EXE was never disassembled, hex-read or resource-dumped; dialog templates are computed from the dumped rectangles; the icon is w16's own (`tools/mkpificon.py`).

**Sources for the file format.** The published descriptions were the starting map, as facts only: Ralf Brown's file-format list ("Program Information File"), and the Windows 3.1 User's Guide and Resource Kit for what each setting means. Every offset and bit below was then confirmed by the PIF Editor's own output: one setting changed at a time, the file saved, the bytes compared (PE4, PE7 Standard, PE8 and PE12 386 Enhanced, PE13-PE17 files written by others and re-saved). Reading the .PIF files 3.1's PIF Editor writes is observation of output; Microsoft's own .PIF files are used only at test time from `.tools` (`tests/scripts/pifedit/pifvar.py` makes variants of them under `build/`).

**The .PIF file** (`pif.c`'s header has the table). A new PIF is 545 bytes in both modes: the base section (171h bytes: the Windows 2.x PIF), then the "MICROSOFT PIFEX" header (next 187h, offset 0, length 171h), "WINDOWS 386 3.0" (data 19Dh, 68h bytes) and "WINDOWS 286 3.0" (data 21Bh, 6 bytes), each header 16 bytes of name and three words: next, data offset, data length (FFFFh ends the chain). Byte 1 is a checksum, the sum of bytes 2-170h modulo 256 (Setup's own _DEFAULT.PIF and DOSPRMPT.PIF have 0 there and put the 286 section first: files read keep their layout). Base: title at 02h (30 bytes, blank padded, no NUL), 20h KB maximum and 22h KB minimum ("KB Required" in Standard mode; a Standard-mode save sets the maximum to it when it is larger), program at 24h (63 bytes, upper case, NUL then blanks), 63h flags (80h COM1, 40h COM2, 10h Close Window on Exit, 08h No Screen Exchange, 04h Prevent Program Switch, 02h graphics), start-up directory at 65h (64, upper case), Standard mode's parameters at A5h (64 bytes, NUL, then blanks from the second byte after the text, so an empty field keeps 00 00), E5h-EEh 7F 01 00 FF 19 50 00 00 07 00 (EDh gets 10h with graphics), 16Fh E0h (10h Keyboard), 170h 20h (a Standard-mode save sets 40h). 386 data: +0 KB Desired, +2 KB Required, +4 foreground and +6 background priority, +8 EMS limit, +0Ah EMS required, +0Ch XMS limit, +0Eh XMS required, +10h flags (01h Allow Close When Active, 02h Background, 04h Exclusive, 08h Full Screen, 20h/40h/80h reserve Alt+Tab/Alt+Esc/Alt+Space), +11h (01h Alt+Enter, 02h Alt+PrtSc, 04h PrtSc, 08h Ctrl+Esc, 10h Detect Idle Time, 20h no high memory area, 40h a shortcut key, 80h EMS locked), +12h (01h XMS locked, 02h Allow Fast Paste, 04h Lock Application Memory), +14h video (10h/20h/40h text/low/high graphics, 02h/04h/08h ports not monitored, 01h Emulate Text Mode, 80h Retain Video Memory), +18h the shortcut key's scan code, +1Ah its modifiers (08h Alt, 04h Ctrl, 03h Shift), +1Ch 000Fh once a key is set, +28h 386 Enhanced mode's parameters (64 bytes, as the base's). 286 data: XMS limit, XMS required, flags (01h Alt+Tab, 02h Alt+Esc, 04h Alt+PrtSc, 08h PrtSc, 10h Ctrl+Esc, 20h No Save Screen; 4000h COM3, 8000h COM4). Each mode writes only its own fields (and title, program, directory, Close Window on Exit); switching mode writes the mode left. Defaults: KB Required 128, 386 KB Desired 640, priorities 100/50, EMS and XMS limits 0 (Standard) and 1024 (386), Close Window on Exit, Full Screen, Detect Idle Time, Uses High Memory Area, Emulate Text Mode, Allow Fast Paste.

**Files written by others.** A Windows 2.x PIF (the base alone, 171h bytes) opens after "This file is an old Windows PIF. If you save it, PIF Editor will convert it to the new PIF format." and gets the extension appended (its 386 memory from the base; in Standard mode with bytes FFFF 0064 0032 0400 0000 at +1Eh and blanks from the 386 parameters' fourth byte). A file without the extension section and of another length than 171h (up to the largest observed, 525 bytes) gives "This is an invalid Windows PIF. If you save it, PIF Editor will convert it and information not relevant to Windows may be lost." and is converted the same way. The message comes before the window shows the file; after Open the program's text is selected. A file of another name than .PIF asks first ("This file does not have the .PIF filename extension. ..."); a file that is no PIF, or missing, gives "File not found. / Check to ensure the path and filename are correct." (a missing directory: "Path not found. ...") and leaves what was shown. On reading a file 3.1 cuts each text field after its last character that is not a blank by writing a NUL there: for a field filled with NULs to its end that is the byte after the field. So re-saving Setup's DOSPRMPT.PIF clears the low byte of 20h (640 KB becomes 512) and E5h, and re-saving a PIF made in Standard mode clears the first byte of the following "WINDOWS 286 3.0" name, losing that section for readers that look for it. w16 does the same with `[w16] PifFieldFix=0`; with 1 (the default, docs/w16-settings.md) it writes nothing outside a field. Setup's 286-first PIFs and the editor's own keep their layout and their headers when saved again. Close Window on Exit shows checked for a PIF whose flags byte (63h) is 0, as Setup's are, but the file's bit is saved unless the box is used, or unless the file's checksum was wrong (Setup's are 0: then the box's state is saved; observed with Save As, UNVERIFIED with Save, PE9). The 386 bytes +1Eh-+27h of a converted 2.x PIF in Standard mode are what the 386 data held before from their third byte (the file shown before, or the defaults); in 386 Enhanced mode they were zeros once (PE14) and the earlier file's another time (PF11): not reproduced, so PF11 has no 2.x PIF.

**The window** (PE1, PE2): class "Pif" (CS_BYTEALIGNCLIENT), WS_OVERLAPPEDWINDOW, accepting dropped files, at 12,48 on VGA (one and a half dialog base units across, three down), titled "PIF Editor - (Untitled)" or "- NAME.PIF"; one child dialog per mode at its client's origin (Standard 449 x 297 pixels, 386 Enhanced 447 x 260: the template's width less a pixel), the frame a pixel taller than AdjustWindowRect gives, and a 25-pixel status line under it (a black line, the button face, a sunken box 8 pixels in from the sides, MS Sans Serif 10). It starts in the mode Windows runs in; Mode > the other mode warns ("w16 is not running in 386 enhanced mode. The PIF information you enter may not be appropriate. Are you sure you want to switch to this mode?", OK/Cancel, information icon; "Windows" in 3.1). Controls, IDs, tab order and mnemonics are 3.1's; the check boxes and radio buttons are plain ones the program sets. The status line names the focused control's topic ("Press F1 for Help on Program Filename.", "Choose the Advanced button for more PIF Editor options.") or the menu item under the cursor ("Create new PIF.", "Show settings for standard-mode w16."), and is empty while a message box or common dialog has the focus. Text limits 62 (program), 30, 63, 63; numbers 3 or 5 digits.

**Advanced Options** (386 Enhanced): an overlapped window with a caption (no WS_POPUP), its top left 12, 13 pixels from the frame's client origin with x down to a multiple of 8 (PE19), its own status line; the view's Advanced button disabled while it is up. The shortcut key box (class "PifHKy", w16's code) shows "None" or the key pressed with the modifiers held ("Alt+Ctrl+P", "Ctrl+F1", "Ctrl+Shift+K", the name by GetKeyNameText from the scan code), in the dialog's font; Backspace empties it and leaves the key as it was; a key without Alt or Ctrl is refused at OK ("Invalid shortcut key. / Press F1 to determine valid keys or consult your documentation."). Allow Close When Active asks first (stop icon). Priorities must be 1-10000: leaving the box says "Numbers entered are invalid. / Priorities must be between 1 and 10000.", OK puts the old value back silently and stays.

**Checks** (PE10, PE11, PE15): when the focus leaves a number box and again before a save, out of range: "Numbers entered are invalid." with "KB Required numbers must be -1, or between 0 and 640." (KB Desired likewise), "KB Required numbers must be between 0 and 16384." (EMS, XMS), "KB Limit numbers must be -1, or between 0 and 16384."; the box gets its last good value and the focus. 3.1's message texts have " \n\r\r" between their two parts, which w16 passes on unchanged. After the file is chosen, a program without .EXE, .COM or .BAT: "Invalid extension in program name. / Check to ensure the extension is .EXE, .COM, or .BAT. Choose OK to ignore this error." (OK/Cancel, Cancel the default). New, Open and Exit with changes ask "Save current changes?" (Yes/No/Cancel); a mode switch, or Advanced closed with OK but nothing changed, is no change. Save of an untitled PIF is Save As (COMMDLG, "PIF Files (*.PIF)", "All Files (*.*)", "*.pif", overwrite prompt). Help > About: ShellAbout("PIF Editor").

**Help** (PH01-PH19): PIFEDIT.HLP; Contents is HELP_CONTEXT 50h (Standard) or 51h (386 Enhanced), Standard/386 Enhanced/Advanced Options HELP_CONTEXT 22h/23h/24h (the menu IDs), Search HELP_PARTIALKEY, How to Use Help HELP_HELPONHELP; F1 HELP_CONTEXT of the focused control's ID (in the Standard-mode window the number boxes and the reserved keys have 384h-38Bh instead: KB Required, XMS Required, XMS Limit, Alt+Tab, Alt+Esc, Alt+PrtSc, PrtSc, Ctrl+Esc), F1 in a menu nothing; every request but Search and How to Use Help followed by HELP_SETCONTENTS 50h, 51h or 52h (Advanced); HELP_QUIT at exit.

**Tests:** `tests/pifscenarios.sh` (PF01-PF13, each run with Microsoft's PIF Editor and w16's on the reference 3.1; every saved .PIF and WIN.INI byte for byte, screens within `tests/scripts/pifedit.baseline`): the window and menus in both modes (PF01, PF02), the mode warnings (PF03, PF04: their text says "w16"), every Standard-mode setting saved in six steps (PF05) and every 386 Enhanced and Advanced setting in nine (PF06), the validation messages of both modes (PF07, PF08), the save prompts and Save As over a file (PF09), Setup's DOSPRMPT.PIF and _DEFAULT.PIF, a 2.x PIF, a re-saved PIF and a file that is no PIF opened and saved in each mode (PF10, PF11, `[w16] PifFieldFix=0`), the PIFs for the WINOLDAP and WINOA386 tests (PF12, PF13). `tests/pifhelp.sh` (PH01-PH19) compares the WinHelp requests: all 19 the same (121 requests). Results (installation-05): all thirteen scenarios pass, every .PIF they save (39) and WIN.INI byte-identical with 3.1's; 55 of 59 screens are 0 pixels off, the other four are the texts that say "w16" (two menu status lines, the two mode warnings). A PIF saved by w16's editor (PF12) runs WC1.COM through both WINOLDAPs alike (`WINOLDAP_PIF=... tests/winoldap.sh pif`: PASS, the parameters "a b" typed in the Parameters dialog, the start-up directory C:\WINDOWS\SYSTEM); in 386 Enhanced mode (PF13's PROBE3.PIF, WC1.COM run from SNAP under Microsoft's WIN386) Microsoft's WINOA386 gives the program the 386 section's parameters ("enhargs") and w16's the Standard-mode ones ("stdargs"), as below.

**w16's own:** "w16" for "Windows" in the mode texts and the menu's status lines; the icon; `[w16] PifFieldFix`.

**WINOLDAP and WINOA386.** Both find their section by walking the headers (`winoldap/pifread.c`, §9.8, §12.11), as 3.1's do, so the editor's layout (386 section first) and Setup's (286 first) read alike; WINOA386 takes the parameters from the 386 section (+28h), as 3.1's ("enhargs", not the base's "stdargs": `tests/pifread.sh -c`), and both read title, program and directory as the editor writes them (blank padding trimmed).

## 16. OLE

OLE 1.0 is two DLLs: OLECLI.DLL, the client library that Write, Cardfile
and Packager import (40 of its 259 ordinals, docs/exportdb), and OLESVR.DLL,
the server library of Paintbrush, Sound Recorder and Packager. They talk to
each other only through DDE messages, so the two can be replaced
separately. w16's OLECLI is in `ole/olecli/`; the OLESVR agent's notes are
§16.8 onwards.

### 16.1 Observation tools (probes OC1, OC2, OC3)

- **OC1** (`tests/ole/oc1.c`) is a scripted OLE client. It runs a script
  `C:\OC.TXT` through the documented client API and logs every call with
  its arguments and OLESTATUS, every client callback, and every OLESTREAM
  Get/Put with its size and first bytes. The command list is in the
  source. It also drives other programs: `menu <class> <item>`, `type`,
  `keymenu`, `wmclose`, `wins`, `snap`.
- **OC2** (OCSPY.DLL, `tests/ole/ocspy.c`) installs system-wide
  WH_GETMESSAGE and WH_CALLWNDPROC hooks and logs every WM_DDE_* message
  of every task. Windows are shown as `module:class#n`, numbered in the
  order the log first sees them, so that two runs can be compared. Atoms
  and clipboard formats are shown by name, and data handles by size,
  checksum and first bytes. Posted messages are logged when their receiver
  removes them from its queue, so the log shows the order of retrieval, not
  of posting. A message posted to a window that is already destroyed never
  shows up. The spy also logs faults (TOOLHELP InterruptRegister) with
  their CS:IP and module/segment. OC1's `spytimes 1` puts each DDE line's
  time (ms, GetTickCount) in front. A message identical to the last one
  logged by the get-message hook is logged once (a PeekMessage NOREMOVE
  seen again), so a client's second, identical TERMINATE does not show.
  On w16's USER the spy works since 2026-11-15 (USER now calls the
  WH_GETMESSAGE and WH_CALLWNDPROC hooks); w16's install tree has no
  TOOLHELP.DLL, so a run there borrows the reference's for the test.
- **OC3** (OCSRV.EXE) records how a server is started. It is registered as
  a class's `server` in the scratch REG.DAT (`regset`), logs its command
  line, the show command and the raw PSP tail, then starts the real server
  with the same command line.
- `tests/ole/ocrun.sh <script> <outdir> [OLECLI.DLL]` runs one script on a
  scratch copy of the reference 3.1. Program Manager is the shell and OC1
  starts from `load=`: with an OLECLI importer as `shell=`, the session
  never got to run it. `tests/ole/obsrun.sh` runs `tests/ole/obs/*.txt`
  three at a time. `tests/ole/olecmp.py` compares two runs (logs, streams,
  bitmaps) after masking what differs by nature (see 16.4).
  OC1 ends the session with ExitWindows after 2 seconds of message pumping
  (with a server still exiting, the session never ended) and holds
  carets in their phase (SetCaretBlinkTime 65535) so that screenshots of
  the programs it drives compare.

Observation hazard: Microsoft's OLECLI calls an OLESTREAM's Get/Put with a
DS that is not the client's or its own (OC1's export-prolog Put faulted on
`mov ds,ax` with AX=00FE). A client's stream methods must load their own
DS (MakeProcInstance or `__loadds`).

### 16.2 The API as observed (Microsoft's OLECLI 3.1, OC1)

- `OleQueryClientVersion` = 0x00001001. `OleIsDcMeta`: 1 for a metafile DC.
- `OleRegisterClientDoc` returns a handle `ssss:0000` (a global block).
  `OleRevokeClientDoc` with objects left gives OLE_ERROR_NOT_EMPTY.
  `OleRenameClientDoc`, `OleSavedClientDoc` and `OleRevertClientDoc` send
  nothing for objects that are not open. `OleEnumObjects` gives the
  objects in creation order, then NULL with OLE_OK.
- Operations that need the server return OLE_WAIT_FOR_RELEASE. The client
  then gets OLE_RELEASE, and OleQueryReleaseMethod names the operation
  (OLE_CREATE, OLE_UPDATE, OLE_CLOSE, OLE_ACTIVATE, OLE_OTHER for pokes and
  OleExecute, OLE_REQUESTDATA, OLE_SETDATA, OLE_CREATEFROMFILE,
  OLE_CREATELINKFROMFILE, OLE_LOADFROMSTREAM, OLE_CREATEINVISIBLE). The
  INITIATE broadcasts and the server start happen inside the call. A
  refused EXECUTE or POKE (OleExecute, OleSetData) still ends with
  OLE_OK as the release error.
- Callbacks: OLE_CHANGED after an update (OleUpdate, OleRequestData, a
  link's data); OLE_SAVED when the server sends the `/Save` advise (after
  the last format of the batch: Native, then the picture); OLE_CLOSED when
  the server ends the document conversation; OLE_RELEASE at the end of
  each asynchronous operation.
- Errors observed: an unregistered class gives OLE_ERROR_CLASS (no object).
  A missing file gives OLE_ERROR_OPEN, for OleCreateFromFile and links.
  An empty clipboard makes `OleQueryCreateFromClip` (StdFileEditing and
  Static) and `OleQueryLinkFromClip` return OLE_ERROR_FORMAT. Linking from
  a clipboard that has no ObjectLink gives OLE_ERROR_CLIPBOARD (no
  object). A NULL object gives OLE_ERROR_OBJECT. A blank object (no data
  yet) gives OLE_ERROR_BLANK from OleQueryBounds (rect 0,0,0,0),
  OleDraw, OleGetData of the picture, and OleSaveToStream. OleSaveToStream
  writes the header, class, topic and item first (27 bytes for PBrush).
- `OleQueryBounds` is in HIMETRIC as `0,0,cx,-cy`. A metafile picture's
  extents are used as they are. A DIB's pixels are scaled by
  `pixels*100000/biXPelsPerMeter` (640x442 at 3780 is 16931x11693).
- `OleQuerySize` is exactly the byte count OleSaveToStream writes.
- `OleEnumFormats`: an embedded object gives "Native", "OwnerLink",
  CF_METAFILEPICT. A link gives "ObjectLink", "Native", CF_METAFILEPICT.
- `OleGetData`: Native and the object's own picture come back with OLE_OK
  (they stay OLECLI's). A metafile made for a Paintbrush object comes back
  with OLE_WARN_DELETE_DATA. OwnerLink (embedded) and ObjectLink (link)
  are "class\0topic\0item\0\0" in a block rounded up by GlobalAlloc, whose
  tail is not cleared. The other one gives OLE_ERROR_FORMAT.
- `OleCopyToClipboard`: an embedded object puts "Native", "OwnerLink" and
  CF_METAFILEPICT. A link puts "OwnerLink" (holding class, file and item),
  "Native" and CF_METAFILEPICT.
- `OleCreateFromClip` (StdFileEditing) and `OleClone` are synchronous
  (OLE_OK, no DDE), and so is `OleCreateInvisible` with fLaunch FALSE.
  `OleEqual` on two copies is OLE_OK.
- `OleSetBounds`, `OleSetHostNames`, `OleSetColorScheme` and
  `OleSetTargetDevice` on an object that is not open return OLE_OK and
  send nothing.
- `OleDraw` of a metafile picture into a metafile DC fails with
  OLE_ERROR_DRAW, after it has recorded SaveDC and IntersectClipRect.
  Drawing a Paintbrush object into one works.

### 16.3 The DDE protocol (OLECLI side)

OLECLI makes hidden top-level windows: `OleSrvrWndClass` (title "") for a
server's system topic and `OleDocWndClass` (title "Window Name") for a
document. INITIATE goes out as a broadcast (SendMessage to -1).

- **Finding a server.** For a new or embedded object, OLECLI INITIATEs
  (class, "OLEsystem"), then (class, "System"). If nobody answers, it
  destroys that window and starts the server with
  `LoadModule(<server>, tail " -Embedding [file]", show)`. The server
  comes from REG.DAT, `<class>\protocol\StdFileEditing\server`. The
  string's first character, the space, is taken as the length byte, so the
  server's PSP tail has length 32 and its command line is "-Embedding " or
  "-Embedding <file>" (OC3). The show command is SW_SHOWNORMAL to create
  or activate and SW_HIDE for a file or a link. A new window then
  INITIATEs (class, "OLEsystem") once. For a link or a file, OLECLI first
  INITIATEs (class, <file>) on a document window, then tries the system
  topic, then starts the server with the file name and INITIATEs
  (class, <file>).
- **Embedded document names**: `<doc class>%<doc name>%<object name>#nnn`
  (e.g. `OC1%OCDOC%obj0#001`). nnn counts up from 001 for each embedded
  object OLECLI names: one created, loaded, made from a file or the
  clipboard, cloned, or renamed with OleRename. Links don't take a number.
- **OleCreate**: EXECUTE `[StdNewDocument("<class>","<name>")]` on the
  system topic, then INITIATE (class, name). Then ADVISE "/Save" Native,
  ADVISE "/Save" <picture>, ADVISE "/Close" Native, ADVISE "/Close"
  <picture> (DDEADVISE flags 8000: fAckReq), and EXECUTE
  `[StdShowItem("")]`. Then OLE_RELEASE. OleCreateInvisible leaves out
  StdShowItem. If the server refuses an advise (PBrush refuses CF_DIB),
  OLECLI sends `[StdCloseDocument]` instead and still ends with OLE_OK; the
  object stays blank.
- **OleActivate** of an embedded object that is not open: EXECUTE
  `[StdEditDocument("<name>")]` on the system topic, INITIATE the document,
  POKE item "" Native (DDEPOKE flags 0: OLECLI frees the data), the four
  ADVISEs, then EXECUTE `[StdDoVerbItem("",<verb>,<TRUE|FALSE: fShow>,
  <TRUE|FALSE: NOT fTakeFocus>)]`. The last argument is the inverse of
  fTakeFocus (seen both ways).
- **OleUpdate** of an open embedded object: REQUEST item "" Native, then
  the picture, then OLE_CHANGED. The DATA comes back with flags 3000
  (fResponse|fRelease).
- **OleClose** of an embedded object: EXECUTE `[StdCloseDocument]`. The
  server then TERMINATEs the document and the system conversations, and
  OLECLI answers each TERMINATE and destroys its window. On OleDelete,
  OLECLI sends `[StdExit]` and TERMINATE on a system conversation whose
  server window is still there.
- **Pokes** (only while open): `StdHostNames` (format "Binary": WORD 0,
  WORD offset of the document name, "app\0doc\0"), `StdDocDimensions`
  ("Binary": the words cx,-cy,cx,-cy of the rectangle, whatever its
  origin), `StdColorScheme` (the LOGPALETTE), `StdTargetDevice` (the
  client's OLETARGETDEVICE block). OleSetData pokes item "" with the
  client's format.
- **Server-initiated**: DATA on "/Save" (flags 2000) updates Native and the
  picture and gives OLE_SAVED after the last format. Data on "/Close"
  (a changed object closed by the user, Yes to the server's question)
  gives OLE_CHANGED; OLECLI then posts `[StdCloseDocument]` and gives
  OLE_CLOSED at once, before the server's TERMINATE arrives (srvclose). If
  that callback is missing, Write drops a newly inserted object. TERMINATE gives
  OLE_CLOSED. OleDelete and OleRelease of an open embedded object both send
  `[StdCloseDocument]`.
- **Links**: (OleCreateLinkFromFile) INITIATE (class, file), then ADVISE
  item <item> Native (no /Save), ADVISE "StdDocumentName" "Binary", REQUEST
  Native, OLE_CHANGED. If the server shows no visible window (OLECLI
  started it hidden), TERMINATE. A link loaded from a stream whose server
  has the file open connects and stays connected (hot link). OleActivate
  of a link that is not connected starts the server shown (when it is not
  running), connects as above and sends StdDoVerbItem; a link set to
  oleupdate_oncall gets StdDoVerbItem alone, with no advise or request.
  OleUpdate of a link that is not connected connects, REQUESTs Native and
  lets the server go again. OleClose of a link just TERMINATEs.
  OleCreateFromFile is the same, but without the advises; its server is let
  go after the REQUEST.

### 16.4 The stream format

Each field is its own Put call, in this order (see `ole/olecli/stream.c`;
the published MS-OLEDS "OLE 1.0 format" describes the same layout):

    DWORD 0x00000501, DWORD 1 link | 2 embedded | 3 static
    string class        DWORD length incl. NUL + bytes; "" is DWORD 0 alone
    embedded: string topic (""), string item (""), DWORD size, Native
              (size = the Native block's GlobalSize: 141558-byte BMP -> 141600)
    link:     string file, string item, string network name, DWORD 0,
              DWORD update option
    picture:  DWORD 0x0501, DWORD 0 (none) | 5, string class:
      "METAFILEPICT": DWORD xExt, DWORD -yExt, DWORD 8+n, METAFILEPICT as
                      in memory (mm, xExt, yExt, the hMF handle value!),
                      n bytes of metafile = its GlobalSize (past the
                      metafile's own size: whatever memory held)
      other (generic): DWORD 0 + string format name (a registered format),
                      DWORD size, data

Two fields are non-deterministic in Microsoft's own output: the hMF word,
and a metafile's bytes past its mtSize up to the GlobalSize rounding. The
same document saved twice by Microsoft's OLECLI differs there, so
byte-identity is checked with those bytes masked (`olecmp.py`).

Paintbrush (class "PBrush", render draw) gets special treatment: OLECLI's
built-in handler (the PBxxx exports). Only Native is advised and
requested; the Native data is a .BMP file; the embedded object is saved
with no picture (format 0); bounds and drawing come from the DIB; OleGetData
of CF_METAFILEPICT builds a metafile: SetWindowOrg(0,0), SetWindowExt(w,h),
CreatePalette of the DIB's colours, SelectPalette, RealizePalette,
StretchDIBits(SRCCOPY, DIB_RGB_COLORS), then the stock palette selected and
realized again and the DIB palette deleted. A PBrush link is saved with a
generic picture: class "PBrush", format "Native", the BMP file.

### 16.5 w16's OLECLI.DLL

`ole/olecli/`, built into `build/dos/OLECLI.DLL` and installed as
`build/w16/SYSTEM/OLECLI.DLL` (the tree's Cardfile imports it). Large
model; every entry point loads OLECLI's DS (`OLEAPI`: `__loadds`); no C
library (`i4.asm` has the 32-bit helpers and `__PIA`).

- **Exports**: all 259 of 3.1's ordinals, with the same names and
  residency (`tools/mkoleexp.py` from `docs/exportdb`). The OleXxx calls
  validate the object and call through its vtbl, whose entries are the LExxx
  exports. MF/BM/DIB/GEN share the LE methods. The ERRxxx methods return
  OLE_ERROR_STATIC. DEFxxx and the PB creation entries are the
  object-handler interface. The 81 ordinals 955-1035 have no name in 3.1
  and nothing imports them: they are stubs returning 0, made ordinal-only
  by `tools/nestripnames.py`, since wlink cannot export without a name.
  The resources are 3.1's dialog CONNECTDLG ("Connect Network Drive"), its
  two strings and a VERSIONINFO.
- `olecli.c`: client documents, objects (shared global blocks that stay
  locked; a far pointer to one is the OLEOBJECT) and the API. `le.c`:
  the vtbl and the creation functions. `dde.c`: the DDE engine. An
  operation is a list of steps (connect, execute, advise, request, poke,
  terminate, ...). Steps run inside the API call until one has to wait for
  a posted answer; the window procedure then carries on, and the list ends
  with OLE_RELEASE. `stream.c`, `pres.c`, `clip.c`, `reg.c` and `defs.c`
  hold the rest.
- Blocks Microsoft's OLECLI leaves uninitialised (the rounding of Native
  copies, poke blocks) are zeroed, so w16's output is deterministic.
- A build with `-dOLETRACE` logs every step to `C:\OLECLI.LOG`.

### 16.6 Tests

- `tests/olescenarios.sh [NAME...]` runs each scenario `tests/ole/obs/NAME.txt`
  (OC1 scripts, among them `write1` and `card1`, in which OC1 drives
  Write and Cardfile: Insert Object > Paintbrush Picture, Exit & Return,
  save, reopen) with Microsoft's OLECLI and OLESVR and with each other
  combination: `-c "w16:ms ms:w16 w16:w16"`, where the server side is
  `build/dos/OLESVR.DLL` once the OLESVR agent's is there. Every run is
  compared with the all-Microsoft one by `tests/ole/olecmp.py`.
- Boot-test case **olecli**: part `tree` starts Cardfile from the install
  tree's Accessories group, under its KRNL386 and its KRNL286; part
  `cardw16` runs card1 with w16's Cardfile as the client
  (`OLESCEN_FILES`); one part per scenario runs olescenarios.sh. Case **missingdll@tree** takes OLECLI.DLL
  out of its copy of the tree.

Known differences, masked in olecmp:
- In a Write document, Write's own object number (from the clock; Microsoft
  against Microsoft differs there too), the word 8 bytes on, and the
  property pages: olecmp compares the text and the pictures. Microsoft's
  OLECLI gives the same values in every run there, w16's others (0x1257
  against 0x1227; local offsets 0x30 apart). They look like Write's own
  handles, which depend on the memory layout (UNVERIFIED).
- The hMF word and the tail of a metafile in a stream.
- The rounding of Native blocks past a .BMP's or RIFF's own size.
- In the metafile OLECLI makes of a Paintbrush DIB, the row padding of the
  DIB in the StretchDIBits record. Microsoft's differs from the Native
  DIB's in a way that is the same every run, but w16 doesn't reproduce it.
  UNVERIFIED: perhaps it goes through a device bitmap first.

### 16.7 UNVERIFIED, and not done

- These were not observed: OleCreateFromTemplate
  (`[StdNewFromTemplate("class","name","template")]` is a guess);
  StdOpenDocument for a running server that lacks the document;
  OleReconnect and OleCopyFromLink of a link that is not connected;
  OleLockServer/OleUnlockServer; OleSetData and OleExecute on an object
  that is not open (w16: Native taken, or OLE_ERROR_NOT_OPEN); links with
  oleupdate_onsave (w16 advises "<item>/Save"); links with a picture
  other than Paintbrush's; static objects' stream layout; BITMAP and DIB
  presentations in streams; the OLE_QUERY_PAINT rate (w16: every 64
  metafile records); OLE_QUERY_RETRY on a busy server (not implemented);
  how long OLECLI waits for a server it started (w16: 30 s of INITIATEs).
- The parameters of DEFxxx, PBxxx, CHECKNETDRIVE and SETNEXTNETDRIVE,
  and what ordinals 955-1035 do.
- Object handlers (a class's `handler` key) are not loaded. The network
  dialog is never brought up; links on network drives keep their name
  (SETNETNAME, WNetGetConnection), but nothing is reconnected.
- Microsoft's OLECLI calls a client's OLESTREAM methods with a foreign DS
  (16.1); w16 calls them with its own. A client that relies on either is
  broken on 3.1 anyway.

### 16.8 What w16's OLESVR needs (seen from the client side)

The conversation above is the contract: a system topic "OLEsystem" (and
"System") on a hidden window that answers INITIATE with the class name.
EXECUTE takes StdNewDocument, StdEditDocument, StdOpenDocument,
StdCloseDocument, StdExit, StdShowItem and StdDoVerbItem with arguments as
quoted; a refused advise of an unsupported format must be NACKed. ADVISE
takes "/Save" and "/Close" items for embedded objects, "" and
"StdDocumentName" for links. REQUEST and POKE take Native and the picture
(DATA flags 3000 for a request, 2000 for advise data). The "Binary" pokes
StdHostNames, StdDocDimensions, StdColorScheme and StdTargetDevice have
the layouts in 16.3. TERMINATE comes first from the server when the user
closes it. Servers are started as `<server> -Embedding [file]`, with
SW_SHOWNORMAL or SW_HIDE. OLECLI copies every picture it is sent: blocks
the server owns may go when it exits.

### 16.9 OLESVR: observation tools (probes OS1, OS2, OSSPY)

**Tools** (`tests/olesvr/`, built by `make osprobes`):
- `OSSPY.DLL`: system-wide WH_GETMESSAGE, WH_CALLWNDPROC and WH_CBT hooks that log every WM_DDE_* message any window receives, decoded (atoms by name, data blocks with flags, format name, GlobalSize, a checksum and the bytes; metafile pictures through their METAFILEPICT and metafile; windows as `MODULE:Class#n`, numbered by lifetime), to `C:\OS.LOG`. The probes write their own lines to the same file (`OsPrintf`), so callbacks and API results fall between the messages that caused them.
- `OS1.EXE`: an OLE server that logs every call OLESVR makes into its OLESERVERVTBL, OLESERVERDOCVTBL and OLEOBJECTVTBL, with arguments and results; options pick the class, OLE_SERVER_SINGLE, forced return codes; a script (`C:\OS1.TXT`, run from a timer) calls the server APIs (register/revoke/rename/save documents, notifications through the client's CallBack, block/unblock, revoke objects, invalid arguments).
- `OS2.EXE`: the shell of the run; a client through OLECLI (create, links, update, activate, set data/host names/bounds/target device/colour scheme, streams) and a raw DDE client (initiate, execute, request, poke, advise, unadvise, terminate) for what OLECLI never sends. It also starts `SNAP.EXE` for the UI scenarios (Write, Cardfile, Paintbrush).
- `tests/olesvr/osrun.sh NAME OUT [svr] [cli]` runs one script of `tests/olesvr/scripts`; `osobs.sh` several; `tests/olesvrscen.sh` all of them with Microsoft's OLESVR and OLECLI and with w16's, and compares (16.12).

### 16.10 The server side as observed (Windows 3.1, OLESVR with OS1, Paintbrush, Packager, Sound Recorder; OLECLI with OS2, Write, Cardfile)

Windows and topics:
- `OleRegisterServer` creates a hidden overlapped window of class **SrvrWndClass** (module OLESVR, title "Srvr", 8 window extra bytes). It answers `WM_DDE_INITIATE` (sent) for app = the class name and topic **"OLEsystem"** or **"System"** (both atoms required: no wildcards; the reply's atoms are new ones, the topic spelt as asked). OLECLI tries OLEsystem, then System, starts the server (`server -Embedding`, or `server -Embedding file` for a link), and initiates OLEsystem again.
- An **OLE_SERVER_MULTI** server (one document per instance) that has a document does not answer the system topic (so OLECLI starts another instance), and refuses StdNewDocument, StdNewFromTemplate, StdEditDocument and StdOpenDocument of another document. OLE_SERVER_SINGLE takes any number, including two with the same name. `use` is not checked (7 registered).
- Each document has a window of class **DocWndClass** (title "Doc", WS_CHILD, 8 extra bytes), a child of the server's window: the server window, which gets the broadcast, passes an INITIATE for (class, document name) on to its documents' windows in their order, and each answers itself (so with several documents of one name the oldest answers first). Each object a document hands out has a child window of the document's, class **ItemWndClass** (title "ITEM", WS_CHILD, 4 extra bytes), made when GetObject gives it. Documents are found by atom: a name that is no atom (GlobalFindAtom 0) finds the document named "". Each client in conversation with a server or document window is also a property of that window, named "@" and the client's handle in four digits made as '0' + nibble (117Ch is "@117<"), with the handle as its value; it goes when the conversation ends. A client that advised on an object is likewise a property of the object's window (same name; its value looked like a handle of the library's own: w16 stores the near address of the client's first advise, and the comparison leaves that value out) (seen with OS2's `wins`, which dumps OLESVR's windows and their properties; the `reuse` scenario shows that the handles later windows get are then the same as with 3.1's OLESVR).
- The class must be registered with `class\protocol\StdFileEditing\server` (the `verb` keys, `StdExecute` alone, an empty `protocol` key: OLE_ERROR_CLASS 20). The same class may be registered twice. `protocol\StdExecute\server` decides whether unknown commands reach the server's Execute.

System topic EXECUTE (ACK status 8000h on success, 0000h on failure; the ACK carries the command block back):
- Commands: `[StdNewDocument("class","doc")]` -> Server.Create(lhdoc, class, doc), `[StdNewFromTemplate("class","doc","template")]` -> CreateFromTemplate, `[StdEditDocument("doc")]` -> Edit(lhdoc, *registered class*, doc), `[StdOpenDocument("doc")]` -> Open (an open document of that name: ACK without a call), `[StdShowItem("doc","item"[,B])]` -> GetObject (first time) and Show, `[StdCloseDocument("doc")]` -> Doc.Close, `[StdExit]` -> Server.Exit. The class argument is not checked against the server's.
- Form: exactly one bracketed command, nothing after `]`, no spaces, names case-insensitive, strings in double quotes without escapes, booleans TRUE/FALSE (any case; not 1/0), verbs unsigned decimal. A malformed command gets 0000h without any call - except that the arguments are read only as far as the command needs: `[StdNewDocument("c","d","x")]` calls Create, then Doc.Release, and fails.
- Not a Std command: with StdExecute registered the whole text (also `[A][B]`) goes to Server.Execute (8000h if it returns OLE_OK); otherwise 0000h. Text not starting with `[` fails.
- REQUEST on the system topic gets a positive ACK and no data; POKE gets no answer at all.

Document topic:
- **EXECUTE**: `[StdShowItem("item"[,B])]` -> Show(obj, fTakeFocus); `[StdDoVerbItem("item",verb,S,B)]` -> DoVerb(obj, verb, fShow = S, fTakeFocus). In both B is the *inverse* of fTakeFocus (OLECLI's OleActivate(..., fTakeFocus TRUE) sends FALSE). `[StdCloseDocument]` -> Doc.Close. Others: Doc.Execute with StdExecute registered, else 0000h. Same strict form.
- **REQUEST** item, cf -> GetObject (the first time the document sees the item; objects are kept per document, across conversations, until revoked), then EnumFormats from 0 until cf or 0 (every time), then GetData. The answer: `WM_DDE_DATA` flags 3000h (fResponse | fRelease), the request's atom, a block of GlobalSize(the server's handle) + 4 bytes, zero-filled, with the handle's bytes; for CF_METAFILEPICT (and the other handle formats) the value is the server's handle. The server's block is freed. A format the object does not list, or a failing GetData: ACK 0000h. "StdDocumentName" is an ordinary item here.
- **POKE**: `StdHostNames` (Binary: {client name offset, document name offset, names}) -> Doc.SetHostNames (the client name taken at the start of the names whatever the first word says; OLECLI writes 0 there); `StdDocDimensions` ({cx, cy}) -> Doc.SetDocDimensions with RECT {0, cy, cx, 0}; `StdColorScheme` (a LOGPALETTE) -> Doc.SetColorScheme with a pointer into the block; `StdTargetDevice` -> kept, and given to each object (a copy) by SetTargetDevice just before its next GetData; any other item -> GetObject (first time) and SetData(cf, a new block of GlobalSize(poke) - 4 bytes with the value; the handle for picture formats), with no format check. ACK 8000h if the call returned OLE_OK.
- **ADVISE** item[/Change|/Save|/Close] (another suffix: 0000h without a call). `StdDocumentName` is recorded without a call. Otherwise GetObject (first time), the format check, and the advise is recorded. A successful advise is acknowledged only when it has fAckReq; a failed one always (0000h).
- **UNADVISE**: GetObject (first time, even for an item never advised), 8000h if such an advise existed, else 0000h.
- Notifications (the OLECLIENT OLESVR passes to GetObject; its CallBack returns OLE_OK): OLE_CHANGED, OLE_SAVED, OLE_CLOSED -> every client with an advise of that kind (no suffix = /Change) on the object gets, in order, every format it advised on the object (whatever the kind of those advises), as `WM_DDE_DATA` flags 2000h (never fAckReq), item "item", "item/Save" or "item/Close"; GetData per client and format; no data (hData 0) when the client's latest advise of that kind deferred updates. OLE_RENAMED -> GetData(ObjectLink), then WM_DDE_TERMINATE to every conversation that used the object (once per object). `OleSavedServerDoc` = OLE_SAVED for every object; `OleRevertServerDoc` does nothing visible; `OleRenameServerDoc` sends the new name (Binary, block GlobalSize(name) + 4) to the StdDocumentName advises and renames the topic.
- **TERMINATE** from the client: answered (after the Release below, when that applies); its advises go.

Revoking and releasing:
- `OleRevokeServerDoc`: Obj.Release for every object (in the order they were made), then WM_DDE_TERMINATE to each client - posted after the message being handled is answered (the ACK of a StdCloseDocument comes first) - and OLE_WAIT_FOR_RELEASE; Doc.Release when the last client has answered. Without clients: Doc.Release at once, OLE_OK. Doc.Close is not called by a revoke.
- `OleRevokeServer`: every document as above (Doc.Release at once for those without clients), the system conversations terminated; Server.Release when everything is gone (at once, OLE_OK, when nothing is connected).
- `OleRevokeObject`: Obj.Release, then WM_DDE_TERMINATE at once to each conversation that used the object (object by object); OLE_OK.
- Server.Release without a revoke (the server may quit): when a document conversation ends (client's TERMINATE, or a revoked document released) and no conversation is left; when a system conversation ends only for an OLE_SERVER_MULTI server none of whose documents made on the system topic still waits for its first client (Packager, whose closed document stays registered, is let go; a SINGLE server never is there). Release is called before the client's TERMINATE is answered: a server that revokes itself inside Release gets OLE_OK at once (Obj.Release, Doc.Release and its second Release happen inside) and the client gets the one TERMINATE the revoke posts; a server that revokes later finds nothing connected (the `relq` scenario, with OS1's /Q and /QQ).
- Blocking: `OleBlockServer` queues every posted DDE message for the server's windows (INITIATE, being sent, is answered); a second call returns OLE_ERROR_SERVER_BLOCKED (47). `OleUnblockServer` handles one queued message per call and says whether more are queued; the block ends when the queue is empty.
- Results: invalid handles OLE_ERROR_HANDLE (18) (also a revoked document); NULL pointers OLE_ERROR_ADDRESS (16) (class name, OLESERVER, LHSERVER*, LHSERVERDOC*, the BOOL* of OleUnblockServer, OleRevokeObject(NULL)); a NULL OLESERVERDOC is accepted. `OleQueryServerVersion` = 00001001h.
- Paintbrush, embedded by Write and Cardfile: StdNewDocument (`WINWRITE%(Untitled)%<time>#001`: Write's object id is the time, also stored in the .WRI), advises /Save and /Close Native, StdShowItem, StdHostNames; File > Update sends /Save, File > Exit & Return /Close then the revoke. Editing again: StdEditDocument, POKE Native, advises, StdDoVerbItem("",0,TRUE,FALSE).

### 16.11 w16's OLESVR.DLL

Same module name, ordinals, names and residency as Windows 3.1's (WEP resident, the rest non-resident; 21-31 are the library's own procedures, which nothing imports: w16 exports its window procedures and the object CallBack there and harmless stubs for the rest). Servers, documents, objects, advises, conversations and blocked messages are linked lists in the library's local heap, spilling into global blocks when it is full: no limits. Its windows are 3.1's (16.10); the window extra bytes hold the server, document or object. The parser, notifications, revoke ordering and Release rules follow 16.10. `make install-tree` puts it in `SYSTEM\OLESVR.DLL`.

UNVERIFIED (not observed, or inferred from one case): what a server's own task exit without revoking does (w16 posts TERMINATE to the clients and forgets the server); OLE_RENAMED's purpose (the ObjectLink is fetched and dropped); deferred advises' DATA (hData 0, observed only for /Change); ADVISE/UNADVISE on the system topic (ignored); a revoked document's or server's second revoke (OLE_WAIT_FOR_RELEASE); a CallBack through a revoked object's OLECLIENT (OLE_ERROR_HANDLE); a document-name offset in StdHostNames past the block (3.1 reads beyond it; w16 gives "").

**What w16's OLESVR is missing** (against Windows 3.1's, as far as observed):
- **A second TERMINATE from a dying server.** When Paintbrush (a link) or Packager (after a close) quits as OLESVR lets it go, 3.1's OLESVR sometimes posts the client a second WM_DDE_TERMINATE from the dying window: in three runs of the `pblink` scenario Packager's came twice once, Paintbrush's twice once, neither once. w16's posts one; the comparison leaves the repeat out (osnorm.py). Probably 3.1 terminates the conversations still on its books when the window is destroyed, and whether the client's answer has come in by then is a matter of timing (UNVERIFIED).
- **The library's own exports.** Ordinals 21-31 exist with 3.1's names, but only 21-23 (the window procedures) and 26 (the objects' CallBack) do anything; SENDDATAMSG, FINDITEMWND, TERMINATECLIENTS, TERMINATEDOCCLIENTS, DELETECLIENTINFO, SENDRENAMEMSG and ENUMFORTERMINATE are stubs returning 0 (nothing in the install imports them, and their parameters are unknown). The entry table holds two more unnamed entries than 3.1's (the linker's), and 3.1's unnamed ordinal 32 is not reproduced as such.
- **The value of an object window's client properties** (3.1's looks like a handle in its own heap; w16 stores the near address of the client's first advise): same name and room in USER's heap, different value.
- **Uninitialised memory.** Blocks 3.1 does not zero (the copy SetData gets, the target device copy) hold whatever the global heap held; w16's hold what its heap held, which matches only when the heap history does.
- **Parameter validation** is IsBadReadPtr/IsBadWritePtr on the pointers OLE_ERROR_ADDRESS was observed for, nothing more; 3.1's handling of other invalid pointers (a fault, or KERNEL's parameter validation) was not probed.
- **Not observed, so not done:** DDE busy (fBusy) answers and retries (OLESVR never answers busy; a client's busy ACK of DATA is ignored); OLE_RENAMED's real purpose; how a server whose task ends without revoking is cleaned up; 386 Enhanced mode and WfW 3.11's OLESVR (only Standard mode on 3.1 was probed).

### 16.12 Tests and results

- `tests/olesvrscen.sh [-c COMBOS] [NAME...]`: the 18 scripts of `tests/olesvr/scripts` (raw DDE: raw, single, exec, bad, block, reinit, relq, reuse, wins; OLECLI driven by OS2: basic, link, multi, notify, pblink with Paintbrush, Packager and Sound Recorder, pbrush; the UI through SNAP: wrpb and wredit (Write embeds and edits a Paintbrush picture, saves a .WRI), cardpb (Cardfile, a .CRD)) with Microsoft's OLESVR and OLECLI and with w16's OLESVR: the DDE log and callbacks (`tests/olesvr/osnorm.py`: OS1's and OS2's waits, Write's clock-made object ids, uninitialised block tails masked), the saved files (masked as `tests/ole/olecmp.py` does) and the screens (Cardfile's caret masked) must be equal.
- Results (installation-05, 2026-11-16): w16's OLESVR against Microsoft's, with Microsoft's OLECLI: all 18 equal (block and multi after their last masking change: a timing-dependent log line, and the rounding past OS1's 10-byte Native, which OLECLI keeps and pokes back). OC1's scenarios (`tests/olescenarios.sh -c "w16:ms ms:w16 w16:w16"`, 16.6): all 36 pairs equal, so w16's OLECLI and OLESVR work together and with Microsoft's in all four combinations.
- With w16's OLECLI, OS2 shows OLECLI differences OC1 does not (for the OLECLI side, 16.5): the clipboard formats registered in another order (Native's number differs), no INITIATE (class, "System") before starting a server, a link advised on Native instead of the picture, StdDocDimensions in a 32-byte block, and, when an embedded object is edited again, StdHostNames poked as format 6 without its two offset words (Paintbrush's caption then lacks "Paintbrush Picture in").
- Boot-test case **olesvr**: part `tree` (`tests/olesvr/ostree.sh`: the install tree alone with w16's OLECLI and OLESVR, the reference's Paintbrush and REG.DAT for the test only; OS2 as the shell embeds, updates, closes and deletes a Paintbrush picture; its log equal to the same tree with Microsoft's OLESVR) and one part per scenario (`olesvrscen.sh -c w16-ms`). Cardfile inserting a Paintbrush picture in the tree fails there with "Object not created because server application could not be found", with w16's or Microsoft's OLESVR, OLECLI, SHELL and CARDFILE alike, while OS2's OleCreate("PBrush") in the same tree works: the launch from Cardfile's task fails outside the OLE libraries (UNVERIFIED where: KERNEL's LoadModule from that task is the suspect). Under w16's USER the spy's system-wide hooks see no DDE messages.

## 17. COMMDLG

w16's `COMMDLG.DLL` (`shell/commdlg/`, `build/dos/COMMDLG.DLL`, installed as `SYSTEM\COMMDLG.DLL` by `make install-tree`) is a drop-in replacement for Windows 3.1's common dialog library: copied over the original in a scratch copy of the reference install, its dialogs are meant to look and behave as the original's for every program (Notepad, Write, Paintbrush, Cardfile, Program Manager's Browse, Control Panel, ...). Windows 3.x only: w16 builds no Windows 2.x COMMDLG (the user's decision: 2.x builds are Program Manager's alone). Everything here was observed black-box on the reference 3.1 with the probe CD1 (`tests/apps/cd1.c`: calls each function with the flags and values of a script line and logs the returned structure, the hook's messages and which window procedures the dialog's controls have) driven by SNAP; the original's dialog templates, strings and bitmaps were read as data (`tools/nedlg.py`, a resource listing). Nothing was disassembled.

**Exports.** The original's entry table has 48 entries: 26 exported, by name (GetOpenFileName 1, GetSaveFileName 2, ChooseColor 5, FileOpenDlgProc 6, FileSaveDlgProc 7, ColorDlgProc 8, LoadAlterBitmap 9, FindText 11, ReplaceText 12, FindTextDlgProc 13, ReplaceTextDlgProc 14, ChooseFont 15, FormatCharDlgProc 16, FontStyleEnumProc 18, FontFamilyEnumProc 19, PrintDlg 20, PrintDlgProc 21, PrintSetupDlgProc 22, EditIntegerOnly 23, WantArrows 25, CommDlgExtendedError 26, GetFileTitle 27, WEP 28, dwLBSubclass 29, dwUpArrowHack 30, dwOKSubclass 31), and 22 more (32-53) with entry flags 00, not exported and unnamed: the linker's entry points into its moveable code segments (Wine's spec calls 32 DllEntryPoint, a Win95 name). WfW 3.11's COMMDLG, a later build (it adds a "Network..." string and fixes "Fuschia"), has only the 26 exported ones, which is why the export database lists 26 there. Nothing imports them: `tools/neimports.py --importers COMMDLG` over the win31 and wfw311a installs finds 18 and 12 importers using only ordinals 1, 2, 11, 15, 20 and 26 (wfw311a: no 11), Setup's SETUP.EXE names COMMDLG only as a file, and Windows/386 2.11 has no COMMDLG at all (its Control Panel's "COMMDLG" is a dialog resource of its own). w16 exports the 26 and leaves 32-53 out. The dialog procedures are the exported ones (GetWindowLong(DWL_DLGPROC) of an Open dialog is FILEOPENDLGPROC's address, Save As FILESAVEDLGPROC's); the others are subclass procedures, observed on these controls: dwLBSubclass the Open dialog's folder list, dwOKSubclass its OK button, WantArrows the colour dialog's two colour boxes, EditIntegerOnly the Print dialog's From/To/Copies, dwUpArrowHack Print Setup's printer list. LoadAlterBitmap loads one of COMMDLG's bitmaps with a colour of its table replaced (UNVERIFIED: its exact parameters; nothing imports it). The original is marked as a Windows 3.00 module, and so is ours: USER sends CBN_SELENDOK/CANCEL only to dialogs of 3.10 modules, and the hook logs differed until it was.

**Common to all.** A dialog's state is a window property ("DlgInstData", the original's string). The hook (xx_ENABLEHOOK) sees every message the dialog procedure gets, before it; a nonzero answer ends the processing. The set-up's own notifications reach the hook before WM_INITDIALOG (lParam: the caller's structure). Templates: ENABLETEMPLATEHANDLE (hInstance is the template), ENABLETEMPLATE (lpTemplateName in hInstance), else ours, which reproduce the original's layouts, control IDs (dlgs.h) and styles exactly. Help sends HELPMSGSTRING ("commdlg_help") to the owner, wParam the dialog. The registered messages are the SDK's strings ("commdlg_FileNameOK", "commdlg_LBSelChangedNotify", "commdlg_ShareViolation", "commdlg_ColorOK", "commdlg_SetRGBColor", "commdlg_FindReplace"): the probe registers them and the original's hooks receive exactly those numbers. CommDlgExtendedError is one value for all tasks.

**Open / Save As** (templates 1536, 1537 with OFN_ALLOWMULTISELECT; title "Open"/"Save As", Save's type label "Save File as &Type:"). The lists hold names as DOS gives them (capitals) and draw them in lower case: files 4 pixels into their item, items as high as the font; folders 16 pixels high, the picture (16x16, our own art) 4 + 4n pixels in at depth n, the name 20 pixels after the picture: the path's folders open (the current one in its own picture), its subfolders closed, sorted; the selection is shown only while the list has the focus, as the whole item highlighted with a focus rectangle. Drives: "x: label" (fixed disks, label in lower case), "x: \\server\share" (network), "x:" (floppies). Save As draws its file list in gray. The name box is moved one pixel left and widened by two to the file list's edges. Both combo boxes use the extended keyboard interface (F4 does nothing, Down drops the list). Flags bits 10000000h and 20000000h are set in the caller's structure while the types or drives list is down (still set if the dialog ends with it down). The set-up and every change send the dialog itself WM_COMMAND (cmb1 or cmb2, notification 8000h): for the types list it sets the name box to the pattern, lists the files and sends LBSELCHSTRING (cmb1, index); for drives it changes drive if needed and sends LBSELCHSTRING (cmb2). A type chosen: its pattern goes into the name box with the whole text selected, the focus left where it is (typesel1; Cardfile's scenarios type a name into the box after choosing a type). A choice made in the dropped list arrives as CBN_CLOSEUP, CBN_SELCHANGE and then the dialog's own WM_COMMAND 8000h, posted at the close-up (so the Flags bit is still set when the dialog ends first); one made with the list up (keys) is taken at its CBN_SELCHANGE, without 8000h. The name box shows a pattern in lower case (Paintbrush's "*.BMP;*.DIB" as "*.bmp;*.dib"); a non-empty lpstrFile is put there whole, lower-cased in the caller's buffer itself (also on Cancel), without going to its folder. nFilterIndex follows the types list as it changes (also on Cancel); a custom filter with a description is the list's first entry (index 0); a typed pattern is written into lpstrCustomFilter after its description. OK: a pattern lists it (in its folder), "x:" or a folder goes there, a file: the file list scrolls to the first name starting with the typed text's first character, then the checks: FILEMUSTEXIST implies PATHMUSTEXIST and CREATEPROMPT both, in the returned Flags too; messages are boxes titled with the dialog's caption, exclamation icon: "<name as typed>\nCannot find this file...", "...Path does not exist...", "...This filename is not valid.", "...is a reserved device name...", "<FULL PATH>\nThis file already exists.\n\nReplace existing file?" (Yes/No), "...does not exist.\n\nCreate the file?" (question icon); FILEOKSTRING to the hook can refuse. The result is the full path in capitals, a default extension appended as given afterwards ("C:\WINDOWS\README.txt"); nFileOffset, nFileExtension, lpstrFileTitle; a buffer too small: FNERR_BUFFERTOOSMALL with the path's length (without the NUL) in its first two bytes. Several files: the name box lists them in lower case one space apart; the result is "C:\DIR name name ...", nFileOffset at the first name, nFileExtension 0, lpstrFileTitle empty. Double-clicking a file sends the dialog WM_COMMAND IDOK with lParam 0. NOCHANGEDIR restores the directory. UNVERIFIED: sharing violations (SHAREVISTRING), write-protected and unready drives, network and CD-ROM drives (DOSBox shows none), how the original shortens long folder paths in the folder line, LBSELCHSTRING's codes for several files.

**Find / Replace** (1540, 1541; modeless). The owner gets FINDMSGSTRING with lParam the caller's FINDREPLACE: Find Next FR_FINDNEXT, Replace FR_REPLACE, Replace All FR_REPLACEALL, Cancel/close FR_DIALOGTERM, the others cleared; the check boxes give FR_WHOLEWORD/FR_MATCHCASE, Up/Down FR_DOWN, and the Replace dialog (no direction) sets FR_DOWN on every action. FR_NO* disable a control, FR_HIDE* hide and disable it; FR_SHOWHELP shows Help. The action buttons are disabled while there is nothing to find. wFindWhatLen 0: FRERR_BUFFERLENGTHZERO; no owner window: NULL with the last error left as it was.

**Color** (`shell/commdlg/color.c` has the details): basic colours 8x6 and custom 8x2 boxes (24x17, 29 and 22 apart), Define Custom Colors widens the dialog (it is made full width and cut down), HLS with 240 steps: the textbook model, its integer rounding fitted by observation alone (scenario hls1: CD1's colour probe types 6597 RGB and HLS values into the 3.1 dialog; `tests/scripts/commdlg/hls1.table` records what it showed, and ours shows the same), the field drawn in blocks of 4 hue by 8 saturation steps and the luminosity bar in blocks of 8 so that the display driver dithers them as the original's (pixel-identical), the cross and arrow positions, the edits (a number takes effect as typed, above the maximum becomes the maximum), Add to Custom Colors (written to lpCustColors at once), COLOROKSTRING.

**Font** (`font.c`): checks (CDERR_STRUCTSIZE, CFERR_MAXLESSTHANMIN, CFERR_NOFONTS with the "Fonts" message box), the face list from EnumFontFamilies with the CF_ filters, the TrueType picture, styles Regular/Italic/Bold/Bold Italic with simulations, the sizes (TrueType/vector 8-72, raster faces their own), effects and the 16 named colours, the sample, the OK checks (1098/1099/1282/1283), the returned LOGFONT, iPointSize (tenths) and nFontType (with SIMULATED/REGULAR/ITALIC/BOLD bits). UNVERIFIED: printer fonts, CF_WYSIWYG, the description line.

**Print / Print Setup** (`print.c`): the printer from WIN.INI or hDevNames, the driver loaded and its ExtDeviceMode/DeviceCapabilities used for DEVMODE, quality, paper sizes and sources; DEVNAMES as the original lays it out (device name first at offset 8, wDefault with DN_DEFAULTPRN and two more bits: 4000h on a DEVNAMES this call made, 8000h set by the Print dialog), PD_RETURNDEFAULT, the page and copies checks and messages, the "Default Printer (... on ...)" texts, Options... (ExtDeviceMode with DM_PROMPT), the orientation picture (our own icons).

**Tests.** `tests/commdlgscen.sh` runs each `tests/scripts/commdlg/*.txt` on the reference 3.1 twice, with Microsoft's COMMDLG and ours, and compares CD1.LOG (structures, hook messages) and every screenshot against `tests/scripts/commdlg.baseline` (what our own pictures cost). Scenarios: open1-3, multi1, fbehav (messages and returned fields), drop1 (the combo boxes), typesel1, scroll1, find1, color1-9, hls1, font1-6, fontapps, print1-7, obs-procs (window procedures, observation only), and with the programs themselves notepad and apps (Program Manager's Browse, Write, Paintbrush, Cardfile). `-n` runs the original only, for observation. Every scenario starts with SNAP `blinktime 65535`: carets keep their phase. Known: the screens differ by our own pictures (the baseline); print6 compares without window handles (`nohwnd`: from its second call ours are one 12-byte USER block further on, UNVERIFIED why); font5's sample in a 72-point face is placed differently (4644 pixels, in the baseline; UNVERIFIED rule). The boot test's `commdlg` case runs open1, find1, color1, font1 and print1. Printer drivers come from the reference Setup disks (`tools/szdd.py` expands them into the scratch install).

## 18. DDEML

DDEML.DLL, the DDE Management Library, wraps the DDE message protocol in
an API of instances, string handles, data handles, conversations and
transactions (Programmer's Reference Vol. 1 ch. 5, Vol. 2 Dde*, Vol. 3
XTYP_* and the structures). Windows 3.1's has 38 exports (module DDEML,
ordinals 1-39 without 25; `tools/neimports.py --exports DDEML`) and
imports only KERNEL, USER, GDI and KEYBOARD. Nothing in the 3.1 or WfW
3.11 installs imports it; applications ship expecting it. w16's is in
`shell/ddeml/` and installed as `SYSTEM\DDEML.DLL`.

### 18.1 Observation tools (DM.EXE)

- `DM.EXE` (`tests/ddeml/dm.c`, `make dmprobes`) plays every part: DDEML
  client, server and monitor, raw-DDE client and server. Copies run at
  once, named by a letter; A is the shell and runs `C:\DM.TXT`, a line
  `B: command` runs in copy B (posted, A pumps meanwhile). It logs every
  call with its result and DdeGetLastError, every callback (type, format,
  conversation, strings, data, dwData1/2, the answer it gives) through
  OSSPY.DLL (§16.9), whose hooks log every WM_DDE_* message; since this
  chapter OSSPY also logs sent ones on request (OsSpySent, ordinal 10:
  DDEML sends TERMINATE). Handles are named, not shown: strings by their
  string, conversations, lists, instances and data handles as c1, l1, i1,
  d1 in order of first sight (a data handle a call makes always gets a
  new name), transaction IDs by their counter (x0001). `raw on` shows the
  values (the x*.dm explorations). A monitor's events are logged the same
  way: the data handles in a callback event get new names, the global
  handles in a message's lParam are named h1, h2 by first sight (their
  reuse is compared), atoms and words are shown. `iwatch on` makes a
  copy's raw window log every INITIATE it gets.
- `tests/ddeml/xrun.sh OUTDIR "COMBOS" NAME...` runs scripts (the x*.dm
  explorations too) in the given combinations and prints each log's
  differences from ms-ms: for matching a behaviour before a scenario
  covers it.
- The OpenWatcom `ddeml.h` declares DdeAccessData with a near result; the
  probe imports ordinal 17 as DdeAccessDataF with the SDK's far one (and
  w16's DDEML exports its own function under that ordinal).
- `tests/ddeml/dmrun.sh NAME OUT [cli] [srv]` runs one script on a scratch
  copy of the reference 3.1. `startx X` starts `DMX.EXE`: DM.EXE whose
  import of DDEML is renamed DDEMX, with DDEMX.DLL a copy of the server
  library renamed (tools/nerename.py; test-only, made in the run
  directory), so that two DDEML libraries talk in one session. A run
  `NAME+x` turns every `start` into `startx`.

### 18.2 Windows 3.1's DDEML as observed

Instances and windows:
- `DdeInitialize` makes a top-level DMGClass window (style 04C00000, 6
  extra bytes) and then a DMGFrame (84000000, 4 bytes); a monitor gets a
  DMGMonitorClass window instead (04C00000, 4 bytes: LOWORD(id), 0),
  and only when it asks for events (MF_ flags; `flags.dm`'s monitor
  without them has none: the windows made after it show it). idInst is MAKELONG(an address in the library, a counter); one
  counter, from 2, numbers instances and every conversation and holding
  window. HCONV is MAKELONG(the conversation's window, its number). A
  client conversation is a DMGClientClass child of DMGClass; a server
  one a DMGServerClass child of the instance's DMGHoldingClass child of
  DMGClass. No validation of afCmd was seen (any value initialises);
  calling it again with an instance changes its filters.
- `DdeGetLastError` with an instance returns its error and clears it;
  with 0 the library's (DLL_NOT_INITIALIZED at first), with any other
  invalid id INVALIDPARAMETER.
- `DdeUninitialize`: UNREGISTER for its names to every other instance,
  TERMINATE to every conversation, in the order of the instance's
  windows (the DMGClass's children in z-order; a holding window's
  children, server conversations or a list's, where it comes; a
  conversation with itself gets one from each side), then lets the other
  tasks run first, and takes its own messages one at a time, letting
  the others run again after each TERMINATE one of its client windows
  takes (`xun1.dm`-`xun4.dm`); frees the strings it still holds; a
  second call fails with INVALIDPARAMETER.
- The termination dialog (`xterm.dm` with `tests/ddeml/dmshot.sh`,
  screenshots and keys on a virtual X display; Programmer's Reference
  Vol. 2, DdeUninitialize): when a partner has not answered every
  TERMINATE 3 s after DdeUninitialize began, the resource TERMDIALOG comes
  up ("Windows DDE", system-modal: every other task stops while it is
  up; Retry is the default button: Return chooses it). Retry: 30 s more, then
  the dialog again; Continue: waits for ever (no dialog again); Close:
  gives up, the conversations are freed and DdeUninitialize returns 1.
  No TERMINATE is sent again (the partner sees one). Times measured on
  the emulated clock by a task that ticks every 100 ms: the dialog 2966
  ms after the call, the next one 29934 ms after Retry. ExitWindows does
  not bring it (DDEML's windows do nothing at WM_ENDSESSION: a client
  whose partner never answers lets the session end; its partner gets no
  TERMINATE).

Strings: a string handle is the string's global atom (HIWORD 0), shared
by all instances and tasks, case-insensitive; each create adds a
reference, Keep adds one and returns the atom, Free removes one, and
DdeUninitialize removes those an instance still holds: an instance
keeps one entry per create and keep, appended; a free takes out the
string's first entry and moves the last into its place, and
DdeUninitialize frees the first entry until none is left (the order a
monitor sees MH_CLEANUP in, `xstr.dm`). Code page 0,
CP_WINANSI or GetKBCodePage() (437 here) only, else INVALIDPARAMETER.
"" gives 0 without an error. With idInst 0 the atom is made all the same
(error DLL_NOT_INITIALIZED); with a dead id, 0 and INVALIDPARAMETER.
DdeQueryString copies at most max-1 characters (max 0: nothing, 0), with
a NULL buffer returns the length; for hsz 0 it returns 0 and empties the
buffer. DdeCmpStringHandles compares values (0 smallest). An
instance-specific name is MAKELONG(base atom, the instance's DMGFrame);
its string is "Svc:(143c)", the window as four lower-case hex digits ("Svc:(0d14)").

Data handles: a handle is a DDE block: WORD flags, WORD format, data
(GlobalAlloc of cb+4, not cleared). Made with no item name it has no
header (an EXECUTE block). Its size is GlobalSize less the header.
DdeGetData past the end: 0, INVALIDPARAMETER. DdeAddData past the end
reallocates (the gap zeroed), same handle. afCmd other than 0 and
HDATA_APPOWNED: INVALIDPARAMETER. Freeing a handle twice crashes 3.1.

Connecting:
- DdeConnect, DdeConnectList and DdeReconnect send INITIATE with the
  two atoms to every top-level window in turn (EnumWindows, z-order, the
  exported InitEnum; `xconn.dm`: raw windows below the first server
  never see a DdeConnect's), not as a broadcast. A DdeConnect stops at
  the first window whose answer gave it its conversation. A server
  instance answers when its filter allows (FILTERINITS: always on at
  first; DdeNameService(0, DNS_FILTEROFF) lets every service in; with
  the filter on, a NULL service needs a registered name). XTYP_CONNECT
  (hsz1 topic, hsz2 service, dwData1 the client's CONVCONTEXT if the
  client is a DDEML window of the same library, else 0, dwData2 same
  instance); NULL service or topic: XTYP_WILDCONNECT, whose HSZPAIR list
  makes one conversation each. The server sends ACK, then
  XTYP_CONNECT_CONFIRM, whose conversation is what the server window's
  handle reads after the ACK: MAKELONG(window, the window's third word)
  if the handle is a window, else 0 (a window the client has made since
  can have taken the handle: below). A second ACK to one DdeConnect (a
  WILDCONNECT with several pairs): the client sends TERMINATE at once
  (SendMessage), so the server gets XTYP_DISCONNECT and then
  CONNECT_CONFIRM with conversation 0. A conversation with the client's
  own instance has ST_ISSELF (status 0115).
- DdeConnectList (`xconn.dm`, `xconn2.dm`, raw values): a list is a
  DMGHoldingClass window, child of the client's DMGClass (words: LOWORD
  of the instance, 0, its number, 0), made before the INITIATE's client
  window, which is its child; HCONVLIST = MAKELONG(window, number). The
  first answer goes to that window. A further answer from a server
  window of the same library gets a new client window (child of the
  list, numbered then; its CONVCONTEXT all zero, its service asked for
  the first one's). A further answer from any other server (another
  library, raw DDE): the client sends it TERMINATE at once, makes a new
  client window and sends an INITIATE for exactly that service and topic
  from it to the same top-level window (not to the others); the server,
  still inside its first ACK, answers that one. The old server window is
  gone by then and the new client window usually gets its handle, so
  that server's CONNECT_CONFIRM names a conversation made of the new
  client window and its number; the server's answering TERMINATE then
  reaches the old client window under the new window's handle. The
  enumeration then goes on from the first client window.
  DdeQueryNextServer and DdeDisconnectList go through the list window's
  children in z-order: children are added below the others, so the
  order is the order the windows were made. Given an old list: the new
  conversations that duplicate a live one of the old list (service and
  topic) get TERMINATE (in the new list's order), then the old list's
  windows move into the new list one by one (SetParent puts each on
  top: their order turns round, above the new ones), and the old window
  is destroyed (its handle is dead). Nobody answering: 0, no error, and
  the list window stays behind (until the instance goes).
  DdeDisconnectList terminates the conversations and leaves the window,
  its fourth word set to 2000; a call that finds the window empty
  destroys it (both return 1).
- REGISTER/UNREGISTER (DdeNameService, DdeUninitialize) reach every
  instance in the z-order of their DMGClass windows, newest first,
  inside the call; DdeNameService(0, FILTERON/OFF) returns 0;
  CLIENTONLY instances get DLL_USAGE.
- DdeDisconnect posts TERMINATE and returns; the partner's TERMINATE
  destroys the window later. A partner's TERMINATE is answered first,
  then XTYP_DISCONNECT is called; the window goes. The handle is dead
  afterwards (DdeReconnect: NO_CONV_ESTABLISHED). While the partner's
  queue is full (8 messages), the TERMINATE waits: the library yields
  and posts again (`xun4.dm`: a client ending eleven conversations with
  one server; the server's ninth answer waits until the client has
  taken one). A server window that gets the answer to its own TERMINATE
  also leaves one message of its own (no DDE message) in its task's
  queue (`xun2.dm`: it takes one place among the 8).

Transactions:
- REQUEST: DATA flags 3000 back; a sync request returns a data handle the
  application frees, lpuResult untouched. POKE: block flags 2000. EXECUTE
  with item 0: the command block alone; with an item: a block with the
  DDE header (flags 2000) goes as the command. ADVSTART: DDEADVISE options
  always with 2000 set (6000 NODATA, A000 ACKREQ, E000 both). Only the
  low word of lpuResult is written (the ACK status). A server's callback
  result is sent as result & C00F. NACK: NOTPROCESSED, busy: BUSY,
  timeout: DATA/POKE/EXEC/ADV/UNADVACKTIMEOUT.
- A data handle given to DdeClientTransaction goes as it is: its own
  format word wins over uFmt, its flags get 2000.
- Async: TRUE, the ID MAKELONG(address, per-conversation counter from 1);
  XTYP_XACT_COMPLETE with dwData1 the ID and dwData2 the status.
  DdeAbandonTransaction(conv, 0) and (0, 0) drop them all.
- A local partner (a server window of the same library) that has the
  matching CBF_FAIL_ flag makes DdeClientTransaction fail at once: 0, no
  message, NO_ERROR.
- Server: CBR_BLOCK holds the transaction and blocks the conversation;
  DdeEnableCallback one/all runs held callbacks inside the call;
  EC_QUERYWAITING says whether any are held. DdeEnableCallback works
  inside a callback, and so does a synchronous transaction inside
  XACT_COMPLETE.
- Advise: links are kept newest first per conversation; DdePostAdvise
  walks the server conversations newest first and calls XTYP_ADVREQ with
  dwData1 the number of later links with the same item and format;
  NODATA links get a DATA with no data, without ADVREQ. An ACKREQ link
  whose last DATA is unanswered gets no ADVREQ; the ACK then brings one
  with CADV_LATEACK. The client takes all DATA already posted before it
  calls the callbacks, and acknowledges a DATA without data always.
  UNADVISE of an unknown link: NACK without a callback; XTYP_ADVSTOP's
  dwData2 is the item.
- CONVINFO after a synchronous transaction: item, format and type come
  from freed memory on 3.1 (the probe's DS shows through); a new client
  conversation reports fmt CF_TEXT, type XTYP_CONNECT, status 0015
  (0011 with a raw server), a server one 0001 with no partner.
- DDE blocks (data handles, the ADVISE options) are fixed global blocks:
  their handles are selectors (ending 7 or F); a block freed by the
  receiver is the next one a call allocates (`xmon.dm` as xmon+x).

Monitors (`monitor.dm`, `xmon.dm`, `xmon2.dm`, `xstr.dm`):
- An event's data handle is a block without the DDE header; DdeAccessData
  gives the event's size rounded up to 32 (3.1 makes no global block per
  event: the handles point into one block of the monitor instance).
  MONHSZSTRUCT's cb is 18 plus the string's length.
- Strings: besides MH_CREATE (1), MH_KEEP (2), MH_DELETE (3) and
  MH_CLEANUP (4), 3.1 reports its own atom traffic: 5 for the atom a new
  string handle gets (before 1), 6 for one more reference, 7 for one
  less (after 3 and after each 4). References it reports: a registered
  name (6 at DdeNameService, 7 at unregistering, after the callbacks);
  around each REGISTER/UNREGISTER callback, in the receiving instance's
  task (6 before, 7 after); a server conversation holds its service
  twice and its topic (6 6 6 when it is made, after the server's
  MONCONVSTRUCT) and adds the ACK's two (6 6); a client conversation
  adds its service asked for when the ACK comes (6, after its
  MONCONVSTRUCT) and keeps the ACK's; both let their three go when they
  end (7 7 7: the client when the answer to its TERMINATE comes, the
  server after XTYP_DISCONNECT); a REQUEST's item (6, the client, before
  posting); a link's item (6 at the server after XTYP_ADVSTART, then
  the link event; at the client when the ACK comes, then the link
  event; at UNADVISE the server reports the link's end, then 7, then
  calls XTYP_ADVSTOP; the client 7, then the event); an advise DATA's
  item (6, the server, before posting). POKE, ADVISE, UNADVISE and the
  answers' atoms are not reported.
- MONCONVSTRUCT and MONLINKSTRUCT name both conversations: the partner
  as MAKELONG(its window, the window's third word) in a connect block
  (a raw partner: whatever that word holds), in a link block the
  partner conversation if it is of the same library, else MAKELONG(its
  window, 0). The server reports its connect before the ACK, the client
  when the ACK comes.
- Sent messages: the hook reports every DDE message sent, except an
  INITIATE to a DMGClass window of its own library, or to any DMGClass
  window from a client of its own library: those are reported after the
  window handled them (by that window, or by the client), and not at
  all when the INITIATE made the conversation of a DdeConnect (it ends
  there); a list's are reported after its answers.
- Callbacks: every callback of every instance of the library is
  reported, the monitors' own (REGISTER) too, XTYP_MONITOR not.
  dwData1 of a connect is the client's CONVCONTEXT address.
- Errors: NO_CONV_ESTABLISHED is reported; NOTPROCESSED from a NACK is
  not.

### 18.3 w16's DDEML.DLL

`shell/ddeml/`: `inst.c` (instances, DMGClass/DMGFrame, errors, name
service, the callback), `hsz.c` (string handles), `data.c` (data
handles), `conv.c` (connecting, lists, the client and server windows,
transactions, advise loops, blocking, CONVINFO), `mon.c` (monitors and
their hooks), `entry.asm`, `ddeml.rc` (3.1's TERMDIALOG layout, w16
wording, and a version resource), `ddeml.lnk` (the 38 ordinals and
names; the linker adds unnamed entries 40-42). Large model, SS != DS,
every entry loads DDEML's DS; marked a 3.00 module as 3.1's is; imports
KERNEL, USER and KEYBOARD only. Every record (instance, name, string
reference, data handle, conversation, transaction, link, held message,
list) is a linked list node in DDEML's local heap: the one bound is that
64K segment. The windows, handle layouts, counters and orders of 18.2
are reproduced: lists are their windows (the order of a list is its
window's children's), DdeUninitialize walks the instance's windows, a
full queue makes a TERMINATE wait, and the one message 3.1 leaves in
the queue when a server window takes the answer to its own TERMINATE is
a message w16 posts to the instance window that does nothing
(WM_DMG_NOP). Data blocks are fixed, as 3.1's, and allocated zeroed
(3.1's hold stale memory past the data; the comparison stops at the
first NUL). A monitor instance has one global block for its events,
made at DdeInitialize. DdeUninitialize shows TERMDIALOG (3.1's layout,
w16's wording) after 3 s and after each 30 s Retry, as 3.1.

### 18.4 Tests and results

- `tests/ddemlscen.sh [-c COMBOS] [-n] [NAME...]` runs every script of
  `tests/ddeml/scripts` but the x*.dm explorations (strings, data, conn,
  flags, advise, monitor, uninit, transact (the client transactions),
  raw: DDEML against raw DDE both ways, progman: a
  DDEML client driving 3.1's Program Manager with CreateGroup, AddItem,
  ShowGroup, DeleteItem, Groups and group requests, ExitProgman) as NAME
  (one library) and NAME+x (the other copies on the other library), with
  Microsoft's DDEML and w16's in every client/server combination
  (ms-ms is the reference), and compares the logs after
  `tests/ddeml/dmnorm.py`: data blocks up to their first NUL (3.1 does
  not clear them; the tail is whatever the memory held). Atom values and
  the frame window in instance-specific names are compared as they are
  (18.6). The monitor's event handles are named, not shown (18.1).
- Boot-test case **ddeml**: parts tree286 and tree386 (`tests/ddeml/
  ddtree.sh`: the install tree alone under w16's KRNL286 / KRNL386, DM.EXE
  as the shell, a DDEML server and client and a DDEML client driving
  w16's Program Manager; the log equal to the same tree with Microsoft's
  DDEML), and one part per scenario and combination. Full run
  (installation-05, 2026-11-16, 76b6b94): the parts strings, data, xact,
  advise, flags, uninit, raw (with +x) ok; tree286 and tree386 differ
  only in the "Progman" atom spelling (below: w16's own Program Manager
  shows it too, so 3.1's DDEML is what makes the "Progman" atom);
  conn, conn+x, monitor, monitor+x, progman as below.
- Results (installation-05, 2026-11-17, boot case ddeml): strings,
  data, transact, advise, flags, uninit, raw (each also +x, in every
  combination), progman, tree286 and tree386 equal Microsoft's, with no
  masking beyond the data tails (atom values and instance names
  included, 18.6). Not equal yet then: conn (lists with several
  servers) and monitor (the event stream).
- Part **term** of the boot case: the termination dialog, Retry then
  Close, with Microsoft's DDEML and with w16's (`tests/ddeml/dmshot.sh`,
  `tests/ddeml/termcmp.py`): both dialogs at the same times (within 300
  ms of emulated time), DdeUninitialize returning 1, the dialog's picture
  equal but for the three rebranded words ("w16" for "Windows").
- Results (installation-05, 2026-11-19, task 3): conn and monitor equal
  too, in every combination (the INITIATE enumeration, lists as
  windows, another library's answers asked again, DdeUninitialize's
  order and queue places, and the monitor events of 18.2): full boot
  test (dffe779 on master 429e079): ddeml ok in every part, 123 cases
  ok; failing: lzexpand (quirks, known), trails@timed (known flake),
  toolhelp@ref3 (Microsoft's own run, no w16 file and no DDEML in it:
  its group e did not finish; alone again: ref386 the same way).
  Masking: only the data blocks' tails.

### 18.5 UNVERIFIED, and not done

- `xun5.dm` (an instance with conversations to itself and to a second
  instance of the same task) never returned from 3.1's DdeUninitialize
  in a run with nobody to press a button: probably the termination
  dialog (no screenshot was taken), a conversation of the other
  instance left unanswered; not looked into further.
- Monitors: dwTime and the blocks' words the probe does not print are
  not compared.
- HDDEDATA, HCONVLIST, the low words of idInst and transaction IDs and
  the conversation windows' extra words are w16's own values (addresses
  inside the library); the probe logs them by name only.
- Not probed: DdeReconnect inside XTYP_DISCONNECT, CF_BITMAP/
  CF_METAFILEPICT data (3.1 imports GDI's CopyMetaFile and BitBlt, so it
  probably copies such data for HDATA_APPOWNED handles; w16 copies the
  block only), 386 Enhanced mode, WfW 3.11's DDEML.

### 18.6 Matching 3.1's atoms and window handles

Atom values and window handles are places in USER's heap, so they come
out as 3.1's only when the library asks USER for the same things in the
same order. Found with `tests/ddeml/scripts/xatoms.dm` (every global atom
listed before and after each call, window handles shown):
- At its first DdeInitialize, 3.1's DDEML adds three global atoms, in
  this order, and keeps them: "Progman", "Who" and "Made me?". (Hence
  "PROGMAN" reading back as "Progman" in any session that used DDEML.)
- Its classes are registered at that first DdeInitialize, not when the
  library loads (with another library's instance made first, 3.1's
  handles show none of them yet in USER's heap), before the atoms.
- Six classes: DMGFrame, DMGClass, DMGClientClass, DMGServerClass,
  DMGHoldingClass and DMGMonitorClass (the last only a monitor
  instance's window shows, `xmon.dm`): every class moves later handles
  by 0x44 bytes, and six put w16's DMGFrame and DMGClass at 3.1's
  handles.
With these, instance-specific names ("Svc:(143c)") and string handle
values are compared unmasked.

## 19. LZEXPAND

LZEXPAND.DLL reads files compressed with COMPRESS.EXE, the `*.??_` files
of setup disks (Programmer's Reference Vol. 1 ch. 9, Vol. 2: CopyLZFile,
GetExpandedName, LZClose, LZCopy, LZDone, LZInit, LZOpenFile, LZRead,
LZSeek, LZStart). Windows 3.1's (module LZEXPAND, "Windows file expansion
library", flags 8301h, flags2 08h, expected version 3.00, file version
3.10.0.103) exports those ten as ordinals 1-10, WEP 11 and
___EXPORTEDSTUB 12 (both resident); its entries 13-30 are unnamed and not
exported (its own far calls). It imports only KERNEL and USER's
IsCharUpper, IsCharLower and lstrcmp. In the 3.1 install SETUP.EXE,
DRIVERS.CPL, MAIN.CPL and VER.DLL import it (`tools/neimports.py
--importers LZEXPAND`). w16's is in `shell/lzexpand/`, installed as
`SYSTEM\LZEXPAND.DLL`.

The file format is the publicly documented "SZDD" one: a 14-byte header
(`SZDD` 88h F0h 27h 33h, the algorithm byte `A`, the last character of
the name for COMPRESS's /r or 0, the expanded size), then LZSS: a flag
byte, eight items low bit first, a set bit a literal byte, a clear one a
2-byte match (a 12-bit position in a 4096-byte ring that starts filled
with spaces and is written from 4096 - 16, a 4-bit length - 3). COMPRESS
writes matches of at most 16 bytes (seen in its output of the probe's
files: the 3.0 SDK's COMPRESS.EXE 1.00, run once by hand in DOSBox; the
tests use no Microsoft file), although the format allows 18.

### 19.1 Observation tools

- `LZP.EXE` (`tests/lzexpand/lzp.c`, `make lzprobes`), run as the shell
  (`tests/lzexpand/lzrun.sh OUTDIR ms|w16 [/S|/3]`), calls every function
  and logs each result, the bytes read (count, a checksum, the first and
  last bytes), the position LZSeek reports and the underlying DOS file's
  position, OFSTRUCTs, and the next DOS handle a task would get, to
  `C:\LZP.LOG`, in sections (`== NAME`). A second copy started with
  WinExec is the second task. `LZRUN_ARG` runs the explorations instead
  (`x`, `y`, `w`, `z`, `q`, `k`, `v`; lzp.c's header).
- Its files are made at test time by `tests/lzexpand/mkinputs.py` with
  w16's own compressor `tools/szddenc.py` (greedy hash-chain matching,
  16 bytes at most unless asked for 18): text, binary data past 64K,
  empty and plain files, a wrong algorithm byte, headers cut short,
  streams cut short between items and inside a match, header sizes
  smaller and larger than the data, MS-DOS 5's older "SZ" variant, a
  match into the primed spaces, names for GetExpandedName's rules, 18
  files for the handle limit, and 18-byte-match variants.
- Two runs of Microsoft's library give identical logs: nothing is masked
  (`tests/lzexpand/lzcmp.py` compares section by section).

### 19.2 Windows 3.1's LZEXPAND as observed

Handles:
- A compressed file gets the handle 400h + slot, 16 slots (the 17th
  LZOpenFile or LZInit: LZERROR_GLOBALLOC, -5; LZOpenFile closes the file
  then, LZInit does not). Slots are taken in turn after the one taken
  last, not the lowest free one (1024, 1025, ... 1039, then 1024 again if
  free). Plain files get their DOS handle back. The table is the
  library's: another task can read a handle a task opened (the same
  decoder), and a task that ends leaves its handles open (no clean-up).
- Values 400h-40Fh are LZ handles for every function; any other value is
  a DOS handle passed to _lread/_llseek/_lclose (LZRead of a negative
  count: LZERROR_BADVALUE first). An LZ handle not open: LZRead and LZSeek
  LZERROR_GLOBLOCK (-6), LZCopy LZERROR_BADINHANDLE, LZClose nothing.

LZInit seeks the file to 0 and reads 14 bytes: not the magic (or fewer
bytes): back to 0, the DOS handle is returned; an algorithm other than
`A`: LZERROR_UNKNOWNALG (-8), the file left at 14 and open; else an LZ
handle, the file at 14. A bad DOS handle: LZERROR_BADINHANDLE. The older
"SZ" variant is a plain file. LZClose of an LZ handle closes its file.

LZOpenFile calls OpenFile; if that fails it tries again with the name
COMPRESS gives: the last character of a 3-character extension made `_`,
`_` added to a shorter one, `._` added without one ("X.TXT" X.TX_, "X.A"
X.A_, "X" X._), the OFSTRUCT the second try's. Only OF_READ, alone or
with OF_SHARE_ flags, goes on to LZInit (OF_READWRITE, OF_WRITE,
OF_EXIST, OF_PARSE, OF_REOPEN, OF_CREATE, OF_DELETE: OpenFile's result as
it is); if LZInit fails its error is returned and the file closed.

GetExpandedName opens the name with OpenFile (the search path applies):
not found, LZERROR_BADINHANDLE (-1) and the buffer untouched; else the
name as given is copied and, for a compressed file (magic and a full
header; the algorithm not checked) whose name ends with `_` in an
extension (not "UNDER_", nor with only a dot in a directory), the `_`
becomes the header's character: lower case if the first letter of the
whole string is lower case ("c:\lz\TEXT.TX_" gives `t`), else upper
case, ASCII letters only (E9h stays E9h); a character 0 ends the name
there and drops a dot before it ("NONE0._" NONE0). It returns 1.

LZRead (LZSeek only moves the position: origins 0, 1, 2 from the start,
the position, the header's size; a result below 0 or another origin
LZERROR_BADVALUE, -7; past the end allowed):
- cb < 0: LZERROR_BADVALUE; at or past the header's size: 0.
- Expanding is done in passes into the ring, each copied out to the
  caller when it ends. A match is begun while fewer than 1009 bytes of
  the pass are out, a literal while fewer than 1010 (2 fewer while
  skipping to a position after a seek). Expanding goes on to one item
  past the end of the request and stops at the header's size: a header
  saying less than the data gives everything up to the end of the item
  that crosses it (a size of 100 gives 114 bytes). A file whose data ends
  within its first 1024 bytes ends its first pass after the first item.
- The file is read 1024 bytes at a time from wherever its pointer is (a
  caller's _llseek between reads changes what is expanded); it is
  positioned only when expanding starts from the beginning.
- A read before the start of the last pass expands again from the
  beginning; one within it copies from the ring.
- A pass longer than 1024 bytes fails: LZERROR_READ (-3). Only matches of
  17 or 18 bytes, which COMPRESS does not write, make one, so 3.1 reads
  COMPRESS's files of any size in one read, but streams of other
  compressors with long matches fail on some reads from 2048 bytes up
  (and on smaller ones started late in a pass). The bytes of earlier
  passes are delivered, the position is the failing pass's start, the
  next read expands from the beginning; 3.1 still expands to the end of
  the request first (its file position shows it).
- Data that ends before the header's size: LZERROR_READ, the position
  the failing pass's start (looking ahead past the data does not fail).
  After data that ends between items, reads from there on fail again;
  after data that ends inside a match, a read at that place.
- LZRead(h, buf, 0) as the first read of a new handle leaves 3.1's handle
  expanding wrongly from then on: literals come out, matches give zeros,
  in some sequences raw compressed bytes (`== zero`: its uninitialised
  buffers, not reproduced). A 0-byte read later returns 0 and changes
  nothing.

LZCopy and CopyLZFile (the same function; LZStart returns 1 and keeps the
buffers until LZDone): from an LZ handle the file is expanded from its
start, whatever the handle's position, which it leaves alone; a DOS
handle is checked like LZInit (seek 0, header) and a plain file copied
whole from its start. It reads 32K at a time and writes 32K at a time,
from the destination's start (what was there is overwritten, not cut).
Results: the expanded size; a bad source LZERROR_BADINHANDLE; a write
that fails LZERROR_BADOUTHANDLE (-2); a wrong algorithm -8; data ending
between items: what was expanded is written, then LZERROR_READ; data
ending inside a match: nothing more is written and the destination's
pointer goes back to 0, LZERROR_READ; a header size below the data:
everything to the end of the crossing item written, LZERROR_READ.

Standard and 386 Enhanced mode behave the same.

### 19.3 w16's LZEXPAND.DLL

`shell/lzexpand/`: `lzexp.c` (everything), `lzexpand.h` (the decoder's
record and the observed constants), `entry.asm` (entry point, WEP,
___EXPORTEDSTUB returning 0), `lzexpand.lnk` (the twelve ordinals, names
and residency; 16-byte alignment as 3.1's), `lzexpand.rc` with
`tools/mkver.py`'s version resource (3.1's numbers, w16's texts). Large
model, SS != DS, every entry loads its DS. `tools/nemark.py` now also
sets the module flags (8301h and flags2 08h, as 3.1's) besides the
expected version. Each slot is a moveable GMEM_SHARE block (the
library's: handles outlive tasks) holding the decoder: the position, the
bytes expanded, the start of the last pass, the ring and a 1024-byte
input buffer; it follows 19.2, including the passes and their limits, so
that the same reads fail.

OpenWatcom's resource compiler writes a 16-bit VERSIONINFO whose length
words exceed its data (by 12 bytes in DDEML's and LZEXPAND's, from the
strings' value lengths), and 3.1's VER.DLL then finds no version:
GetFileVersionInfoSize returns 0 for w16's DDEML.DLL as well.
LZEXPAND's version block is written by `tools/mkver.py` (the SDK's
VS_VERSIONINFO layout) and included as a type-16 resource; the probe
checks it through VER.DLL. Every other w16 module with a version
resource now has one from `tools/mkver.py` too (§20.5).

### 19.4 Tests and results

- `tests/lzexpand/lzscen.sh [-m MODES] [-n] [SECTION...]`: the probe with
  Microsoft's LZEXPAND and with w16's on the reference install, per mode
  (/S and /3), the logs compared per section. `tests/lzexpand/lztree.sh
  OUTDIR ms|w16 286|386`: the probe in w16's install tree alone under its
  KRNL286 or KRNL386, with w16's LZEXPAND or Microsoft's copied in (and
  the tree's own VER.DLL, w16's, §20, for both).
- The probe's VER.DLL section is the real 3.1 caller: VerInstallFile
  (Setup's way of installing a file: LZOpenFile, then LZCopy) of
  compressed, renamed (/r), plain, cut, missing and short-header files,
  forced and not, and GetFileVersionInfo/VerQueryValue of LZEXPAND.DLL.
- Boot-test case **lzexpand**: parts s, 3, tree286 and tree386 compare
  every section but eof, zero and long matches; part quirks compares
  those three. Results (installation-05, 2026-11-16): s, 3, tree286 and
  tree386 equal Microsoft's in every section (LZInit, LZOpenFile,
  LZRead, LZSeek, LZClose, LZCopy, LZStart, GetExpandedName, limit,
  tasks, seek back, VER.DLL); quirks differs as below.
- Still different (the same lines on the reference install and in the
  tree):
  - **eof**: after a second failure on TRUNC.TX_ (data cut inside a
    match) a read further on returns, on 3.1, the bytes from 6 places
    earlier (stale ring state); w16 returns the right bytes.
  - **zero**: a 0-byte LZRead as a new handle's first read: 3.1's garbage
    is not reproduced; w16 reads correctly.
  - **long matches**: of 28 bisections and 6 reads on 18-byte-match
    streams, two differ: FAR18 after a 1-byte read (3.1's next pass
    starts one byte earlier) and BIG18 after a 2000-byte read (3.1 ends a
    pass before a match that crosses 4096 bytes of output, but ending one
    there disagrees with CUT5000, a COMPRESS-style file, which w16
    follows).

### 19.5 UNVERIFIED, and not done

- LZERROR_WRITE (-4) for a short write (a full disk) is assumed; only a
  failing write (-2) was seen. LZERROR_GLOBLOCK from a failing GlobalLock
  and LZERROR_GLOBALLOC from a failing GlobalAlloc were not provoked.
- Whether 3.1 positions the file before the first read of a handle that
  LZInit made (w16 does; LZInit leaves it at 14 in both).
- ___EXPORTEDSTUB's purpose; nothing in the 3.1 or WfW 3.11 installs
  imports it. WfW 3.11's LZEXPAND (67 entries, the same 12 exported
  names) was not probed.
- Not driven through their own interfaces: Setup's LZCopy/LZStart,
  DRIVERS.CPL and MAIN.CPL's font installation (VER.DLL stands for them).

## 20. VER

VER.DLL reads version resources and installs files after comparing
versions (Programmer's Reference Vol. 2: GetFileResource,
GetFileResourceSize, GetFileVersionInfo, GetFileVersionInfoSize,
VerFindFile, VerInstallFile, VerLanguageName, VerQueryValue; Vol. 3:
VS_FIXEDFILEINFO; Vol. 4: the VERSIONINFO resource; Vol. 1 ch. 9:
installation). Windows 3.1's (module VER, "VER - A Version stamping
library", flags 8301h, flags2 08h, expected version 3.00, file version
3.10.0.103) exports WEP 1 (resident, in a fixed segment) and the eight
functions as 2, 3 and 6-11 (4 and 5 do not exist); entries 12-20 are
unnamed and not exported (`tools/neinfo.py`). It imports KERNEL,
KEYBOARD's AnsiToOem and OemToAnsi, USER's LoadString and lstrcmpi, and
LZEXPAND's LZCopy, LZOpenFile, LZSeek, LZRead and LZClose. In the 3.1
install SETUP.EXE, DRIVERS.CPL and MAIN.CPL import it (WfW 3.11: also
WINFILE, WINSETUP, WFWSETUP; `tools/neimports.py --importers VER`).
w16's is in `shell/ver/`, installed as `SYSTEM\VER.DLL`.

### 20.1 Observation tools

- `VRP.EXE` (`tests/ver/vrp.c`, `make verprobes`), run as the shell
  (`tests/ver/vrun.sh OUTDIR ms|w16 [/S|/3]`), calls every function and
  logs the results, the buffers filled (with a fill pattern around them),
  lengths, pointers as offsets into the block, and after each
  VerInstallFile the destination and current directories (names,
  attributes, contents), to `C:\VRP.LOG`, in sections (`== NAME`:
  exports, resource, size, query, language, find, install, modules,
  audit: malformed input and corners, 20.8).
  `C:\VRP.ARG` selects sections (`k`: KERNEL's GetModuleHandle with file
  names, an exploration not compared).
- Its files are made at test time by `tests/ver/mkinputs.py`: version
  blocks by `tools/mkver.py` (now importable: `fixed()`, `versioninfo()`),
  well formed and broken (OpenWatcom's lengths, root lengths from 0 to
  past the resource, wrong key, signature, structure version, value
  lengths, short keys, children cut short); NE modules by `tools/mkne.py`
  (new: resource-only libraries, loadable like a .FON, with integer and
  named types and names and table lengths that disagree with the data);
  compressed copies by `tools/szddenc.py`; sources and destinations for
  every VerInstallFile case; copies for VerFindFile in the application,
  Windows, system, current, program and PATH directories; a D: drive
  (`tests/refrun.sh`'s new `REFRUN_DDRIVE`).
- `tests/ver/vscen.sh` runs both libraries per mode and
  `tests/ver/vcmp.py` compares the logs per section. Two runs of
  Microsoft's library give identical logs: nothing is masked.

### 20.2 Windows 3.1's VER.DLL as observed

Reading resources:
- Files are opened with LZOpenFile and read with LZSeek and LZRead: a
  compressed file is read as it expands, and a name that is not there is
  tried with COMPRESS's name (`X.DLL` finds `X.DL_`). OpenFile's search
  applies to names without a path.
- The resource is looked for in the NE resource table: the file must
  start with "MZ", its new header with "NE"; a resource table at the
  resident-name table's offset means none, and so does an alignment
  shift over 12 (12 finds, 13 to FFFFh do not). Only integer types and
  names are found: a named type or name (also the exact upper-case name,
  or a "#16" string) never is.
- GetFileResourceSize: the table's length (length << shift), the file
  offset to `*lpdwFileOffset` (NULL is allowed, as for
  GetFileVersionInfoSize's handle); 0, and the offset untouched, if not
  found.
  GetFileResource with an offset reads dwResLen bytes from there whatever
  the file is (a text file's bytes too), TRUE only if all were read; with
  0 it looks the resource up and reads at most its length.
- GetFileVersionInfoSize looks up type 16, name 1 only (a version
  resource named 2 or "VS_VERSION_INFO" is not found; with names 1 and 2,
  name 1 wherever it is). The handle is the resource's file offset, set as
  soon as the resource is found, even when 0 is returned. It returns the
  root's wLength when that is not 0, not more than the table's length, and
  the dword at offset 20 is FEEF04BDh; nothing else is checked (not the
  key, wValueLength or the structure version). OpenWatcom's blocks fail
  there: their wLength exceeds the resource (by 12 bytes with eight
  strings). GetFileVersionInfo is GetFileResource(16, 1): the handle is an
  offset, read from as it is.

VerQueryValue walks the block: the path's names between backslashes
(empty ones skipped: "", "\" and "\\" are the root; a leading backslash
is optional; "/" is an ordinary character) are compared with each child's
key regardless of case; the children of a node start after its value on a
4-byte boundary from the block's start (the same offsets at any address)
and each must end within its parent, so a block whose lengths are too
long loses its later children (OpenWatcom's: CompanyName is found, the
other strings and VarFileInfo are not). A node of 4 bytes or less ends
the search (a root length of 0 or 4: nothing, not even the root; one of
8000h or more, read as negative, nothing either); the key is read no
further than the parent's end (for the root, its own), so a root of
length 5 or 8 has its value at 8; a child's key is compared up to its
NUL or one byte before its parent's end (a key running to the parent's
end without a NUL loses its last character). The value is after the
key, wValueLength bytes, however many that claims (FFFFh is returned as
it is); nothing is written when the path is not found. A block whose
root runs past its segment loses what lies past it, without a fault.

VerLanguageName: 45 identifiers have names (the SDK's list, with
"Croato-Serbian (latin)" and "Serbo-Croatian (cyrillic)" in lower case),
every other value 0-FFFFh gives "Unknown Language". The return value is
the whole name's length; the buffer gets at most cbLang - 1 characters
and a NUL, and with cbLang 0 the whole name. w16's table was written from
the probe's log of all 65536 identifiers (`== language`), nothing else.

VerFindFile:
- The current copy is looked for in the application's directory, then
  the Windows and system directories (for a shared file: Windows, system,
  application), then with OpenFile's search (current directory, program's
  directory, PATH). lpszWinDir is not used. The directory is the found
  path's (OpenFile's, upper case) less the file name, the root keeping its
  backslash; nothing found: "".
- Recommended: the system directory for a shared file (VFFF_ISSHAREDFILE
  is bit 0; other bits are ignored), else lpszAppDir as given with one
  backslash at its end removed, its length counted with it ("C:\" gives
  "C:", length 4).
- VFF_CURNEDEST when the two strings differ regardless of case (also when
  nothing was found and lpszAppDir is not empty); VFF_FILEINUSE when a
  loaded module's file is the one found, whatever the module's name (a
  module of that name from another directory does not count, an open file
  neither).
- A buffer too small is left alone and VFF_BUFFTOOSMALL set; the length
  is always set to what is needed; when the destination does not fit,
  VFF_CURNEDEST is left out.

VerInstallFile:
- Forced, with a name in lpszTmpFile that exists in the destination
  directory: that file is installed (the second call); the source is not
  read and nothing is compared. A name that is not there, or not forced:
  the name is ignored.
- Else the source (lpszSrcDir and lpszSrcFilename joined; an empty
  directory leaves the name to OpenFile's search) is opened with
  LZOpenFile (failing, also for an unknown algorithm: VIF_CANNOTREADSRC)
  and expanded with LZCopy into "temp.NNN" in the destination directory,
  NNN the first unused of 000, 001, ... 009, 00A, ... FFF, in
  hexadecimal (a directory of that name counts as used; the name is not
  searched for elsewhere, also with an empty destination directory; all
  4096 used, or a missing directory: VIF_CANNOTCREATE; data that does
  not expand: VIF_CANNOTREADSRC, the file deleted). The second call's
  name is looked for the same way (a directory of that name is
  "installed": renamed to the destination).
- Unless forced, it is compared with the destination file (never with the
  copy in lpszCurDir), and only when that has a version (a malformed one
  counts as none): a source without one, or with a lower file version
  (the product version and the OS are not compared): VIF_SRCOLD; another
  file type or subtype: VIF_DIFFTYPE; a language and code page of the old
  file's Translation that the new one's lacks (code page 0 in the old
  one matching any), both having a Translation: VIF_DIFFLANG (code-page
  differences too: VIF_DIFFCODEPG was never returned, also for 932, 1200,
  437 or FFFFh); VIF_MISMATCH with any of these. What is compared is the
  resource as long as its table entry says (a root of 20 bytes is
  compared by the fixed part after it), the fixed part whatever the
  root's wValueLength, and every entry the Translation's wValueLength
  claims (one of 2 bytes has none: the old file's languages are all
  missing; one past the block in the old file gives VIF_DIFFLANG).
- A read-only destination: VIF_WRITEPROT alone (forced too), the
  temporary file deleted. A destination in use (a loaded module's file):
  VIF_FILEINUSE alone, forced too. Differences: those bits, VIF_TEMPFILE
  and the temporary name (its length with the NUL to *lpwTmpFileLen); a
  buffer too small: VIF_BUFFTOOSMALL instead, the length needed, the file
  deleted. A NULL lpwTmpFileLen counts as room enough; a NULL
  lpszTmpFile gets nothing (forced, it is no second call).
- Else the destination is deleted (one that cannot be, such as a
  directory of that name: VIF_CANNOTDELETE | VIF_ACCESSVIOLATION, the
  temporary file deleted), the temporary file renamed to it (a rename
  that fails, as into a directory that is not there: VIF_CANNOTRENAME
  alone, the temporary file deleted, the caller's in a second call too),
  and the copy
  in lpszCurDir deleted, unless VIFF_DONTDELETEOLD is given, lpszCurDir
  is NULL or "", or it names the destination's directory however spelt
  ("c:\vt\dst\", or relative); a copy there in use:
  VIF_CANNOTDELETECUR (the new file stays installed). On success
  lpszTmpFile and its length are not written.

Standard and 386 Enhanced mode behave the same.

### 20.3 w16's VER.DLL

`shell/ver/`: `ver.c` (everything), `ver.h` (the SDK's constants),
`entry.asm` (entry point, WEP, __AHINCR), `ver.lnk` (names, ordinals,
WEP resident in a fixed segment, 16-byte alignment), `ver.rc` with
`tools/mkver.py`'s version resource (3.1's numbers, w16's texts); the
module flags and expected version are set by `tools/nemark.py`. Large
model, SS != DS, every entry loads its DS. It follows 20.2 throughout.
Where 3.1's way is not visible, w16's choices are: a file is in use when
it is the file of one of the loaded modules, walked from KERNEL's along
the module table's link at offset 6 (Pietrek ch. 3) and compared by full
name (KERNEL's GetModuleHandle ignores the directory, 20.6); names go through AnsiToOem before MS-DOS calls and
OpenFile's results through OemToAnsi; a shared copy of Windows is taken
to be one whose system directory is not inside its Windows directory
(UNVERIFIED, 20.7); VerQueryValue reads nothing past its block's
segment (KERNEL's GetSelectorLimit), where 3.1 too finds nothing; the
comparison reads each file's resource into a zeroed block 64 bytes
longer, so that a fixed part or Translation entries past the resource
read as zeros (20.8).

### 20.4 Tests and results

- `tests/ver/vscen.sh [-m MODES] [-n] [SECTION...]`: both libraries on
  the reference install, the logs compared per section;
  `VRUN_X=1 tests/ver/vrun.sh` runs in DOSBox-X, which keeps read-only
  attributes (DOSBox drops them); `tests/ver/vtree.sh OUTDIR ms|w16
  286|386`: w16's install tree alone, booted at C:\WINDOWS
  (`tests/dropin.sh`'s new `DROPIN_TREEDIR` and `DROPIN_DDRIVE`) so that
  its log compares with 3.1's line for line; `tests/ver/vmods.sh`: w16's
  modules (20.5); `tests/ver/vfonts.sh`: Control Panel's Fonts applet.
- Boot-test case **ver**, parts s, 3, x, tree286, tree386, mods, fonts,
  fontsnew. Results (installation-05, 2026-11-16; again 2026-11-21 with
  the section audit, 20.8): every part passes.
  s and 3: all eight sections equal Microsoft's (about 50 files sized and
  read, 2000 VerQueryValue calls, 140 VerFindFile, 190 VerInstallFile);
  x: the install section equal in DOSBox-X; tree286 and tree386: w16's VER
  on w16's KERNELs gives 3.1's own log. w16's VER with w16's LZEXPAND on
  the reference install gives the same log too. Since 2026-11-18 (20.6)
  tree286 and tree386 run Microsoft's VER.DLL on w16's tree as well: its
  log against 3.1's, and w16's against it; all equal.
- The real caller: MAIN.CPL (Fonts > Add, `tests/scripts/input/
  cplfonts.txt`, real keys) adds w16's test font W16TEST.FON with
  Microsoft's VER.DLL and with w16's: every screen 0 pixels, the same file
  in the system directory, the same WIN.INI line. With a newer
  W16TEST.FON (4.0) already in the system directory, both install the new
  one as W16TEST0.FON beside it.
- Microsoft's VER.DLL on w16's KERNEL: until 2026-11-18 its
  GetFileResource with offset 0 changed its caller's SI (the probe's
  length variable, which the probe keeps in SI; its next fill then overran
  its buffer and KRNL286 faulted), and it missed modules whose module name
  differs from their file's. Both were KERNEL's (20.6); it now gives 3.1's
  log on w16's tree under both kernels.

### 20.5 w16's modules' version resources

OpenWatcom's `wrc` writes a 16-bit VERSIONINFO whose strings' value
lengths count one byte more than it writes ("text\0" in the script) and
whose lengths are padded as if that byte were there, so the root claims
more than the resource holds: 3.1's VER.DLL finds no version at all, and
a block that fits still loses every string after the first (20.2). Every
w16 module with a version resource now gets it from `tools/mkver.py`:
CALC.EXE, DDEML.DLL, OLECLI.DLL (their `.rc` files include the block as
type 16), the fonts (`tools/fontc.py`) and VGA.DRV (`tools/vgares.py`),
as LZEXPAND.DLL and VER.DLL already did. Their numbers are now 3.1's
(3.10.0.103; OLECLI's file version 1.10.0.0, as 3.1's), so that an
installer's VerInstallFile compares them as it compares 3.1's files; the
texts are w16's. `tests/ver/vmods.sh` reads every file of the install
tree through both VER.DLLs: the ten with a version resource (CALC.EXE,
DDEML.DLL, LZEXPAND.DLL, OLECLI.DLL, VER.DLL, VGA.DRV and the four fonts)
give their fixed part, translation and eight strings, identically.

### 20.6 Found on the way (KERNEL), fixed

- 3.1's GetModuleHandle compares a name with the module names and then
  with the loaded modules' file names, ignoring the directory: "VUSEX.DLL"
  and "C:\VT\VUSEX.DLL" find module XUSE loaded from
  C:\WINDOWS\SYSTEM\VUSEX.DLL; "VUSEX" and "XUSE.DLL" find nothing (probe
  section `k`). w16's KERNEL took the base name as a module name. Probed
  in full with TK60 and fixed, with LoadLibrary's and LoadModule's
  lookup (9.2, "Finding loaded modules by name").
- Microsoft's VER.DLL's GetFileResource with offset 0, on w16's KRNL286
  and KRNL386, changed its caller's SI: the probe VRP keeps its length
  `n` in SI:DI across the call (OpenWatcom), and it came back as 6420h,
  the offset of the file name it had passed. Found with `[w16]
  trace=` (now recording SI and DI) and `tracefrom=VER` on VER's
  imports: SI became the name's offset inside VER between two LZEXPAND
  calls, with no import called in between, also with Microsoft's
  LZEXPAND, and w16's file and LZEXPAND calls kept SI and the stack
  (TK31). Between them VER calls KERNEL's IsROMModule (323), which it
  takes with GetProcAddress at start-up (untraced). 3.1's IsROMModule and
  IsROMFile remove 6 bytes of arguments (TK31: SP 6 bytes higher with 6
  words passed; Schulman's prototype has one word), w16's removed 2: the
  4 bytes left made VER's epilogue pop the name's offset into SI. Fixed
  (9.7); Microsoft's VER.DLL on w16's tree now gives 3.1's log, section
  for section, under both kernels.

### 20.7 UNVERIFIED, and not done

- A shared copy of Windows (Setup /N): how 3.1 tells, and that shared
  files then go to the Windows directory (SDK), were not observed.
- VIF_OUTOFSPACE (assumed for a failing write), VIF_OUTOFMEMORY,
  VIF_SHARINGVIOLATION, VIF_CANNOTREADDST and VIF_DIFFCODEPG were not
  provoked, nor a rename failing other than with "path not found"; a read-only copy in lpszCurDir
  neither (DOSBox-X deletes read-only files).
- Names with characters above 127 (ANSI/OEM conversion): DOSBox's host
  file names make them unreliable.
- Dates: whether the installed file keeps the source's date was not
  compared (DOSBox did not keep the dates the probe set).
- WfW 3.11's VER.DLL (3.11.0.300, the same exported names) was not
  probed; 3.1's Setup and DRIVERS.CPL were not driven through their own
  interfaces.

### 20.8 The VER audit

An outside, read-only review of `shell/ver/` (2 High, 5 Medium, 5 Low
findings) was checked against the code and against 3.1 with the probe's
section `audit` (`C:\VT\AUD` and the install pool, `tests/ver/
mkinputs.py`), compared like every other section in the boot-test case
**ver** (parts s, 3, tree286, tree386); every verdict, its evidence and
commit are in its triage file, `docs/notes/ver-audit-triage.md`, which
is in git history (removed once the audit was complete). 3.1's VER.DLL lived
through every malformed case (keys and roots at a segment's end, values
claiming 64K, NULL output pointers), so w16 answers as it does rather
than merely surviving. Fixed: a child's key read past its parent's end
(lstrcmpi), roots of 8000h and more, alignment shifts over 12, the
comparison's reading of the root's length instead of the table's,
Translations under 4 bytes, temporary names searched for with OpenFile
(the PATH, not directories), TEMP.FFF overwritten when every name was
taken, the temporary file left after a failed rename, NULL output
pointers written through, a Windows directory at a drive's root taken
for a shared copy. Kept as 3.1 has them: wValueLength returned as it is,
the 24 bytes GetFileVersionInfoSize reads past a short resource, a
matched key's value placed by its parent's end. Left out of the probe:
a resource whose table length leaves out the fixed part (3.1 then
compares what its buffer held before; w16 compares zeros).

## 21. TIMER.DRV

TIMER.DRV is the installable driver (10.7) behind MMSYSTEM's timer
services (Multimedia Programmer's Guide ch. 6; Programmer's Reference
Vol. 2: timeBeginPeriod, timeEndPeriod, timeGetDevCaps,
timeGetSystemTime, timeGetTime, timeKillEvent, timeSetEvent; Vol. 3:
TIMECAPS, MMTIME). MMSYSTEM opens it from its own DRV_LOAD with
OpenDriver("timer"), SYSTEM.INI `[drivers] timer=timer.drv`. The SDK
documents only the application's side; nothing documents what MMSYSTEM
asks of the driver. Windows 3.1's (module TIMER, description
`timer:Timer`, flags 8309h, flags2 08h, expected version 3.10, file
version 3.10.0.103, an installable driver) has four segments (DriverProc
in a fixed one) and exports WEP 1 and DRIVERPROC 2, both resident. It
imports KERNEL's GetVersion, LocalInit, GlobalWire, __WinFlags,
GetSelectorBase and GlobalPageLock, USER's MessageBox and LoadString, and
MMSYSTEM's DriverCallback (31), StackEnter (32) and StackLeave (33); its
strings are "Timer Driver" and a message that VTDAPI.386 is missing
(`tools/neinfo.py`, `tools/resdump.py`). Windows for Workgroups 3.11's is
the same file. w16's is in `drivers/timer/` (README there), installed as
`SYSTEM\TIMER.DRV`; the install tree's SYSTEM.INI names it, and
`[boot] drivers=mmsystem.dll` (MMSYSTEM.DLL itself is still the user's,
assets/README.TXT; w16's USER skips a boot driver it cannot find without
a message, `user/driver.c`, so the lines do nothing without it).

### 21.1 Observation tools

- **TMS.DRV** (`tests/timer/tms.c`, `tmsent.asm`): a pass-through timer
  driver. `[drivers] timer=tms.drv` puts it where MMSYSTEM opens its
  driver; it loads the real TIMER.DRV (Microsoft's or w16's) as a library
  and hands every DriverProc call on unchanged, recording the message,
  identifier, handle, both parameters, 16 bytes behind each parameter that
  is a readable far pointer (before and after), the result, the interrupt
  flag, the caller's module (USER, MMSYSTEM, or the application when
  MMSYSTEM jumped) and the nesting depth. The records live in its fixed
  data; the probe prints them after each call ("  TMS ..."), and DRV_FREE
  prints the last ones at Windows' end. Black-box observation between two
  Microsoft modules (§3).
- **TPR.EXE** (`tests/timer/tpr.c`, `tprs.asm`), run as the shell
  (`tests/timer/trun.sh OUTDIR ms|w16 [shim|plain] [/S|/3]`): every time*
  function in every mode, and the 8254's channel 0 (latched count,
  read-back status), the PIC, the BIOS tick count, DOS's clock, the CMOS
  clock, GetTickCount and WM_TIMER around them; sections and their letters
  in its header. Lines starting with "~" are measurements.
- **TPC.DLL** (`tests/timer/tpc.c`, `tpcs.asm`): the callback, in a DLL's
  fixed code as the SDK asks; records every register at entry, the caller,
  timeGetTime, the BIOS ticks, channel 0's count, the PIC's in-service
  register, the task and the nesting depth, and can kill its own event,
  set another, wait with interrupts enabled or call time* functions. It
  also has an INT 8 hook in front of the driver's (counting interrupts and
  logging channel 0's count at each: when callbacks come, in interrupts)
  and calls MMSYSTEM's StackEnter and StackLeave with marker registers.
- `tests/timer/tscen.sh [/S|/3]` runs TPR with Microsoft's driver and
  with w16's, plain and with TMS.DRV, and `tests/timer/tcmp.py` compares
  each pair (boot-test case `timer`).

### 21.2 Windows 3.1's TIMER.DRV as observed

**Messages** (TMS.DRV's records). MMSYSTEM calls DriverProc directly with
identifier 0 and the driver's handle, timeGetTime by a jump, so that the
driver returns straight to the application:

| Message | Parameters | Result, Standard mode |
|---|---|---|
| 0800h kill event | lParam1 the ID | 0; 97 (TIMERR_NOCANDO) for an ID not in use (also after a one-shot event fired) |
| 0804h set event | lParam1 -> {WORD wDelay, WORD wResolution, DWORD lpFunction, DWORD dwUser, WORD wFlags} (MMSYSTEM's copy) | the ID (DX 0); 0 for wDelay 0 or with 15 events set; any resolution and flags taken |
| 0808h system time | | DX:AX milliseconds since the driver started (36-90 ms after KERNEL's GetTickCount began, over runs) |
| 080Ch capabilities | lParam1 -> TIMECAPS, lParam2 its size | {wPeriodMin 1, wPeriodMax 65535} and 0 for size 4; 97 and nothing written otherwise |
| 0810h begin period | lParam1 | 0 for 1-55 ms, counted; 97 for 0 and for 56 up |
| 0814h end period | lParam1 | 0 for a period begun; 97 otherwise |

timeGetSystemTime with a size below 8 answers 129 (TIMERR_STRUCT) without
calling the driver; timeSetEvent with a NULL callback answers non-zero
without calling it either (MMSYSTEM's own). Through USER: message 0 0,
DRV_LOAD 1, DRV_ENABLE 1, DRV_OPEN 1 (opened at boot as handle 2, inside
MMSYSTEM's DRV_LOAD), DRV_INSTALL 2, DRV_EXITAPPLICATION 0,
DRV_EXITSESSION 0, DRV_DISABLE 1, DRV_FREE 0, any other message 0.

**Events.** Fifteen at once; an event takes the lowest free slot 1-15 and
its ID is (count << 4) | slot, the count going up by one with every event
set (1 to FFFh, then 1 again). Flags bit 0 makes it periodic; with bit 15
set it is never called (a one-shot one disappears when it is due; a
periodic one stays); other bits change nothing. A task's end changes
nothing: its events go on being called and its periods stay begun (the
driver's DRV_EXITAPPLICATION answers 0 and does nothing).

**The callback** is called at interrupt time after the end of interrupt
(the PIC's in-service register 0), interrupts enabled (flags 3206h), on
MMSYSTEM's interrupt stack (SS a 600h block, SP 05C2h at entry; GetExePtr
of that block gives TIMER.DRV), with the driver's DS, ES = SS, AX FB00h,
BX the ID, CX = SS, DX 0, the interrupted task current, the arguments
(ID, 0, dwUser, 0, 0). It is never reentered, and no other event is
called while it runs. StackEnter, called with marker registers, keeps
every register but SS:SP (SP 05FCh: the old SS:SP pushed on the new
stack) and the flags; StackLeave gives SS:SP back.

**The 8254.** From its start the driver runs channel 0 in mode 2 (status
B4h), at 65536 counts while nothing needs more. timeBeginPeriod and
timeSetEvent set it at once (control word and count: the count restarts)
to the shortest of the periods begun and the active events' resolutions
(wResolution, or wDelay when that is 0), 1-55 ms at P x 1193.18 counts
(65536 for 55): timeBeginPeriod(10) gives 11932, an event of resolution 5
5966, one of 30 ms and resolution 0 35795. It goes back when the event or
period ends. timeGetTime steps by 1 ms whatever the period (the count is
interpolated). The BIOS time of day, GetTickCount (SYSTEM.DRV), DOS's
clock and WM_TIMER keep their rate under a 1 ms period and a 1 ms event:
109-110 BIOS ticks, 5987-6042 ms of GetTickCount, 55-56 WM_TIMER of
100 ms over 6 CMOS seconds.

**When callbacks come** (TPR's sections trace and kin, counted in timer
interrupts; DOSBox gives the same numbers run after run). An event is
timed at its own resolution when nothing asks for less: a 3 ms periodic
one first after the 55 ms period under way ends, then every 3 ms; a 7 ms
one under timeBeginPeriod(7) every 7 ms from 14. A one-shot event comes at
the first interrupt after its delay (at period 10: 10 ms at the 2nd
interrupt, 20 at the 3rd, 30 at the 4th). 3.1's driver also has faults,
the same in every run, which w16's does not copy:
- after it restarts the 8254, its clock gains a whole 55 ms period (an
  event of d < 55 ms with resolution 0 is called at the 2nd interrupt, d
  ms after it was set, but timeGetTime in it reads 55 + 2d);
- at the 55 ms period some one-shot events come one interrupt early (100
  and 109 ms after 55 ms, 150 after 110, 200 after 165, 250 after 220),
  and some under shorter periods at the first interrupt (45 ms at period
  10, 30 at period 1);
- its clock goes back: timeGetTime often reads 55 ms less than a moment
  before, for one 55 ms period, and then 55 ms more (MMP polling it for
  3 s with no event or period: 0 to 7 such steps; TPR: a 55 ms step back
  in 18 to 21 of its 41 waits, in every run after the periodic section's
  first event, in some runs elsewhere). An unsigned difference from a
  reading before the step comes out near 2^32. What that did to the
  tests (2026-11-19): TPR's waits ended at once, which an earlier version
  of this list took for events not called after a 1 ms periodic event
  (with waits that take the largest reading, 3.1 calls them all in every
  run); and SNDBLST2.DRV, which imports timeGetTime, gave up waiting for a
  playing buffer in WODM_CLOSE in about 1 close in 10 (51-53 ms instead
  of 369; 3 of 32 under Microsoft's MMSYSTEM, 4 of 32 under w16's), left the buffer undone and answered 3 (MMSYSERR_NOTENABLED) to
  every open after that.

**386 Enhanced mode.** The driver leaves the timing to VTDAPI.386 (its
string 2 says so) and answers differently: timeGetDevCaps takes any size
from 4; timeBeginPeriod, timeEndPeriod and timeKillEvent answer 0 to
anything; 8 events at once, IDs (count << 3) | slot (slots 0-7, the
count from 2), returned sign-extended in DX:AX (FFFF8118h); flags bit 1
(not bit 15) marks an event never called. Every timing is exact (a
one-shot event of d ms after d ms, a periodic one every d ms); callbacks
come from VTDAPI's side (another stack, SP 0FECh, ES not SS, BX 0,
CX 000Dh, flags 0246h). timeGetTime there runs 1.7% slower than the BIOS
ticks (1998 ms in 37 ticks) and starts about half a second before
KERNEL's GetTickCount.

### 21.3 w16's TIMER.DRV

`drivers/timer/timer.asm` (NASM): everything in 21.2's tables, the slots,
IDs and flags, the 8254 policy (mode 2, the shortest period, restarted at
once), and callbacks through StackEnter, DriverCallback (DCB_FUNCTION)
and StackLeave with 3.1's SP, DX and register contents but SI and DI
(left over from the driver's code; 3.1's change from call to call). Its
first set-event message calls StackEnter and StackLeave once at task
time, when MMSYSTEM makes its interrupt stack: the stack is then
TIMER.DRV's, as with 3.1's, when the first event is set in the probe's
context section (with its event section first, GetExePtr still gives
MMSYSTEM: UNVERIFIED why). The clock counts the 8254's counts exactly,
also across restarts, and calls the INT 8 handler before it (SYSTEM.DRV,
DOSX, the BIOS) once per 65536 counts; events fire at the first interrupt
after their time, so w16's one-shot events come within their delay plus
one period, and its clock never jumps. 386 Enhanced mode: the same, on
VTD's virtual 8254, with 3.1's answers there (above); no VTDAPI. POPF
does not restore the interrupt flag of Windows' code in 386 Enhanced mode
(it runs below IOPL): the first version left interrupts off for good
after a timeGetTime, and every `cli` is now undone with `sti`.

### 21.4 Tests and results

`tests/timer/tscen.sh` (boot-test case `timer`, parts `s`, `3`, `qt`) on
installation-05. Standard mode, plain: TPR's 225 lines identical with
Microsoft's driver and with w16's, 42 measurements within `tcmp.py`'s
limits; with TMS.DRV: TMS's record of every message identical (1042
lines) after the masks `tcmp.py` lists (the time values, the nesting
depth, two selectors), which differ between two runs of Microsoft's own
or with the size of the driver's segments. 386 Enhanced mode: the same
lines identical, the timings shown (21.2, 21.5). `qt`: QuickTime for
Windows 2.1.2's sample movie on stock 3.1 with only w16's TIMER.DRV
(`tests/qtplay.sh -t TIMER.DRV`) plays as on 3.1: the same window, the
last frame 0 pixels off, 12 s on both; the case `quicktime` (the whole
install tree over 3.1) now uses w16's TIMER.DRV too, and passes.

Reliability (2026-11-19): TPR's waits measured time as an unsigned
difference of timeGetTime readings, and 3.1's clock steps back (21.2), so
its waits ended at once in every run in some places and in some runs in
others; that made `mmsystem@timer` and `timer@s` fail now and then (a
500 ms one-shot event killed before it came: 0 against w16's 97; TPC's
40 ms callback ending at once: 98 calls against 60). The waits now take
the largest reading and the callback counts only steps of 0-10 ms; in 16
runs of 3.1's after that every checked measurement stayed within
`tcmp.py`'s bounds of every other (periodic 1 ms: 998 calls in all 16,
nest: 59 in all 16), and `tcmp.py` now also checks the nest section's depth and
the waiting one-shot event exactly (44 measurements checked). Under load
the numbers that follow the host's speed (the clocks' starting points,
the sampled 8254 period, how much emulated time 6 CMOS seconds hold)
moved on both sides alike; `tcmp.py` checks them as properties now (its
header says how), and TPR's event section kills its short one-shot
events before logging.

### 21.5 UNVERIFIED, and not done

- 386 Enhanced mode timing: without VTDAPI.386, VTD's virtual 8254 makes
  w16's one-shot events 20-80 ms late and a 5 ms periodic event about 65
  calls a second instead of 200; the callbacks run on MMSYSTEM's stack.
  VTDAPI's interface (reached through INT 2Fh AX=1684h) was not observed;
  a logging hook in TMS.DRV, put in before the real driver loads, could
  record it.
- 3.1's timing faults (21.2) are not copied. In 386 Enhanced mode the ID
  count may also count a refused event (it starts at 2: not told apart).
- Who owns MMSYSTEM's interrupt stack when the first event set is the
  probe's event section (w16: MMSYSTEM; 3.1: TIMER.DRV).
- NULL pointers (TPR's section n), an 80286, w16's WIN386, real hardware.

## 22. TOOLHELP.DLL

TOOLHELP.DLL is the Tool Helper library debuggers and system utilities
use (Programmer's Reference Vol. 1 ch. 8; Vol. 2, the 34 functions; Vol.
3, GLOBALENTRY, LOCALENTRY, MODULEENTRY, TASKENTRY, STACKTRACEENTRY,
CLASSENTRY, MEMMANINFO, SYSHEAPINFO, TIMERINFO, the NFY* structures and
the constants). Windows 3.1's (module TOOLHELP, "TOOLHELP - Debug/Tool
Helper library", flags 8309h, flags2 08h, expected version 3.00, file
version 3.10; a fixed code segment 1 and a fixed DGROUP 2) exports WEP 1
(resident) and GlobalHandleToSel 50 to TaskSwitch 83 (`tools/neinfo.py`).
In the 3.1 install Dr. Watson (17 of its functions) and Packager
(NotifyRegister) import it; QuickTime 2.1.2's QTIM.DLL, QTOLE.DLL,
QTHNDLR.DLL and QTNOTIFY.EXE too (`tools/neimports.py --importers
TOOLHELP`). w16's is in `shell/toolhelp/`, installed as
`SYSTEM\TOOLHELP.DLL`.

Microsoft's TOOLHELP reads Windows 3.1's own structures: THHOOK, the
global heap's arenas and Burgermaster (KRNL286's and KRNL386's layouts),
local heaps' information blocks, module tables, task databases, USER's
class list. w16's KERNEL and USER keep those structures in 3.1's layouts
already (§6.11, §9.2, §9.7, §9.10, §10), so w16's TOOLHELP walks w16's
own structures in those layouts, and it works under Microsoft's KERNEL
too. Where it needs KERNEL it uses what 3.1's KERNEL offers:
ToolHelpHook (KERNEL.341) for the notifications, the scheduler's frame
on a waiting task's stack for TaskGetCSIP, TaskSetCSIP, TaskSwitch and
StackTraceFirst (w16's KERNEL now leaves 3.1's frame), DPMI 0203h/0205h
for the faults, USER's UserSeeUserDo (USER.216) for the first class.

### 22.1 Observation tools

- `THP.EXE` (`tests/toolhelp/thp.c`, `thps.asm`; its child `THC.EXE`,
  its library `THD.DLL`; `make thprobes`), run as the shell
  (`tests/toolhelp/thrun.sh OUTDIR ms|w16 SYSTEM`), calls every function
  in states it makes and logs the results section by section (letters in
  C:\THP.ARG): e (dwSize and NULL checks, GlobalHandleToSel), g (the
  global heap: GlobalInfo, the three walks, GlobalEntryHandle on blocks
  of every kind and on non-blocks, GlobalEntryModule, a walk from an
  entry on), l (local heaps: its own, one LocalInit made in a block,
  USER's, GDI's with the objects it makes counted by type), m (modules),
  t (tasks, with THC waiting), s (stack traces: frames of known kinds,
  odd and even BP, near and far; the waiting child's; BP after
  InitTask), c (classes, UserSeeUserDo), i (SystemHeapInfo, MemManInfo,
  TimerCount), r (MemoryRead/Write: huge blocks, limits, code, an alias),
  n (what KERNEL itself reports to a raw ToolHelpHook procedure; then
  NotifyRegister's notifications for strings, THD loaded and freed, a
  program run, NF_TASKSWITCH, NF_RIP, LogError and LogParamError, two
  callbacks in order), x (InterruptRegister: the child's faults
  restarted, ended with TerminateApp, passed on to its own callback; the
  probe's own), k (the scheduler's frame, TaskGetCSIP, TaskSetCSIP,
  TaskSwitch, TerminateApp), d (both TOOLHELPs side by side, below), w
  (Dr. Watson). Handles and selectors are logged by what they are ("dg.s"
  its DGROUP's selector, "M:USER", "T:child", "?"), raw numbers in "%"
  lines; every structure is filled with EEh first, so a field a function
  leaves alone shows.
- Section d loads a second TOOLHELP beside TOOLHELP.DLL: the other one
  (Microsoft's beside w16's, or w16's beside Microsoft's) renamed
  TOOLHLPM by `tools/nerename.py` at test time (a test-only combination,
  §3), and asks both the same questions in the same state: every entry
  of the three global walks, GlobalEntryHandle of every block (by handle
  and by selector), every local heap found, GlobalEntryModule of every
  segment of every module, the module, task and class walks,
  SystemHeapInfo, MemManInfo, GlobalHandleToSel, a stack trace; each
  structure compared byte for byte ("d: 0 difference(s)").
- `tests/toolhelp/thscen.sh` runs the probe in groups of sections, a boot
  each (a crash costs one group), on ref286 (3.1's KRNL286,
  `REFRUN_KRNL286`), ref386 (KRNL386, Standard mode), ref3 (386 Enhanced
  mode), tree286 and tree386 (w16's tree alone, its KRNL286 / KRNL386),
  with either TOOLHELP; `thcmp.py` compares the logs section by section
  (`--raw` the numbers too, `--mask FILE` lines left out or rewritten,
  each explained in the file). `tests/toolhelp/thwatson.sh`: Dr. Watson
  with each TOOLHELP.

### 22.2 Windows 3.1's TOOLHELP as observed

Common to all: a structure's dwSize must be its size exactly, else
FALSE; NULL gives FALSE (GlobalNext(NULL) faults under KRNL286 and is
not tried); MemManInfo returns TRUE whatever it gets, NULL too.

- **GlobalHandleToSel**: the value with bit 0 set, whatever it is (0
  gives 1).
- **Global heap** (KRNL286 / KRNL386): wcItems = the Burgermaster's count
  + 1 (a walk visits one less), wcItemsFree and wcItemsLRU its counts.
  dwAddress = the arena's address (the block's less 10h) / the block's;
  dwBlockSize; hBlock; wcLock = arena byte 0 ('M' 4Dh for a fixed block,
  'Z' for the last sentinel) / the lock count; wcPageLock 0 / the
  page-lock count; wFlags = the arena's flags (04h DGROUP, 08h
  discardable code) | 0100h (GF_PDB_OWNER) for a PDB's block, whose
  hOwner is then the task, not the PDB; wHeapPresent by the "LH" at the
  heap information block's 22h / 28h; dwNext the next arena (selector /
  offset), 0 at the last sentinel; dwNextAlt 0 in a walk of the whole
  heap and after GlobalEntryHandle, else the next in the LRU ring (newest
  first, 0 when it comes round) or the free list (KRNL286: the walk stops
  before the last sentinel; KRNL386: the sentinel is its last entry).
  wType: FREE (owner 0), SENTINEL (FFFFh), INTERNAL (the Burgermaster,
  FFFDh, and "not there" arenas, FFFCh: never BURGERMASTER), MODULE (a
  module's table), TASK (a task database), RESOURCE with wData the type
  (but a font file's fonts are UNKNOWN), a module's segment with wData
  its number: DGROUP or DATA when its arena has the DGROUP flag (DGROUP
  for the automatic data segment), else CODE (3.1's KERNEL's own data
  segment, whose arena lacks it, is CODE); all else UNKNOWN.
  GlobalEntryHandle takes a handle or a selector and fails for a
  discarded block, an alias, a huge block's second selector; any flags
  but 1 (LRU) and 2 (FREE) walk the whole heap.
- **Local heaps**: wAddress = the arena + 4 (fixed and free blocks) or
  + 6 (moveable, whose handle is at the arena + 4); wSize = the next
  arena less wAddress, less 2 more under KRNL286; wcLock from the handle;
  wType from GDI's objects (their second word, 'GO' + type - 1), 0 in
  USER's (retail), FFh for free; hHeap the selector; the walk ends before
  the last sentinel (LocalInfo's count is one more), and the call that
  ends it fills in the sentinel and sets wNext to 0. LocalInfo answers
  for any block (the word at its pointer + 4), LocalFirst only for a
  heap.
- **Modules, tasks**: the module list from THHOOK 04h, names from the
  resident-name table, the path from the module table's file record;
  ModuleFindName compares exactly ("user" finds nothing);
  ModuleFindHandle takes the handle or selector. Tasks from THHOOK 0Eh;
  szModule is the TDB's 8 bytes as they are and a NUL; wPSPOffset is the
  PDB's selector; wSS:wSP the TDB's 02h; the stack fields from the stack
  segment's instance data.
- **Stack traces**: StackTraceFirst refuses the current task and starts
  from the scheduler's frame (BP at 10h, the address at 12h); a frame is
  far when the saved BP is odd, or when the word above the return IP is
  a code selector of the caller's privilege whose limit takes the IP,
  else near (Schulman ch. 10, as observed); a chain ends at a BP of 0 or
  one not above the last. hModule and wSegment are left alone for a
  selector outside the global heap. InitTask returns with a zero word
  pushed on the caller's stack and BP pointing at it (SP 2 less than at
  the call), so a trace ends with the start-up code's frame.
- **Classes**: UserSeeUserDo(5) gives the first class; a class: 00h the
  next, 04h its atom (USER's), 14h its module (CLASSENTRY.hInst).
- **SystemHeapInfo**: GetFreeSystemResources' percentages and the two
  DGROUPs' selectors. **MemManInfo**: DPMI 0500h's first nine values as
  they are, wPageSize 1000h. **TimerCount**: in Standard mode the tick
  count and how far the 8253 has got (never going back); in 386 Enhanced
  mode the VTD's (Schulman), not GetTickCount's count.
- **MemoryRead/Write**: any selector there is (an alias too), up to its
  limit (a request past it is cut short, one beginning past it gives 0);
  code is written too.
- **Notifications**: KERNEL's codes (§9.7) turned into NFY_*; LOADSEG's
  wType is the segment's flags as the module table has them, wcInstance
  0; FREESEG for each segment of a module deleted, a library's automatic
  data segment and module table first, the rest (and a program's) in an
  order that changes from boot to boot. The callbacks are called in the
  order they were registered until one returns TRUE; NF_TASKSWITCH adds
  TASKIN/TASKOUT, NF_RIP the RIP notification; LogError and
  LogParamError reach every callback (NFYLOGERROR 10 bytes,
  NFYLOGPARAMERROR 14). NotifyRegister takes a NULL procedure and, under
  KRNL286, any task handle; one registration a task. A callback left by
  a task that ended without NotifyUnRegister is still called (the probe
  ended there); w16's drops an ended task's callbacks.
- **Interrupts**: InterruptRegister takes a NULL procedure too, one a
  task. The callback runs on the faulting task's stack as that task,
  with [SP] the return into TOOLHELP, then AX, the number (0, 3 with the
  IP after INT 3, 6 ...), a handle, the fault's IP, CS, flags; RETF
  passes it on (the next callback, then Windows), removing 10 bytes and
  IRET restarts, TerminateApp ends the task (exit code FFh). Under DOSBox
  a write to a code segment, a null selector and a stack past its limit
  do not fault, so those were not observed.
- **Task control**: TaskGetCSIP of the current task is 0; TaskSetCSIP
  returns the old address and the task goes on at the new one when next
  run; TaskSwitch runs it there at once as that task (with DS its TDB
  under 3.1); TerminateApp(another task, NO_UAE_BOX) ends it with exit
  code FFh and returns.

### 22.3 w16's TOOLHELP.DLL

`shell/toolhelp/toolhelp.c` (modules, tasks, information, notifications,
interrupts, task control, stack traces, classes), `global.c` (the global
and local heaps, MemoryRead/Write), `entry.asm` (the procedure KERNEL's
hook calls, the DPMI exception handlers and the protected-mode vectors 1
and 3, the dispatch that runs the callbacks on the faulting stack and
passes a fault on by letting it happen again into the handler before
ours, TaskSwitch's entry, the 8253, the VTD), `th.h`. Large model, SS !=
DS, every export loads its DS; code and data fixed and preloaded, as
3.1's. Registrations are kept in growing lists in its local heap (no
limit) and dropped when a task ends (EXITTASK) or is ended by
TerminateApp. The notification hook is installed while anyone has a
callback, the fault handlers while anyone has an interrupt callback; a
fault in a "__GP" range (KERNEL's HasGPHandler) is Windows' own and not
reported.

Changes elsewhere: the scheduler's frame is 3.1's (DX BX ES CX AX DI SI
DS, BP at 10h, the address at 12h: `kernel/kentry.asm` task_switch,
`task.c` a new task's first frame); InitTask leaves 3.1's zero word and
BP (`kentry.asm`); a deleted module's FREESEG notifications come in
3.1's order and the segment-load (50h) and DLL-start (64h) ones with ES
the hModule, the name in the resident-name table (`kernel/toolhelp.c`);
USER's UserSeeUserDo (USER.216) answers requests 1-5 (`user/class.c`);
`tests/refrun.sh` kills a DOSBox that ignores the time limit's TERM.

Found and left (`tests/toolhelp/tree.mask`): 3.1 gives a code segment
one byte more than its minimum allocation, rounded (2B0h: 2D0h under
KRNL286; 0E0h: 100h under KRNL386), w16's loader its minimum allocation;
3.1's KERNEL frees a module's segments in an order that changes from boot
to boot (it seems, their places in the heap), w16's in segment order
after the automatic data segment and the module table.

### 22.4 Tests and results

Boot-test case `toolhelp` (`tests/toolhelp/thcase.sh`): parts ref286,
ref386, ref3 (3.1 with each TOOLHELP: the logs equal, section d's counts
aside, `same.mask`; section d without a difference), tree286, tree386
(w16's tree alone with w16's TOOLHELP against 3.1 with Microsoft's;
`tree.mask` names each difference, all of them w16's modules', not
TOOLHELP's), watson (Dr. Watson started, watching a program, closed: its
log with each TOOLHELP as with Microsoft's, on 3.1 and on w16's tree),
qt (QuickTime's player on w16's tree laid over 3.1, which brings w16's
TOOLHELP; `tests/qtplay.sh`, `W16_QTDIR`).

Microsoft's TOOLHELP on w16's tree: every read-only function answers as
w16's does (section d, structure for structure); it fails where w16's
system is not 3.1's underneath: MemoryWrite to a code segment ends
Windows, its fault handlers never see a fault (w16's KERNEL reports it
instead), and TaskSwitch and TerminateApp did not complete (sections r,
x, k end there). docs/notes/toolhelp-handover.md has the details and
the runs.

### 22.5 UNVERIFIED, and not done

- Low-stack faults (number | 8000h, SS:SP pushed), Ctrl+Alt+SysRq
  (100h), INT 1, NFY_RIP and NFY_INCHAR (a debugging KERNEL's), stack
  and GP faults (DOSBox does not raise them): w16's TOOLHELP has none of
  the first two, reports the others as it does the faults observed.
- The callback frame's "handle" word (3.1: an offset of TOOLHELP's own;
  w16: 0) and the AX a callback gets (TOOLHELP's DS in both).
- TerminateApp(UAE_BOX): w16's calls FatalAppExit(0, NULL) in the task,
  whose box was not compared with 3.1's.
- Dr. Watson with a fault: under DOSBox 3.1 itself showed nothing and
  went no further.
- WfW 3.11 (no importer of TOOLHELP there) was not probed.

## 23. MMSYSTEM.DLL

MMSYSTEM.DLL is Windows 3.1's multimedia system library (Multimedia
Programmer's Guide; the Multimedia Programmer's Reference is not among
the documents w16 has). Windows 3.1's (module MMSYSTEM, description
`System APIs for Multimedia`, flags 8309h, flags2 08h, expected version
3.10, file version 3.10.0.103) exports WEP 1, sndPlaySound 2,
mmsystemGetVersion 5, DriverProc 6 (WEP and DriverProc resident),
OutputDebugStr 30, DriverCallback 31, StackEnter 32, StackLeave 33,
MMDRVINSTALL 34, the joystick (101-109), MIDI (201-216, 301-313), aux
(350-354), wave (401-421, 501-514), timer (601-607), MCI (701-717),
mmTask (900-905), Drv* (1100-1104) and mmio (1210-1226) functions: 136
entries (`tools/neinfo.py`). It is a `[boot] drivers=` driver (10.7):
USER opens it at the shell's InitApp, and its DRV_LOAD opens the timer
driver (21) and the joystick driver. MMTASK.TSK (module MMTASK, flags
030Ah, a 4 KB stack, 1Ch bytes of data; imports InitTask, WaitEvent,
OldYield, InitApp and SetMessageQueue only) is the program
mmTaskCreate starts. 3.1's importers in the reference install: Media
Player, Sound Recorder, MCISEQ, MCIWAVE, MIDIMAP, SND.CPL and
TIMER.DRV; QuickTime for Windows' QTIM.DLL (mmio, wave out, MIDI out,
time*) and MCIQTW.DRV (the MCI driver helpers).

w16's is built in layers (`docs/notes/mmsystem-handover.md` has the
plan, the state and the raw observations): 1 the core, 2 MMIO, 3 wave,
MIDI, aux, joystick and sndPlaySound, 4 MCI, the Media Control Interface
(all four done: every export is w16's own; only MCIPARSECOMMAND, which
3.1 faults on with zeros, is a stub).

### 23.1 Observation tools

- **MMP.EXE** (`tests/mmsystem/mmp.c`, `mmio.c`) and its library
  **MMC.DLL** (`mmc.c`, `mmcs.asm`), run as the shell
  (`tests/mmsystem/mrun.sh OUT ms|w16`, sections in `MRUN_ARG`): the
  drivers open at boot and MMSYSTEM's DriverProc through USER, the Drv*
  functions, DriverCallback with every type and bad target (MMC's
  callback records every register, the stack and the caller), the
  interrupt stack (StackEnter/StackLeave nested with marker registers,
  before and after it exists), timeSetEvent's checks, the mmTask
  functions and the task MMTASK starts (MMC's task function logs its
  registers, frame, command tail, data segment and queue size), what
  OutputDebugStr does (INT 21h and INT 41h hooked), the argument sizes
  of the undocumented exports (a pop counter), and the MMIO sections:
  FOURCCs, DOS files and RIFF chunks (the file's bytes dumped), memory
  files, buffering as a custom I/O procedure sees it (MMC's "TST "
  procedure logs every message and the MMIOINFO fields it owns),
  errors, the file-name flags, procedures and rename.
- `tests/mmsystem/mcmp.py` compares two logs (masks for what differs
  between two runs of Microsoft's own, known differences explained);
  `tests/mmsystem/mscen.sh` runs core, mmio and the timer probe (21.1,
  `TRUN_MM=w16`) with both MMSYSTEMs and compares them (boot-test case
  `mmsystem`, parts core, mmio, timer, qt, dev, prog).
- **MDS*.DRV and MMSYSLOG.DLL** (`tests/mmsystem/mds.c`, `mdl.c`): logging
  pass-through device drivers. `[drivers] wave=mdsw1.drv` with `[mmslog]
  MDSW1=sndblst2.drv` puts one where MMSYSTEM opens a wave (MIDI, aux)
  driver; it loads the real one and hands every DriverProc and
  wodMessage/widMessage/modMessage/midMessage/auxMessage call to
  MMSYSLOG, which records it (the arguments, the bytes behind the pointers
  before and after, the result, the caller) and makes it. A scratch copy
  of the real driver with its imports of MMSYSTEM renamed to MMSYSLOG
  (`tools/nerename.py`) has MMSYSLOG between it and MMSYSTEM too: each
  DriverCallback and each MIDI call (MIDIMAP's) is recorded and passed on
  by a far jump with the stack as it was. The variants export what the
  real drivers do (W: SNDBLST2's five entries; F: Creative's SB16FM; M:
  DriverProc and modMessage; A: auxMessage). MMP's device sections
  (`tests/mmsystem/mmdev.c`: n numbering and capabilities, e error texts,
  w and i wave output and input, m and j MIDI, a aux, y joystick, x
  sndPlaySound, b MessageBeep) print the records after every step, with
  the probe's handles, headers and callbacks by name.
  `tests/mmsystem/mrun.sh` (MRUN_SND none, sb2: Windows 3.1's own
  SNDBLST2.DRV from its Setup files with the keys Setup writes (MSADLIB.DRV
  left out: its Ad Lib detection in DOSBox varies from run to run), multi: several keys, sb16: Creative's drivers, MRUN_SHIM=1) and
  `tests/mmsystem/mdscen.sh` (every case with both MMSYSTEMs, compared by
  `mcmp.py`); `tests/mmsystem/mprog.sh` runs Sound Recorder, Control
  Panel's Sound and MIDI Mapper applets and Media Player with real keys.
- **MCI (layer 4).** MMP's section Q (`tests/mmsystem/mmci.c`) runs a
  script, `C:\MCI.TXT`: command strings (mciSendString, with any return
  size and callback), mciSendCommand with blocks written in the script,
  mciExecute (its message box read and closed), mciGetErrorString, the
  yield procedures, the driver helpers from the application and the
  command tables. **MCT.DRV** (`tests/mmsystem/mct.c`, `mct.rc`) is a test
  MCI driver: [mci] lines name it with options (`type=N` the device type
  it reports, `table=NAME` a command table of its own written for the
  probe with every item type), the script sets its answers (return value,
  DWORDs and strings written into the block, notification, mciDriverYield
  calls) and MMSYSLOG records every DriverProc call with the parameter
  block before and after and the strings it points to, and its calls of
  MMSYSTEM's MCI helpers with their results. **MDSC*.DRV** put MMSYSLOG
  between MMSYSTEM and 3.1's MCIWAVE.DRV and MCISEQ.DRV (and QuickTime's
  MCIQTW.DRV), whose imports of MMSYSTEM go through it (renamed, as for
  the device drivers). `tests/mmsystem/mcirun.sh` runs one script (plain:
  MCT.DRV; shim: also the Sound Blaster driver and the MCI drivers behind
  MDSC; qt: on a QuickTime-installed drive), `tests/mmsystem/mciscen.sh`
  every script (`tests/mmsystem/mci/*.txt`) with both MMSYSTEMs, compared
  by `mcmp.py`; `tests/mmsystem/mci/gentypes.py` writes the survey of
  every documented device-type command (explore3).

### 23.2 Windows 3.1's MMSYSTEM as observed (layers 1 to 4)

- mmsystemGetVersion 0101h. After boot the open drivers are TIMER
  (handle 2) and MMSYSTEM (1); the midimapper is not opened at boot.
  MMSYSTEM's DriverProc answers 1 to DRV_INSTALL and
  DRV_EXITAPPLICATION, 0 to the other messages tried. The Drv*
  functions are USER's OpenDriver, CloseDriver, SendDriverMessage,
  GetDriverModuleHandle and DefDriverProc.
- **DriverCallback**(dwCallback, uFlags, hDevice, uMessage, dwUser, dw1,
  dw2): type uFlags & 7; DCB_WINDOW (1) PostMessage(window, uMessage,
  hDevice, dw1) and DCB_TASK (2) PostAppMessage likewise, answering
  what USER answers (0 for window or task 0); DCB_FUNCTION (3) calls
  dwCallback(hDevice, uMessage, dwUser, dw1, dw2) and answers 1, or 0
  when its selector is not code; other types 0. Unless DCB_NOSWITCH (8)
  the function runs on the interrupt stack when it is free. It gets AX
  = its code segment's access rights (FB00h), BX = hDevice, CX = ES =
  the caller's SS, the caller's DX, SI, DI and DS, flags with PF.
- **The interrupt stack**: one 600h-byte block (GlobalAlloc(GMEM_SHARE)),
  made by the first timeSetEvent. StackEnter switches to it (SP 05FCh,
  the old SS:SP above, carry clear) when it exists and is free; else it
  stays, one word lower, carry set; StackLeave undoes either. Every
  register but SS:SP is kept. A timer callback through TIMER.DRV runs at
  SP 05C2h on it (21.2).
- **Timer services**: as 21.2 says; timeSetEvent refuses a callback
  that is not code (NULL, data, 0001:0000) with 11 (MMSYSERR_INVALPARAM)
  without asking the driver.
- **OutputDebugStr** does nothing observable (no INT 21h, no INT 41h).
- **Tasks**: mmTaskCreate(fn, &hTask, dwInst) starts MMTASK.TSK and
  returns 0 at once with the task's handle; KERNEL's LoadModule runs the
  new task first, which yields back after its start-up; mmTaskCreate
  puts fn and dwInst in its data at 10h and 14h; the task then calls
  fn with DX:AX and the stack holding dwInst (SI 0, DI the instance, ES
  0, BP over a 0 at the stack's top), its queue holding 64 messages, and
  ends when fn returns. The command tail is empty. mmTaskSignal(h) is
  PostAppMessage(h, WM_USER, 0, 0); mmTaskBlock dispatches window
  messages and returns the number of the first task message (a signal
  sent right after mmTaskCreate waits in the new task's queue for it);
  mmTaskYield lets the other tasks run, also with a message waiting
  (MMP's explore12: a second task counting its turns ran during 20
  calls; the message stays queued); mmGetCurrentTask is GetCurrentTask.
- **Argument sizes** of the undocumented exports: MMDRVINSTALL 8 bytes,
  JOYSETCALIBRATION 26 (the joystick ID and six pointers); MCIPARSECOMMAND
  faults with zeros (UNVERIFIED).
- **MMIO** (the Programmer's Guide ch. 7 for the interface): a buffer is
  filled by one MMIOM_READ of its whole size and written back by one
  MMIOM_WRITEFLUSH of the bytes in it; MMSYSTEM keeps the file's place
  itself (lBufOffset) and tells the procedure to seek only when its
  lDiskOffset is not where the next read or write goes, so a seek inside
  the buffer moves the pointer, any other one empties the buffer at the
  new place without a message, and SEEK_END asks the procedure at once;
  without a buffer the same holds for every read and write. Writes past
  a full buffer flush it; mmioAdvance and mmioSetInfo take pchNext,
  pchEndRead and MMIO_DIRTY from the caller. mmioOpen keeps the flags it
  was given (MMIO_CREATE, MMIO_ALLOCBUF) in dwFlags, htask stays 0, the
  default buffer is 8192 bytes, and the caller's MMIOINFO only gets
  wErrorRet (257 for a missing file); MMIO_PARSE, MMIO_EXIST,
  MMIO_DELETE are OpenFile's (EXIST and PARSE copy the full name back;
  a failed DELETE answers 262), MMIO_GETTEMP is GetTempFileName's with
  the name from its fourth character as the prefix; all answer 1. A
  compound name "NAME.EXT+ELEMENT" picks the procedure installed for
  EXT; mmioRename of a plain name goes to DOS. mmioStringToFOURCC takes
  four bytes whatever they are (a 0 becomes a space); flags other than
  MMIO_TOUPPER give FFFFFFFFh. RIFF: created chunks are MMIO_DIRTY and
  mmioAscend writes their size and a pad byte; mmioDescend skips
  chunks by seeking past them before it checks the parent's end (so a
  failed search can leave the file far past its end). Memory files keep
  everything in the buffer: SEEK_END counts back from the end (3.1's
  answers -1 to a negative offset). Bad handles: 5 (or -1, 265, 0).

- **Device drivers** (layer 3; MMP's device sections over Windows 3.1's
  own SNDBLST2.DRV, MSADLIB.DRV and MIDIMAP.DRV with the logging drivers
  MDS*.DRV between them and MMSYSTEM). MMSYSTEM opens them at boot (its
  DRV_LOAD, after the timer), one OpenDriver per `[drivers]` key and
  direction: `wave` for wodMessage, `wave` again for widMessage, then
  `wave1` ... `wave9` (a gap is skipped, `wave10` is not read), the `midi`
  keys for modMessage and midMessage, `midimapper` (modMessage; only when
  there are MIDI output devices), the `aux` keys. An open whose entry the
  driver does not export is closed at once. Each entry gets DRVM_INIT
  (100) and its GETNUMDEVS; a driver with 0 devices stays open. IDs follow
  that order, each driver's devices together; the messages are Wine's
  mmddk.h numbers (WODM_* 3-20, WIDM_* 50-60, MODM_* 1-13, MIDM_* 53-62,
  AUXDM_* 3-6), called directly as xxxMessage(device, msg, dwUser, p1,
  p2) with dwUser the instance the driver wrote at OPEN (wave: through a
  pointer to a DWORD on MMSYSTEM's stack; MIDI: to the handle's own
  field). The callbacks (WOM_*, WIM_*, MOM_*, MIM_*) are the drivers' own
  DriverCallback calls (a function callback with DCB_NOSWITCH set by
  SNDBLST2); the drivers' callbacks run on MMSYSTEM's interrupt stack,
  which exists once drivers are loaded. Then MMSYSTEM plays SystemStart.
- **Checks MMSYSTEM makes itself** (everything else reaches the driver
  unchanged, NULL pointers and odd sizes included): handles are typed (a
  wave output handle given to waveIn*: 5); waveOutOpen/waveInOpen: flags
  other than WAVE_FORMAT_QUERY|WAVE_ALLOWSYNC 10, a callback type above 3,
  a window that is not one or a function that is not code 11 (the handle
  untouched), then *phwo = 0, a bad ID 2 (WAVE_MAPPER: each device in
  turn); a query has hWave 0 and allocates nothing; a NULL handle pointer
  opens the device anyway. midiOutOpen sets *phmo = 0 first; flags 10;
  FFFFh goes to the midimapper (6 without one). GetDevCaps: FFFFh 6 (MIDI
  out: the mapper's), a bad ID 2, size 0 answers 0 without the driver.
  Volume functions take IDs (a handle is a bad ID: 2). xxxMessage with a
  bad handle answers 0. Headers (WAVEHDR 32 bytes, MIDIHDR 28): NULL or
  another size 11; dwFlags with bits other than 1Fh and bit 31 11;
  PREPARE of a prepared header 0 without the driver, otherwise the flags
  cleared (wave out keeps the loop bits) and the driver asked; its 8
  (MMSYSERR_NOTSUPPORTED, SNDBLST2's answer) makes MMSYSTEM set
  WHDR_PREPARED itself (7 for a NULL lpData); UNPREPARE: queued 33, not
  prepared 0 without the driver, the driver's 8: WHDR_PREPARED cleared;
  WRITE/ADDBUFFER: queued 33, not prepared 34 (MIDI 64); WRITE clears
  WHDR_DONE first, ADDBUFFER does not. Closing with a buffer queued is the
  driver's business: SNDBLST2 waits for a playing one, sends it back done
  and closes (0); paused, it answers 33 at once and the handle stays
  usable (reset, unprepare, close). MMP now tests the paused case only:
  the wait for a playing buffer runs on 3.1's clock, which steps back
  (21.2), and failed in about 1 run in 10 under either MMSYSTEM. The error texts (string n for
  error n; MMSYSERR 0-11, WAVERR 32-35, MIDIERR 64-69, taken by calling
  3.1's functions): size 0 answers 0, NULL 9, another number "" and 9.
- **sndPlaySound** (Programmer's Guide ch. 4; MMP section x): nothing
  without a wave output device (FALSE, NULL too). A hidden window of
  MMSYSTEM's, class "#42", receives the callbacks: waveOutOpen(WAVE_MAPPER,
  the format inside the image, the window, instance 0,
  CALLBACK_WINDOW|WAVE_ALLOWSYNC), one header over the data (dwFlags and
  dwLoops 0 at PREPARE; BEGINLOOP|ENDLOOP and FFFFFFFFh after it for
  SND_LOOP), WRITE. MM_WOM_DONE starts a timer (ID 1); the WM_TIMER that
  finds the sound done unprepares and closes. A file is read whole into
  one block, the header in front (dwUser the format's address), kept until
  the next sound; SND_MEMORY plays the caller's image in place. Names:
  `[sounds]` first (case-insensitive, the file before the comma, blanks
  trimmed), else a file as OpenFile finds it (no ".wav" added); missing
  or not a wave: SystemDefault unless SND_NODEFAULT (FALSE, no driver
  call); a memory image that is not RIFF: FALSE. NULL or "" stops (RESET
  unless done, UNPREPARE, CLOSE) and answers TRUE; each new sound stops
  the last; SND_NOSTOP while one plays, SND_LOOP without SND_ASYNC and
  unknown flags: FALSE. SND_SYNC returns when it has played.
- **MessageBeep** goes through MMSYSTEM on 3.1: USER's DoBeep in
  MMSOUND.DRV finds MMSYSTEM by GetModuleHandle (never loads it) and
  hands every type but FFFFh to sndPlaySound(name, SND_ASYNC) at once,
  the name by bits 4-7 of the type (1 SystemHand, 2 SystemQuestion, 3
  SystemExclamation, 4 SystemAsterisk, anything else SystemDefault), each
  stopping the sound before; FFFFh, no MMSYSTEM, or FALSE from
  sndPlaySound (no wave device, no sound for the name nor for
  SystemDefault): the speaker. Once found, the driver also plays the
  SOUND API's notes on a 5 ms periodic timeSetEvent. MMP section b and
  `tests/soundprobe.sh beep` (drivers/sound/README.md, "The beep"); w16's
  MMSOUND.DRV does the same.
- **Joystick** (no driver in the reference install): everything 6
  (MMSYSERR_NODRIVER), joyGetNumDevs 0, joySetCapture with a bad window
  165 first.

**MCI (layer 4;** MMP's section Q with MCT.DRV, and 3.1's MCIWAVE, MCISEQ
and QuickTime's MCIQTW behind MDSC; the scripts are
`tests/mmsystem/mci/explore1-11.txt` and `qtw.txt`):

- **Opening a device** loads its driver with USER's OpenDriver(file,
  NULL, lParam) on the file of its [mci] line, lParam an
  MCI_OPEN_DRIVER_PARMS {WORD wDeviceID (the new ID), LPCSTR lpstrParams
  (the line's options; DRV_OPEN's own lParam1 is USER's empty string),
  WORD wCustomCommandTable FFFFh, WORD wType 0} on the stack, which the
  driver fills; then MCI_OPEN_DRIVER (0801h) with the open's flags and
  block. Every later command goes by SendDriverMessage(message, flags,
  block), any message number (0900h, 1234h, even 07FFh from
  mciSendCommand); a close is MCI_CLOSE_DRIVER (0802h, the close's flags
  and block) then CloseDriver. MCIWAVE loads its table at DRV_LOAD.
- **Devices**: IDs are the lowest free from 1; aliases are the task's,
  case-insensitive; an alias is always passed (ALIAS added: the device
  name, or the element), except for a type or element given by number; a
  command's own block is patched for the call (the alias pointer, a type
  ID turned into its [mci] key's name: "other2" for HIWORD 2) and put
  back after, the device ID left in it. A device is named by its [mci]
  key, a type's name (the type's first key: "animation" opens
  "animation1"), a driver's file without a path, or an element: [mci
  extensions] by extension (unknown: 263), "type X", "X!element", "new"
  (needs an alias: 299). An element may be opened twice without
  shareable (the driver decides). A missing driver: 266.
- **Command tables**: a command string is looked up in the driver's own
  table, then its type's table, then the core table. 3.1 has type tables
  for videodisc, overlay, cdaudio, animation, waveaudio and sequencer (not
  vcr, dat, scanner, digitalvideo, other), chosen by the [mci] key's name
  when it names a type ("videodisc", "animation1"), else by the type the
  driver reported. The core table has open, close, play, record, seek,
  stop, pause, resume, set, status, capability, info, sysinfo, break,
  sound, save and load ("media present" and "info file" are the type
  tables'). A value's DWORD follows from its item's place in the table
  (after dwCallback; the return value first; a rectangle takes two). A
  type's table takes a table number like a driver's (the lowest free from
  1; 0 is the core table) and goes with its last device;
  mciLoadCommandResource loads the same resource twice as two tables,
  FFFFh when missing or with no module; mciFreeCommandResource: TRUE once.
- **"open" through a device's own entry** (the driver's or its type's
  table has "open": MCIWAVE; the animation, overlay and waveaudio types):
  the arguments are parsed again after DRV_OPEN into a fresh zeroed block
  with no device ID and the window only with notify, and every string
  parsed then reaches the driver 4 bytes into a copy of itself (alias
  "abcdefghij" as "efghij", type "waveaudio" as "audio"); the device token
  and the default alias are not affected. Before the driver loads, a
  repeated keyword (295) and type or alias without a value (269) are
  refused, and "element" (263); an unknown keyword fails after DRV_OPEN
  (the driver closed again: 259).
- **The parser**: blanks and tabs separate words; case does not matter;
  quoted strings (305 extra characters after, 294 no closing quote; a
  quoted keyword is no keyword: 290); a keyword of several words matches
  with any blanks between; integers are decimal with a "-" (the signed
  range), no "+" or "0x" (270), and a positive one wraps past 2^32 unless
  a digit multiplies past 429496729 (270); "a:b:c:d" packs bytes; a
  constant also takes an integer; errors: repeated keyword 295, two values
  of one constant or break on with off 284, unknown keyword 259 (290 where
  the values themselves are keywords, status), unknown command 261,
  missing integer 270, missing constant 312, missing string 269, no
  command 267, no device 292, "all" with a command that returns a value
  279.
- **Answers**: an integer becomes "%ld" (signed) and needs 12 bytes (268,
  as do a rectangle "%d %d %d %d" under 28 and a quantity under 12); size
  0 is no buffer. MCI_RESOURCE_RETURNED: string HIWORD(value) of
  MMSYSTEM's table (or the driver's with MCI_RESOURCE_DRIVER), LoadString
  cut to the size; colonized "%02d:%02d:%02d" and ":%02d" for four, both
  flags "01:02:03:"; precedence resource, colonized, integer-returned (the
  value read through as a pointer), plain. A string return gets the
  caller's buffer and size (NULL: NULL and 0), unchecked after;
  mciSendString puts a NUL in the buffer's first byte first. sysinfo
  quantity writes the DWORD into the buffer and formats it in place.
  Errors: the low word, a driver's own (512 up) with the device ID in the
  high word. mciSendCommand leaves only the low word of the block's
  second DWORD when the driver answered MCI_RESOURCE_RETURNED (Media
  Player reads MCIWAVE's "can eject" so); "all" sends to each of the
  task's devices in ID order, takes nothing out, and two non-zero answers
  give 280.
- **System commands**: sysinfo (quantity, name N, open, installname; by
  [mci] keys in file order, a type counting the keys named after it, or
  the task's open devices in ID order; name ignores the buffer's size;
  posts MM_MCINOTIFY itself, lParam 0); break (on N: MMSYSTEM's yield
  procedure with data N, hwnd; off; neither 273); sound (sndPlaySound;
  262 when it fails).
- **Notification**: the string path always puts the window argument in
  dwCallback; drivers post with mciDriverNotify (PostMessage
  MM_MCINOTIFY, status, device ID; its answer). **Auto-open**: a command
  to a name not open opens it with MMSYSTEM's window as callback and
  sends the command with notify to that window, which closes the device
  when notified (with a block on its stack it never filled); "notify" from
  the caller: 300; "close" of an unknown name: 263.
- **Yield**: each device starts with MMSYSTEM's yield procedure and data
  VK_CANCEL; mciSetYieldProc refuses NULL; mciDriverYield calls the
  procedure and answers what it answers. The procedure leaves queued
  messages where they are and lets the other tasks run even when one is
  waiting (explore12). w16's did not until 2026-11-19 (PeekMessage with
  PM_NOREMOVE alone returns at once then): MCIWAVE's "play ... wait"
  waits for its MMTASK task by yielding, and with a message in the
  application's queue it spun for ever (14 of 16 runs of explore7 under
  load); mmTaskYield had the same fault. **Helpers**: creator task,
  driver data, 0 or FALSE for a bad ID.
- **mciGetErrorString** reads MMSYSTEM's string table (the texts are
  3.1's: wave and MIDI 0-69, MCI 257-349, device types, modes, time
  formats and the rest 513-1227): size 0 or NULL 0; else the text cut to
  the size, FALSE when there is none; with a device ID in the high word
  and an error from 512 the driver's string, its device gone 278's text.
  **mciExecute**: the command string with no buffer or window; on an
  error a message box without owner, caption the device token, the
  error's text, MB_ICONHAND.

### 23.3 w16's MMSYSTEM.DLL

`shell/mmsystem/`: `mmfix.asm` (one fixed
code segment, with a fixed data segment: StackEnter/StackLeave,
DriverCallback, the time* functions with 3.1's jumps, calls and frames,
mmTaskSignal, the Drv* jumps), `core.c` (DriverProc, the interrupt
stack, mmTask*; mmTaskCreate finds the new task by its instance in
KERNEL's task list), `mmio.c` (MMIO with the DOS and memory procedures:
MMSYSTEM's own handles are global blocks on a list),
`dev.c` (layer 3: the device drivers opened at DRV_LOAD, every wave, MIDI
and aux function with 3.1's checks, the handles as blocks of its local
heap on a list, the error texts from its string table), `snd.c`
(sndPlaySound and MMSYSTEM's window "#42", SystemStart at boot; the
window also closes the devices MCI opened for one command), `mci.c`
(layer 4: devices, the driver protocol, the command-string parser over
the three tables, answers to strings, the system commands, auto-open,
yield, notification, the helpers, command tables, mciGetErrorString and
mciExecute), the tables written by `tools/mkmcitab.py` (w16's own core
and six type tables, RCDATA in the driver tables' documented format,
built from the documented commands and checked keyword by keyword on 3.1:
23.2), the MCI texts in `mmsystem.rc` (3.1's mciGetErrorString's),
`stubs.c` (the joystick functions without a driver, MCIPARSECOMMAND),
`mmtask.asm` (MMTASK.TSK). Installed as `SYSTEM\MMSYSTEM.DLL` and
`SYSTEM\MMTASK.TSK`.

### 23.4 Tests and results

`tests/mmsystem/mscen.sh` on installation-05 (boot-test case
`mmsystem`): core (145 lines) identical but four known differences (the
mmTask function's BX, CX and DI and its code selector: handle values and
leftovers of 3.1's MMTASK start-up), the prestack section identical, the
MMIO sections (270 lines) identical, TPR plain and with TMS.DRV
passing `tests/timer/tcmp.py` with w16's MMSYSTEM over w16's TIMER.DRV
against Microsoft's two (with Microsoft's TIMER.DRV under both
MMSYSTEMs every exact line is identical too). `qt`: QuickTime's sample
movie with w16's MMSYSTEM, MMTASK and TIMER.DRV over stock 3.1 plays as
on 3.1 (same window, last frame 0 pixels off, 12 s); with layer 1 alone
its player could not open the movie (it reads it through mmio). On w16's
install tree alone (everything w16's, KRNL286 as `tests/dropin.sh -T`
boots it) closing the player ended Windows ("Standard Mode: Fault in
MS-DOS Extender"): not MMSYSTEM's, MMTASK's, TIMER's or TOOLHELP's doing
(each of Microsoft's put back faulted the same), but KERNEL's: the
player's task ends with one of its blocks in FS, and KRNL286's task
clean-up handed it to the DPMI host, which reloaded FS (9.2, "A selector
freed while FS or GS holds it"; `docs/notes/quicktime-handover.md`). Since
then every `qtplay.sh` run (the parts qt of `timer`, `mmsystem`,
`toolhelp`, and `quicktime` with its parts over, tree and tree386) also
closes and reopens the movie, ends the player with File > Exit and with
Alt+F4, and ends Windows, on both sides.
`dev` (`tests/mmsystem/mdscen.sh`, layer 3): MMP's device sections with
both MMSYSTEMs over the same Microsoft drivers through MDS*.DRV, every
record of every message and callback compared: no devices (with the
mapper's shim), SNDBLST2/MIDIMAP (n, e, w, i, m, j a y, x, b,
and all of them without the shims) and several keys (multi): identical
after `mcmp.py`'s masks, which hide only what the heap, the stack and
the drivers' own leftovers decide (MMSYSTEM's internal handles and
blocks, the driver's pointer in a header's reserved field, MIDIMAP's
stack in what it hands back, garbage in fields 3.1 leaves unset, MIDI
time stamps) and the timings (the "~" lines, WM_TIMER ticks). `prog`
(`tests/mmsystem/mprog.sh`): Sound Recorder opening and playing
DING.WAV, Control Panel's Sound applet testing a sound and the MIDI
Mapper applet, Media Player (its Device menu, CHIMES.WAV opened and
played, CANYON.MID refused by MCIWAVE with its error box and opened with
the MIDI Sequencer): every screen 0 pixels off from 3.1's with
Microsoft's MMSYSTEM. `mci` (`tests/mmsystem/mciscen.sh`, layer 4): the
eleven MCI scripts with both MMSYSTEMs over MCT.DRV (e3: 2025 lines, every
documented device-type command for each of the eleven types) and over
3.1's MCIWAVE and MCISEQ behind MDSC (e5, e7): every line identical after
`mcmp.py`'s masks, which hide what heaps and stacks decide (MCIWAVE's
header fields and MMTIME it leaves unset, the block on 3.1's stack its
auto-close hands the driver, DX left by the helpers that answer a BOOL,
a driver's stack address and instance, the bytes past a short string in
3.1's open reparse); the drivers' DRV_EXITAPPLICATION when MCIWAVE's or
MCISEQ's background task ends is compared apart (23.5). With
`$W16_QTDIR/ref-installed/c` (a QuickTime-installed drive) the case qt
opens, queries and plays QuickTime's sample movie through MCIQTW.DRV:
the commands and MCIQTW's records identical (QuickTime's own streaming of
the sound through the wave driver follows the clock: left out). Full run
(2026-11-18): make test passes, the boot test 122 ok with only the
failures known before (ddeml conn, lzexpand quirks, soundtime).
MessageBeep (boot-test case `soundbeep`, `tests/soundprobe.sh beep`):
w16's MMSOUND.DRV against Microsoft's over Microsoft's MMSYSTEM and over
w16's, each with SNDBLST2.DRV and without a wave driver: the driver's
calls of MMSYSTEM (sndPlaySound's names and flags, timeSetEvent,
timeKillEvent) and the wave sounds played identical.

### 23.5 UNVERIFIED, and not done

- Layer 4 (MCI): w16's MMTASK task ends as soon as its function returns;
  3.1's later, at a step two Microsoft runs agree on (MCIWAVE's task
  after the next commands; one or two OldYield calls before the end did
  not reproduce it), so the drivers' DRV_EXITAPPLICATION comes at another
  step (mcmp.py compares those records apart). Not observed: what 3.1
  does with a task's devices when it ends (w16 closes them at
  MMSYSTEM's DRV_EXITAPPLICATION); MMSYSTEM's default yield procedure
  beyond its answer 0 (w16: the break key, pressed while the break
  window is active, answers -1; break off disables it); MCI_SOUND's
  flags; open by a driver's file name takes the options of the first
  [mci] line naming that file with options (one case seen); "element"
  refused with 263 (why); a type table registered with
  mciLoadCommandResource's type; the bytes 3.1 shows past a copied
  string of up to four characters in the open reparse (its heap);
  "open" through the custom table of a driver whose [mci] key is not a
  type's name (QuickTime's MCIQTW has none); mciGetDeviceIDFromElementID
  (nothing opened by element ID seen); MCIPARSECOMMAND.
- Layer 3: a joystick driver (none in the reference: only the answers
  without one; the joy* functions with a driver are still stubs); the
  sndPlaySound timer's period (about 500 ms) and whether 3.1 caches a
  sound by name; when 3.1 makes the interrupt stack (w16: when the
  drivers load); the MIDI header flag checks (assumed as the wave ones);
  WAVE_MAPPER with more than one device (which error when none takes the
  format); a task callback's check; an unwritable error-text buffer (3.1
  answered 0 to 0001:0000 and was unstable afterwards); SystemStart
  without a `[sounds]` entry; whether 3.1 plays SystemExit itself.
  Creative's SB16 drivers (SB16SND.DRV stops at "Incorrect MIDIPORT
  setting" in DOSBox-X), so only Windows 3.1's own drivers were observed;
  QuickTime's sound (its probe runs without a wave driver). After a
  FreeLibrary of an MMSYSTEM a program loaded with LoadLibrary (not a
  `[boot] drivers=` one), Microsoft's stays loaded and w16's goes
  (SN1 part L; which counts are right, UNVERIFIED).
- The interrupt stack's flags after a switch (AF differs between 3.1's
  runs), and which module owns the block (TIMER or MMSYSTEM in 3.1's
  runs); the mmTask function's BX and CX.
- MCIPARSECOMMAND's arguments; MMDRVINSTALL's meaning (w16 answers 0).
- MMIO: SEEK_END with a positive offset in a memory file; growing memory
  files; buffers over 64 KB; the DOS procedure's position after DOS
  refuses a seek (w16's goes to the end, which matches the one case
  seen); MMIO_GLOBALPROC across tasks; a procedure installed twice.
- 386 Enhanced mode and KRNL286 were not run for layers 1-2 (the timer
  probe ran in Standard mode).

## 24. SB16.DRV

SB16.DRV is w16's own driver for a Sound Blaster 16 (module SB16,
description `wave,midi,aux:w16 Sound Blaster 16`): an installable driver
(10.7) that MMSYSTEM opens from its `[drivers]` keys (23.2) and that serves
wave output and input, MIDI output and input through the card's MPU-401
port, and five auxiliary devices on the card's mixer. Windows 3.1 ships no
SB16 driver; its own SNDBLST2.DRV (Sound Blaster 1.5, wave and MIDI,
8-bit mono) is the reference for the driver's behaviour toward MMSYSTEM,
and the SDK for what the SB16 does beyond it. Creative's own SB16 Windows
drivers stop in DOSBox-X at "Incorrect MIDIPORT setting in the SYSTEM.INI
file" (23.2) and were not used. The card's interface is Creative's *Sound
Blaster Series Hardware Programming Guide*, taken as a statement of
hardware facts. `drivers/sb16/README.md` has the files and the sources;
`docs/notes/sb16-handover.md` the state of the work. It is not in the
default SYSTEM.INI (a machine without the card boots as before):
`assets/README.TXT` says how to enable it.

### 24.1 Observation tools

- MMP's device sections (23.1, `tests/mmsystem/mmdev.c`) over SB16.DRV:
  `tests/mmsystem/mrun.sh` sound `sb16w` (wave=, midi=, aux= sb16.drv,
  `[sb16.drv]` port=220 int=5 dma=1 hdma=5 mpuport=330, DOSBox-X with
  `sbtype=sb16`); MDSX1-3.DRV, a logging-driver variant with all six
  entries (`tests/mmsystem/mdsx.lnk`), sit between MMSYSTEM and it, and a
  scratch copy of it with its imports of MMSYSTEM renamed to MMSYSLOG has
  its DriverCallback calls recorded too.
- The same sections over SNDBLST2.DRV under Microsoft's MMSYSTEM
  (`mdscen.sh -m`, sound `sb2`) are the reference;
  `tests/sb16/sbcmp.py` compares the two logs with the driver-private
  fields masked and the expected differences listed (24.4).
- **SBP.EXE** (`tests/sb16/sbp.c`), run as the shell, plays a script:
  WAV files through wave output in several headers, a looped header, the
  wave volume, recording, MIDI notes, MCI strings, sndPlaySound; it logs
  what came back (done counts, positions, recorded sizes).
  `tests/sb16/sbaudio.sh` runs it in DOSBox-X with the sound recorded
  (DX-CAPTURE /A) and DOSBox-X's own synthesiser on the MPU-401 with a
  SoundFont of plain sine waves (`tools/mksf2.py`, w16's own), and
  `tests/sb16/sbaudio.py` checks the recording against the material it
  generates.
- `tests/mmsystem/mprog.sh` with `MPROG_SND=sb16w`: Sound Recorder,
  Control Panel's Sound and Media Player over SB16.DRV.
- `tests/sb16/sbscen.sh` (parts dev, audio, prog) is the boot-test case
  `sb16`.

### 24.2 What SNDBLST2.DRV does toward MMSYSTEM (observed)

MMP's sections n, w, i, m, j, a, x over SNDBLST2 with the logging drivers
(raw logs: `tests/sb16/sbscen.sh dev`'s `sb2/` directory):

- DriverProc through USER: message 0 answers 0, DRV_LOAD 1, DRV_ENABLE 0,
  DRV_OPEN 1 (lParam1 USER's empty string). DRVM_INIT answers 8
  (MMSYSERR_NOTSUPPORTED) on each entry; GETNUMDEVS 1; an unknown message
  8, also through waveOutMessage etc.
- Capabilities: the bytes asked for, no more (size 1 writes one byte).
  Microsoft's IDs (MID 1, wave out 6, wave in 7, MIDI out 3, MIDI in 4),
  version 1.01, every name "Creative Labs Sound Blaster 1.5"; wave
  output formats 1M08 and 2M08, input 1M08 only, one channel, no volume;
  MIDI out a MIDI port (technology 1), channel mask FFFFh.
- Wave open: a NULL or unsupported format 32 (WAVERR_BADFORMAT; a block
  alignment of 0 too), with WAVE_FORMAT_QUERY too; a second open 4
  (MMSYSERR_ALLOCATED); the instance written through the dwUser pointer
  is a near offset into its data (0000010Ch); WOM_OPEN (and WOM_CLOSE in
  the close) is called inside the open, on the caller's stack.
  DriverCallback's flags are the client's callback type with DCB_NOSWITCH
  (000Bh for a function, 0009h a window, 000Ah a task); CALLBACK_NULL: no
  call.
- PREPARE and UNPREPARE answer 8 (MMSYSTEM prepares). WRITE sets
  WHDR_INQUEUE (clears DONE), queues and returns; the driver uses lpNext,
  reserved and bit 31 of dwFlags while a header is queued. WOM_DONE comes
  at interrupt time on MMSYSTEM's interrupt stack, in queue order, with
  INQUEUE cleared and DONE set. RESET returns everything inside the call
  (the headers not yet begun first), the position back to 0. PAUSE,
  RESTART, BREAKLOOP 0 (also when idle or not paused). Loops: one header
  with BEGINLOOP|ENDLOOP comes back with both flags kept.
- GETPOS: size below 8 answers 1 (nothing written), NULL 0, TIME_BYTES
  kept, every other type (TIME_MS too) answered in samples.
- Close: with a header queued while paused 33 (WAVERR_STILLPLAYING) at
  once; with one playing it waits for it (23.2).
- Wave volume, pitch, rate: 8.
- Wave input: ADDBUFFER clears dwBytesRecorded and DONE, sets INQUEUE;
  START with no header 0; WIM_DATA per full header at interrupt time;
  STOP leaves the queue; RESET returns all (inside the call); the
  position counts the bytes the headers took; close with a header queued
  33.
- MIDI out: the open writes no instance; MOM_OPEN/MOM_CLOSE inside the
  calls; DATA 0; LONGDATA sends, sets DONE (never INQUEUE) and calls
  MOM_DONE before it returns; RESET 0; volume, patch caching 8. MIDI in:
  ADDBUFFER sets INQUEUE; STOP leaves the buffer; RESET returns it
  (MIM_LONGDATA, dwParam2 the milliseconds since START); a close with
  one queued 65 (MIDIERR_STILLPLAYING).

### 24.3 w16's SB16.DRV

`drivers/sb16/` (README there): `sb16a.asm` (entry, interrupt handler,
vector and PIC, huge copy), `sb16.c` (DriverProc, configuration, DSP,
mixer, DMA, the interrupt-time work), `wave.c`, `midi.c`, `aux.c`. Large
model with SS != DS, everything in one fixed code and one fixed data
segment (`tools/irqcheck.py --fixed`, in `make test`).

- **Configuration** at DRV_ENABLE: `[sb16.drv]` port (hex), int, dma,
  hdma, mpuport (hex); else the BLASTER setting (A I D H P); else the
  SB16 mixer's registers 80h (IRQ) and 81h (DMA); else 220h, 5, 1, 5,
  330h. DSP reset (base+6, AAh), version (E1h): 4.00 or later gives the
  wave and aux devices; the MPU-401 reset (FFh twice) and put in UART
  mode (3Fh) gives the MIDI devices. In 386 Enhanced mode nothing is
  touched and no device offered (24.5).
- **Wave**: one 8 KB DMA buffer of conventional memory (GlobalDOSAlloc;
  physical = linear in Standard mode; within one 64 KB page), auto-
  initialised, two halves of about 1/16 s; DSP 4.xx's B6h/C6h (output)
  and BEh/CEh (input) with the rate by 41h/42h and the mode byte
  (signed 16-bit, stereo). The interrupt handler acknowledges the DSP
  (mixer 82h, base+0Eh/0Fh), sends the end of interrupt, and with
  interrupts on and never reentered, on MMSYSTEM's interrupt stack
  (StackEnter), fills the half just played from the queue (the headers'
  data may be huge), returns the headers the card has played past, and
  stops the card (DSP reset) after a half of silence with nothing left.
  Loops go back to the loop's first header; BREAKLOOP ends the loop at
  the end of the pass. Pause/continue: D0h/D5h, D4h/D6h. Positions add
  the DMA controller's count within the half under way, in bytes,
  samples or milliseconds (TIME_MS kept: SNDBLST2 answers samples).
  Everything else as 24.2: the answers, the error codes, the callback
  order and where each is made, the instance (a near offset), a close
  with headers queued (33 when paused or stopped, else after they have
  played, at most their length and a second). One direction at a time
  (the other's open: 4).
- **MIDI**: short messages with running status kept for channel
  messages, a DWORD without a status byte continuing the last one;
  RESET sends a note off for every note on and All Notes Off (controller
  123) on every channel. Input bytes are taken by the interrupt handler
  (the MPU-401 shares the card's IRQ on a real SB16) and turned into
  MIM_DATA (real-time bytes alone, at once) and MIM_LONGDATA (system
  exclusive into the queued buffers), time-stamped with timeGetTime.
- **Aux**: master, wave, MIDI, CD, line in: mixer pairs 30h-39h, 5 bits
  of each 16-bit volume; the levels the mixer had at start are the first
  answers; waveOutSetVolume is the wave pair.

### 24.4 Tests and results

Boot-test case `sb16` (`tests/sb16/sbscen.sh`), on installation-05, run
name `w16-sb16`:

- `dev`: MMP's sections n, w, i, m j a, x, b and all of them without the
  logging drivers over SB16.DRV: Microsoft's MMSYSTEM and w16's give
  identical logs (`mcmp.py`; 178, 404, 214, 224, 277, 118 and 448
  lines). Against SNDBLST2 under Microsoft's MMSYSTEM (`sbcmp.py`):
  nothing differs but the expected differences, which are the SB16's
  extras: its capabilities, the formats it takes (16-bit, stereo, 22050
  and 44100 Hz; the opens and closes that follow), wave volume (0 and the
  value where SNDBLST2 answers 8; NULL 11), TIME_MS kept, the aux
  devices (and the boot's open of the aux key). Every other message,
  answer, error code, header flag and callback (its message, its
  parameters, whether it comes inside the call or at interrupt time) is
  the same, including the reset order and the closes with headers
  queued.
- `audio` (`tests/sb16/sbaudio.sh`, both MMSYSTEMs): eight WAV files of
  w16's (a rising sweep left, a falling one right) played in four
  headers each: 8-bit mono 11025 Hz, 8-bit stereo 22050, 16-bit mono
  22050, 16-bit stereo 44100, 16-bit mono 8000, 8-bit mono 44100, 16-bit
  stereo 32000, and a 16-bit mono 22050 Hz header looped three times;
  the recording (DOSBox-X's mixer at 44100 Hz) correlates with the files
  at 0.9993 to 0.9998 per channel (limits 0.995, 8-bit 0.99), left with
  left (left-right 0.01-0.03), each piece within 8 ms of the file's
  length (limit 15), the level within 0.1 dB of the first file's; the
  file played at wave volume C000h comes out 13.9 dB lower (the SB16
  mixer's 2 dB steps: 14). Not sample-accurate: at 44100 Hz (no
  resampling) the residual after the gain is 0.032 of full scale for the
  8-bit and the 16-bit file alike, which is DOSBox-X's mixing, not the
  data (the correlation says the samples are the file's). Windows'
  CHIMES.WAV through MCIWAVE (auto-opened by "play ... wait") and
  DING.WAV through sndPlaySound: 0.995 and 0.994 (8-bit, 22050 Hz; their
  quiet ends count as silence, so their length is checked only from
  below). The pieces are aligned by the attack of each file (normalised
  correlation of its first 100 ms with 20 ms of silence before; aligning
  by the loudest stretch put DING.WAV's steady tone one period off).
  QuickTime for Windows' sample movie through MCIQTW (`$W16_QTDIR`): its
  sound track comes out under both MMSYSTEMs (4.6 and 5.4 s of sound in
  17 pieces: a phrase, beats a second apart, a closing phrase). SBP's
  log: every header back, the final
  positions exact in bytes, samples and milliseconds, every recording
  header filled (DOSBox-X records silence). MIDI: SBP's seven notes and
  SCALE.MID's four through MCISEQ on port 0 come out at their pitch
  (within 0.1 Hz); MCISEQ's first note is 50-100 ms short on both
  MMSYSTEMs (its timing).
- `prog`: Sound Recorder playing DING.WAV, Control Panel's Sound applet
  testing a sound, Media Player playing CHIMES.WAV and opening CANYON.MID:
  w16's MMSYSTEM's screens identical to Microsoft's over SB16.DRV, and
  over SB16.DRV as over SNDBLST2 but Sound Recorder's Record button,
  enabled with the SB16 (DING.WAV is 22050 Hz, which SNDBLST2 cannot
  record). The configuration dialog (DRV_CONFIGURE sent through USER by
  SBP's "config"; `tests/scripts/input/sb16cfg.txt`): shown with the
  settings in use, Escape answers 0 (DRVCNF_CANCEL).
- Reliability (2026-11-19): the case `sb16` passed 10 of 10 runs on
  installation-05 (two loops of five side by side, with QuickTime's
  sides). Full run at 4091877 (make test and the whole boot test,
  `W16_QTDIR` set): make test passes; boot test 120 ok, 7 failures:
  ddeml (conn) and lzexpand (quirks), known on master; holdbtn,
  quicktime (tree) and taskman (tree), known to fail now and then (the
  first two passed when run again, taskman@tree fails on master 429e079
  too); sysdrvall (kbtype, 28 and 2 pixels) and trails (timed, cpl)
  under load, both passing when run beside master's checkout, where
  trails@timed failed instead. None of these loads SB16.DRV (it is in no
  default SYSTEM.INI).
- Found on the way: with a shell other than Program Manager, Microsoft's
  MMSYSTEM plays SystemExit (CHIMES.WAV) when Windows ends; w16's does
  not (23.5 had it as UNVERIFIED; not changed here).

### 24.5 UNVERIFIED, and not done

- **386 Enhanced mode**: the driver offers no devices there (GetWinFlags'
  WF_ENHANCED: the card is not touched). It would
  need a VxD that gives the system VM the card's ports (or virtualises
  them per VM), its IRQ (VPICD: a virtualised IRQ, so that the handler
  runs in the system VM) and its DMA channels (VDMAD: the DMA buffer's
  physical address, or the transfers translated), and the DMA buffer and
  the handler's code and data page-locked.
- **FM**: no OPL3 synthesiser device (it needs a MIDI-to-OPL voice
  allocator and a patch set of w16's own: not done); the MIDI aux device
  only sets the FM's volume.
- MIDI input with real data (DOSBox has no MIDI input source): the
  buffer handling is verified, the parsing (running status, system
  exclusive, real time) is not. DOSBox raises the MPU-401's interrupt on
  IRQ 9, which the driver does not hook; a real SB16 shares the card's.
- Wave input of a real signal (DOSBox-X records silence); full duplex
  (one direction at a time); the order of more than two headers in a
  reset (SNDBLST2's with two: the one not begun first); SNDBLST2's use of
  lpNext, reserved and bit 31 (driver-private; SB16.DRV's differ).
- DRV_CONFIGURE: a dialog of w16's own (port, IRQ, DMA, high DMA, MPU
  port, written to `[sb16.drv]`, DRVCNF_RESTART on OK); what SNDBLST2
  answers to DRV_QUERYCONFIGURE and DRV_INSTALL was not observed (it has
  a dialog of its own: it imports DialogBox).
- The MIDI Mapper over SB16.DRV: not tried (with SNDBLST2 its default
  setup answers 68, 23.2); Media Player's MIDI Sequencer plays through
  the mapper, so CANYON.MID is only opened in `prog`; MCISEQ on port 0
  ("set port 0") plays through the MPU-401 (`audio`).
