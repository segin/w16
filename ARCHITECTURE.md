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
| Trademarks ("Microsoft", "Windows", "MS-DOS") | — | Not licensed by MIT. Use only to describe compatibility, never as the product's name. |

Notes:
- The MIT licence on the MS-DOS repository has no explicit patent grant. Any relevant patents from the 1980s and early 1990s have expired.
- MS-DOS 4.0 was co-developed with IBM, and IBM copyright notices appear in its sources. Microsoft published it under MIT, and the project relies on that in good faith.

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
| `SYSTEM.DRV` | SYSTEM | `system.drv` | Timer, system and hardware info | P0 |
| `KEYBOARD.DRV` | KEYBOARD | `keyboard.drv` | Keyboard input, `ToAscii`, OEM/ANSI conversion | P0 |
| `KBD*.DLL` (e.g. `KBDUS.DLL`) | KBD* | `[keyboard] keyboard.dll` | Keyboard layout tables | P1 |
| `MOUSE.DRV` | MOUSE | `mouse.drv` | Mouse input | P0 |
| `VGA.DRV` (and others: `SUPERVGA.DRV`, `EGA.DRV`, `8514.DRV`, …) | DISPLAY | `display.drv` | GDI DDI display driver | P0 |
| `VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON` | — | `fonts.fon`, `fixedfon.fon`, `oemfonts.fon` | System, fixed and OEM fonts | P0 |
| `SOUND.DRV` | SOUND | `sound.drv` | Legacy `OpenSound` API (speaker) | P1 |
| `COMM.DRV` | COMM | `comm.drv` | Serial and parallel ports | P1 |
| `MMSOUND.DRV` | MMSOUND | `[drivers]` | Multimedia sound support driver | P1 |
| `TIMER.DRV` | TIMER | `[drivers] timer` | Multimedia high-resolution timer | P1 |
| `MIDIMAP.DRV` | MIDIMAP | `[drivers] midimapper` | MIDI mapper | P2 |
| `POWER.DRV` | POWER | `[drivers]` | APM | P3 |
| `LANGxxx.DLL` | LANGxxx | `language.dll` | Language and collation (default is built-in) | P2 |
| `*.DRV` (network, e.g. `LANMAN21.DRV`) | (varies) | `network.drv` | WNet* API provider | P2 |

### 6.4 System DLLs (redistributable APIs that apps import)

| File | Module | Role | Pri |
|---|---|---|---|
| `COMMDLG.DLL` | COMMDLG | Common dialogs (Open/Save, Color, Font, Print, Find/Replace) | P0 |
| `SHELL.DLL` | SHELL | Drag-drop, `ShellExecute`, registration database (`Reg*`), `ExtractIcon` | P0 |
| `WIN87EM.DLL` | WIN87EM | x87 emulator / FPU support. Most C runtimes import it | P0 |
| `TOOLHELP.DLL` | TOOLHELP | Task, module, heap and global walkers; notifications | P1 |
| `VER.DLL` | VER | Version resources, `VerInstallFile` | P1 |
| `LZEXPAND.DLL` | LZEXPAND | LZ decompression (`COMPRESS.EXE` format) | P1 |
| `DDEML.DLL` | DDEML | DDE Management Library | P1 |
| `OLECLI.DLL` | OLECLI | OLE 1.0 client | P1 |
| `OLESVR.DLL` | OLESVR | OLE 1.0 server | P1 |
| `MMSYSTEM.DLL` | MMSYSTEM | Multimedia: wave, MIDI, aux, joystick, timer, MCI, mmio | P1 |
| `MCI*.DRV` (`MCIWAVE.DRV`, `MCISEQ.DRV`, `MCICDA.DRV`) | MCI* | MCI device drivers | P2 |
| `MSADLIB.DRV`, `SNDBLST.DRV`, etc. | (varies) | Hardware audio drivers (`DriverProc`) | P2 |
| `WINMEM32.DLL` | WINMEM32 | 32-bit memory access for 16-bit apps | P2 |
| `PENWIN.DLL` | PENWIN | Pen extensions (Windows for Pen Computing) | P3 |
| `DISPDIB.DLL` | DISPDIB | Full-screen DIB display (multimedia/games) | P2 |
| `WINHELP.EXE` | WINHELP | Started by `USER.WinHelp`, so it is part of the app ABI. `.HLP` format | P1 |
| `PRINTMAN.EXE` | PRINTMAN | Spooler, driven by GDI print jobs | P2 |
| `UNIDRV.DLL` (+ minidrivers like `HPPCL.DRV`) | UNIDRV | Universal printer driver | P2 |
| `PSCRIPT.DRV` | PSCRIPT | PostScript printer driver | P3 |

Commonly found on 3.1 systems but **not** shipped with base 3.1 (apps install them): OLE 2.0 (`COMPOBJ`, `STORAGE`, `OLE2`, `OLE2DISP`, `OLE2NLS`, `TYPELIB`), `WINSOCK.DLL`, Video for Windows (`MSVIDEO`, `AVIFILE`, `MCIAVI.DRV`), Win32s. These are **out of scope** for the base target, but the base must be able to host the originals.

### 6.5 Shell and Applets (not needed for ABI, needed to be usable)

| File | Pri | Notes |
|---|---|---|
| `PROGMAN.EXE` | P1 | Default `shell=`; DDE interface used by installers (ABI-relevant) |
| `CONTROL.EXE` + `MAIN.CPL`, `DRIVERS.CPL`, `SND.CPL` | P2 | The `.CPL` (`CPlApplet`) interface is ABI-relevant |
| `TASKMAN.EXE` | P2 | Ctrl+Esc task list |
| `WINFILE.EXE` | P2 | File Manager. Its extension DLL interface is ABI-relevant |
| `CLIPBRD.EXE`, `SYSEDIT.EXE`, `REGEDIT.EXE` | P3 | |
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

## 7. Proposed Milestones

1. **M1:** NE loader plus KERNEL running on an existing DPMI host. A console-less test app calls KERNEL exports.
2. **M2:** DOSX equivalent, WIN.COM, SYSTEM/KEYBOARD/MOUSE/DISPLAY (VGA), and USER/GDI. A "Hello World" Win16 app built with OpenWatcom runs (Standard mode).
3. **M3:** COMMDLG, SHELL, WIN87EM, TOOLHELP, VER, LZEXPAND, DDEML, OLECLI/OLESVR, MMSYSTEM, WINHELP, PROGMAN. Common 3.1 software runs.
4. **M4:** WIN386 (VMM and the core VxDs), WINOA386, DOS boxes.
5. **M5:** Printing, multimedia drivers, the remaining P2/P3 items.

## 8. Open Items

- **Ordinal tables:** export names and ordinals for each module must be reconstructed from public sources (SDK import libraries and `.DEF` documentation, *Undocumented Windows*, and observing which ordinals real apps import). Each module needs its own `.DEF` with provenance noted.
- **MT-DOS relevance:** 3.1 has no real mode, so the MT-DOS memory manager informs the *semantics* of the handle-based Global/Local APIs (movable, discardable, lock counts, compaction) rather than the selector-based implementation.
