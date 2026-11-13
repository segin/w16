#!/bin/sh
# Run a SNAP2 script (tests/scripts/pm2/NAME.txt) against the Windows 2.x
# build of Program Manager (build/win2/PROGMAN.EXE) on the reference
# Windows/386 2.11, with the Windows 3.1 reference groups installed.
# Screenshots (.DDB) are converted to PNG; MAIN.GRP is copied back.
#   usage: tests/pm2scenario.sh NAME        output: build/ref/pm2/NAME/
# Commands are posted by ID ("cmd 100" = File > New), since SNAP2 cannot
# open menus on Windows 2.x yet. Lines ";restrict Key=Value" go to
# PROGMAN.INI [Restrictions].
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
n=$1
out=$root/build/ref/pm2/$n
mkdir -p "$out"
sed 's/$/\r/' "$root/tests/scripts/pm2/$n.txt" > "$out/SNAP.TXT"
shots=$(grep -o 'C:\\[A-Z0-9]*\.DDB' "$root/tests/scripts/pm2/$n.txt" | sed 's/C:\\//' | tr '\n' ' ')
ref=$root/.tools/ref/win31/cdrive/WINDOWS
printf '[Settings]\r\nWindow=68 48 580 384 1\r\nOrder= 2 3 4 1\r\n\r\n[Groups]\r\nGroup1=C:\\WIN386\\MAIN.GRP\r\nGroup2=C:\\WIN386\\ACCESSOR.GRP\r\nGroup3=C:\\WIN386\\GAMES.GRP\r\nGroup4=C:\\WIN386\\STARTUP.GRP\r\n' > "$out/PROGMAN.INI"
# ";restrict Key=Value" lines: PROGMAN.INI [Restrictions]
if grep -q '^;restrict ' "$root/tests/scripts/pm2/$n.txt"; then
    printf '[Restrictions]\r\n' >> "$out/PROGMAN.INI"
    sed -n 's/^;restrict \(.*\)$/\1\r/p' "$root/tests/scripts/pm2/$n.txt" >> "$out/PROGMAN.INI"
fi
W386_TIMEOUT=${W386_TIMEOUT:-150} W386_OUT=$out W386_GET="$shots WIN386/MAIN.GRP WIN386/PROGMAN.INI" \
    "$root/tests/w386run.sh" "$root/build/dos/SNAP2.EXE" "$root/build/win2/PROGMAN.EXE" \
    "$out/SNAP.TXT" "$out/PROGMAN.INI=WIN386/PROGMAN.INI" \
    "$ref/MAIN.GRP=WIN386/MAIN.GRP" "$ref/ACCESSOR.GRP=WIN386/ACCESSOR.GRP" \
    "$ref/GAMES.GRP=WIN386/GAMES.GRP" "$ref/STARTUP.GRP=WIN386/STARTUP.GRP"
for f in "$out"/*.DDB; do
    [ -f "$f" ] && python3 -I "$root/tools/ddb2png.py" "$f" "${f%.DDB}.png"
done
echo "screenshots: $out"
