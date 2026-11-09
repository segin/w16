#!/bin/sh
# Clock under real mouse and keyboard input (tests/inputrun.sh: DOSBox on
# Xvfb, xdotool), once with Windows 3.1's CLOCK.EXE and once with w16's,
# each as the shell of the reference install (Microsoft's KERNEL), with
# tests/scripts/input/clock.txt. Checks that both runs end (Clock closed
# by Alt+F4 ends Windows) and that CLOCK.INI comes out the same; prints
# how many pixels each screenshot differs by (the time is not held here,
# so the faces differ by the hands and digits; see the shots).
#   usage: tests/clockinput.sh [OUTDIR]   (default build/ref/clockinput/<date-time>)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build/ref/clockinput/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
make -C "$root" -s build/dos/CLOCK.EXE >/dev/null
"$root/tests/inputrun.sh" -t 90 "$root/tests/scripts/input/clock.txt" "$out/orig" -r \
    -i 'boot:shell=CLOCK.EXE' > "$out/orig.log" 2>&1 || true
"$root/tests/inputrun.sh" -t 90 "$root/tests/scripts/input/clock.txt" "$out/w16" -r \
    -f "$root/build/dos/CLOCK.EXE=WINDOWS/CLOCK.EXE" -i 'boot:shell=CLOCK.EXE' \
    > "$out/w16.log" 2>&1 || true
status=0
for who in orig w16; do
    grep -q WINEXIT "$out/$who/work/c/OUT.TXT" 2>/dev/null || { echo "$who: Windows did not exit"; status=1; }
done
cmp -s "$out/orig/work/c/WINDOWS/CLOCK.INI" "$out/w16/work/c/WINDOWS/CLOCK.INI" ||
    { echo "CLOCK.INI differs"; status=1; }
for f in "$out"/orig/*.png; do
    s=$(basename "$f" .png)
    [ -f "$out/w16/$s.png" ] || { echo "$s: missing"; status=1; continue; }
    px=$(magick compare -metric AE "$f" "$out/w16/$s.png" "$out/$s-diff.png" 2>&1 | cut -d' ' -f1)
    echo "$s: $px pixels differ"
done
echo "results: $out ($( [ $status = 0 ] && echo ok || echo FAIL))"
exit $status
