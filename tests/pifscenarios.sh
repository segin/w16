#!/bin/sh
# PIF Editor regression against the original (ARCHITECTURE.md 15.11): each
# scenario in tests/scripts/pifedit/PF*.txt (a SNAP.EXE script, see
# tests/pifobs.sh; "; mode: /3" for 386 Enhanced mode) runs once with
# Microsoft's PIFEDIT.EXE and once with w16's (build/dos/PIFEDIT.EXE over
# C:\WINDOWS\PIFEDIT.EXE) on the reference Windows 3.1. Afterwards WIN.INI
# and the files named on "; get:" lines (the saved .PIFs) must be
# byte-identical, and no screenshot may differ by more pixels than
# tests/scripts/pifedit.baseline records ("scenario/SHOT count"; above
# fails, below is reported). A scenario's "; mask: l,t,r,b" lines name
# rectangles painted black on both sides before comparing.
# PIF_BASELINE=update rewrites the baseline from this run.
#   usage: tests/pifscenarios.sh [scenario...]    (default: all)
# Results go to build/ref/pifscen/<date-time>/ (PIFSCEN_OUT=DIR: there).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=${PIFSCEN_OUT:-$root/build/ref/pifscen/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
sdir=$root/tests/scripts/pifedit
names=${*:-$(cd "$sdir" && ls PF*.txt | sed 's/\.txt$//')}
status=0
base=$root/tests/scripts/pifedit.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
for n in $names; do
    script=$sdir/$n.txt
    result=ok
    "$root/tests/pifobs.sh" "$script" "$out/$n/orig" > /dev/null &
    PIFOBS_EXE=$root/build/dos/PIFEDIT.EXE "$root/tests/pifobs.sh" "$script" "$out/$n/w16" > /dev/null &
    wait
    for b in WIN.INI $(sed -n 's/^; get: *//p' "$script" | xargs -n1 basename 2>/dev/null); do
        if [ ! -f "$out/$n/orig/$b" ]; then
            result=FAIL; echo "  $n: the original made no $b"; continue
        fi
        if ! cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" 2>/dev/null; then
            result=FAIL; echo "  $n: $b differs"
            cmp -l "$out/$n/orig/$b" "$out/$n/w16/$b" 2>&1 | head -8 | sed 's/^/    /' || :
        fi
    done
    masks=$(sed -n 's/^; mask: *//p' "$script" | tr '\n' ' ')
    draw=
    for m in $masks; do
        draw="$draw -draw \"rectangle $(echo "$m" | tr ',' ' ' | awk '{print $1","$2" "$3","$4}')\""
    done
    shots=""
    for f in "$out/$n"/orig/*.png; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .png)
        if [ ! -f "$out/$n/w16/$s.png" ]; then
            result=FAIL; echo "  $n: screenshot $s missing"; continue
        fi
        a=$f b=$out/$n/w16/$s.png
        if [ -n "$draw" ]; then
            eval magick "\"$f\"" -fill black $draw "\"$out/$n/$s-a.png\""
            eval magick "\"$b\"" -fill black $draw "\"$out/$n/$s-b.png\""
            a=$out/$n/$s-a.png b=$out/$n/$s-b.png
        fi
        px=$(magick compare -metric AE "$a" "$b" "$out/$n/$s-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $s has no baseline ($px pixels differ)"
            [ "${PIF_BASELINE:-}" = update ] || result=FAIL
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
if [ "${PIF_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
