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

## Found on the way (not VER's)

- KERNEL: 3.1's GetModuleHandle("VUSEX.DLL") and
  GetModuleHandle("C:\\any\\VUSEX.DLL") find a module named XUSE loaded
  from VUSEX.DLL (file-name match, directory ignored); "VUSEX" and
  "XUSE.DLL" find nothing. w16's maps the base name to a module name.
  Reproduce: `VRUN_ARG=k tests/ver/vrun.sh OUT ms`. Not changed.
- KERNEL: Microsoft's VER.DLL on w16's KRNL286/KRNL386
  (`tests/ver/vtree.sh OUT ms 286`): GetFileResource(..., 0, n, buf)
  overwrites the caller's local `n` (it came back as 25632); the probe's
  next fill then overran its 8 KB buffer and KRNL286 faulted in the
  global heap's arena code. On Microsoft's KERNEL, with w16's LZEXPAND
  too, it does not happen, so a w16 KERNEL difference. Not investigated.
- OpenWatcom's wrc VERSIONINFO defect (ARCHITECTURE.md 20.5): any future
  module must take its version block from tools/mkver.py.

## Next steps (if continuing)

- The two KERNEL findings above.
- A shared-Windows setup (Setup /N) to see where VerFindFile puts shared
  files and how VER tells.
- DRIVERS.CPL's Add (an OEMSETUP.INF disk) and 3.1 Setup as further real
  callers; WfW 3.11's VER.DLL and its WINFILE Properties (VerQueryValue,
  VerLanguageName).

## Full run

(see the end of this note once the full run is done)
