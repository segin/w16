# The `[w16]` section of SYSTEM.INI

Windows 3.1 has no `[w16]` section; every key here is w16's own. 3.1
ignores the section, so a SYSTEM.INI may carry it whichever system
reads it. Each key is read once, when the module that uses it starts,
so a change takes effect at the next start of Windows (or of the program,
for an application's own key). Values are decimal unless noted.

There are two kinds of key:

- **Enhancements** go beyond Windows 3.1. Under w16's fidelity rule
  (ARCHITECTURE.md §6.8), each must be switchable back to stock 3.1 by
  configuration alone: with the stock value there is no visible
  difference and nothing an application could see. Compatible
  enhancements are on by default; ones that can break programs are off.
- **Debugging aids** change nothing an application sees. They report,
  log or check, and are meant for development and tests.

## Enhancements

| Key | Default | Stock 3.1 | Read by | What it does |
|---|---|---|---|---|
| `ScrollBarMenu` | `1` | `0` | USER | The right-click shortcut menu on scroll bars of Windows 2000 and later: Scroll Here, Top, Bottom, Page Up, Page Down, Scroll Up, Scroll Down (Left Edge, Right Edge, Page Left ... on a horizontal bar). |
| `DesktopVersion` | `1` | `0` | USER | The version stamp in the desktop's bottom right corner ("Standard Mode w16 v0.1 Debug Release *commit*"), in the style of Windows 3.0's debug pre-release builds. |
| `NotepadAccelerators` | `1` | `0` | w16's Notepad | Later Windows' Notepad keys, shown in its menus: Ctrl+N New, Ctrl+O Open, Ctrl+S Save, Ctrl+P Print, Ctrl+A Select All, Ctrl+F Find, and F1 shown for Contents. `0` gives 3.1's exact menu and accelerator table. |
| `ExtendedUserHeap` | `0` | `0` | USER | Windows kept outside USER's DGROUP, so far more windows fit. Can break programs that read USER's internal structures, so it is off by default. |
| `ExtendedGdiHeap` | `0` | `0` | GDI | GDI objects kept outside GDI's DGROUP, so far more objects fit. Can break programs that read GDI's internal structures, so it is off by default. |
| `PEError` | `1` | `0` | KERNEL | Starting a 32-bit program (a PE file, e.g. a Windows 95 program) when Win32s is not installed (no W32SYS.DLL where libraries are searched): WinExec returns 21 and starts nothing, and w16's Program Manager says "Cannot run *file*. This 32-bit program requires Win32s, which is not installed." `0`: as 3.1, the program's MS-DOS stub runs as a DOS program ("This program cannot be run in DOS mode."). LoadModule returns 21 either way, as on 3.1. |
| `MouseTrailDecay` | `2` | — | VGA.DRV | With Mouse Trails on, how many timer ticks (55 ms each) a stopped pointer's trail takes to drop each image: `2` fades a full trail in about 0.6 s, as Windows 3.1 looks when used; `1` drops one image a tick, as 3.1's driver does when driven directly by a probe. |
| `LargeMemory` | `1` | `0` | DOSX, WIN386 | How extended memory is taken from XMS. With an XMS 3.0 driver on a 386, w16's DOS extender (Standard mode) and WIN386 (386 Enhanced mode) take the largest free block with XMS 3.0's functions 88h and 89h, whose sizes are 32-bit, then any other blocks; DOSX makes blocks that follow one another one free block. Programs then see one free block as large as the memory (DPMI 0500h, GetFreeSpace, GlobalCompact, TOOLHELP's MemManInfo); some old programs mishandle free-memory values that large. `0`: only XMS 2.0's 08h and 09h, which stop at 65,535 KB a block, and in Standard mode each block apart, as Windows 3.1's DOSX: DPMI 0500h's largest block is then at most 64 MB, while all the memory is still used, as on 3.1 (ARCHITECTURE.md 9.6, "More than 64 MB"). With 64 MB or less there is no difference. On an 80286 the key has no effect. |
| `RegeditNewStyle` | `0` | `0` | w16's REGEDIT | `1`: the Registration Info Editor starts in its New Style view, a Windows 95 Registry Editor-like layout (a key tree with folders and +/- boxes, a splitter, a Name/Data value list, a status bar with the key's path, Registry/Edit/View/Help menus, in-place rename, Edit String); `2`: it starts in 3.1's views; with `1` or `2` every view has View > New Style to switch, and the choice is written back here. `0` is 3.1's program exactly: no View menu, nothing else. Off by default because it changes the program's look. |
| `PifFieldFix` | `1` | `0` | w16's PIFEDIT | When the PIF Editor reads a PIF, 3.1's cuts each text field after its last non-blank character with a NUL, which for a field filled with NULs to its end lands on the byte after it: re-saving Setup's DOSPRMPT.PIF turns 640 KB into 512 KB, and re-saving a PIF made in Standard mode damages the name of its "WINDOWS 286 3.0" section. `1`: nothing is written outside a field. `0`: 3.1's exact bytes. |
| `FontAliases` | `Arial,Times New Roman,Courier New` | (empty) | GDI | Font aliases (ARCHITECTURE.md §11.12): each listed name that no installed font has and that WIN.INI `[FontSubstitutes]` gives an installed TrueType face (w16's tree: Arial=Liberation Sans, Times New Roman=Liberation Serif, Courier New=Liberation Mono) is listed by EnumFonts and EnumFontFamilies, with no face asked too, as that face's fonts under the name, and GetTextFace answers it; programs that look for Arial in the font list (Microsoft Query's Cue Cards) find it. 3.1 lists such a substitute under neither name. With those fonts installed (Windows 3.1's own) it changes nothing. |
| `DebugToAux` | `0` | `1` | KERNEL | Where OutputDebugString's text goes with no debugger. `1`: always to DOS handle 3 (AUX), as 3.1's retail KERNEL does (in DOSBox AUX is the console, so the text is painted over Windows' screen). `0`: to AUX only when it is not the console, and always to `debuglog`. |

w16's test harness writes the stock values (`DesktopVersion=0`,
`NotepadAccelerators=0`, `DebugToAux=1`, `PEError=0`) for every test that compares
with Windows 3.1 (not `LargeMemory=0`: the tests' machines have 16 MB, where
both values give the same; the boot-test case `bigmem` runs with more); another value is set per test with `tests/dropin.sh
-i w16:KEY=VALUE` or the `DROPIN_INI` environment variable (TESTING.md).

The VM Task Manager's device (VMTASK.386, ARCHITECTURE.md 15.12) is
switched by its own line instead: the install tree's SYSTEM.INI has
`device=vmtask.386` in `[386Enh]`; without the line nothing of it is
loaded, as in stock 3.1. Loaded, it changes nothing a program sees
(it only counts; VMTASK.EXE without it shows the Windows tasks alone).

## Debugging aids

| Key | Default | Read by | What it does |
|---|---|---|---|
| `debuglog=FILE` | none | KERNEL | Writes w16's own diagnostics to FILE: parameter-error notes (HandleParamError), WinOldApCall, calls to unimplemented imports, `schedlog`, `modmap` and a copy of OutputDebugString's text. Without it they go to standard output only while no Windows screen is up. |
| `trace=MOD.ORD,...` | none | KERNEL | Records every call to the listed imports (caller, AX, BX, CX, DX, SI, DI, the first six argument words), the last 400, printed in the fault, watchdog and hangwatch reports and when Windows ends. Only imports bound by the loader: procedures a module gets with GetProcAddress are not traced. |
| `tracefrom=MODULE` | none | KERNEL | With `trace`: only the calls made from MODULE's code (e.g. `MMSYSTEM`), so that other callers do not push them out of the ring. |
| `modmap=1` | `0` | KERNEL | Prints every module's selectors once the shell is loaded (and when Windows fails at boot). |
| `watchdog=SECONDS` | `0` (off) | KERNEL | If the boot makes no progress for SECONDS, prints where the processor has been and exits. The test harness sets 15. |
| `hangwatch=SECONDS` | `0` (off) | KERNEL | After the boot: if SECONDS pass with no task switch and no pass of KERNEL's idle loop, prints where the processor has been (as `watchdog`, with the `trace` ring) and exits. For hangs of the whole system once Windows is up (`tests/qtplay.sh` sets 30). A program that computes for that long without yielding trips it too (QuickTime's installer unpacks for 15-21 s on an idle host and up to 46 s under load: give such a case a margin). With `exitmsg`, the longest stretch without progress is printed as Windows ends. |
| `exitmsg=1` | `0` | KERNEL | Prints `w16: back to DOS, code N` when Windows ends. 3.1 prints nothing. |
| `schedlog=1` | `0` | KERNEL | Logs every task switch with what caused it (start, Yield, OldYield, DirectedYield, WaitEvent). |
| `heapcheck=1` | `0` | KERNEL | Checks the global heap and the caller's local heap at every allocation, free, lock and task switch; at the first inconsistency it reports what is wrong and where, and ends Windows. Slow. |
| `idle1689=1` | `0` | KERNEL | A test option: makes the 386 Enhanced mode idle call (INT 2Fh AX=1689h) on every pass of KERNEL's idle loop, as a stress accelerator. 3.1's KRNL286 never makes it. |
| `userlog=1` | `0` | USER | Writes USER's diagnostics to `C:\USER.LOG`. |
| `gdilog=1` | `0` | GDI | Writes GDI's diagnostics to `C:\W16GDI.LOG`: unimplemented ordinals, the display driver's Enable, the font table and font mapping, and each SetDIBits, GetDIBits, SetDIBitsToDevice and StretchDIBits call's DIB format (size, bit count, compression, colours used, image size, colour use) and result. |
| `vmmtrace=1` | `0` | WIN386 | The 386 Enhanced mode VMM's detailed trace, written to COM1 (with the VxDs' dynamic links and callbacks; a VMM service w16 lacks then fails with carry during initialisation instead of ending Windows with a message, so that one run shows all a device needs). |
| `vmmtrace=2`..`5` | `0` | WIN386 | More of the VMM's debugging aids (ARCHITECTURE.md 12.18): 2 the DOS VMs' trace; 3 watches a device's reads of the VMM's memory through `_MapPhysToLinear`; 4 steps W32S.386's Device_Init; 5 steps it and runs the oracle that learns, from the device's own flags, what it compares after each hit of its search of the VMM's first pages (`w32soracle=XX [OFF:SIZE:VALUE ...]`, `w32sprefix=HEX`). |

## Example

```ini
[w16]
; stock Windows 3.1 behaviour
ScrollBarMenu=0
DesktopVersion=0
NotepadAccelerators=0
DebugToAux=1
PEError=0
LargeMemory=0
; while debugging
debuglog=C:\W16DBG.LOG
watchdog=15
```
