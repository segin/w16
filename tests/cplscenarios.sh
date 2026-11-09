#!/bin/sh
# Control Panel regression against the original: each scenario in
# tests/scripts/cpl/*.txt (a SNAP.EXE script, tests/apps/snap.c) runs once
# on the reference Windows 3.1 as it is and once with w16's CONTROL.EXE
# (and the applet DLLs the scenario names) copied over Microsoft's. The INI
# files Control Panel writes (CONTROL.INI, WIN.INI, SYSTEM.INI) must come
# out byte-identical, the CP1 applet probe's log (tests/apps/cp1.c) must
# record the same calls, and no screenshot may differ from the original's by
# more pixels than tests/scripts/cpl.baseline allows ("scenario/SHOT
# count"; what w16's own icon art costs). CPL_BASELINE=update rewrites it.
#
# Directives, as SNAP comments (';!') in the scenario:
#   ;! cp1                  CP1.CPL into WINDOWS\SYSTEM (both runs)
#   ;! file NAME DEST       tests/scripts/cpl/NAME to C:\DEST (both runs)
#   ;! build FILE DEST      build/dos/FILE to C:\DEST (both runs)
#   ;! w16 FILE DEST        build/dos/FILE to C:\DEST in the w16 run
#   ;! ini FILE SEC:KEY=VAL set a key in the reference's C:\FILE (both runs;
#                           tests/scripts/iniset.py)
#   ;! mode /3              WIN.COM switch (default /S)
# Output: build/ref/cpl/<scenario>/{orig,w16}/
#   usage: tests/cplscenarios.sh [scenario...]    (default: all)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$root/tests/scripts/cpl
ref=$root/.tools/ref/win31/cdrive
out=${CPL_OUT:-$root/build/ref/cpl}
names=${*:-$(cd "$dir" && ls *.txt | sed 's/\.txt$//')}
base=$root/tests/scripts/cpl.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
inis="CONTROL.INI WIN.INI SYSTEM.INI"
status=0

# the CP1 log without what differs between any two runs: window handles
# and the addresses of Control Panel's structures
normlog() {
    sed -e 's/hwnd=[0-9A-F]*/hwnd=-/' -e 's/l2=[0-9A-F]\{4\}[0-9A-F]\{4\}\( hwnd\)/l2=ptr\1/' \
        -e '/newinquire in:/d' -e 's/^C:\\WINDOWS\\SYSTEM\\//' "$1" | tr -d '\r'
}

for n in $names; do
    s=$dir/$n.txt
    common="" w16="$root/build/dos/CONTROL.EXE=WINDOWS/CONTROL.EXE" mode=/S
    pre=$out/$n/pre
    rm -rf "$pre"; mkdir -p "$pre"
    while read -r tag a b c; do
        [ "$tag" = ";!" ] || continue
        case $a in
        cp1)  common="$common $root/build/dos/CP1.CPL=WINDOWS/SYSTEM/CP1.CPL" ;;
        file) common="$common $dir/$b=$c" ;;
        build) common="$common $root/build/dos/$b=$c" ;;
        w16)  w16="$w16 $root/build/dos/$b=$c" ;;
        mode) mode=$b ;;
        ini)
            p=$pre/$(echo "$b" | tr / _)
            if [ ! -f "$p" ]; then
                cp "$ref/$b" "$p"
                common="$common $p=$b"
            fi
            python3 -I "$root/tests/scripts/iniset.py" "$p" "$c" ;;
        esac
    done < "$s"
    for who in orig w16; do (
        d=$out/$n/$who
        rm -rf "$d"; mkdir -p "$d"
        extra=$common
        [ $who = w16 ] && extra="$extra $w16"
        CPOBS_FILES="$extra" CPOBS_GET="WINDOWS/CONTROL.INI WINDOWS/WIN.INI WINDOWS/SYSTEM.INI CP1.LOG" \
            "$root/tests/cpobs.sh" "$s" "$d" $mode) &
    done
    wait
    result=ok
    for f in $inis; do
        cmp -s "$out/$n/orig/$f" "$out/$n/w16/$f" || { result=FAIL; echo "  $n: $f differs"; }
    done
    if [ -f "$out/$n/orig/CP1.LOG" ] || [ -f "$out/$n/w16/CP1.LOG" ]; then
        normlog "$out/$n/orig/CP1.LOG" > "$out/$n/orig/cp1.norm" 2>/dev/null || :
        normlog "$out/$n/w16/CP1.LOG" > "$out/$n/w16/cp1.norm" 2>/dev/null || :
        cmp -s "$out/$n/orig/cp1.norm" "$out/$n/w16/cp1.norm" || { result=FAIL; echo "  $n: CP1.LOG differs"; }
    fi
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        k=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$k.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $k missing"; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/$n/w16/$k.BMP" null: 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $k=$px"
        echo "$n/$k $px" >> "$newbase"
        allowed=$(awk -v k="$n/$k" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $k has no baseline ($px pixels differ)"
            [ "${CPL_BASELINE:-}" = update ] || result=FAIL
        elif [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $k differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $k improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${CPL_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
