#!/bin/sh
# Control Panel regression against the original: each scenario in
# tests/scripts/cpl/*.txt (a SNAP.EXE script, tests/apps/snap.c) runs once
# on the reference Windows 3.1 as it is and once with w16's CONTROL.EXE
# (and the applet DLLs the scenario names) copied over Microsoft's. The INI
# files Control Panel writes (CONTROL.INI, WIN.INI, SYSTEM.INI) must come
# out byte-identical, the CP1 applet probe's log (tests/apps/cp1.c) must
# record the same calls, and no screenshot may differ from the original's by
# more pixels than tests/scripts/cpl.baseline allows ("scenario/SHOT
# count"; what w16's own icon art and the "w16 Default" scheme name
# cost). CPL_BASELINE=update rewrites it.
#
# Directives, as SNAP comments (';!') in the scenario:
#   ;! cp1                  CP1.CPL into WINDOWS\SYSTEM (both runs)
#   ;! file NAME DEST       tests/scripts/cpl/NAME to C:\DEST (both runs)
#   ;! build FILE DEST      build/dos/FILE to C:\DEST (both runs)
#   ;! ref FILE DEST        the reference install's C:\FILE to C:\DEST (both
#                           runs; e.g. a font file to add from another directory)
#   ;! w16 FILE DEST        build/dos/FILE (a path with a slash: build/FILE,
#                           e.g. win386/WIN386.EXE) to C:\DEST in the w16 run
#   ;! ini FILE SEC:KEY=VAL set a key in the reference's C:\FILE (both runs;
#                           tests/scripts/iniset.py)
#   ;! saver DEST TEXT      w16's SSSTARS.SCR as C:\DEST (both runs), its
#                           description "SCRNSAVE : TEXT" (at the original's
#                           length: tests/scripts/scrdesc.py)
#   ;! kbrec                KBREC.DRV (tests/apps/kbrec.asm) as KEYBOARD in
#                           front of 3.1's KEYBOARD.DRV (both runs): SNAP's
#                           kbrlog lists the SetSpeed calls
#   ;! survive             Windows 3.1 dies in this scenario (its MAIN.CPL
#                           crashes): the w16 run must still finish (any
#                           run must when 3.1's did)
#   ;! mode /3              WIN.COM switch (default /S)
# C:\CMP.LOG, when a scenario writes one (SNAP's append, blinkrate,
# caretblink, kbrlog), must match too (tests/scripts/cmplog.py: blink
# counts within a tolerance).
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

# the INI files as compared (;! survive: see below)
inifilter() {
    if [ -n "$survive" ]; then grep -av '^[XYWH]=' | tr -d '\r'; else cat; fi
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
        ref)  common="$common $ref/$b=$c" ;;
        kbrec)                          # KBREC.DRV in front of 3.1's driver
            cp "$ref/WINDOWS/SYSTEM/KEYBOARD.DRV" "$pre/KBTARGET.DLL"
            python3 -I "$root/tools/nerename.py" "$pre/KBTARGET.DLL" --module KBTARGET > /dev/null
            common="$common $root/build/dos/KBREC.DRV=WINDOWS/SYSTEM/KEYBOARD.DRV"
            common="$common $pre/KBTARGET.DLL=WINDOWS/SYSTEM/KBTARGET.DLL" ;;
        saver)                          # w16's SSSTARS.SCR, described as $c
            python3 -I "$root/tests/scripts/scrdesc.py" "$root/build/dos/SSSTARS.SCR" \
                "$pre/$(echo "$b" | tr / _)" "$c"
            common="$common $pre/$(echo "$b" | tr / _)=$b" ;;
        w16)  case $b in                # a path: under build/ (win386/WIN386.EXE)
              */*) w16="$w16 $root/build/$b=$c" ;;
              *)   w16="$w16 $root/build/dos/$b=$c" ;;
              esac ;;
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
        CPOBS_FILES="$extra" CPOBS_GET="WINDOWS/CONTROL.INI WINDOWS/WIN.INI WINDOWS/SYSTEM.INI CP1.LOG CMP.LOG" \
            "$root/tests/cpobs.sh" "$s" "$d" $mode) &
    done
    wait
    result=ok
    # ;! survive: 3.1's Control Panel never closes, so never writes its
    # [MMCPL] X=, Y=, W=, H= (CONTROL.EXE, on close): those left out (and
    # the line ends, which that leaves different)
    survive=
    grep -q '^;! survive' "$s" && survive=1
    for f in $inis; do
        # (w16 brands the built-in colour scheme "w16 Default": shell/cpl/main)
        # (both sides through the same sed, which also ends an unended
        # last line: the name aside, the bytes must be the same)
        a=$(sed 's/^\(color schemes=\)w16 Default/\1Windows Default/' "$out/$n/orig/$f" 2>/dev/null | inifilter; echo .)
        b=$(sed 's/^\(color schemes=\)w16 Default/\1Windows Default/' "$out/$n/w16/$f" 2>/dev/null | inifilter; echo .)
        { [ -f "$out/$n/orig/$f" ] && [ -f "$out/$n/w16/$f" ] && [ "$a" = "$b" ]; } || { result=FAIL; echo "  $n: $f differs"; }
    done
    if [ -f "$out/$n/orig/CP1.LOG" ] || [ -f "$out/$n/w16/CP1.LOG" ]; then
        normlog "$out/$n/orig/CP1.LOG" > "$out/$n/orig/cp1.norm" 2>/dev/null || :
        normlog "$out/$n/w16/CP1.LOG" > "$out/$n/w16/cp1.norm" 2>/dev/null || :
        cmp -s "$out/$n/orig/cp1.norm" "$out/$n/w16/cp1.norm" || { result=FAIL; echo "  $n: CP1.LOG differs"; }
    fi
    if [ -f "$out/$n/orig/CMP.LOG" ] || [ -f "$out/$n/w16/CMP.LOG" ]; then
        python3 -I "$root/tests/scripts/cmplog.py" "$out/$n/orig/CMP.LOG" "$out/$n/w16/CMP.LOG" \
            || { result=FAIL; echo "  $n: CMP.LOG differs"; }
    fi
    # a run of 3.1 that finished must finish with w16 too; ";! survive":
    # 3.1 dies in this scenario (w16 may live through it), w16 must finish
    if grep -aq '^SNAP done' "$out/$n/orig/log.txt" 2>/dev/null || grep -q '^;! survive' "$s"; then
        grep -aq '^SNAP done' "$out/$n/w16/log.txt" 2>/dev/null \
            || { result=FAIL; echo "  $n: the w16 run did not finish"; }
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
