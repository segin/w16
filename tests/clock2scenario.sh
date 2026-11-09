#!/bin/sh
# Run a SNAP2 script (tests/scripts/clock2/NAME.txt) against the Windows
# 2.x build of Clock (build/win2/CLOCK.EXE) on the reference Windows/386
# 2.11 (tests/w386run.sh). Screenshots (.DDB) are converted to PNG and
# CLOCK.INI is copied back; the script checks that the session got to the
# end, that the screenshots were taken and that CLOCK.INI was written as
# Windows 3.1's Clock writes it.
#   usage: tests/clock2scenario.sh [NAME]     (default basic)
#   output: build/ref/clock2/NAME/
# Windows/386 2.11 had its own Clock (version 2.1, a different design);
# this build carries the 3.1 behaviour, so there is no pixel reference.
# Its screenshot is taken too (ORIG.png) for a look side by side.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
n=${1:-basic}
out=$root/build/ref/clock2/$n
rm -rf "$out"; mkdir -p "$out"
make -C "$root" -s build/win2/CLOCK.EXE build/dos/SNAP2.EXE >/dev/null
sed 's/$/\r/' "$root/tests/scripts/clock2/$n.txt" > "$out/SNAP.TXT"
shots=$(grep -o 'C:\\[A-Z0-9]*\.DDB' "$root/tests/scripts/clock2/$n.txt" | sed 's/C:\\//' | tr '\n' ' ')
W386_TIMEOUT=${W386_TIMEOUT:-200} W386_OUT=$out W386_GET="$shots WIN386/CLOCK.INI" \
    "$root/tests/w386run.sh" "$root/build/dos/SNAP2.EXE" "$root/build/win2/CLOCK.EXE" \
    "$out/SNAP.TXT" > "$out/log.txt" 2>&1 || true
# the original 2.1 Clock, for comparison by eye
printf '%s\r\n' 'setdate 03/14/1993' 'run CLOCK.EXE' 'wait 3000' 'freeze 10:09:30.50 2000' \
    'snap C:\ORIG.DDB' > "$out/ORIG.TXT"
mkdir -p "$out/orig"
W386_TIMEOUT=${W386_TIMEOUT:-200} W386_OUT=$out/orig W386_GET="ORIG.DDB" \
    "$root/tests/w386run.sh" "$root/build/dos/SNAP2.EXE" "$out/ORIG.TXT=SNAP.TXT" \
    > "$out/orig/log.txt" 2>&1 || true
[ -f "$out/orig/ORIG.DDB" ] && mv "$out/orig/ORIG.DDB" "$out/"
status=0
for f in "$out"/*.DDB; do
    [ -f "$f" ] && python3 -I "$root/tools/ddb2png.py" "$f" "${f%.DDB}.png"
done
for s in $shots; do
    [ -f "$out/$s" ] || { echo "missing screenshot $s"; status=1; }
done
grep -q 'SNAP done' "$out/log.txt" || { echo "the script did not finish"; status=1; }
if [ -f "$out/CLOCK.INI" ]; then
    # the scenario ends digital with seconds, date and title, moved to
    # 40,30 300x200
    printf '[Clock]\r\nMaximized=0\r\nOptions=0,0,0,0,0,0\r\nPosition=40,30,340,230\r\n' > "$out/expect.ini"
    cmp -s "$out/CLOCK.INI" "$out/expect.ini" || { echo "CLOCK.INI differs from expect.ini"; status=1; }
else
    echo "no CLOCK.INI"; status=1
fi
echo "screenshots: $out ($( [ $status = 0 ] && echo ok || echo FAIL))"
exit $status
