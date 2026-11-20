#!/bin/sh
# Run TK64 (tests/apps/tk64.c, KERNEL's FileCdr) and TK65
# (tests/apps/tk65.c, GetProcAddress and imports by name) on the reference
# Windows 3.1: KRNL386 in Standard and 386 Enhanced mode, and KRNL286 in
# Standard mode (REFRUN_KRNL286=1), with TK64A.EXE beside TK64 and TK65's
# DLLs in the Windows directory (tests/refrun.sh). Logs:
# OUTDIR/TKnn-win31-S.log, -3.log, -286.log.
#   usage: tests/tk64ref.sh OUTDIR [MODES] [PROBES]
#          (MODES: default "S 3 286"; PROBES: default "TK64 TK64M TK64O
#          TK64P TK65": TK64x.EXE are copies of TK64.EXE, one case each)
# Then: tools/obsref.py OUTDIR/TK64-win31-S.log OUTDIR/TK64-win31-3.log \
#           > tests/apps/tk64ref.h          (and the same for TK65)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
d=$root/build/dos
mkdir -p "$out"
dlls="$d/TK65D.DLL=WINDOWS/TK65D.DLL"
for i in 1 2 3 4 5 6 7 8 9; do
    dlls="$dlls $d/TK65I$i.DLL=WINDOWS/TK65I$i.DLL"
done
REFRUN_FILES="$d/TK64A.EXE $dlls"
export REFRUN_FILES
for p in ${3:-TK64 TK64M TK64O TK64P TK65}; do
    for mode in ${2:-S 3 286}; do
        case $mode in
        286) REFRUN_KRNL286=1 "$root/tests/refrun.sh" "$d/$p.EXE" /S > "$out/$p-win31-286.log" 2>&1 || true ;;
        *) "$root/tests/refrun.sh" "$d/$p.EXE" /$mode > "$out/$p-win31-$mode.log" 2>&1 || true ;;
        esac
    done
done
