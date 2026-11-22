#!/bin/sh
# Windows Help regression against the original (ARCHITECTURE.md 15.10):
# each scenario tests/scripts/winhelp/WS*.txt (a SNAP.EXE script, see
# tests/whobs.sh) runs once with Microsoft's WINHELP.EXE and once with
# w16's (build/dos/WINHELP.EXE over C:\WINDOWS\WINHELP.EXE) on the
# reference Windows 3.1, over the reference install's own help files.
# Afterwards WIN.INI, WINHELP.BMK and the files named on "; get:" lines
# must be byte-identical (unless "; nocmp: NAME"), and no screenshot may
# differ by more pixels than tests/scripts/winhelp.baseline records
# ("scenario/SHOT count"; above fails, below is reported). A scenario's
# "; mask: l,t,r,b" lines name rectangles painted black on both sides
# before comparing (w16's own branding: "w16 Help" where 3.1 says
# "Windows Help"). WH_BASELINE=update rewrites the baseline.
#   usage: tests/whscenarios.sh [scenario...]    (default: all)
# Results go to build/ref/whscen/<date-time>/ (WHSCEN_OUT=DIR: there).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=${WHSCEN_OUT:-$root/build/ref/whscen/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
sdir=$root/tests/scripts/winhelp
names=${*:-$(cd "$sdir" && ls WS*.txt 2>/dev/null | sed 's/\.txt$//')}
status=0
base=$root/tests/scripts/winhelp.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
for n in $names; do
    script=$sdir/$n.txt
    result=ok
    if grep -q '^; \(hlp\|owhlp\):' "$script" &&
       { ! "$root/tests/winhelp/mkhlp.sh" w16test /dev/null 2>/dev/null ||
         [ ! -d "${WATCOM:-/opt/watcom}/binw" ]; }; then
        echo "== $n: skipped (no OpenWatcom: whc and its help files)"
        continue
    fi
    "$root/tests/whobs.sh" "$script" "$out/$n/orig" > /dev/null &
    WHOBS_EXE=$root/build/dos/WINHELP.EXE "$root/tests/whobs.sh" "$script" "$out/$n/w16" > /dev/null &
    wait
    nocmp=$(sed -n 's/^; nocmp: *//p' "$script" | tr '\n' ' ')
    for b in WIN.INI WINHELP.BMK $(sed -n 's/^; get: *//p' "$script" | xargs -n1 basename 2>/dev/null); do
        case " $nocmp " in *" $b "*) continue ;; esac
        [ -f "$out/$n/orig/$b" ] || continue
        if ! cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" 2>/dev/null; then
            result=FAIL; echo "  $n: $b differs"
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
            [ "${WH_BASELINE:-}" = update ] || result=FAIL
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
if [ "${WH_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
