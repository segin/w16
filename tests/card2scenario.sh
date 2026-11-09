#!/bin/sh
# Run a SNAP2 script (tests/scripts/cf2/NAME.txt) on the reference
# Windows/386 2.11 against the original Windows 2.x Cardfile and against
# the 2.x build of w16's (build/win2/CARDFILE.EXE), each in its own
# session. Screenshots (.DDB) are converted to PNG; card files saved in
# the Windows directory (C:\WIN386\*.CRD named in the script) are copied
# back and compared byte for byte.
#   usage: tests/card2scenario.sh NAME     output: build/ref/cf2/NAME/{orig,w16}
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
n=$1
status=0
for who in orig w16; do
    out=$root/build/ref/cf2/$n/$who
    rm -rf "$out"; mkdir -p "$out"
    if [ $who = orig ]; then
        sed 's/$/\r/' "$root/tests/scripts/cf2/$n.txt" > "$out/SNAP.TXT"
        prog=
    else
        sed 's/^run CARDFILE.EXE/run C:\\CARDFILE.EXE/; s/$/\r/' "$root/tests/scripts/cf2/$n.txt" > "$out/SNAP.TXT"
        prog=$root/build/win2/CARDFILE.EXE
    fi
    shots=$(grep -o 'C:\\[A-Z0-9]*\.DDB' "$root/tests/scripts/cf2/$n.txt" | sed 's/C:\\//' | tr '\n' ' ')
    crds=$(grep -o '^type [A-Z0-9]*\.CRD' "$root/tests/scripts/cf2/$n.txt" | sed 's/^type /WIN386\//' | tr '\n' ' ')
    W386_TIMEOUT=${W386_TIMEOUT:-150} W386_OUT=$out W386_GET="$shots $crds" \
        "$root/tests/w386run.sh" "$root/build/dos/SNAP2.EXE" $prog "$out/SNAP.TXT" > "$out/log.txt" 2>&1 || true
    for f in "$out"/*.DDB; do
        [ -f "$f" ] && python3 -I "$root/tools/ddb2png.py" "$f" "${f%.DDB}.png"
    done
done
o=$root/build/ref/cf2/$n/orig w=$root/build/ref/cf2/$n/w16
for f in "$o"/*.CRD; do
    [ -f "$f" ] || continue
    b=$(basename "$f")
    if cmp -s "$f" "$w/$b"; then echo "$b: same"; else echo "$b: differs"; status=1; fi
done
for f in "$o"/*.png; do
    [ -f "$f" ] || continue
    b=$(basename "$f")
    [ -f "$w/$b" ] || { echo "$b: missing"; continue; }
    echo "$b: $(magick compare -metric AE "$f" "$w/$b" "$w/${b%.png}-diff.png" 2>&1 | cut -d' ' -f1) pixels"
done
echo "output: $root/build/ref/cf2/$n"
exit $status
