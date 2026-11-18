# VER handover (w16's VER.DLL)

Task: w16's own VER.DLL, a drop-in for 3.1's SYSTEM\VER.DLL (module VER,
WEP 1 and the eight functions at 2, 3, 6-11), every behaviour probed
black-box on 3.1; probe compared against Microsoft's DLL; w16's modules'
version resources made readable by 3.1's VER.DLL; boot case with a real
3.1 caller; docs. ARCHITECTURE.md chapter 20 has the observations, the
design and the results; this note is the working state.

Rules followed: no Microsoft binary disassembled or hex-read for code
(the NE export and import tables read with tools/neinfo.py and
tools/neimports.py, version resources read as data with a scratch dumper
for the numbers); facts from the SDK reference (Vol. 1 ch. 9, Vol. 2-4)
and the probe; all DOSBox work on installation-05 under the run name
`w16-ver` (`tools/remote-run.sh -n w16-ver ...`, `tools/remote-test.sh
-n w16-ver`); no Microsoft file committed or used as a test input (the
inputs come from tools/mkver.py, tools/mkne.py, tools/szddenc.py and
tools/fontc.py at test time).

## Status

- [x] probe VRP.EXE (tests/ver/vrp.c), inputs (tests/ver/mkinputs.py with
  the new tools/mkne.py and the importable tools/mkver.py), runner
  (vrun.sh), comparison (vcmp.py, vscen.sh), tree runner (vtree.sh),
  module check (vmods.sh), real caller (vfonts.sh,
  tests/scripts/input/cplfonts.txt)
- [x] shell/ver/: the DLL; exports, ordinals, module flags 8301/08,
  expected version 3.00, file version 3.10.0.103, installed as
  SYSTEM\VER.DLL, in README.TXT
- [x] every w16 module's version resource from tools/mkver.py (CALC,
  DDEML, OLECLI, the fonts, VGA.DRV), 3.1's numbers; all ten read by
  3.1's VER.DLL
- [x] boot case ver: parts s, 3, x, tree286, tree386, mods, fonts,
  fontsnew all pass
- [x] ARCHITECTURE.md 20, this note; TESTING.md, ARCHITECTURE.md 6 and 19
  updated; tests/lzexpand/lztree.sh uses the tree's own VER.DLL now

## How the language table was made

`tests/ver/vrp.c` section `language` calls VerLanguageName for every
identifier 0-FFFFh on 3.1 and logs each one whose name is not the one
of identifier 0001h ("Unknown Language"): 45 names. shell/ver/ver.c's
`langs[]` is that log, typed in. The names are the SDK's list except two
cases ("Croato-Serbian (latin)", "Serbo-Croatian (cyrillic)").

## Test infrastructure added

- tests/refrun.sh: `REFRUN_DDRIVE=DIR` mounts a copy of DIR as D:.
- tests/dropin.sh: `DROPIN_DDRIVE=DIR` (D: after the clock is set) and
  `DROPIN_TREEDIR=NAME` (the -T tree at C:\NAME instead of C:\W16; ver
  uses WINDOWS so that its log equals 3.1's line for line).
- `VRUN_X=1 tests/ver/vrun.sh`: DOSBox-X, the only emulator here that
  keeps read-only attributes (VIF_WRITEPROT); it does let read-only files
  be deleted, unlike DOS.

## Found on the way (not VER's): both fixed 2026-11-18

- KERNEL: 3.1's GetModuleHandle("VUSEX.DLL") and
  GetModuleHandle("C:\\any\\VUSEX.DLL") find a module named XUSE loaded
  from VUSEX.DLL (file-name match, directory ignored); "VUSEX" and
  "XUSE.DLL" find nothing. w16's mapped the base name to a module name.
  Reproduce: `VRUN_ARG=k tests/ver/vrun.sh OUT ms`. Fixed: probe TK60
  (tests/apps/tk60.c, in make test) has 3.1's whole rule, the same under
  KRNL286 and KRNL386 in both modes; ARCHITECTURE.md 9.2 ("Finding
  loaded modules by name"):
  - GetModuleHandle: the whole string against the module names
    (regardless of case), then against the loaded modules' file names (a
    file name that begins the string's last component: "TKFA.DLLX" finds
    TKFA.DLL's module; "TKFA" does not; directories ignored).
  - LoadLibrary/LoadModule/WinExec: the file names first, then the base
    name (last component up to its first dot) against the module names.
  - A file whose module name is already loaded is loaded as a second
    module of that name (w16 used to give the loaded one).
  - LoadLibrary("") and LoadModule("") write the word 2 at the string's
    offset 2.
- KERNEL: Microsoft's VER.DLL on w16's KRNL286/KRNL386
  (`tests/ver/vtree.sh OUT ms 286`): GetFileResource(..., 0, n, buf)
  changed the caller's `n` (it came back as 25632 = 6420h). Cause: VRP
  keeps `n` in SI:DI; VER calls KERNEL's IsROMModule (323, taken with
  GetProcAddress at start-up), which removes 6 bytes of arguments on 3.1
  but removed 2 on w16, so VER's epilogue popped the file name's offset
  (6420h) into SI. Found with `[w16] trace=` (it records SI and DI now)
  and `tracefrom=VER`; 3.1's argument size measured with TK31 (it now
  logs "SP moved" for each export that does not remove exactly what it
  was passed). Fixed: IsROMModule and IsROMFile are RETF 6 in
  kernel/khook.asm. The ver boot case's tree286 and tree386 parts now run
  Microsoft's VER.DLL on w16's tree (against 3.1's log) and w16's VER
  against it.
- OpenWatcom's wrc VERSIONINFO defect (ARCHITECTURE.md 20.5): any future
  module must take its version block from tools/mkver.py.

## Next steps (if continuing)

- A shared-Windows setup (Setup /N) to see where VerFindFile puts shared
  files and how VER tells.
- DRIVERS.CPL's Add (an OEMSETUP.INF disk) and 3.1 Setup as further real
  callers; WfW 3.11's VER.DLL and its WINFILE Properties (VerQueryValue,
  VerLanguageName).

## Full run (installation-05, 2026-11-16, 1c13991)

make test: pass. Boot test: 116 ok, 4 FAIL; ver passes (every part).
- lzexpand: part quirks only, as before (lzexpand-handover.md).
- ddeml: part conn (w16-ms: conversation handles and TERMINATE order);
  fails the same on master 7efdc85 (run w16-ver-base), not this work.
- taskman: part tree, 21-dosesc.png; passed on a rerun (timing).
- soundtime: SN1 segment 44 11.2 ms long against a 9.27 limit, failing
  every time in the run directory w16-ver and passing every time in
  w16-ver-base (master) and w16-ver-tst (this branch, ea1cd74): the
  result depends on the run directory's path (DOSBox-X's emulated timing
  is otherwise deterministic here), not on the code. Worth a look by
  whoever owns tests/soundprobe.sh.
After the merge of master (README.TXT moved to assets/), README.TXT
lists VER.DLL among w16's files and no longer among those to copy from
Windows 3.1; dropin.sh and refrun.sh no longer leave an empty autoexec
line without a D: drive (ea1cd74); ver@tree286 and ver@x rerun: ok.
