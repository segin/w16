#!/bin/sh
# File Manager regression against the original: each scenario in
# tests/scripts/winfile/WF*.txt (a SNAP.EXE script that starts "run
# WINFILE.EXE", see tests/wfobs.sh) runs once with the original File
# Manager and once with w16's (build/dos/WINFILE.EXE, over
# C:\WINDOWS\WINFILE.EXE) on the reference Windows 3.1. Afterwards the
# WINFILE.INI each left must be the same (WF_INICMP=0: not compared), the
# files named on "; get:" lines byte-identical, and no screenshot may
# differ by more pixels than tests/scripts/winfile.baseline records
# ("scenario/SHOT count"; above fails, below is reported).
# WF_BASELINE=update rewrites the baseline from this run.
#   usage: tests/wfscenarios.sh [scenario...]    (default: all)
# Results go to build/ref/wfscen/<date-time>/ (WFSCEN_OUT=DIR: there).
# w16's File Manager runs with Microsoft's strip of pictures linked in
# (tests/wfmsres.sh, a test-only combination, ARCHITECTURE.md 3), so that
# the screens compare w16's drawing code, not its art. A scenario's
# "; mask: l,t,r,b ..." lines name rectangles painted black on both sides
# before comparing (what differs between two runs of 3.1 itself).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=${WFSCEN_OUT:-$root/build/ref/wfscen/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out/msres"
"$root/tests/wfmsres.sh" "$out/msres/WINFILE.EXE"
names=${*:-$(cd "$root/tests/scripts/winfile" && ls WF*.txt | sed 's/\.txt$//')}
status=0
base=$root/tests/scripts/winfile.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
for n in $names; do
    script=$root/tests/scripts/winfile/$n.txt
    "$root/tests/wfobs.sh" "$script" "$out/$n/orig" > /dev/null &
    WFOBS_EXE=$out/msres/WINFILE.EXE "$root/tests/wfobs.sh" "$script" "$out/$n/w16" > /dev/null &
    wait
    result=ok
    if [ "${WF_INICMP:-1}" = 1 ] && [ -f "$out/$n/orig/WINFILE.INI" ]; then
        if ! cmp -s "$out/$n/orig/WINFILE.INI" "$out/$n/w16/WINFILE.INI" 2>/dev/null; then
            result=FAIL; echo "  $n: WINFILE.INI differs"
            diff "$out/$n/orig/WINFILE.INI" "$out/$n/w16/WINFILE.INI" | head -10 | sed 's/^/    /' || :
        fi
    fi
    for f in $(sed -n 's/^; get: *//p' "$script"); do
        b=$(basename "$f")
        [ -f "$out/$n/orig/$b" ] || continue
        cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" 2>/dev/null || { result=FAIL; echo "  $n: $b differs"; }
    done
    masks=$(sed -n 's/^; mask: *//p' "$script" | tr '\n' ' ')
    draw=
    for m in $masks; do
        draw="$draw -draw \"rectangle $(echo "$m" | tr ',' ' ' | awk '{print $1","$2" "$3","$4}')\""
    done
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$s.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $s missing"; continue
        fi
        a=$f b=$out/$n/w16/$s.BMP
        if [ -n "$draw" ]; then
            eval magick "\"$f\"" -fill black $draw "\"$out/$n/$s-a.png\""
            eval magick "\"$out/$n/w16/$s.BMP\"" -fill black $draw "\"$out/$n/$s-b.png\""
            a=$out/$n/$s-a.png b=$out/$n/$s-b.png
        fi
        px=$(magick compare -metric AE "$a" "$b" "$out/$n/$s-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $s has no baseline ($px pixels differ)"
            [ "${WF_BASELINE:-}" = update ] || result=FAIL
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
if [ "${WF_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
