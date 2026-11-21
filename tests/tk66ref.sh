#!/bin/sh
# Run TK66 (tests/apps/tk66.c, the KERNEL audit's third round) on the
# reference Windows 3.1: KRNL386 in Standard and 386 Enhanced mode, and
# KRNL286 in Standard mode (REFRUN_KRNL286=1), with the DLLs they load
# in the Windows directory (tests/refrun.sh). TK66L, S, R, F, I, N and E
# (copies of TK66.EXE, one case each) run in Windows sessions of their own.
# Logs: OUTDIR/TK66x-win31-S.log, -3.log, -286.log.
#   usage: tests/tk66ref.sh OUTDIR [MODES] [PROBES]
#          (MODES: default "S 3 286"; PROBES: default "TK66 TK66L TK66S
#          TK66R TK66F TK66I TK66N"; TK66E only when asked: 3.1 ends
#          Windows or hangs at its first DLL, TKEA.DLL, in all three runs)
# Then: tools/obsref.py OUTDIR/TK66-win31-S.log OUTDIR/TK66-win31-3.log \
#           OUTDIR/TK66L-win31-S.log ... > tests/apps/tk66ref.h  (the S and 3
#           logs of TK66 and of each copy but TK66E)
#       cat OUTDIR/TK66*-win31-286.log > OUTDIR/TK66all-win31-286.log
#       tools/obsref.py --k286 OUTDIR/TK66all-win31-286.log tests/apps/tk66ref.h \
#           > tests/apps/tk66ref286.h
# F: is mounted as a DOSBox floppy drive ("drivetype"; tests/run-tests.sh
# does the same).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
d=$root/build/dos
mkdir -p "$out"
REFRUN_FILES="$d/TKENT2.DLL=WINDOWS/TKENT2.DLL $d/TKBADNM.DLL=WINDOWS/TKBADNM.DLL $d/TKENT.DLL=WINDOWS/TKENT.DLL"
for e in A B C D E; do REFRUN_FILES="$REFRUN_FILES $d/TKE$e.DLL=WINDOWS/TKE$e.DLL"; done
export REFRUN_FILES
REFRUN_PRE="MOUNT F $(cd "$out" && pwd) -t floppy"
export REFRUN_PRE
for p in ${3:-TK66 TK66L TK66S TK66R TK66F TK66I TK66N}; do
    for mode in ${2:-S 3 286}; do
        case $mode in
        286) REFRUN_KRNL286=1 "$root/tests/refrun.sh" "$d/$p.EXE" /S > "$out/$p-win31-286.log" 2>&1 || true ;;
        *) "$root/tests/refrun.sh" "$d/$p.EXE" /$mode > "$out/$p-win31-$mode.log" 2>&1 || true ;;
        esac &
    done
done
wait
