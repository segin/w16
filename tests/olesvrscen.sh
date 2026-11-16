#!/bin/sh
# OLE server scenarios: each script of tests/olesvr/scripts (NAME.os2,
# with NAME.os1 / NAME.snap / NAME.get when it has them) runs on the
# reference Windows 3.1 with Microsoft's OLESVR.DLL and OLECLI.DLL, and
# again with w16's OLESVR (build/dos/OLESVR.DLL) and, once the OLECLI
# agent's build/dos/OLECLI.DLL exists, with w16's OLECLI against both
# OLESVRs: the four combinations. The DDE logs (every WM_DDE_* message
# and every callback into the servers, tests/olesvr/osspy.c), the files
# the scenario saves and its screenshots must equal the reference's.
#   usage: tests/olesvrscen.sh [-o OUTDIR] [-c COMBOS] [NAME...]   (default: all)
# -c the combinations SERVER-CLIENT to compare with ms-ms (default "w16-ms",
#    plus "ms-w16 w16-w16" when build/dos/OLECLI.DLL exists). The probes
#    drive OLECLI further than the OLECLI tests do (tests/olescenarios.sh),
#    so with w16's OLECLI they also show its differences.
# OUTDIR defaults to a fresh build/olesvrscen-<date>-<time>.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/olesvrscen-$(date +%Y%m%d-%H%M%S)
combos=
while getopts o:c: opt; do
    case $opt in
    o) out=$OPTARG ;;
    c) combos=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
s=$root/tests/olesvr/scripts
names=${*:-$(cd "$s" && ls *.os2 | sed 's/\.os2$//')}
if [ -z "$combos" ]; then
    combos=w16-ms
    if [ -f "$root/build/dos/OLECLI.DLL" ]; then combos="$combos ms-w16 w16-w16"; fi
fi
combos="ms-ms $combos"
mkdir -p "$out"
echo "olesvrscen: $out"
# every scenario in every combination, a few at a time
for c in $combos; do
    "$root/tests/olesvr/osobs.sh" "$out" "${c%-*}" "${c#*-}" $names > "$out/run-$c.txt" 2>&1 || true
done
# what is compared: the log without OS1's waits (its script runs from a
# timer, so where those lines fall depends on timing) and the files saved,
# with the identifiers clients make from the time masked (osnorm.py)
for n in $names; do
    get=
    if [ -f "$s/$n.get" ]; then get=$(cat "$s/$n.get"); fi
    for c in $combos; do
        python3 "$root/tests/olesvr/osnorm.py" "$out/$n-$c" $get
    done
done
status=0
for n in $names; do
    ref=$out/$n-ms-ms
    for c in $combos; do
        [ "$c" = ms-ms ] && continue
        d=$out/$n-$c
        result=ok
        if [ ! -s "$ref/os.log" ] || [ ! -s "$d/os.log" ]; then
            result="FAIL (no log)"
        elif ! diff "$ref/os.cmp" "$d/os.cmp" > "$d/os.diff"; then
            result="FAIL (DDE log differs: $d/os.diff)"
        fi
        if [ -f "$s/$n.get" ]; then
            for f in $(cat "$s/$n.get"); do
                b=$(basename "$f")
                if [ ! -f "$ref/$b.norm" ] || ! cmp -s "$ref/$b.norm" "$d/$b.norm"; then
                    result="FAIL ($b differs)"
                fi
            done
        fi
        for bmp in "$ref"/*.BMP; do
            [ -f "$bmp" ] || continue
            b=$(basename "$bmp")
            if [ ! -f "$d/$b" ]; then
                result="FAIL (no $b)"
            elif command -v magick > /dev/null; then
                # NAME.mask: ImageMagick -draw primitives painted black on
                # both shots first (what differs by nature: a caret's blink)
                mask=
                if [ -f "$s/$n.mask" ]; then mask=$(cat "$s/$n.mask"); fi
                magick "$bmp" -fill black -draw "${mask:-point 0,0}" "$d/${b%.BMP}-ref.png"
                magick "$d/$b" -fill black -draw "${mask:-point 0,0}" "$d/${b%.BMP}-run.png"
                px=$(magick compare -metric AE "$d/${b%.BMP}-ref.png" "$d/${b%.BMP}-run.png" \
                    "$d/${b%.BMP}-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1 || true)
                [ "$px" = 0 ] || result="FAIL ($b: $px pixels differ)"
            elif ! cmp -s "$bmp" "$d/$b"; then
                result="FAIL ($b differs)"
            fi
        done
        echo "== $n $c: $result"
        [ "$result" = ok ] || status=1
    done
done
exit $status
