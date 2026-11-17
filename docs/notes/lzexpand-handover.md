# LZEXPAND handover (w16's LZEXPAND.DLL)

Task: w16's own LZEXPAND.DLL, a drop-in for 3.1's SYSTEM\LZEXPAND.DLL
(module LZEXPAND, 12 exported names), every behaviour probed black-box
on 3.1; probe compared against Microsoft's DLL; boot case; docs.
ARCHITECTURE.md chapter 19 has the observations, the design and the
results; this note is the working state.

Rules followed: no Microsoft binary disassembled or hex-read for code
(the NE export table and the version resource's numbers read with
tools/neinfo.py / neres.py, as for DDEML); facts from the SDK reference
(Vol. 1 ch. 9, Vol. 2) and the documented SZDD format, confirmed by the
probe; all DOSBox work on installation-05 under the run name
`w16-lzexpand` (`tools/remote-run.sh -n w16-lzexpand -g build/lz ...`,
`tools/remote-test.sh -n w16-lzexpand`); no Microsoft file committed or
used by the tests (inputs come from tools/szddenc.py at test time).

## Status

- [x] probe LZP.EXE (tests/lzexpand/lzp.c), inputs (mkinputs.py,
  tools/szddenc.py), runner (lzrun.sh), section comparison (lzcmp.py,
  lzscen.sh), tree runner (lztree.sh)
- [x] shell/lzexpand/: the DLL; exports, ordinals, module flags 8301/08,
  expected version 3.00, file version 3.10.0.103 (tools/mkver.py),
  16-byte alignment, installed as SYSTEM\LZEXPAND.DLL, in README.TXT
- [x] boot case lzexpand: parts s, 3, tree286, tree386 pass; quirks fails
  (the three known differences below)
- [x] ARCHITECTURE.md 19, this note

## Known differences (part quirks)

- eof: after a second failure on data cut inside a match, 3.1 returns
  bytes 6 places early (stale ring); w16 the right ones.
- zero: LZRead(h, buf, 0) as the first read of a new handle; 3.1 then
  expands from uninitialised buffers (literals right, matches zero, or raw
  input bytes). Would need 3.1's buffer layout; not attempted.
- long matches (18-byte matches, never written by COMPRESS): FAR18 after a
  1-byte read and BIG18 after a 2000-byte read. The pass model
  (lzexpand.h, ARCHITECTURE.md 19.2) fits all other ~170 observations;
  the 4096-boundary rule that would fix BIG18 breaks CUT5000.

## How the LZRead model was found

LZRead's pass structure was fitted with a simulator against bisection
data (probe modes z, q: the largest read that succeeds after a first
read of k bytes) and buffer dumps (y, w); the simulator is not in the
repository (it was a scratch tool), but every observation it used is
reproducible with `LZRUN_ARG=z|q|y|w tests/lzexpand/lzrun.sh OUT ms` and
the comparisons are in the probe's main log. Key facts: passes of < 1009
(match) / < 1010 (literal) bytes, one look-ahead item, failure when a
pass exceeds 1024 (only 17/18-byte matches), the first pass of a file
smaller than one 1024-byte read ends after its first item, no seek
before refills (a caller's _llseek shows through).

## Found on the way (not LZEXPAND's)

- OpenWatcom wrc's 16-bit VERSIONINFO has length words larger than its
  data; 3.1's VER.DLL returns 0 from GetFileVersionInfoSize for such a
  file (w16's DDEML.DLL too, probe mode v). LZEXPAND uses tools/mkver.py
  instead. Other modules with a VERSIONINFO (ddeml.rc, calc.rc, ...) could
  do the same; not changed here.

## Next steps (if continuing)

- zero: probe what a fresh handle's buffers hold (e.g. read through a
  0-byte-first handle with files whose matches point at every ring
  position) and model the uninitialised state.
- long matches: more bisections around 4096 output bytes with 16- and
  18-byte files to separate the pass-ending rule.
- Drive Setup-style callers through their UI (DRIVERS.CPL Add, MAIN.CPL
  Fonts Add) in a boot case.

## Full run (installation-05, 2026-11-16, a97e23a)

make test: pass. Boot test: 115 ok, 4 FAIL: lzexpand (part quirks only,
as expected), and three not touched by this work: holdbtn (scroll-arrow
repeat timing) and ddeml (tree286, the "Progman" atom), both failing
before this branch (ddeml-handover.md), and trails (part timed: one
run-down 439 ms against 440-660 expected, a timing margin under load).
