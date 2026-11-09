#!/bin/sh
# Run a SNAP2 script (tests/scripts/np2/NAME.txt) against the Windows 2.x
# build of Notepad (build/win2/NOTEPAD.EXE) on the reference Windows/386
# 2.11, with tests/scripts/np/data/SMALL.TXT on C:\. Screenshots (.DDB)
# are converted to PNG; OUT.TXT (what the script saved) is copied back.
#   usage: tests/np2scenario.sh NAME [outdir]   (default build/ref/np2/NAME)
# Commands are posted by ID ("cmd 3" = Search > Find), since SNAP2 cannot
# open menus on Windows 2.x yet; "class Notepad" makes Notepad's window
# their target.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
n=$1
out=${2:-$root/build/ref/np2/$n}
mkdir -p "$out"
sed 's/$/\r/' "$root/tests/scripts/np2/$n.txt" > "$out/SNAP.TXT"
shots=$(grep -o 'C:\\[A-Z0-9]*\.DDB' "$root/tests/scripts/np2/$n.txt" | sed 's/C:\\//' | tr '\n' ' ')
W386_TIMEOUT=${W386_TIMEOUT:-150} W386_OUT=$out W386_GET="$shots OUT.TXT" \
    "$root/tests/w386run.sh" "$root/build/dos/SNAP2.EXE" "$root/build/win2/NOTEPAD.EXE" \
    "$out/SNAP.TXT" "$root/tests/scripts/np/data/SMALL.TXT" > "$out/log.txt" 2>&1 || true
for f in "$out"/*.DDB; do
    [ -f "$f" ] && python3 -I "$root/tools/ddb2png.py" "$f" "${f%.DDB}.png"
done
echo "screenshots: $out"
