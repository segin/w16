#!/bin/sh
# Run TK62 (tests/apps/tk62.c, the KERNEL audit) on the reference Windows
# 3.1: KRNL386 in Standard and 386 Enhanced mode, and KRNL286 in Standard
# mode (REFRUN_KRNL286=1), with TK62A.EXE beside it and TKENT.DLL and
# TKENT2.DLL in the Windows directory (tests/refrun.sh). Logs:
# OUTDIR/TK62-win31-S.log, -3.log, -286.log.
#   usage: tests/tk62ref.sh OUTDIR
# Then: tools/obsref.py OUTDIR/TK62-win31-S.log OUTDIR/TK62-win31-3.log \
#           > tests/apps/tk62ref.h
#       tools/obsref.py --k286 ... OUTDIR/TK62-win31-286.log > tests/apps/tk62ref286.h
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
d=$root/build/dos
mkdir -p "$out"
REFRUN_FILES="$d/TK62A.EXE $d/TKENT.DLL=WINDOWS/TKENT.DLL $d/TKENT2.DLL=WINDOWS/TKENT2.DLL"
export REFRUN_FILES
for mode in S 3; do
    "$root/tests/refrun.sh" "$d/TK62.EXE" /$mode > "$out/TK62-win31-$mode.log" 2>&1 || true
done
REFRUN_KRNL286=1 "$root/tests/refrun.sh" "$d/TK62.EXE" /S > "$out/TK62-win31-286.log" 2>&1 || true
