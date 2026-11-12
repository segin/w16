#!/bin/sh
# The mouse driver's interface, w16's against Windows 3.1's (drivers/mouse/
# README.md): MD1.EXE (tests/apps/md1.c) runs as the shell and records
# Inquire's MOUSEINFO, MouseGetIntVect, CplApplet, how many bytes each
# entry point takes off the stack, and every call the driver makes to the
# event procedure while tests/scripts/input/mouseprobe.txt moves and
# clicks (tests/inputrun.sh: xdotool into DOSBox's emulated PS/2 mouse).
# Three runs, one after the other, on Microsoft's KRNL286, DOSX, USER:
#   ms     Windows 3.1's MOUSE.DRV
#   w16    w16's (build/dos/MOUSE.DRV)
#   swap   Windows 3.1's with WIN.INI SwapMouseButtons=1: the driver
#          reports the buttons as they are (USER swaps them), so its
#          events must be the ms run's
# The TK.LOGs (OBS lines, except the "~" ones, and the event list) must be
# identical.
#   usage: tests/mouseprobe.sh [-o DIR] [-x]
#     -x  DOSBox-X instead of DOSBox (tests/dropin.sh -p 386)
# Exit status 0 when all three agree.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= emu=
while getopts o:x opt; do
    case $opt in
    o) out=$OPTARG ;;
    x) emu="-p 386" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -n "$out" ] || out=$root/build/mouseprobe-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
scr=$root/tests/scripts/input/mouseprobe.txt
md1=$root/build/dos/MD1.EXE
drv=$root/build/dos/MOUSE.DRV
run() {
    name=$1; shift
    "$root/tests/inputrun.sh" -b 30 -t 100 "$scr" "$out/$name" -r $emu \
        -f "$md1=MD1.EXE" -i 'boot:shell=C:\MD1.EXE' -i w16:md1secs=45 "$@" \
        > "$out/$name.txt" 2>&1 || true
    tr -d '\r' < "$out/$name/work/c/WINDOWS/TK.LOG" > "$out/$name.log" 2>/dev/null || : > "$out/$name.log"
    grep -v '^OBS ~' "$out/$name.log" > "$out/$name.cmp" || true
}
run ms
run w16 -f "$drv=WINDOWS/SYSTEM/MOUSE.DRV"
run swap -i WIN.INI:windows:SwapMouseButtons=1
fails=0
grep -q '^MD1 done' "$out/ms.log" || { echo "FAIL: the run with Windows 3.1's MOUSE.DRV did not finish ($out/ms.log)"; fails=1; }
grep -q '^E ' "$out/ms.log" || { echo "FAIL: no events with Windows 3.1's MOUSE.DRV ($out/ms.log)"; fails=1; }
for k in w16 swap; do
    if cmp -s "$out/ms.cmp" "$out/$k.cmp"; then
        echo "ok   $k: the same as Windows 3.1's MOUSE.DRV ($(grep -c '^E ' "$out/$k.log") events)"
    else
        echo "FAIL $k: differs from Windows 3.1's MOUSE.DRV:"
        diff "$out/ms.cmp" "$out/$k.cmp" | head -20
        fails=1
    fi
done
echo "mouseprobe: logs in $out"
exit $fails
