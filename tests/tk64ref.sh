#!/bin/sh
# Run TK64 (tests/apps/tk64.c, KERNEL's FileCdr) on the reference
# Windows 3.1: KRNL386 in Standard and 386 Enhanced mode, and KRNL286 in
# Standard mode (REFRUN_KRNL286=1), with TK64A.EXE beside TK64
# (tests/refrun.sh). Logs:
# OUTDIR/TKnn-win31-S.log, -3.log, -286.log.
#   usage: tests/tk64ref.sh OUTDIR [MODES] [PROBES]
#          (MODES: default "S 3 286"; PROBES: default "TK64 TK64M TK64O
#          TK64P": TK64x.EXE are copies of TK64.EXE, one case each)
# Then: tools/obsref.py OUTDIR/TK64-win31-S.log OUTDIR/TK64-win31-3.log \
#           (and the TK64M, O, P logs) > tests/apps/tk64ref.h
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
d=$root/build/dos
mkdir -p "$out"
REFRUN_FILES="$d/TK64A.EXE"
export REFRUN_FILES
for p in ${3:-TK64 TK64M TK64O TK64P}; do
    for mode in ${2:-S 3 286}; do
        case $mode in
        286) REFRUN_KRNL286=1 "$root/tests/refrun.sh" "$d/$p.EXE" /S > "$out/$p-win31-286.log" 2>&1 || true ;;
        *) "$root/tests/refrun.sh" "$d/$p.EXE" /$mode > "$out/$p-win31-$mode.log" 2>&1 || true ;;
        esac
    done
done
