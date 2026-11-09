#!/bin/sh
# Write regression against the original: each scenario in
# tests/scripts/write/*.txt (a SNAP.EXE script that starts "run WRITE.EXE",
# see tests/wrobs.sh) runs once with the original Write and once with
# w16's (build/dos/WRITE.EXE) on the reference Windows 3.1. Afterwards
# every .WRI the scenario saved must be the same as the original's (bytes
# the original leaves undefined excepted: tools/wri.py cmp), every other
# saved file (.TXT, .DOC) byte-identical, and no screenshot may differ by
# more pixels than tests/scripts/write.baseline records ("scenario/SHOT
# count"; above fails, below is reported). WR_BASELINE=update rewrites
# the baseline from this run. Input documents a scenario opens are named
# on its "; files:" line (paths relative to the reference WINDOWS dir).
#   usage: tests/writescenarios.sh [scenario...]    (default: all)
# Results go to build/ref/wrscen/<date-time>/.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/ref/wrscen/$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"
names=${*:-$(cd "$root/tests/scripts/write" && ls *.txt | sed 's/\.txt$//')}
status=0
base=$root/tests/scripts/write.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
ref=$root/.tools/ref/win31/cdrive/WINDOWS
for n in $names; do
    script=$root/tests/scripts/write/$n.txt
    inputs=
    for f in $(sed -n 's/^; files: *//p' "$script"); do
        mkdir -p "$out/$n/in"
        cp "$ref/$f" "$out/$n/in/"              # scratch copies only
        inputs="$inputs $out/$n/in/$(basename "$f")"
    done
    "$root/tests/wrobs.sh" "$script" "$out/$n/orig" $inputs > /dev/null
    WROBS_EXE=$root/build/dos/WRITE.EXE "$root/tests/wrobs.sh" "$script" "$out/$n/w16" $inputs > /dev/null
    result=ok
    for f in "$out/$n"/orig/*.WRI "$out/$n"/orig/*.TXT "$out/$n"/orig/*.DOC; do
        [ -f "$f" ] || continue
        b=$(basename "$f")
        [ "$b" = SNAP.TXT ] && continue         # the script itself
        if [ ! -f "$out/$n/w16/$b" ]; then
            result=FAIL; echo "  $n: $b not saved"
        elif [ "${b##*.}" = WRI ]; then
            python3 -I "$root/tools/wri.py" cmp "$f" "$out/$n/w16/$b" > "$out/$n/$b.cmp" ||
                { result=FAIL; echo "  $n: $b differs: $(cat "$out/$n/$b.cmp")"; }
        else
            cmp -s "$f" "$out/$n/w16/$b" || { result=FAIL; echo "  $n: $b differs"; }
        fi
    done
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$s.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $s missing"; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/$n/w16/$s.BMP" "$out/$n/$s-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $s has no baseline ($px pixels differ)"
            [ "${WR_BASELINE:-}" = update ] || result=FAIL
        elif [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $s differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $s improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
echo "results: $out"
if [ "${WR_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
