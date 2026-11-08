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
   - hooks protected-mode INT 21h, so AH=4Ch from a task ends that task (and AH=51h/62h give the task's PDB);
   - loads the application named on the command line through the NE loader;
   - makes it the first task (its command tail is the rest of KERNEL's command line) and runs the scheduler from its own stack; when the last task ends, KERNEL exits to DOS with that task's exit code (Windows calls ExitWindows then, as Pietrek describes; w16 has no USER yet).
3. **DOS services** come through INT 21h from protected mode, translated by the DOS extender, exactly as the real KERNEL relies on DOSX/WIN386. The M1 test host is HDPMI16; w16's own DOSX replaces it in M2.
4. **Constant exports** (`__AHINCR`, `__0040H`, `__WINFLAGS`, …). `wlink` cannot export absolute symbols, so these labels live in their own `KCONST` segment and KERNEL resolves those ordinals to runtime values.
5. **Unimplemented imports** bind to 8-byte thunks. A thunk reports `w16: call to unimplemented MODULE.ordinal` and ends the task with exit code 0FFh. Loading never fails just because an API is missing. Imports from Windows modules w16 does not provide yet (USER, GDI, KEYBOARD, SOUND, SYSTEM, DISPLAY, MOUSE, COMM: `pending_modules` in `kernel/loader.c`) bind to these thunks too; any other missing module fails the load with 2, as on Windows.
6. **Modules** (`kernel/module.c`, `kernel/loader.c`, `kernel/mdb.c`): the hModule is the selector of the module's in-memory module table in the Windows 3.1 layout (§9.2); beside it, w16 keeps one record per loaded module in its own global block, in a list that starts with KERNEL. Loading a module loads the modules it imports from first (recursively, each found once and shared, with usage counts), relocates it, patches the prologues of its exported procedures and runs a DLL's initialisation routine; freeing the last use of a DLL calls its `WEP` and releases what it imports. Usage counts follow the import closure (§9.2, tasks). `MakeProcInstance` thunks live in the task's TDB (`kernel/procinst.c`).
8. **Tasks** (`kernel/task.c`; layout and behaviour in §9.2): each task has a TDB in DOS memory in the Windows 3.1 layout, with its PDB (a child PSP of KERNEL's, made with DOS 55h) at offset 100h. The scheduler is non-preemptive: a task switch (`task_switch` in `kentry.asm`) saves the registers on the task's own stack and its SS:SP in the TDB, and switches the DOS PSP, the current drive and directory and the FPU control word with it. A task's end runs on a stack of KERNEL's (`exit_stack`), frees what the task owned and runs the next task with an event. `LoadModule` and `WinExec` (`kernel/loader.c`) start programs; another instance of a loaded program gets a copy of its DGROUP as it was loaded.
7. **Directories:** the system directory is the one `KRNL286.EXE` is in. The Windows directory is the `windir` environment variable, which WIN.COM (or LOSE.COM) must pass, as on Windows 3.1 (§9.2). Without it, KERNEL falls back to the parent of the system directory if that is called `SYSTEM`, else the system directory itself (w16's own convention).

M1 limitations, in order of when they should be fixed:
- **Local heap:** implemented (`kernel/local.c`) with Windows 3.1's observed layout, but `LocalNotify`, `LocalShrink`, `LocalHandleDelta`, `LocalCountFree`/`LocalHeapSize` are not, and the exact order of compaction, discarding and growing on a failed request is UNVERIFIED.
- **Global heap:** implemented (`kernel/global.c`) in Windows 3.1 KRNL286's layout as observed (§9.2, TK19): arenas, Burgermaster, selector table, free list, LRU ring, compaction (moving blocks, also the running task's stack, through `seg_move` in `kentry.asm`), discarding with GlobalNotify. Not yet: KRNL286's placement details (3.1 moves moveable blocks up out of a fixed block's way, keeps discardable code above a fence at the top), THHOOK, reallocating a discarded block to more tiles than it had (GlobalReAlloc returns 0 there). Deliberately different: the heap grows by DPMI chunks on demand instead of taking all memory at start-up, and GlobalCompact/GetFreeSpace count what DPMI can still give.
- **Selectors** (`kernel/selector.c`): every selector comes from the DPMI host (INT 31h 0000h) and is changed through DPMI (0007h-000Ch); Windows 3.1 keeps its own free list in the LDT. So selector values and reuse order differ, and a fresh `AllocSelector(0)` is a not-present data segment instead of Windows' unusable system-type descriptor. GlobalPageLock does not page-lock (DOSX does not page).
- **Loading:** DLLs load with their dependencies (§9.2 lists what was observed). Floating-point OS fixups are applied as observed (§9.2). Not yet: self-loading modules, demand loading and discarding, network directories in the search. The global heap's block records and the unimplemented-import thunks grow in chunks as needed (no fixed tables).
- **Module and task layout:** module tables and TDBs have the Windows 3.1 layout (some segment flag bits differ; resources are loaded through them, `kernel/resource.c`, but resources larger than 64 KB cannot be reloaded after discarding yet). TDB fields w16 does not fill yet: the signal procedures (24h-2Dh, USER's), the per-task interrupt handlers (32h-4Dh), compatibility flags (4Eh), A8h.
- **Tasks:** without USER there are no message queues (the queue slot and Get/SetTaskQueue are there), so Yield is OldYield and nothing posts events but the tasks themselves; a task waiting with no task holding an event waits forever, as on Windows. Not yet: SetPriority, LockCurrentTask, the lazy PSP switch (w16 switches it with every task), TOOLHELP notifications, terminating tasks from outside, DOS programs (WINOLDAP).

### 9.2 Testing and verification

- `make test` runs each test program under `KRNL286.EXE` in headless DOSBox with HDPMI16 (`tests/dosrun.sh`, `tests/run-tests.sh`); the floating-point probe TK6 runs in DOSBox-X, with a coprocessor and without one.
- Test programs print `PASS`/`FAIL` lines for behaviour we are certain of, and `OBS name=value` lines for raw observations to compare between w16 and Windows.
- Code that relies on a recalled-but-unchecked detail is marked `UNVERIFIED:`. `grep -rn UNVERIFIED kernel` lists them.
- A probe can turn its observations into checks: `tests/apps/tk3.c` looks up every `OBS` name in `tk3ref.h`, generated by `tools/obsref.py` from the `TK.LOG` of reference runs in both modes (names whose values differ between the runs are left out; a name only some runs report is compared among those). Values that depend on the program's layout, such as addresses, are printed as `OBS ~name` and checked relative to each other instead.
- Test programs write their results to `TK.LOG` in the current directory with plain INT 21h. On real Windows a task's stdout and `OutputDebugString` are not visible, so a log file is the one channel that works on both systems.
- **Reference systems:** the user supplied disk images of Windows 3.1 (3.10.103) and Windows for Workgroups 3.11a. Both are installed with an unattended Setup (`SETUP /H:` with a settings file) into `.tools/ref/win31/cdrive` and `.tools/ref/wfw311a/cdrive`. `.tools/` is gitignored, so no Microsoft files enter the repository.
- **KRNL286 and KRNL386:** on a 386 or later Windows 3.1 runs **KRNL386.EXE in Standard mode too** (TK19 printed `GetModuleFileName(KERNEL)` = `C:\WINDOWS\SYSTEM\KRNL386.EXE` under `WIN /S`); KRNL286.EXE runs only on an 80286. So every reference run before TK19 observed KRNL386, whose global heap differs from KRNL286's (block sizes, fixed handles, the arena layout; its local heap differs too). `tests/refrun.sh` observes KRNL286 with `REFRUN_KRNL286=1` (copies KRNL286.EXE over KRNL386.EXE in the scratch copy: KRNL286 then runs on the 386, reports the real processor in GetWinFlags, and TOOLHELP still reads its heap) or `REFRUN_CPU=286` (DOSBox-X emulating an 80286; slower). w16's KRNL286.EXE follows KRNL286 where the two differ in the global heap; its local heap still follows KRNL386. A probe whose plain observations differ under KRNL286 gets a second table, `TK_REF286` (`tools/obsref.py --k286 K286LOG [-x REGEX] tkNref.h`), which `tklog.h` consults first when the global heap behaves like KRNL286's (`k286()`: GlobalAlloc(GMEM_FIXED, 1) gives 10h); names given with `-x` (KRNL286 differs where w16 does not follow it) or `-X` (KRNL286 varies between runs) are not checked then. TK3 (global heap values only), TK12, TK15 and TK16 have one; TK1's own checks follow the kernel.
- `tests/refrun.sh PROGRAM.EXE [/S|/3] [ref-dir]` runs a test program on the reference system in headless DOSBox. It makes the program the shell (`shell=` in a scratch copy of SYSTEM.INI), so Windows exits when the program does, then prints `TK.LOG`. This is black-box observation (§3): only the program's own observations are used, and no Windows binary is inspected.
- Windows 3.1 is the authority. WfW 3.11 runs only in 386 Enhanced mode, so where it differs from 3.1 the difference is investigated rather than copied.
- Behaviour confirmed on Windows 3.1 becomes a `PASS`/`FAIL` check. Values that depend on the environment, such as selector numbers and the command tail WIN.COM builds, remain `OBS`.
- First findings, the same in Standard and 386 Enhanced mode unless noted. Several replaced recalled guesses that were wrong:
  - **Initial stack:** the automatic data segment is laid out as static data, then the stack, then the local heap. Initial SP = data size + stack size.
  - **InitTask:** CX and instance word 0Ah equal SP − stack size + 96h. Words 0Ch and 0Eh equal the entry SP. The command tail is NUL-terminated (the CR is replaced), and BX points past its leading blanks.
  - **hInstance:** the handle of the automatic data segment, which is DS with bit 0 clear.
  - **Global handles:** every handle, fixed or moveable, is its selector with bit 0 clear (KRNL386; KRNL286: a fixed block's handle is its selector).
  - **GlobalSize:** rounds up to 32 bytes (KRNL386; KRNL286 rounds arena and block together to 32, GlobalSize being an odd multiple of 10h: see the global heap findings).
  - **Lock counts:** kept only for discardable blocks. GlobalFlags of a locked fixed or moveable block is 0, and of a locked discardable block 0101h.
  - **GlobalAlloc(GMEM_MOVEABLE, 0):** returns a block that is already discarded (GlobalFlags 4000h, size 0, GlobalLock returns NULL). `GlobalAlloc(GMEM_FIXED, 0)` succeeds too (KRNL386; KRNL286 returns 0).
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
  - **lstrcpy/lstrcat** return the destination. A NULL far pointer does not fault in them: it acts as a small scratch string (lstrlen(NULL) = 2, `lstrcpy(NULL, "x")` returns NULL but later reads of NULL see "x"). w16 treats NULL as an empty string instead (`kernel/string.c`).
- DLL loading findings (`tests/apps/tk5.c` with the test DLLs `tkdll.c`, `tkdlb.c`, `tkid.c`, `tknod.asm`; run on Windows 3.1 by `tests/tk5ref.sh`; both modes identical):
  - **Search order** for a name without a path (a copy in each directory, removed one by one): current directory, Windows directory, system directory, the directory of the running task's program, the PATH directories. No extension is added (`LoadLibrary("TKNOD")` did not find TKNOD.DLL). While Windows loads its shell there is no running task: DLLs the shell imports were not found in the shell's own directory (Windows then waits forever), so `tests/tk5ref.sh` puts the DLLs in the Windows directory.
  - **Loaded modules** are matched by the base name of the argument before any file is opened, whatever the path (`C:\TKNODIR\TKDLB.DLL`, a directory that does not exist, returned the loaded TKDLB), and by `MAKELP(0, hModule)`; the usage count goes up and the initialisation does not run again.
  - **Results:** the hInstance (DGROUP handle = DS & ~1) of a DLL with data, the hModule of a DLL without (odd), an odd selector other than the hModule for KERNEL. Errors: 2 file not found (also for `""`, and when a module the DLL imports is missing), 3 path not found, 20 (14h) not an NE file (a text file, an MZ file without NE header), 0 when the initialisation returns 0. After a failure nothing of the DLL stays loaded.
  - **Usage counts:** 1 for a DLL loaded once, implicitly or not; each LoadLibrary/LoadModule adds 1, each FreeLibrary/FreeModule takes 1; at 0 the DLL's `WEP` runs with 0 and the module is gone. `GetModuleUsage` takes the hModule or hInstance. `LoadModule` of a DLL behaves like LoadLibrary; FreeModule's AX afterwards is not meaningful (F300h).
  - **Implicit dependencies** load before the program; a dependency's initialisation runs before (and nested deeper on the stack than) that of the DLL importing it.
  - **Initialisation routine (the NE entry point of a DLL):** AX = 1, CX = heap size, DX = DS = the DGROUP selector, DI = hInstance, SI = 0, ES = 0 (no command line), DS:0000–000F all zero and no local heap: the DLL makes it itself (LocalInit). For a DLL loaded by LoadLibrary it runs on the caller's stack; for the shell's implicit DLLs on another stack. BX and BP held leftover values. A DLL without a data segment gets DS = DX = DI = its hModule, CX = 0. When the routine returns 0, the DLL's WEP still runs before it is freed.
  - **Imported modules** are looked for as NAME.DLL only: a DLL importing TKEXEM, which existed only as TKEXEM.EXE, failed with 2.
  - **Exports:** imports and GetProcAddress find names in the non-resident table too. GetProcAddress on a program module (by hModule or hInstance, by name or ordinal) returns 0.
  - **Prologues of exported procedures:** in a DLL `8C D8 90` (mov ax,ds; nop) and `1E 58 90` (push ds; pop ax; nop) become `B8` + the DGROUP selector; in a program, three NOPs.
  - **MakeProcInstance (protected mode):** called with a program's DS it returns a new 8-byte thunk every time, `B8 <caller's DS> EA <procedure>`, in the task's TDB (see the task findings below); the hInstance argument is ignored (hInstance, hInstance | 1, 0 and a DLL's hInstance gave the same thunk). Thunks are 8 bytes apart; FreeProcInstance makes a thunk the next one handed out. Called with a DLL's DS it returns the procedure itself; with the DS of a global block of no module, a thunk loading that DS.
  - **GetInstanceData** copies from the instance's DGROUP to the same offset in the caller's and returns the count, also for a DLL's instance.
  - `LoadLibrary("")` with its own buffer left the buffer alone. UNVERIFIED: in an earlier probe that passed a `""` literal, a different string literal of the program read as "L" afterwards; the cause was not pinned down.
  - **Implicit DLLs of a program started later** (TK5 from WIN.INI's `load=` with Program Manager as shell, `REFRUN_SHELL=PROGMAN.EXE tests/tk5ref.sh`): their initialisation runs on the new task's stack, one after the other rather than nested — inside InitTask, as Pietrek's *Windows Internals* describes; only the shell's are initialised during loading. Confirmed for a program started by LoadModule (TK15, below).
  - **Environment:** every task's environment holds lowercase `windir=C:\WINDOWS` (no `WINDIR`), equal to GetWindowsDirectory.
  - `tests/run-tests.sh` runs TK5 and TK11 in the same layout (the DLLs in `C:\WINDOWS`, `C:\TKPATH` on the PATH via `DOSRUN_PRE`; `tests/refrun.sh` has `REFRUN_PRE` for the same), with KERNEL started through the test tool `WINENV.COM` (`tests/dos/winenv.asm`), which passes `windir` as WIN.COM does. KERNEL is in `C:\W16SYS` there, so the Windows directory can only come from `windir`.
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
- Floating-point findings (`tests/apps/tk6.c` with `tk6fp.asm`, `tk6n.c`, the arithmetic probes `tk7.c` and `tk8.c` (with `TK8T1`-`TK8T5`), `tk9.c` (exports) and `tk10.c` (fixup records); run by `tests/tk6ref.sh` in DOSBox-X with a coprocessor and without one (`fpu=false`), both modes; Standard and 386 Enhanced mode identical; implemented in `kernel/loader.c` (`os_fixup`), `kernel/kentry.asm` (`int3d_handler`) and `win87em/`):
  - **Reference environment:** DOSBox's FPU keeps no exception flags in the status word and cannot be switched off, so these probes run in DOSBox-X (`REFRUN_X`, `DOSRUN_X`). Neither emulator raises floating-point exceptions (IRQ 13; checked with a DOS program), so how Windows 3.1 delivers an unmasked exception *with a coprocessor* is not observable here. With a coprocessor DOSBox-X reads control word bit 6 as 1; the probe ignores that bit.
  - **OS fixups:** OpenWatcom (`-fpi`) references `FIxRQQ` symbols at each floating-point instruction; wlink turns them into additive NE relocations of type 3 (OSFIXUP) with t1 = 1 (`FIARQQ`/`FJARQQ`, DS prefix), 2 (SS), 3 (CS), 4 (`FIERQQ`, ES), 5 (`FIDRQQ`, FWAIT + escape), 6 (`FIWRQQ`, NOP FWAIT), one record per instruction at its FWAIT. The symbols' values come from OpenWatcom's `emu87.lib`; `tests/apps/fixrqq.asm` defines them for programs without a run-time library.
  - **With a coprocessor** the loader turns the FWAIT of types 1-5 into a NOP (`9B D9 C0` -> `90 D9 C0`) and type 6 into INT 3Dh (`90 9B` -> `CD 3D`). INT 3Dh works in a program that does not import WIN87EM (WIN87EM is not loaded for it), so KERNEL handles it (an FWAIT).
  - **Without one** the loader adds the FIxRQQ value at the FWAIT and, for types 1-3, the FJxRQQ value one byte further: `CD 34`-`CD 3B` (escape D8h-DFh) followed by the ModR/M byte, `CD 3C` + the escape byte with the segment in its top two bits (DS 00, SS 01, CS 10, ES 11), `CD 3D` for a lone FWAIT. WIN87EM's emulator handles those; a program that does not import WIN87EM computed a wrong result and Windows hung at its exit. No code is patched back after it runs, in either case.
  - **Before any `__fpMath` call** a task's control word is 037Fh with a coprocessor and 1332h with the emulator.
  - **`__fpMath`** (register interface; BX = function; SI, DI, DS, SP kept; BX, CX, DX otherwise changed): 0 initialises (FINIT, control word 1332h, the coprocessor gets 1330h; AX = 0, CF = 0; the handler is not changed); 1 and 2 do the same with CF = 1 (CF = 0 with the emulator); 3 sets the exception handler DX:AX (AX = 253Eh afterwards); 4 sets the control word AX (the coprocessor gets AX & FF3Ch, i.e. invalid and denormal unmasked; the emulator all of AX) and returns AX & FF3Ch; 5 returns the control word last given to 4 (with the emulator: its control word); 6 rounds ST to an integer with the rounding control AX & 0C00h (AX = that control word; 1 with the emulator); 7 pops ST into DX:AX as a 32-bit integer rounded with AX & 0C00h (out of range 8000:0000h; the 3.1 emulator also changes SI and DI, w16's does not); 8 returns the exception flags (status & 3Fh with a coprocessor; with the emulator every flag raised since the last 9 or 0, including bits 6 and 7); 9 clears the exceptions (AX = 0, CF = 1 with a coprocessor); 10 returns the number of registers in use; 11 returns 1 with a coprocessor, 0 without, DX kept; 12 keeps AX (meaning unknown); see below for 13 and up.
  - **`__Win87EmInfo(buf, n)`:** n < 12 returns FFFFh and writes nothing; otherwise 12 bytes: 0600h, the save area size 01CDh, WIN87EM's data and code selectors, 1 with a coprocessor (0 without), 0; returns 0. **`__Win87EmSave` / `__Win87EmRestore(buf, n)`:** n < 01CDh returns FFFFh and does nothing; otherwise 0. With a coprocessor the area starts with an FSAVE image (which reinitialises the coprocessor); with the emulator its first 94 bytes are not written; WIN87EM's own state follows (w16 uses its own layout there).
  - **Emulator status word:** TOP always reads 0; a stack underflow sets IE only (and calls the handler with 8Bh, see below); ES is never set except for the square root of a negative number (81h). FINIT is not emulated: it sets bit 6 (0040h), leaves the control word, and calls the handler with 87h whatever the masks.
  - **Exceptions (emulator):** an unmasked exception calls the handler set by function 3 after the instruction, with AL = the code (83h zero divide, 87h unemulated, 88h square root of a negative number, which is reported even when masked), AH varying between runs, WIN87EM's DS, and on the stack the far return address and then the program's DS (OpenWatcom's handler reads its DS there). The handler returns with RETF and the program continues.
  - **Arithmetic** (`tk7.c`: add, subtract, multiply, divide both ways, compare, square root, FRNDINT, FIST to 16 and 32 bits, FST to 32 and 64 bits, on twelve operand pairs including overflow, underflow, a denormal, zeros and a division by zero; the four rounding controls and three precisions; constants and integer loads; all exceptions masked): the 3.1 emulator gives the same 80-bit results and flags as the coprocessor except that -0.0 loads as +0, 0/0 sets ZE as well as IE, FRNDINT of 0 sets PE, the condition codes read 0 after FLDCW + FCLEX, and its square root of 1e308 is one unit in the last place above the correctly rounded result. w16's emulator matches the 3.1 emulator in all of these except that square root (it rounds correctly).
  - **Unusual fixup records** (`tk10.c`, rewritten after linking by `tools/nechain.py`): the additive flag of an OS fixup record does not matter and records are never followed as chains. A record without the flag whose site held the offset of another site patched only its own site, as a number (04BCh became 04B1h with a coprocessor, 60EEh without); the other site was left alone. With a coprocessor the loader lowers the first byte by 0Bh (9Bh -> 90h) rather than storing 90h.
  - **`__fpMath` above 12** (`tk9.c`, 13-24): AX = DX = FFFFh, CF = 0, BX and CX kept, nothing else changed, with and without a coprocessor.
  - **Ordinal 2** (`tk9.c`): takes one word argument (it pops two bytes; 0, 1 and 200h behaved the same) and reinitialises like function 0 (register stack emptied, control word 1330h); AX = 0, CX = 0, and BX, SI, DI came back as 01A6h in every run (an internal value, it seems; w16 leaves them). Called with nothing pushed and all registers 0 it ended the program. Without a coprocessor the program ended at its next floating-point instruction afterwards; w16 resets its emulator instead (UNVERIFIED: what the word means).
  - **Instructions the 3.1 emulator does not emulate** (`tk8.c`): FBLD, FBSTP and FXTRACT, like FINIT (handler called with 87h, nothing else changes). It does emulate F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN (in a fresh state; `TK8T1`-`TK8T5` run one each) with 8087 semantics: results within a few units in the last place of the exact value (up to 6 for FYL2XP1), FPTAN leaves an unnormalised pair Y, X (Y / X = tan) instead of tan and 1.0, and FPATAN gives nonsense outside the 8087's range 0 <= Y < X. PE is set unless the result is exact. A coprocessor (DOSBox-X) gives correctly rounded results and the 387's FPTAN. w16 emulates these within about two units, FPTAN in the 387's form, FPATAN with the 387's quadrants; the flags match, the last bits do not.
  - **FPREM** (emulator): the remainder is complete in one go whatever the exponents (C2 never set; the low quotient bits in C0, C3, C1); a zero divisor gives +0 with no exception. **FSCALE** (emulator): no exception flags at all; the scale is the top 16 bits of ST(1)'s significand shifted right by (15 - its unbiased exponent) & 15, negated if ST(1) is negative (so 0.9 scales by E666h = -6554). w16 does both the same way.
  - **Square root** (emulator): of 24 doubles and the integers 2-61, 12 results are one unit in the last place above or below the correctly rounded one (and PE was missing for two inexact results). They are not explained by the exact value's position between its neighbours (the wrong ones lie 0.26-0.67 of a unit above the lower one, but others there are right). A Newton iteration y = (y + x/y) / 2 with every step rounded to nearest at 64 bits reproduces 80 of the 83 results; the square roots of 51, 61 and 1e308 still differ, and no variant tried (other precisions, truncation, rounding up, mixed) fits all. So no rule was found: w16 rounds correctly, and these values are shown, not checked. The square root of +infinity is invalid (the handler gets 88h).
  - **Stack faults** (emulator): an FSTP to memory from an empty stack stores the register's stale contents (0 with exponent 3FFFh for a register not used since function 0), sets no flag, and calls the handler with 8Bh although IE is masked; the reference program ended soon after. w16 reports 8Bh for every stack underflow whatever the masks.
  - **Deep stack** (emulator): the register stack holds more than eight values; twenty pushed came back intact, with no flag set; the handler was called once with 8Ah (stack overflow) when the depth first passed 14 (at the fifteenth push). With a coprocessor the ninth push sets IE and leaves eight. w16 spills the registers below ST(7) to a block that grows as needed (not kept in the save area, UNVERIFIED there).
  - **Save area** (`tk8.c`): after the 94-byte FSAVE part, words 94/96 hold the handler (emulator only), 98 1 with a coprocessor, 100 the control word of function 4, 104 that & FF3Ch, 256 function 12's word; the rest held selectors, addresses and values of unknown meaning and was zero from 304 on. w16 writes the known words there and keeps its own state from 304.
  - **IRQ 13** (with a coprocessor): not raised by DOSBox or DOSBox-X, and no 86Box or PCem is installed here, so not observed. w16 implements the documented AT path, UNVERIFIED: WIN87EM hooks INT 75h, clears the coprocessor's BUSY latch (port F0h), sends EOI to both PICs, takes the highest-priority unmasked exception from the status word (81h invalid, 8Ah/8Bh stack fault, 82h-86h the others), clears the flags and calls the program's handler as the emulator does. Raised by hand (INT 75h after an unmasked zero divide was pending) on w16 it called the handler with 83h.
  - **Still open:** the 3.1 emulator's own rounding of square roots and transcendentals (not reproduced bit for bit; see above); exception delivery with a coprocessor (IRQ 13, implemented UNVERIFIED); what ordinal 2's argument means; w16 also exports `WEP` as ordinal 6 (UNVERIFIED that Windows has none); the 387-only FPREM1, FSIN, FCOS, FSINCOS are treated as not emulated (UNVERIFIED).

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
  - **Huge blocks:** GlobalAlloc over 64K gives tiles 8 apart; GlobalHandle and GlobalSize of tiles after the first give 0. **Tile limits:** on a 386 (KRNL286 and KRNL386) tile i's limit is GlobalSize − 64K·i − 1, so each reaches the end of the block (2A350h bytes under KRNL286: 2A34Fh, 1A34Fh, A34Fh); on an emulated 80286 (`REFRUN_CPU=286`) every tile but the last has FFFFh, the most a 286 descriptor holds. GlobalReAlloc to more tiles always moves the block to a new selector run, so the handle changes, and keeps the data (the new memory is not cleared); a fixed block grows that way only with GMEM_MOVEABLE under KRNL286 (KRNL386 also without). Shrinking to fewer tiles keeps the handle and frees the tiles beyond.

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

**Which slot.** On a 386 (DOSBox), `WIN /S` runs Microsoft's KRNL386.EXE; KRNL286.EXE runs only on a 286 or when KRNL386.EXE is missing (Pietrek; observed with TK18 and by the heap work). `tests/dropin.sh` deletes KRNL386.EXE in its scratch copy, so DOSX falls back to the KRNL286.EXE slot, which holds w16's KERNEL; `-r` deletes it too, so the reference is Microsoft's KRNL286 on the same machine. (`tests/refrun.sh` REFRUN_CPU=286 runs a real 286 in DOSBox-X; the drop-in does not use it yet because MOUSE.DRV faults under DOSBox-X, gap 3 below.)

**Other DOSX differences found while booting:** INT 31h 0007h on the selector in SS: HDPMI16 returns through the SS it has cached, DOSX does not, so KERNEL must not have pushed anything on the old stack memory after copying it (`seg_move` now runs on a stack of KERNEL's; TK19 as the shell returned through zeros before). USER's GetMessage reaches WaitEvent with interrupts disabled, so KERNEL's idle loop enables them.

**Probes for the boot** (not in `make test`; run them as the shell with `tests/dropin.sh -f build/dos/TKnn.EXE=TKnn.EXE -i 'boot:shell=C:\TKnn.EXE'`, and with `-r`): TK20, a minimal windowed program (InitApp, RegisterClass, CreateWindow, painting, SetTimer, the message loop): on w16 every step matches 3.1 up to the loop, then no WM_TIMER arrives, not even by PeekMessage, and a CreateSystemTimer procedure of TK20's own is not called either (4 calls on 3.1 in the same second); TK21, CreateSystemTimer and GetSystemMsecCount without InitApp: the same on w16 as on 3.1 (18 calls in 18 ticks). So SYSTEM.DRV's timer procedures stop being called once USER has set up a task (InitApp, a window): the first gap below. TK22 (was TK19 before the merge with the heap work): the boot probe above. USER calls, at boot, SYSTEM.EnableSystemTimers, CreateSystemTimer(0, DISPLAY's cursor procedure) and CreateSystemTimer(10000, USER's), and later re-creates its own timer at the SetTimer rate; COMM makes a 100 ms one ([w16] trace, below).

**Where it stands** (`tests/dropin.sh`): Microsoft's SYSTEM, KEYBOARD, MOUSE, VGA, MMSOUND and COMM drivers, the fonts, GDI and USER all load and initialise on w16's KERNEL, SHELL.DLL and PROGMAN load, and `BOOTLOG.TXT` is byte-for-byte the one Windows 3.1 writes. The screen shows the desktop and the hourglass; PROGMAN's window does not appear yet (PROGMAN makes a parameter error, ERR_BAD_HWND from SendMessage, then every task waits). With CLOCK.EXE as the shell DOSBox stops with "IRET: Illegal descriptor type 0" (an IRET to a freed selector). Under DOSBox-X (machine=svga_s3) MOUSE.DRV's initialisation faults (GP with ES = KERNEL's PSP selector, after GetDOSEnvironment).

**Debugging aids** (w16 only, while booting Windows; `[w16]` keys in SYSTEM.INI, set with `tests/dropin.sh -i w16:KEY=VALUE`): `trace=MOD.ORD,...` binds those imports to thunks that record each call (caller, registers, arguments) for the reports; `modmap=1` prints every module's selectors once the shell is loaded; exceptions 0, 6, 0Bh-0Eh are reported with module:segment, registers and the last 16 INT 21h calls, instead of DOSX's "Fault outside of MS-DOS Extender"; `[w16] watchdog=SECONDS` in SYSTEM.INI (dropin.sh sets 15) samples IRQ 0 and, after SECONDS without a boot-log line, prints where the processor has been and exits (it keeps itself the first INT 08h handler, as SYSTEM.DRV's timer calls the previous handler from its own); K327 (HandleParamError) notes the first parameter errors.

**The import surface** (`tools/kimports.py WINDOWS_DIR`): the boot modules and PROGMAN import 151 KERNEL entry points. Of the 60 w16 lacked, all are now exported; their implementations follow the SDK, Schulman and Pietrek and are UNVERIFIED unless noted (`kernel/kmisc.c`, `kernel/local.c`, `kernel/kentry.asm`; GlobalWire/Unwire, GlobalFix/Unfix, GlobalHandleNoRIP and GetFreeSpace are global.c's, observed by the heap work). The selector services among them (170, 171, 175-177, 184, 185, 187-189) are TEMPORARY versions in `kernel/selstub.c`, to be replaced by the observed ones (TK17). Probed so far: DiagOutput (TK22). Not yet observed on 3.1 and therefore guesses: SetErrorMode's storage, GetExeVersion while booting, LocalCountFree/LocalHeapSize/LocalShrink, GetHeapSpaces, GetFreeSpace (DPMI's largest block), the IsBad*Ptr family, Catch/Throw's buffer layout, K327 (returns doing nothing), GetCodeInfo, LongPtrAdd, FileCdr, DoSignal (does nothing), GlobalPageLock/Unlock (locks), Reserved1/2/5 (AnsiNext, AnsiPrev, lstrcmp in byte order).

**Remaining gaps, by what blocks the boot first:**
0. SYSTEM.DRV's timer procedures stop being called after InitApp (TK20 versus TK21), so USER's timers, and likely PROGMAN's start-up, never run; the import trace shows USER only calling OldYield and WaitEvent meanwhile, and no PostEvent at all. Next: find which callback (DISPLAY's cursor procedure at rate 0, USER's, COMM's) does not return or what makes SYSTEM skip them.
1. PROGMAN does not come up: find why it gets a bad window handle (the first API whose result differs from 3.1's), then why every task waits (USER's queues and events: PostEvent from USER's interrupt-time code, Yield/WaitEvent semantics with a queue, the per-task signal procedure USER sets with SetTaskSignalProc, which KERNEL should call when a task starts and ends — Pietrek, TDB 2Ah).
2. The IRET to a freed selector with CLOCK (suspected: the temporary selector services, now replaced by `kernel/selector.c`, or a code alias freed while in use; to be rechecked).
3. MOUSE.DRV under DOSX-on-DOSBox-X (ES = PSP after GetDOSEnvironment; compare the registers GetDOSEnvironment leaves on 3.1).
4. KERNEL's own usage count; GetProcAddress on constant entries returning ES:BX (Pietrek); exception handling for USER's/GDI's `__GP` tables (parameter validation by fault; KERNEL.342 `__GP`, HasGPHandler); the per-task interrupt vectors (TDB 32h-4Dh); WINOLDAP.
5. Probes on 3.1 for every UNVERIFIED export above.
6. TK19 (the heap probe) as the shell under DOSX with Microsoft's USER: FAILs on GlobalNotify's TDB 2Eh and block adjacency (USER's and GDI's own blocks are in the heap there), then TOOLHELP faults (GP, error code 0070h) walking the heap.
