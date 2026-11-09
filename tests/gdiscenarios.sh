#!/bin/sh
# w16's GDI against Microsoft's: each scenario (a SNAP.EXE script,
# tests/apps/snap.c) runs on the reference Windows 3.1 once with its own
# GDI.EXE and once with build/dos/GDI.EXE copied over it (Microsoft's
# KERNEL, USER, drivers and fonts both times); every screenshot SNAP takes
# is compared pixel by pixel (ImageMagick AE count; diff images kept).
# The w16 run's screenshots are taken through w16's GDI (GetDIBits), so a
# GetDIBits bug shows up as differences too.
#   usage: tests/gdiscenarios.sh [-o DIR] [SCRIPT...]
#          (default scripts: tests/scripts/gdi/*.txt tests/scripts/pm/*.txt)
#   DIR (default build/ref/gdiscen) gets ms/NAME/ and w16/NAME/ with the
#   screenshots and logs, and NAME-SHOT-diff.png.
# Prints one line per screenshot and a summary; exits 1 if any differs.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/ref/gdiscen
if [ "${1:-}" = -o ]; then out=$2; shift 2; fi
scripts=${*:-$(ls "$root"/tests/scripts/gdi/*.txt "$root"/tests/scripts/pm/*.txt)}
gdi=$root/build/dos/GDI.EXE
mkdir -p "$out"
total=0 differ=0 status=0
for s in $scripts; do
    n=$(basename "$s" .txt)
    for who in ms w16; do
        d=$out/$who/$n
        rm -rf "$d"; mkdir -p "$d"
        sed 's/$/\r/' "$s" > "$d/SNAP.TXT"
        extra=
        [ $who = w16 ] && extra="$gdi=WINDOWS/SYSTEM/GDI.EXE"
        REFRUN_FILES="$d/SNAP.TXT $extra" REFRUN_OUT=$d \
            "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt" 2>&1 || true
    done
    for f in "$out/ms/$n"/*.BMP; do
        [ -f "$f" ] || continue
        b=$(basename "$f" .BMP)
        total=$((total + 1))
        if [ ! -f "$out/w16/$n/$b.BMP" ]; then
            echo "$n/$b: missing in the w16 run"; differ=$((differ + 1)); status=1; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/w16/$n/$b.BMP" "$out/$n-$b-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        echo "$n/$b: $px pixels differ"
        if [ "$px" != 0 ]; then differ=$((differ + 1)); status=1; fi
    done
done
echo "== $total screenshots, $differ differ"
exit $status
