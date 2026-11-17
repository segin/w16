#!/bin/sh
# The installable-driver probe (ARCHITECTURE.md 10.7): DRP.EXE (tests/drv/
# drp.c) as the shell of a scratch copy of the reference Windows 3.1
# (Microsoft's KRNL286), with its driver DRT.DLL (tests/drv/drt.c) in
# WINDOWS\SYSTEM, on Microsoft's USER and on w16's (plain); the same with
# DRT.DLL also in SYSTEM.INI's [boot] drivers= (boot: what a boot driver
# gets); and under KRNL386 (k386: Microsoft's KRNL386 and USER against
# w16's, for the probe's GlobalAlloc section). The logs (C:\DRP.LOG) must
# be identical.
#   usage: tests/drv/drun.sh [-o OUTDIR] [-m] [plain] [boot] [k386]
#          (default all three)
#     -m   Microsoft's USER only (to look at its logs)
# Exit status 0 when every comparison made agrees.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out= sides="ms w16"
while getopts o:m opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) sides=ms ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
runs=${*:-plain boot k386}
[ -n "$out" ] || out=$root/build/drun-$(date +%H%M%S)
mkdir -p "$out"
dos=$root/build/dos
for r in $runs; do
    for s in $sides; do
        set -- -f "$dos/DRP.EXE=DRP.EXE" -f "$dos/DRT.DLL=WINDOWS/SYSTEM/DRT.DLL" \
            -i 'boot:shell=C:\DRP.EXE' -c 'WIN /S' -t 120 -W DRP.LOG -o "$out/$r-$s"
        [ $s = ms ] || set -- "$@" -f "$dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE"
        [ $r != boot ] || set -- "$@" -i 'boot:drivers=mmsystem.dll drt.dll'
        if [ $r = k386 ]; then
            # KRNL386: Microsoft's, or w16's (dropin.sh -3)
            if [ $s = ms ]; then set -- -r -3 "$@"; else set -- -3 "$@"; fi
        else
            set -- -r "$@"              # Microsoft's KRNL286
        fi
        rm -rf "$out/$r-$s"
        DROPIN_WSECS=5 "$root/tests/dropin.sh" "$@" > "$out/$r-$s.txt" 2>&1 &
    done
done
wait
fails=0
for r in $runs; do
    for s in $sides; do
        f=$out/$r-$s/c/DRP.LOG
        if [ -f "$f" ]; then tr -d '\r' < "$f" > "$out/$r-$s.log"; else : > "$out/$r-$s.log"; fi
    done
    [ "$sides" = ms ] && { echo "$r: $out/$r-ms.log"; continue; }
    if [ ! -s "$out/$r-ms.log" ]; then
        echo "FAIL $r: no log from Microsoft's USER"; fails=$((fails + 1))
    elif cmp -s "$out/$r-ms.log" "$out/$r-w16.log"; then
        echo "ok   $r: identical ($(wc -l < "$out/$r-ms.log") lines)"
    else
        echo "FAIL $r: differs:"
        diff "$out/$r-ms.log" "$out/$r-w16.log" | head -20
        fails=$((fails + 1))
    fi
done
exit $fails
