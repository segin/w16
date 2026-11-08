#!/bin/sh
# Program Manager regression against the original: each scenario in
# tests/scripts/pm/*.txt (a SNAP.EXE script that starts "run PROGMAN.EXE")
# runs once with the original Program Manager and once with w16's
# (build/dos/PROGMAN.EXE) on the reference Windows 3.1. Afterwards the
# group files and PROGMAN.INI must be byte-identical, and no screenshot may
# differ from the original by more pixels than recorded in
# tests/scripts/pm/baseline.txt ("scenario/SHOT count"). The baseline is
# what our own icon art and the known menu-bar spacing difference cost; a
# count above it fails, a count below it is reported so the baseline can
# be tightened. PM_BASELINE=update rewrites the baseline from this run.
#   usage: tests/pmscenarios.sh [scenario...]    (default: all)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/ref/scenarios
files="WINDOWS/PROGMAN.INI WINDOWS/MAIN.GRP WINDOWS/ACCESSOR.GRP WINDOWS/GAMES.GRP WINDOWS/STARTUP.GRP WINDOWS/TESTGROU.GRP WINDOWS/STUP2.GRP"
names=${*:-$(cd "$root/tests/scripts/pm" && ls *.txt | sed 's/\.txt$//')}
status=0
base=$root/tests/scripts/pm/baseline.txt
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
for n in $names; do
    for who in orig w16; do
        d=$out/$n/$who
        rm -rf "$d"; mkdir -p "$d"
        if [ $who = orig ]; then
            sed 's/$/\r/' "$root/tests/scripts/pm/$n.txt" > "$d/SNAP.TXT"
            extra=
        else
            sed 's/^run PROGMAN.EXE$/run C:\\PROGMAN.EXE/; s/$/\r/' "$root/tests/scripts/pm/$n.txt" > "$d/SNAP.TXT"
            extra=$root/build/dos/PROGMAN.EXE
        fi
        REFRUN_FILES="$d/SNAP.TXT $extra" REFRUN_OUT=$d REFRUN_GET="$files" \
            "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt"
    done
    result=ok
    for f in $files; do
        b=$(basename "$f")
        if [ -f "$out/$n/orig/$b" ] || [ -f "$out/$n/w16/$b" ]; then
            cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" || { result=FAIL; echo "  $n: $b differs"; }
        fi
    done
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$s.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $s missing"; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/$n/w16/$s.BMP" null: 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $s has no baseline ($px pixels differ)"
            [ "${PM_BASELINE:-}" = update ] || result=FAIL
        elif [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $s differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $s improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${PM_BASELINE:-}" = update ]; then
    # keep entries of scenarios not run this time
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
