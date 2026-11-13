#!/bin/sh
# Cardfile regression against the original: each scenario in
# tests/scripts/cf/*.txt (a SNAP.EXE script that starts "run
# CARDFILE.EXE") runs on the reference Windows 3.1 once with the
# original Cardfile and once with w16's (build/dos/CARDFILE.EXE), the two
# at the same time (tests/cfrun.sh). Afterwards every card file the
# script saved (C:\*.CRD) must be byte-identical (tools/crdcmp.py: the
# bits the original leaves undrawn in 3.0 pictures excepted), and so must
# WIN.INI
# (Autodial's settings); no screenshot may differ from the original's by
# more pixels than tests/scripts/cf.baseline allows ("scenario/SHOT
# count": what our own icon art costs, and differences still open). A
# count above the baseline fails; one below it is reported so the
# baseline can be tightened. CF_BASELINE=update rewrites the baseline.
# Input files a scenario needs are named on its "; needs:" line and come
# from tests/scripts/cf/ (card files made by the original, see there).
# CF_COMMDLG=w16 runs ours on w16's COMMDLG.DLL (its own baseline,
# tests/scripts/cf-commdlg.baseline).
#   usage: tests/cardscenarios.sh [scenario...]   (default: all)
#   output: build/ref/cfscen/<date-time>/<scenario>/{orig,w16}
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$root/tests/scripts/cf
out=$root/build/ref/cfscen/$(date +%Y%m%d-%H%M%S)
names=${*:-$(cd "$dir" && ls *.txt | sed 's/\.txt$//')}
base=$root/tests/scripts/cf.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
status=0
# CF_COMMDLG=w16: w16's run uses w16's COMMDLG.DLL (build/dos) as well, so
# its Open, Save As, Find and Print Setup are ours against Microsoft's
w16dlls=
if [ "${CF_COMMDLG:-}" = w16 ]; then
    w16dlls=$root/build/dos/COMMDLG.DLL=WINDOWS/SYSTEM/COMMDLG.DLL
    base=$root/tests/scripts/cf-commdlg.baseline
    [ -f "$base" ] || : > "$base"
fi
mkdir -p "$out"
echo "output: $out"
for n in $names; do
    needs=$(sed -n 's/^; needs: *//p' "$dir/$n.txt" | tr ' ' '\n' | sed "s|^|$dir/|" | tr '\n' ' ')
    CFRUN_GET=WINDOWS/WIN.INI "$root/tests/cfrun.sh" "$dir/$n.txt" "$out/$n/orig" $needs > /dev/null &
    CFRUN_OURS=1 CFRUN_GET=WINDOWS/WIN.INI "$root/tests/cfrun.sh" "$dir/$n.txt" "$out/$n/w16" $needs $w16dlls > /dev/null
    wait
    result=ok
    for f in "$out/$n"/orig/*.CRD "$out/$n/orig/WIN.INI"; do
        [ -f "$f" ] || continue
        b=$(basename "$f")
        if [ ! -f "$out/$n/w16/$b" ]; then
            result=FAIL; echo "  $n: $b missing"
        elif [ "$b" = WIN.INI ]; then
            cmp -s "$f" "$out/$n/w16/$b" || { result=FAIL; echo "  $n: $b differs"; }
        elif ! r=$(python3 -I "$root/tools/crdcmp.py" "$f" "$out/$n/w16/$b"); then
            result=FAIL; echo "  $n: $b $r"
        fi
    done
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$s.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $s missing"; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/$n/w16/$s.BMP" "$out/$n/w16/$s-diff.png" 2>&1 |
             cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $s has no baseline ($px pixels differ)"
            [ "${CF_BASELINE:-}" = update ] || result=FAIL
            continue
        fi
        if [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $s differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $s improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${CF_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
