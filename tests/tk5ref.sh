#!/bin/sh
# Run a DLL probe (TK5.EXE, the default, or TK11.EXE) with the test DLLs on
# the reference Windows 3.1 in Standard and 386 Enhanced mode
# (tests/refrun.sh), and write the logs to OUTDIR as NAME-win31-S.log and
# NAME-win31-3.log.
#   usage: tests/tk5ref.sh OUTDIR [TK11]
# Then: tools/obsref.py OUTDIR/TK5-win31-S.log OUTDIR/TK5-win31-3.log \
#           > tests/apps/tk5ref.h
# With REFRUN_SHELL=PROGMAN.EXE the probe starts from WIN.INI's load=
# instead of being the shell (Windows then runs until REFRUN_TIMEOUT).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
prog=${2:-TK5}
d=$root/build/dos
mkdir -p "$out"
REFRUN_PRE='SET PATH=C:\TKPATH;Z:\'
# The DLLs go to the Windows directory: TK5 runs as the shell, and when
# Windows loads the shell, the DLLs it imports are not looked for in its
# own directory (C:\); with them there, Windows waits forever.
REFRUN_FILES="$d/TKID.TPL"
for f in TKDLL.DLL TKDLB.DLL TKBAD.DLL TKNODEP.DLL TKNOD.DLL TKUSEX.DLL TKEXEM.EXE; do
    REFRUN_FILES="$REFRUN_FILES $d/$f=WINDOWS/$f"
done
export REFRUN_PRE REFRUN_FILES
for mode in S 3; do
    "$root/tests/refrun.sh" "$d/$prog.EXE" /$mode > "$out/$prog-win31-$mode.log" 2>&1 || true
done
