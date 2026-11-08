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

w16 is licensed under the MIT licence (`LICENSE`). Everything in the repository must be compatible with that, which is why copyleft code below is reference only.

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
| Third-party copyleft code (GPL, LGPL), e.g. VBADOS/VBMOUSE.DRV (GPL-2.0), Wine's 16-bit DLLs (LGPL) | **Reference only** | Read for facts and approach; never copy or adapt code, which would stop w16 being MIT. |
| Test-only tools (DOSBox, HDPMI16, the user-supplied reference Windows copies) | **Not shipped** | Used to build and test only; kept out of the repository (`.tools/`). |
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
| `VGA.DRV` | DISPLAY | `display.drv` | GDI DDI display driver, 640×480×16 (other display drivers deferred, see §6.10) | P0 |
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
| `WIN87EM.DLL` | WIN87EM | x87 emulator / FPU support. Most C runtimes import it. w16: `win87em/` (§9.2, floating-point findings) | P0 |
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

### 6.9 Fonts (`.FON`)

w16 needs its own raster and vector font set, drawn from scratch. Face names, file names and **metrics must match exactly**, because USER layout depends on them. For example, dialog base units come from the System font's average character width and height, so a one-pixel difference moves every dialog control. Glyph shapes are original, following the same approach as Wine's own `.FON` files.

**File format:** `.FON` is a resource-only NE DLL containing `RT_FONTDIR` and `RT_FONT` resources. Each `RT_FONT` is a Windows `.FNT` file, version 2.0 (raster) or 3.0. A host tool, `tools/fontc`, compiles text glyph sources into `.FNT` and packages them into `.FON`. Metrics are recorded per size from black-box measurement (`GetTextMetrics`, `GetCharWidth`) on real Windows 3.1.

| Set | Files | Faces | Pri |
|---|---|---|---|
| VGA (96 dpi) system | `VGASYS.FON`, `VGAFIX.FON`, `VGAOEM.FON` | System, Fixedsys, Terminal | P0 |
| VGA (96 dpi) "E" set | `SSERIFE.FON`, `SERIFE.FON`, `COURE.FON`, `SYMBOLE.FON`, `SMALLE.FON` | MS Sans Serif, MS Serif, Courier, Symbol, Small Fonts | P1 |
| Vector | `MODERN.FON`, `ROMAN.FON`, `SCRIPT.FON` | Modern, Roman, Script | P2 |
| DOS box | `DOSAPP.FON`, `EGA80WOA.FON`, `EGA40WOA.FON`, `CGA80WOA.FON`, `CGA40WOA.FON` | Terminal (windowed DOS apps) | P2 |
| 8514 (120 dpi) | `8514SYS.FON`, `8514FIX.FON`, `8514OEM.FON`, `SSERIFF.FON`, `SERIFF.FON`, `COURF.FON`, `SYMBOLF.FON`, `SMALLF.FON` | as above | Deferred |
| EGA/CGA | `EGASYS.FON`, `EGAFIX.FON`, `EGAOEM.FON`, `CGASYS.FON`, `CGAFIX.FON`, `CGAOEM.FON`, the `*B.FON`/`*A.FON` sets | as above | Deferred |

The TrueType core fonts (Arial, Times New Roman, Courier New, Symbol, Wingdings) are not `.FON` files. They are covered later by metric-compatible, freely licensed TrueType fonts (e.g. Liberation) installed under the same face names.

### 6.10 Scope: VGA First

For now, w16 targets **VGA (640×480, 16 colours, 96 dpi) only**: `VGA.DRV`, the VGA grabber (`VGA.3GR`/`VGA.2GR`) and the VGA font sets. SuperVGA, 8514/A (120 dpi), EGA and CGA display drivers and their font sets are deferred. Nothing in the design should assume VGA in a way that would prevent adding them later; for example, metrics come from the display driver and are never hard-coded.

### 6.11 Design Note: Undocumented Layouts Are Part of the Contract

Programs from the period depend on some internal data layouts that the SDK never documented. When w16's own programs or observed applications rely on one, w16's modules must reproduce it exactly. Known so far:

| Layout | Who relies on it | Evidence |
|---|---|---|
| **HICON memory block** (USER): `GlobalLock(hIcon)` yields a 12-byte header (hotspot x, y, width, height, bytes per row, planes, bits per pixel), then the AND mask, then the XOR bits in the display driver's format. | Program Manager (ours and the original) copies these bytes straight into `.GRP` files (`shell/progman/group.c`, `icon_bits`). Other applications are believed to read it too. | Described in published books on Windows internals. Confirmed black-box: a `.GRP` file written by w16's Program Manager from this block on Windows 3.1 is byte-identical to the one the original Program Manager writes. |
| **`.GRP` file format** | Program Manager; installers and tools that read group files | Observed on Windows 3.1 (`shell/progman/group.c` header comment, `tools/grpdump.py`). |

So w16's USER must store icons (and probably cursors, which share the format) in exactly this layout. Add rows here as more such layouts turn up.

## 7. Proposed Milestones

1. **M1:** NE loader plus KERNEL running on an existing DPMI host. A console-less test app calls KERNEL exports.
2. **M2:** DOSX equivalent, WIN.COM, SYSTEM/KEYBOARD/MOUSE/DISPLAY (VGA), USER/GDI, `tools/fontc`, and the P0 VGA system fonts (§6.9). A "Hello World" Win16 app built with OpenWatcom runs (Standard mode).
3. **M3:** COMMDLG, SHELL, WIN87EM, TOOLHELP, VER, LZEXPAND, DDEML, OLECLI/OLESVR, MMSYSTEM, WINHELP, PROGMAN. Common 3.1 software runs.
4. **M4:** WIN386 (VMM and the core VxDs), WINOA386, DOS boxes.
5. **M5:** Printing, multimedia drivers, the remaining P2/P3 items.

## 8. Open Items

- **Ordinal tables (largely resolved):** `docs/exports/*.txt` lists, for every Windows 3.1 system module, which ordinals exist, observed black-box with `tests/apps/texp.c` (`GetProcAddress` by ordinal; Standard and 386 Enhanced mode agree), plus the same for Windows/386 2.11. Names come from OpenWatcom's import libraries and Wine's X11-licensed spec files (wine-20020228); `tools/exports.py` regenerates them. Still open: names for some OLE, SHELL and COMMDLG ordinals, and each export's exact semantics. Observed along the way: `GetProcAddress` ignores bit 15 of an ordinal (32769 finds ordinal 1).
- **Windows 2.x reference:** Windows/386 2.11 runs headless (`tests/w386run.sh`): MS-DOS 4.00 built from Microsoft's MIT source (`tests/msdos-build.sh`) boots from an image (`tools/mkhdd.py`) in DOSBox-X. Observed: it has no system MDI client class, no private-profile calls, no `WinExec`, no SHELL.DLL (see `build/ref/tw2-w386.txt` and `docs/exports`). The 2.x builds of w16 programs must work around each.
- **MT-DOS relevance:** 3.1 has no real mode, so the MT-DOS memory manager informs the *semantics* of the handle-based Global/Local APIs (movable, discardable, lock counts, compaction) rather than the selector-based implementation.

## 9. Implementation Notes

### 9.1 KRNL286.EXE layout and boot (M1)

`KRNL286.EXE` has the same shape as the original: an MZ stub followed by the NE module `KERNEL`.

1. **Bootstrap** (the MZ stub, `kernel/boot/`, real mode, small model). It finds its own path from the DOS environment and reads the resident NE tables, every KERNEL segment and their relocation records into conventional memory. It then enters 16-bit protected mode through DPMI (INT 2Fh AX=1687h), creates a descriptor per segment, applies KERNEL's relocations, and far-calls the NE entry point with ES:BX pointing to a `bootinfo` block (`kernel/bootinfo.h`, internal to w16). KERNEL may not import anything.
2. **KERNEL** (`kernel/`, large model, SS ≠ DS, no C runtime). Exported APIs are `far pascal __loadds`. Its startup:
   - registers itself as module 0;
   - hooks protected-mode INT 21h, so AH=4Ch from a task ends that task;
   - loads the application named on the command line through the NE loader;
   - starts it with the Windows task-entry register set.
3. **DOS services** come through INT 21h from protected mode, translated by the DOS extender, exactly as the real KERNEL relies on DOSX/WIN386. The M1 test host is HDPMI16; w16's own DOSX replaces it in M2.
4. **Constant exports** (`__AHINCR`, `__0040H`, `__WINFLAGS`, …). `wlink` cannot export absolute symbols, so these labels live in their own `KCONST` segment and KERNEL resolves those ordinals to runtime values.
5. **Unimplemented imports** bind to 8-byte thunks. A thunk reports `w16: call to unimplemented MODULE.ordinal` and ends the task with exit code 0FFh. Loading never fails just because an API is missing. Imports from Windows modules w16 does not provide yet (USER, GDI, KEYBOARD, SOUND, SYSTEM, DISPLAY, MOUSE, COMM: `pending_modules` in `kernel/loader.c`) bind to these thunks too; any other missing module fails the load with 2, as on Windows.
6. **Modules** (`kernel/module.c`, `kernel/loader.c`): one record per loaded module in its own global block, in a list that starts with KERNEL. Loading a module loads the modules it imports from first (recursively, each found once and shared, with usage counts), relocates it, patches the prologues of its exported procedures and runs a DLL's initialisation routine; freeing the last use of a DLL calls its `WEP` and releases what it imports. `MakeProcInstance` thunks live in growable thunk segments (`kernel/procinst.c`).
7. **Directories:** the system directory is the one `KRNL286.EXE` is in, the Windows directory its parent if that directory is called `SYSTEM` (else the same). This is w16's own convention until WIN.COM exists.

M1 limitations, in order of when they should be fixed:
- **Local heap:** implemented (`kernel/local.c`) with Windows 3.1's observed layout, but `LocalNotify`, `LocalShrink`, `LocalHandleDelta`, `LocalCountFree`/`LocalHeapSize` are not, and the exact order of compaction, discarding and growing on a failed request is UNVERIFIED.
- **Global heap:** each block is one DPMI allocation; there is no Windows arena. A single-tile block grows by copying into a new DPMI block and switching the selector's base (`seg_move` in `kentry.asm`), because a task's DGROUP grows with its local heap while it is the task's stack.
- **Loading:** DLLs load with their dependencies (§9.2 lists what was observed). Floating-point OS fixups are applied as observed (§9.2). Not yet: self-loading modules, demand loading and discarding, network directories in the search. The global heap's arena table (`MAX_ARENA` in `kernel/global.c`) and the unimplemented-import thunks (`MAX_THUNKS` in `kernel/thunk.c`) are still fixed-size; both should grow.
- **Module and task layout:** w16's own structures are used where Windows has the MDB and TDB (the hModule is the selector of the NE tables as read from the file).
- **Tasks:** a single task; it shares KERNEL's PSP. `LoadModule`/`LoadLibrary` of a program would need a second task and fail with 16 (UNVERIFIED code); starting tasks is future work. DLL initialisation of the first program's imports runs on KERNEL's stack (Windows 3.1 used a stack other than the task's too); a DLL loaded later initialises on the caller's stack, as observed.

### 9.2 Testing and verification

- `make test` runs each test program under `KRNL286.EXE` in headless DOSBox with HDPMI16 (`tests/dosrun.sh`, `tests/run-tests.sh`); the floating-point probe TK6 runs in DOSBox-X, with a coprocessor and without one.
- Test programs print `PASS`/`FAIL` lines for behaviour we are certain of, and `OBS name=value` lines for raw observations to compare between w16 and Windows.
- Code that relies on a recalled-but-unchecked detail is marked `UNVERIFIED:`. `grep -rn UNVERIFIED kernel` lists them.
- A probe can turn its observations into checks: `tests/apps/tk3.c` looks up every `OBS` name in `tk3ref.h`, generated by `tools/obsref.py` from the `TK.LOG` of reference runs in both modes (names whose values differ between the runs are left out; a name only some runs report is compared among those). Values that depend on the program's layout, such as addresses, are printed as `OBS ~name` and checked relative to each other instead.
- Test programs write their results to `TK.LOG` in the current directory with plain INT 21h. On real Windows a task's stdout and `OutputDebugString` are not visible, so a log file is the one channel that works on both systems.
- **Reference systems:** the user supplied disk images of Windows 3.1 (3.10.103) and Windows for Workgroups 3.11a. Both are installed with an unattended Setup (`SETUP /H:` with a settings file) into `.tools/ref/win31/cdrive` and `.tools/ref/wfw311a/cdrive`. `.tools/` is gitignored, so no Microsoft files enter the repository.
- `tests/refrun.sh PROGRAM.EXE [/S|/3] [ref-dir]` runs a test program on the reference system in headless DOSBox. It makes the program the shell (`shell=` in a scratch copy of SYSTEM.INI), so Windows exits when the program does, then prints `TK.LOG`. This is black-box observation (§3): only the program's own observations are used, and no Windows binary is inspected.
- Windows 3.1 is the authority. WfW 3.11 runs only in 386 Enhanced mode, so where it differs from 3.1 the difference is investigated rather than copied.
- Behaviour confirmed on Windows 3.1 becomes a `PASS`/`FAIL` check. Values that depend on the environment, such as selector numbers and the command tail WIN.COM builds, remain `OBS`.
- First findings, the same in Standard and 386 Enhanced mode unless noted. Several replaced recalled guesses that were wrong:
  - **Initial stack:** the automatic data segment is laid out as static data, then the stack, then the local heap. Initial SP = data size + stack size.
  - **InitTask:** CX and instance word 0Ah equal SP − stack size + 96h. Words 0Ch and 0Eh equal the entry SP. The command tail is NUL-terminated (the CR is replaced), and BX points past its leading blanks.
  - **hInstance:** the handle of the automatic data segment, which is DS with bit 0 clear.
  - **Global handles:** every handle, fixed or moveable, is its selector with bit 0 clear.
  - **GlobalSize:** rounds up to 32 bytes.
  - **Lock counts:** kept only for discardable blocks. GlobalFlags of a locked fixed or moveable block is 0, and of a locked discardable block 0101h.
  - **GlobalAlloc(GMEM_MOVEABLE, 0):** returns a block that is already discarded (GlobalFlags 4000h, size 0, GlobalLock returns NULL). `GlobalAlloc(GMEM_FIXED, 0)` succeeds too.
  - **GlobalFree of a non-handle:** returned 0.
  - **GetWinFlags:** 0419h in Standard mode and 0C29h in 386 Enhanced mode, on a 486 with an FPU. That includes WF_80x87, and in Enhanced mode also WF_PAGING.
- Local heap findings (`tests/apps/tk3.c`; Standard and 386 Enhanced mode identical; `kernel/local.c` describes the layout in full):
  - **Task heap:** InitTask makes the heap in the last CX (heap size) bytes of DGROUP, i.e. as `LocalInit(DS, 0, CX)`. Instance word 06h is pHeap, the heap information block; word 08h (the atom table in published descriptions) stays 0.
  - **LocalInit(seg, start, end):** start is rounded up to 4; with start 0 the heap is the last `end` bytes of the segment. A 0Ah-byte busy sentinel arena (padded to 0Ch) comes first, then the information block as the data of a 30h-byte busy fixed arena (pHeap = start + 10h), one free arena, and a last sentinel at (end − 0Ah) & ~3. Returns 1. LocalInit(sel, 16, 40) crashed the program, so tiny heaps are not probed.
  - **Information block** (offsets from pHeap): 04h arena count, 06h first sentinel, 0Ah last sentinel, 0Eh/0Fh two bytes LocalCompact sets (00h/10h, or FFh/00h when it had to try discarding), 14h newest handle table, 16h first free handle, 18h handles per table (20h), 1Eh far pointer into KERNEL (the LocalNotify procedure, in published descriptions), 24h 0200h (extra bytes when growing), 26h last − first + 0Ah, 28h 484Ch ("LH"). Word 1Ah reads 8A41h in both modes; its meaning is unknown and w16 stores 0.
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
  - **GetModuleHandle:** names match regardless of case; "TK4.EXE" and "C:\TK4.EXE" find module TK4, "TK4 " does not; `MAKELP(0, h)` with the hModule, hInstance or hInstance | 1 returns the module; NULL and unknown names give 0.
  - **GetModuleFileName:** the full upper-case path ("C:\TK4.EXE" for a program started from C:\), returning the number of characters copied; at most n − 1 characters plus NUL; n = 0 writes nothing; an unknown handle returns 0 with an empty string.
  - **GetProcAddress:** names match regardless of case, "#3" means ordinal 3, bit 15 of an ordinal is ignored; ordinal 0, gaps, unknown ordinals and names, and the module-name entry give 0; a constant export such as `__AHINCR` comes back as FFFF:value.
  - **lstrcpy/lstrcat** return the destination.
- DLL loading findings (`tests/apps/tk5.c` with the test DLLs `tkdll.c`, `tkdlb.c`, `tkid.c`, `tknod.asm`; run on Windows 3.1 by `tests/tk5ref.sh`; both modes identical):
  - **Search order** for a name without a path (a copy in each directory, removed one by one): current directory, Windows directory, system directory, the directory of the running task's program, the PATH directories. No extension is added (`LoadLibrary("TKNOD")` did not find TKNOD.DLL). While Windows loads its shell there is no running task: DLLs the shell imports were not found in the shell's own directory (Windows then waits forever), so `tests/tk5ref.sh` puts the DLLs in the Windows directory.
  - **Loaded modules** are matched by the base name of the argument before any file is opened, whatever the path (`C:\TKNODIR\TKDLB.DLL`, a directory that does not exist, returned the loaded TKDLB), and by `MAKELP(0, hModule)`; the usage count goes up and the initialisation does not run again.
  - **Results:** the hInstance (DGROUP handle = DS & ~1) of a DLL with data, the hModule of a DLL without (odd), an odd selector other than the hModule for KERNEL. Errors: 2 file not found (also for `""`, and when a module the DLL imports is missing), 3 path not found, 20 (14h) not an NE file (a text file, an MZ file without NE header), 0 when the initialisation returns 0. After a failure nothing of the DLL stays loaded.
  - **Usage counts:** 1 for a DLL loaded once, implicitly or not; each LoadLibrary/LoadModule adds 1, each FreeLibrary/FreeModule takes 1; at 0 the DLL's `WEP` runs with 0 and the module is gone. `GetModuleUsage` takes the hModule or hInstance. `LoadModule` of a DLL behaves like LoadLibrary; FreeModule's AX afterwards is not meaningful (F300h).
  - **Implicit dependencies** load before the program; a dependency's initialisation runs before (and nested deeper on the stack than) that of the DLL importing it.
  - **Initialisation routine (the NE entry point of a DLL):** AX = 1, CX = heap size, DX = DS = the DGROUP selector, DI = hInstance, SI = 0, ES = 0 (no command line), DS:0000–000F all zero and no local heap: the DLL makes it itself (LocalInit). For a DLL loaded by LoadLibrary it runs on the caller's stack; for the shell's implicit DLLs on another stack. BX and BP held leftover values.
  - **Exports:** imports and GetProcAddress find names in the non-resident table too. GetProcAddress on a program module (by hModule or hInstance, by name or ordinal) returns 0.
  - **Prologues of exported procedures:** in a DLL `8C D8 90` (mov ax,ds; nop) and `1E 58 90` (push ds; pop ax; nop) become `B8` + the DGROUP selector; in a program, three NOPs.
  - **MakeProcInstance (protected mode):** called with a program's DS it returns a new 8-byte thunk every time, `B8 <caller's DS> EA <procedure>`, in a thunk segment of its own; the hInstance argument is ignored (hInstance, hInstance | 1, 0 and a DLL's hInstance gave the same thunk). Thunks are 8 bytes apart; FreeProcInstance makes a thunk the next one handed out. Called with a DLL's DS it returns the procedure itself.
  - **GetInstanceData** copies from the instance's DGROUP to the same offset in the caller's and returns the count, also for a DLL's instance.
  - `LoadLibrary("")` with its own buffer left the buffer alone. UNVERIFIED: in an earlier probe that passed a `""` literal, a different string literal of the program read as "L" afterwards; the cause was not pinned down.
  - `tests/run-tests.sh` runs TK5 in the same layout (KRNL286 in `C:\WINDOWS\SYSTEM`, the DLLs in `C:\WINDOWS`, `C:\TKPATH` on the PATH via `DOSRUN_PRE`; `tests/refrun.sh` has `REFRUN_PRE` for the same). A NULL far pointer does not fault in them: it acts as a small scratch string (lstrlen(NULL) = 2, `lstrcpy(NULL, "x")` returns NULL but later reads of NULL see "x"). w16 treats NULL as an empty string instead (`kernel/string.c`).

- Floating-point findings (`tests/apps/tk6.c` with `tk6fp.asm`, and `tk6n.c`; run by `tests/tk6ref.sh` in DOSBox-X with a coprocessor and without one (`fpu=false`), both modes; Standard and 386 Enhanced mode identical; implemented in `kernel/loader.c` (`os_fixup`), `kernel/kentry.asm` (`int3d_handler`) and `win87em/`):
  - **Reference environment:** DOSBox's FPU keeps no exception flags in the status word and cannot be switched off, so these probes run in DOSBox-X (`REFRUN_X`, `DOSRUN_X`). Neither emulator raises floating-point exceptions (IRQ 13; checked with a DOS program), so how Windows 3.1 delivers an unmasked exception *with a coprocessor* is not observable here. With a coprocessor DOSBox-X reads control word bit 6 as 1; the probe ignores that bit.
  - **OS fixups:** OpenWatcom (`-fpi`) references `FIxRQQ` symbols at each floating-point instruction; wlink turns them into additive NE relocations of type 3 (OSFIXUP) with t1 = 1 (`FIARQQ`/`FJARQQ`, DS prefix), 2 (SS), 3 (CS), 4 (`FIERQQ`, ES), 5 (`FIDRQQ`, FWAIT + escape), 6 (`FIWRQQ`, NOP FWAIT), one record per instruction at its FWAIT. The symbols' values come from OpenWatcom's `emu87.lib`; `tests/apps/fixrqq.asm` defines them for programs without a run-time library.
  - **With a coprocessor** the loader turns the FWAIT of types 1-5 into a NOP (`9B D9 C0` -> `90 D9 C0`) and type 6 into INT 3Dh (`90 9B` -> `CD 3D`). INT 3Dh works in a program that does not import WIN87EM (WIN87EM is not loaded for it), so KERNEL handles it (an FWAIT).
  - **Without one** the loader adds the FIxRQQ value at the FWAIT and, for types 1-3, the FJxRQQ value one byte further: `CD 34`-`CD 3B` (escape D8h-DFh) followed by the ModR/M byte, `CD 3C` + the escape byte with the segment in its top two bits (DS 00, SS 01, CS 10, ES 11), `CD 3D` for a lone FWAIT. WIN87EM's emulator handles those; a program that does not import WIN87EM computed a wrong result and Windows hung at its exit. No code is patched back after it runs, in either case.
  - **Before any `__fpMath` call** a task's control word is 037Fh with a coprocessor and 1332h with the emulator.
  - **`__fpMath`** (register interface; BX = function; SI, DI, DS, SP kept; BX, CX, DX otherwise changed): 0 initialises (FINIT, control word 1332h, the coprocessor gets 1330h; AX = 0, CF = 0; the handler is not changed); 1 and 2 do the same with CF = 1 (CF = 0 with the emulator); 3 sets the exception handler DX:AX (AX = 253Eh afterwards); 4 sets the control word AX (the coprocessor gets AX & FF3Ch, i.e. invalid and denormal unmasked; the emulator all of AX) and returns AX & FF3Ch; 5 returns the control word last given to 4 (with the emulator: its control word); 6 rounds ST to an integer with the rounding control AX & 0C00h (AX = that control word; 1 with the emulator); 7 pops ST into DX:AX as a 32-bit integer rounded with AX & 0C00h (out of range 8000:0000h; the 3.1 emulator also changes SI and DI, w16's does not); 8 returns the exception flags (status & 3Fh with a coprocessor; with the emulator every flag raised since the last 9 or 0, including bits 6 and 7); 9 clears the exceptions (AX = 0, CF = 1 with a coprocessor); 10 returns the number of registers in use; 11 returns 1 with a coprocessor, 0 without, DX kept; 12 keeps AX (meaning unknown). Higher numbers were not tried.
  - **`__Win87EmInfo(buf, n)`:** n < 12 returns FFFFh and writes nothing; otherwise 12 bytes: 0600h, the save area size 01CDh, WIN87EM's data and code selectors, 1 with a coprocessor (0 without), 0; returns 0. **`__Win87EmSave` / `__Win87EmRestore(buf, n)`:** n < 01CDh returns FFFFh and does nothing; otherwise 0. With a coprocessor the area starts with an FSAVE image (which reinitialises the coprocessor); with the emulator its first 94 bytes are not written; WIN87EM's own state follows (w16 uses its own layout there).
  - **Emulator status word:** TOP always reads 0; a stack underflow sets IE only; ES is never set except for the square root of a negative number (81h). FINIT is not emulated: it sets bit 6 (0040h), leaves the control word, and calls the handler with 87h whatever the masks.
  - **Exceptions (emulator):** an unmasked exception calls the handler set by function 3 after the instruction, with AL = the code (83h zero divide, 87h unemulated, 88h square root of a negative number, which is reported even when masked), AH varying between runs, WIN87EM's DS, and on the stack the far return address and then the program's DS (OpenWatcom's handler reads its DS there). The handler returns with RETF and the program continues.
  - **Not observed / open:** exception delivery with a coprocessor (IRQ 13; w16 does nothing there yet); what ordinal 2 does (w16 exports a stub; it is unnamed on Windows, and w16 also exports `WEP` as ordinal 6, UNVERIFIED); `__fpMath` functions above 12; which instructions besides FINIT the 3.1 emulator leaves out. w16's emulator does not yet do BCD and the transcendental instructions (F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN; it treats them like FINIT), and is tested only as far as TK6 goes.

### 9.3 Additional permitted sources and tools

| Item | Status |
|---|---|
| OpenWatcom headers and import libraries (`h/win`, `lib286/win/windows.lib`) | Allowed (open source, Sybase OWPL): API names, ordinals, constants and structure layouts as facts. Used directly only to build test programs. |
| HDPMI16 (HX DOS extender, freeware, "may be used for any purpose") | Test-only DPMI host, fetched into `.tools/` by `tools/fetch-hdpmi.sh`; never committed or redistributed. |
| DOSBox | Test-only emulator. |
