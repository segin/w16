#!/bin/sh
# Run TK5.EXE (the DLL loading probe) with its DLLs on the reference
# Windows 3.1 in Standard and 386 Enhanced mode (tests/refrun.sh), and
# write the logs to OUTDIR as TK5-win31-S.log and TK5-win31-3.log.
#   usage: tests/tk5ref.sh OUTDIR
# Then: tools/obsref.py OUTDIR/TK5-win31-S.log OUTDIR/TK5-win31-3.log \
#           > tests/apps/tk5ref.h
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
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
    "$root/tests/refrun.sh" "$d/TK5.EXE" /$mode > "$out/TK5-win31-$mode.log" 2>&1 || true
done
