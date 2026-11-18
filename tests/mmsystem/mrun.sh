#!/bin/sh
# Run the MMSYSTEM probe MMP.EXE (tests/mmsystem/mmp.c) as the shell of a
# scratch copy of the reference Windows 3.1 (black-box observation;
# ARCHITECTURE.md 23), with Microsoft's MMSYSTEM.DLL or w16's
# (build/dos/MMSYSTEM.DLL and MMTASK.TSK). OUTDIR/mmp.log is the probe's
# log.
#   usage: tests/mmsystem/mrun.sh OUTDIR ms|w16 [/S|/3] [k286|k386]
# k386 (default): Microsoft's KRNL386; k286: Microsoft's KRNL286.
# MRUN_ARG: the sections to run (C:\MMP.ARG, mmp.c's header).
# MRUN_TIMER=w16: w16's TIMER.DRV too. MRUN_SHOT=1: a screenshot at the
# end (OUTDIR/shot.png).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 mm=${2:-ms} mode=${3:-/S} kern=${4:-k386}
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/run" "$out/mmp.log" "$out/MMP.ARG"
set -- -f "$dos/MMP.EXE=MMP.EXE" -f "$dos/MMC.DLL=WINDOWS/SYSTEM/MMC.DLL" \
    -i 'boot:shell=C:\MMP.EXE' -c "WIN $mode" -t "${MRUN_TIMEOUT:-300}" -o "$out/run"
if [ "$mm" = w16 ]; then
    set -- "$@" -f "$dos/MMSYSTEM.DLL=WINDOWS/SYSTEM/MMSYSTEM.DLL" \
        -f "$dos/MMTASK.TSK=WINDOWS/SYSTEM/MMTASK.TSK"
fi
[ "${MRUN_TIMER:-ms}" = w16 ] && set -- "$@" -f "$dos/TIMER.DRV=WINDOWS/SYSTEM/TIMER.DRV"
if [ -n "${MRUN_ARG:-}" ]; then
    printf '%s' "$MRUN_ARG" > "$out/MMP.ARG"
    set -- "$@" -f "$out/MMP.ARG=MMP.ARG"
fi
if [ -n "${MRUN_SHOT:-}" ]; then
    set -- "$@" -s "$out/shot.png"
else
    set -- "$@" -W MMP.LOG
fi
if [ "$kern" = k286 ]; then set -- -r "$@"; else set -- -r -3 "$@"; fi
DROPIN_WSECS=${DROPIN_WSECS:-10} "$root/tests/dropin.sh" "$@" > "$out/dropin.txt" 2>&1 || true
if [ -f "$out/run/c/MMP.LOG" ]; then
    tr -d '\r' < "$out/run/c/MMP.LOG" > "$out/mmp.log"
else
    echo "no MMP.LOG ($out/dropin.txt)" >&2
    exit 3
fi
