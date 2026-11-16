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
| `MouseTrailDecay` | `2` | — | VGA.DRV | With Mouse Trails on, how many timer ticks (55 ms each) a stopped pointer's trail takes to drop each image: `2` fades a full trail in about 0.6 s, as Windows 3.1 looks when used; `1` drops one image a tick, as 3.1's driver does when driven directly by a probe. |
| `DebugToAux` | `0` | `1` | KERNEL | Where OutputDebugString's text goes with no debugger. `1`: always to DOS handle 3 (AUX), as 3.1's retail KERNEL does (in DOSBox AUX is the console, so the text is painted over Windows' screen). `0`: to AUX only when it is not the console, and always to `debuglog`. |

w16's test harness writes the stock values (`DesktopVersion=0`,
`NotepadAccelerators=0`, `DebugToAux=1`) for every test that compares
with Windows 3.1; another value is set per test with `tests/dropin.sh
-i w16:KEY=VALUE` or the `DROPIN_INI` environment variable (TESTING.md).

## Debugging aids

| Key | Default | Read by | What it does |
|---|---|---|---|
| `debuglog=FILE` | none | KERNEL | Writes w16's own diagnostics to FILE: parameter-error notes (HandleParamError), WinOldApCall, calls to unimplemented imports, `schedlog`, `modmap` and a copy of OutputDebugString's text. Without it they go to standard output only while no Windows screen is up. |
| `trace=MOD.ORD,...` | none | KERNEL | Records every call to the listed imports (caller, registers, arguments) for the fault reports and `debuglog`. |
| `modmap=1` | `0` | KERNEL | Prints every module's selectors once the shell is loaded (and when Windows fails at boot). |
| `watchdog=SECONDS` | `0` (off) | KERNEL | If the boot makes no progress for SECONDS, prints where the processor has been and exits. The test harness sets 15. |
| `exitmsg=1` | `0` | KERNEL | Prints `w16: back to DOS, code N` when Windows ends. 3.1 prints nothing. |
| `schedlog=1` | `0` | KERNEL | Logs every task switch with what caused it (start, Yield, OldYield, DirectedYield, WaitEvent). |
| `heapcheck=1` | `0` | KERNEL | Checks the global heap and the caller's local heap at every allocation, free, lock and task switch; at the first inconsistency it reports what is wrong and where, and ends Windows. Slow. |
| `idle1689=1` | `0` | KERNEL | A test option: makes the 386 Enhanced mode idle call (INT 2Fh AX=1689h) on every pass of KERNEL's idle loop, as a stress accelerator. 3.1's KRNL286 never makes it. |
| `userlog=1` | `0` | USER | Writes USER's diagnostics to `C:\USER.LOG`. |
| `gdilog=1` | `0` | GDI | Writes GDI's diagnostics to `C:\W16GDI.LOG`: unimplemented ordinals, the display driver's Enable, the font table and font mapping. |
| `vmmtrace=1` | `0` | WIN386 | The 386 Enhanced mode VMM's detailed trace, written to COM1. |

## Example

```ini
[w16]
; stock Windows 3.1 behaviour
ScrollBarMenu=0
DesktopVersion=0
NotepadAccelerators=0
DebugToAux=1
; while debugging
debuglog=C:\W16DBG.LOG
watchdog=15
```
