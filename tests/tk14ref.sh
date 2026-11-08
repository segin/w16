#!/bin/sh
# Run the profile and file probe TK14 on the reference Windows 3.1 in
# Standard and 386 Enhanced mode (tests/refrun.sh, a scratch copy of the
# install), with PATH=C:\TKPATH;Z:\ and TEMP=C:\TKTMP, and write the logs to
# OUTDIR as TK14-win31-S.log and TK14-win31-3.log.
#   usage: tests/tk14ref.sh OUTDIR
# Then: tools/obsref.py OUTDIR/TK14-win31-S.log OUTDIR/TK14-win31-3.log \
#           > tests/apps/tk14ref.h
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
d=$root/build/dos
mkdir -p "$out"
REFRUN_PRE='MD C:\TKTMP
SET PATH=C:\TKPATH;Z:\
SET TEMP=C:\TKTMP'
export REFRUN_PRE
for mode in S 3; do
    "$root/tests/refrun.sh" "$d/TK14.EXE" /$mode > "$out/TK14-win31-$mode.log" 2>&1 || true
done
