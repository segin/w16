#!/bin/sh
# SHELL.DLL under real mouse and keyboard input (tests/inputrun.sh: xdotool
# on the virtual display, through DOSBox's mouse and keyboard and the
# drivers): each script runs on the reference Windows 3.1 with Microsoft's
# SHELL.DLL and with w16's, everything else Microsoft's, and the shots
# are compared. Scripts: tests/scripts/input/shabout.txt (About boxes of
# Program Manager and Notepad), shdrop.txt (a file dragged from File
# Manager into a Program Manager group). Allowed pixel differences are in
# tests/scripts/shell.baseline as "input-SCRIPT/SHOT count" (w16's
# About-box text and picture; the free memory figure).
#   usage: tests/shellinput.sh [-o DIR] [-b SECS] [script...]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= boot=40
while getopts o:b: opt; do
    case $opt in
    o) out=$OPTARG ;;
    b) boot=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -n "$out" ] || out=$root/build/shellinput-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
base=$root/tests/scripts/shell.baseline
status=0
for s in ${*:-shabout shdrop}; do
    scr=$root/tests/scripts/input/$s.txt
    "$root/tests/inputrun.sh" -b "$boot" -t 160 "$scr" "$out/$s-ref" -r > "$out/$s-ref.txt" 2>&1 || true
    "$root/tests/inputrun.sh" -b "$boot" -t 160 "$scr" "$out/$s-w16" -r \
        -f "$root/build/dos/SHELL.DLL=WINDOWS/SYSTEM/SHELL.DLL" > "$out/$s-w16.txt" 2>&1 || true
    result=ok
    for f in "$out/$s-ref"/*.png; do
        [ -f "$f" ] || continue
        n=$(basename "$f" .png)
        px=$(magick compare -metric AE "$f" "$out/$s-w16/$n.png" "$out/$s-$n-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        allowed=$(awk -v k="input-$s/$n" '$1 == k { print $2 }' "$base")
        if [ "$px" -gt "${allowed:-0}" ]; then
            result=FAIL
            echo "  $s: $n differs by $px pixels (allowed ${allowed:-0})"
        fi
        echo "  $s/$n $px"
    done
    echo "== $s: $result"
    [ $result = ok ] || status=1
done
echo "shellinput: results in $out"
exit $status
