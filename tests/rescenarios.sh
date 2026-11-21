#!/bin/sh
# Registration Info Editor regression against the original (ARCHITECTURE.md
# 15.9): each scenario in tests/scripts/regedit/RG*.txt (a SNAP.EXE script,
# see tests/regobs.sh) runs once with Microsoft's REGEDIT.EXE and once with
# w16's (build/dos/REGEDIT.EXE over C:\WINDOWS\REGEDIT.EXE) on the
# reference Windows 3.1. Afterwards REG.DAT, WIN.INI and the files named on
# "; get:" lines must be byte-identical, and no screenshot may differ by
# more pixels than tests/scripts/regedit.baseline records ("scenario/SHOT
# count"; above fails, below is reported). A scenario's "; mask: l,t,r,b"
# lines name rectangles painted black on both sides before comparing.
# RE_BASELINE=update rewrites the baseline from this run.
#
# New Style scenarios (tests/scripts/regedit/NS*.txt) have no 3.1
# counterpart: they run w16's REGEDIT only and compare with w16's own
# approved screens, tests/scripts/regedit/ns-ref/<scenario>/<SHOT>.png
# (exact; RE_NSREF=update copies this run's screens there for review).
#   usage: tests/rescenarios.sh [scenario...]    (default: all)
# Results go to build/ref/rescen/<date-time>/ (RESCEN_OUT=DIR: there).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=${RESCEN_OUT:-$root/build/ref/rescen/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
sdir=$root/tests/scripts/regedit
names=${*:-$(cd "$sdir" && ls RG*.txt NS*.txt 2>/dev/null | sed 's/\.txt$//')}
status=0
base=$root/tests/scripts/regedit.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
for n in $names; do
    script=$sdir/$n.txt
    result=ok
    case $n in
    NS*)
        REGOBS_EXE=$root/build/dos/REGEDIT.EXE "$root/tests/regobs.sh" "$script" "$out/$n/w16" > /dev/null
        shots=""
        for f in "$out/$n"/w16/*.png; do
            [ -f "$f" ] || continue
            s=$(basename "$f" .png)
            r=$sdir/ns-ref/$n/$s.png
            if [ "${RE_NSREF:-}" = update ]; then
                mkdir -p "$sdir/ns-ref/$n"; cp "$f" "$r"; shots="$shots $s=new"; continue
            fi
            if [ ! -f "$r" ]; then
                result=FAIL; echo "  $n: $s has no approved reference"; continue
            fi
            px=$(magick compare -metric AE "$r" "$f" "$out/$n/$s-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
            shots="$shots $s=$px"
            [ "$px" = 0 ] || { result=FAIL; echo "  $n: $s differs from the approved screen by $px pixels"; }
        done
        echo "== $n: $result (pixels:$shots)"
        [ $result = ok ] || status=1
        continue ;;
    esac
    "$root/tests/regobs.sh" "$script" "$out/$n/orig" > /dev/null &
    REGOBS_EXE=$root/build/dos/REGEDIT.EXE "$root/tests/regobs.sh" "$script" "$out/$n/w16" > /dev/null &
    wait
    for b in REG.DAT WIN.INI $(sed -n 's/^; get: *//p' "$script" | xargs -n1 basename 2>/dev/null); do
        [ -f "$out/$n/orig/$b" ] || continue
        if ! cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" 2>/dev/null; then
            result=FAIL; echo "  $n: $b differs"
            case $b in *.DAT) ;; *) diff "$out/$n/orig/$b" "$out/$n/w16/$b" | head -10 | sed 's/^/    /' || : ;; esac
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
            [ "${RE_BASELINE:-}" = update ] || result=FAIL
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
if [ "${RE_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
