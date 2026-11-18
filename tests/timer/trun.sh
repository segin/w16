#!/bin/sh
# Run the timer probe TPR.EXE (tests/timer/tpr.c) as the shell of a scratch
# copy of the reference Windows 3.1 (black-box observation; ARCHITECTURE.md
# 21), with Microsoft's MMSYSTEM.DLL over Microsoft's TIMER.DRV or over
# w16's (build/dos/TIMER.DRV), optionally with the logging driver TMS.DRV
# (tests/timer/tms.c) between them. OUTDIR/tpr.log is the probe's log.
#   usage: tests/timer/trun.sh OUTDIR ms|w16 [shim|plain] [/S|/3] [k286|k386]
# k386 (default): Microsoft's KRNL386; k286: Microsoft's KRNL286 (Standard
# mode only). TRUN_ARG: the sections to run (C:\TPR.ARG, tpr.c's header).
# TRUN_X=1: in DOSBox-X instead of DOSBox. TRUN_MM=w16: w16's MMSYSTEM.DLL
# and MMTASK.TSK (build/dos) instead of Microsoft's.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 drv=${2:-ms} shim=${3:-shim} mode=${4:-/S} kern=${5:-k386}
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/run" "$out/tpr.log" "$out/TPR.ARG"
set -- -f "$dos/TPR.EXE=TPR.EXE" -f "$dos/TPC.DLL=WINDOWS/SYSTEM/TPC.DLL" \
    -i 'boot:shell=C:\TPR.EXE' -c "WIN $mode" -t "${TRUN_TIMEOUT:-300}" -W TPR.LOG -o "$out/run"
[ "$drv" = w16 ] && set -- "$@" -f "$dos/TIMER.DRV=WINDOWS/SYSTEM/TIMER.DRV"
if [ "${TRUN_MM:-ms}" = w16 ]; then
    set -- "$@" -f "$dos/MMSYSTEM.DLL=WINDOWS/SYSTEM/MMSYSTEM.DLL" \
        -f "$dos/MMTASK.TSK=WINDOWS/SYSTEM/MMTASK.TSK"
fi
if [ "$shim" = shim ]; then
    set -- "$@" -f "$dos/TMS.DRV=WINDOWS/SYSTEM/TMS.DRV" -i 'drivers:timer=tms.drv'
fi
if [ -n "${TRUN_ARG:-}" ]; then
    printf '%s' "$TRUN_ARG" > "$out/TPR.ARG"
    set -- "$@" -f "$out/TPR.ARG=TPR.ARG"
fi
[ -n "${TRUN_X:-}" ] && set -- "$@" -p 386
# TRUN_SHOT=1: a screenshot at the end instead (OUTDIR/shot.png; to see a
# hang or a fault box)
if [ -n "${TRUN_SHOT:-}" ]; then
    skip=
    for a in "$@"; do
        shift
        [ -n "$skip" ] && { skip=; continue; }
        [ "$a" = -W ] && { skip=1; continue; }
        set -- "$@" "$a"
    done
    set -- "$@" -s "$out/shot.png"
fi
if [ "$kern" = k286 ]; then set -- -r "$@"; else set -- -r -3 "$@"; fi
DROPIN_WSECS=${DROPIN_WSECS:-10} "$root/tests/dropin.sh" "$@" > "$out/dropin.txt" 2>&1 || true
if [ -f "$out/run/c/TPR.LOG" ]; then
    tr -d '\r' < "$out/run/c/TPR.LOG" > "$out/tpr.log"
else
    echo "no TPR.LOG ($out/dropin.txt)" >&2
    exit 3
fi
