#!/bin/sh
# The MMSYSTEM probes on Microsoft's MMSYSTEM.DLL and on w16's
# (ARCHITECTURE.md 23), all at once, each pair compared:
#   core   MMP.EXE (tests/mmsystem/mmp.c) as the shell, Microsoft's
#          MMSYSTEM against w16's (tests/mmsystem/mrun.sh), compared by
#          tests/mmsystem/mcmp.py; also its section S alone (StackEnter
#          before there is an interrupt stack)
#   mmio   MMP.EXE's MMIO sections (FDMBE: tests/mmsystem/mmio.c) the same
#          way
#   timer  the timer probe TPR.EXE (tests/timer/tpr.c), plain and with the
#          logging driver TMS.DRV: Microsoft's MMSYSTEM over Microsoft's
#          TIMER.DRV against w16's MMSYSTEM over w16's TIMER.DRV, compared
#          by tests/timer/tcmp.py as tests/timer/tscen.sh does
#   usage: tests/mmsystem/mscen.sh [-o OUTDIR] [PART...]   (default: all)
# Exit status 0 when every comparison agrees.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=
while getopts o: opt; do
    case $opt in
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
parts=${*:-core mmio timer}
[ -n "$out" ] || out=$root/build/mscen-$(date +%H%M%S)
mkdir -p "$out"
m=$root/tests/mmsystem t=$root/tests/timer
for p in $parts; do
    case $p in
    core)
        "$m/mrun.sh" "$out/core-ms" ms > "$out/core-ms.txt" 2>&1 &
        "$m/mrun.sh" "$out/core-w16" w16 > "$out/core-w16.txt" 2>&1 &
        MRUN_ARG=S "$m/mrun.sh" "$out/pre-ms" ms > "$out/pre-ms.txt" 2>&1 &
        MRUN_ARG=S "$m/mrun.sh" "$out/pre-w16" w16 > "$out/pre-w16.txt" 2>&1 & ;;
    mmio)
        MRUN_ARG=FDMBE "$m/mrun.sh" "$out/mmio-ms" ms > "$out/mmio-ms.txt" 2>&1 &
        MRUN_ARG=FDMBE "$m/mrun.sh" "$out/mmio-w16" w16 > "$out/mmio-w16.txt" 2>&1 & ;;
    timer)
        for k in plain shim; do
            "$t/trun.sh" "$out/timer-ms-$k" ms $k > "$out/timer-ms-$k.txt" 2>&1 &
            TRUN_MM=w16 "$t/trun.sh" "$out/timer-w16-$k" w16 $k > "$out/timer-w16-$k.txt" 2>&1 &
        done ;;
    esac
done
wait
fails=0
cmp() {     # NAME TOOL A B
    if [ ! -s "$3" ] || [ ! -s "$4" ]; then
        echo "FAIL $1: no log ($3, $4)"; fails=$((fails + 1)); return
    fi
    if python3 "$2" "$3" "$4" > "$out/cmp-$1.txt" 2>&1; then
        echo "ok   $1: $(tail -n 1 "$out/cmp-$1.txt")"
    else
        echo "FAIL $1: $(grep -m1 FAIL "$out/cmp-$1.txt") ($out/cmp-$1.txt)"; fails=$((fails + 1))
    fi
}
for p in $parts; do
    case $p in
    core)
        cmp core "$m/mcmp.py" "$out/core-ms/mmp.log" "$out/core-w16/mmp.log"
        cmp prestack "$m/mcmp.py" "$out/pre-ms/mmp.log" "$out/pre-w16/mmp.log" ;;
    mmio)
        cmp mmio "$m/mcmp.py" "$out/mmio-ms/mmp.log" "$out/mmio-w16/mmp.log" ;;
    timer)
        for k in plain shim; do
            cmp "timer-$k" "$t/tcmp.py" "$out/timer-ms-$k/tpr.log" "$out/timer-w16-$k/tpr.log"
        done ;;
    esac
done
exit $fails
