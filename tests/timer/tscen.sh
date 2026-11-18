#!/bin/sh
# The timer probe on Microsoft's MMSYSTEM over Microsoft's TIMER.DRV and
# over w16's (ARCHITECTURE.md 21): TPR.EXE (tests/timer/tpr.c) as the
# shell of the reference 3.1, plain (the timings as an application sees
# them) and with the logging driver TMS.DRV between MMSYSTEM and the timer
# driver (every message, its parameters and its result), four runs at once
# (tests/timer/trun.sh); each pair compared by tests/timer/tcmp.py.
#   usage: tests/timer/tscen.sh [-o OUTDIR] [/S|/3]
# /3: 386 Enhanced mode, compared with tcmp.py -3 (its documented
# differences shown, not failed: docs/notes/timer-handover.md).
# TRUN_ARG: the probe's sections (tpr.c's header; default all).
# Exit status 0 when both comparisons agree.
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
mode=${1:-/S}
[ -n "$out" ] || out=$root/build/tscen-$(date +%H%M%S)
mkdir -p "$out"
for d in ms w16; do
    for k in plain shim; do
        "$root/tests/timer/trun.sh" "$out/$d-$k" $d $k "$mode" > "$out/$d-$k.txt" 2>&1 &
    done
done
wait
fails=0
opt=
[ "$mode" = /3 ] && opt=-3
for k in plain shim; do
    if [ ! -s "$out/ms-$k/tpr.log" ] || ! grep -q '^TPR end' "$out/ms-$k/tpr.log"; then
        echo "FAIL $k: no complete log from Microsoft's TIMER.DRV ($out/ms-$k)"; fails=$((fails + 1)); continue
    fi
    if [ ! -s "$out/w16-$k/tpr.log" ] || ! grep -q '^TPR end' "$out/w16-$k/tpr.log"; then
        echo "FAIL $k: no complete log from w16's TIMER.DRV ($out/w16-$k)"; fails=$((fails + 1)); continue
    fi
    # shellcheck disable=SC2086
    if python3 "$root/tests/timer/tcmp.py" $opt "$out/ms-$k/tpr.log" "$out/w16-$k/tpr.log" > "$out/cmp-$k.txt" 2>&1; then
        echo "ok   $k: $(tr '\n' ';' < "$out/cmp-$k.txt" | sed 's/  */ /g; s/;$//')"
    else
        echo "FAIL $k: $(grep -m1 FAIL "$out/cmp-$k.txt") ($out/cmp-$k.txt)"; fails=$((fails + 1))
    fi
done
exit $fails
