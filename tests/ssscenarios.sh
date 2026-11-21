#!/bin/sh
# Screen saver regression against the original (ARCHITECTURE.md 15.8):
# each scenario in tests/scripts/ss/*.txt (a SNAP.EXE script,
# tests/apps/snap.c) runs once on the reference Windows 3.1 as it is and
# once with w16's SSSTARS.SCR (and whatever else the scenario names)
# copied over Microsoft's. The INI files (CONTROL.INI, WIN.INI,
# SYSTEM.INI) must come out byte-identical; the window dumps in the
# logs must be the same (the lines "WIN ..."); no screenshot may differ
# from the original's by more pixels than tests/scripts/ss.baseline
# allows ("scenario/SHOT count"; SS_BASELINE=update rewrites it), except
# the running saver's: screenshots named R*.BMP are checked on both sides
# by tests/scripts/ss/stars.py instead (a black screen with white star
# squares of 1-4 pixels, about as many as the density), since the stars
# are placed at random.
#
# Directives, as SNAP comments (';!') in the scenario:
#   ;! file NAME DEST       tests/scripts/ss/NAME to C:\DEST (both runs)
#   ;! build FILE DEST      build/dos/FILE to C:\DEST (both runs)
#   ;! w16 FILE DEST        build/dos/FILE to C:\DEST in the w16 run
#   ;! ini FILE SEC:KEY=VAL set a key in the reference's C:\FILE (both runs;
#                           tests/scripts/iniset.py)
#   ;! stars N              the density the R*.BMP screens are checked for
#   ;! timeout SECS         the session's time limit (default 120)
#   ;! nodump               do not compare the window dumps
#   ;! dumpskip REGEX       leave the dump lines matching REGEX out (windows
#                           of other programs that differ in their own ways)
#   ;! crop SHOT GEOMETRY  compare only that part of the screenshot (an
#                           ImageMagick geometry, WxH+X+Y): a dialog over
#                           the running saver, whose stars are random
#   ;! get FILE             also fetch C:\FILE and compare it (e.g. a copy
#                           of CONTROL.INI made mid-script with copyfile)
# Output: build/ref/ss/<scenario>/{orig,w16}/
#   usage: tests/ssscenarios.sh [scenario...]    (default: all)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$root/tests/scripts/ss
ref=$root/.tools/ref/win31/cdrive
out=${SS_OUT:-$root/build/ref/ss}
names=${*:-$(cd "$dir" && ls *.txt | sed 's/\.txt$//')}
base=$root/tests/scripts/ss.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
status=0

# the window dumps of a log: a top-level window's id is its menu's
# handle, left out; lines matching the scenario's dumpskip left out
dumps() {
    grep -a '^WIN' "$1" | tr -d '\r' | sed -e 's/^\(WIN class=.*\) id=[0-9A-F]* /\1 /' |
        { if [ -n "$dskip" ]; then grep -E -v "$dskip"; else cat; fi; } || true
}

for n in $names; do
    s=$dir/$n.txt
    common="" w16="$root/build/dos/SSSTARS.SCR=WINDOWS/SSSTARS.SCR" stars=25 tmo=120 nodump=
    get="" inis="CONTROL.INI WIN.INI SYSTEM.INI" crops="" dskip=""
    pre=$out/$n/pre
    rm -rf "$pre"; mkdir -p "$pre"
    while read -r tag a b c; do
        [ "$tag" = ";!" ] || continue
        case $a in
        file)  common="$common $dir/$b=$c" ;;
        build) common="$common $root/build/dos/$b=$c" ;;
        w16)   w16="$w16 $root/build/dos/$b=$c" ;;
        stars) stars=$b ;;
        timeout) tmo=$b ;;
        nodump) nodump=1 ;;
        dumpskip) dskip="$b" ;;
        get)   get="$get $b" inis="$inis $b" ;;
        crop)  crops="$crops $b=$c" ;;
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
        REFRUN_TIMEOUT=$tmo CPOBS_FILES="$extra" \
            CPOBS_GET="WINDOWS/CONTROL.INI WINDOWS/WIN.INI WINDOWS/SYSTEM.INI$get" \
            "$root/tests/cpobs.sh" "$s" "$d") &
    done
    wait
    result=ok
    for f in $inis; do
        { [ -f "$out/$n/orig/$f" ] && [ -f "$out/$n/w16/$f" ] && cmp -s "$out/$n/orig/$f" "$out/$n/w16/$f"; } ||
            { result=FAIL; echo "  $n: $f differs"; }
    done
    grep -q '^SNAP done' "$out/$n/orig/log.txt" || { result=FAIL; echo "  $n: the original's script did not finish"; }
    grep -q '^SNAP done' "$out/$n/w16/log.txt" || { result=FAIL; echo "  $n: w16's script did not finish"; }
    if [ -z "$nodump" ]; then
        dumps "$out/$n/orig/log.txt" > "$out/$n/orig/dump.txt"
        dumps "$out/$n/w16/log.txt" > "$out/$n/w16/dump.txt"
        cmp -s "$out/$n/orig/dump.txt" "$out/$n/w16/dump.txt" ||
            { result=FAIL; echo "  $n: the window dumps differ (diff $out/$n/{orig,w16}/dump.txt)"; }
    fi
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        k=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$k.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $k missing"; continue
        fi
        case $k in
        R*)
            for who in orig w16; do
                if ! r=$(python3 -I "$dir/stars.py" "$out/$n/$who/$k.BMP" "$stars"); then
                    result=FAIL; echo "  $n: $who's $k: $r"
                fi
            done
            shots="$shots $k=stars"
            continue ;;
        esac
        g=""
        for cr in $crops; do [ "${cr%%=*}" = "$k" ] && g=${cr#*=}; done
        if [ -n "$g" ]; then
            px=$(magick compare -metric AE "$f[$g]" "$out/$n/w16/$k.BMP[$g]" null: 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        else
            px=$(magick compare -metric AE "$f" "$out/$n/w16/$k.BMP" null: 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        fi
        shots="$shots $k=$px"
        echo "$n/$k $px" >> "$newbase"
        allowed=$(awk -v k="$n/$k" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $k has no baseline ($px pixels differ)"
            [ "${SS_BASELINE:-}" = update ] || result=FAIL
        elif [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $k differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $k improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${SS_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
exit $status
