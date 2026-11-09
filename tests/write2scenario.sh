#!/bin/sh
# Run a SNAP2 script (tests/scripts/write2/NAME.txt) against the Windows
# 2.x build of Write (build/win2/WRITE.EXE) on the reference Windows/386
# 2.11. Screenshots (.DDB) are converted to PNG; the .WRI files the script
# names ("type C:\X.WRI", or PRACTICE.WRI, which the 2.11 install has)
# are copied back.
#   usage: tests/write2scenario.sh NAME         output: build/ref/write2/NAME/
# SNAP2 cannot open menus on Windows 2.x: commands are posted by ID
# ("cmd 4099" = File > Save), after "class MSWRITE_MENU".
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
n=$1
out=$root/build/ref/write2/$n
mkdir -p "$out"
script=$root/tests/scripts/write2/$n.txt
sed 's/$/\r/' "$script" > "$out/SNAP.TXT"
shots=$(grep -o 'C:\\[A-Z0-9]*\.DDB' "$script" | sed 's/C:\\//' | tr '\n' ' ')
docs=$(grep -o 'C:\\[A-Z0-9\\]*\.WRI' "$script" | sed 's/C:\\//; s/\\/\//g' | sort -u | tr '\n' ' ')
W386_TIMEOUT=${W386_TIMEOUT:-150} W386_OUT=$out W386_GET="$shots $docs" \
    "$root/tests/w386run.sh" "$root/build/dos/SNAP2.EXE" "$root/build/win2/WRITE.EXE" \
    "$out/SNAP.TXT"
for f in "$out"/*.DDB; do
    [ -f "$f" ] && python3 -I "$root/tools/ddb2png.py" "$f" "${f%.DDB}.png"
done
echo "results: $out"
