#!/bin/sh
# Program Manager DDE regression against the original: each script in
# tests/scripts/dde/*.txt (for TDDE.EXE; its first line is
# "run PROGMAN.EXE") runs once with the original and once with w16's
# Program Manager on the reference Windows 3.1. The logged DDE answers
# and the resulting group files and PROGMAN.INI must be identical.
#   usage: tests/ddescenario.sh [script...]    (default: all)
# DDESCEN_WHO=orig (or w16): run one side only (exploring; no comparison).
# DDESCEN_OUT: the results directory (default build/ref/ddescen).
# A script's lines ";get PATH" (PATH under C:\, e.g. WINDOWS/ARR.GRP)
# add files to compare. Scripts named grp*.txt also get tools/mkbadgrp.py's malformed and
# edge-case group files in C:\WINDOWS (their own TDDE "ini" lines name
# them in PROGMAN.INI), and those files are compared afterwards too.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=${DDESCEN_OUT:-$root/build/ref/ddescen}
files="WINDOWS/PROGMAN.INI WINDOWS/MAIN.GRP WINDOWS/ACCESSOR.GRP WINDOWS/GAMES.GRP WINDOWS/STARTUP.GRP WINDOWS/DDETEST.GRP WINDOWS/DDEKEEP.GRP WINDOWS/MAIN0.GRP WINDOWS/COLLIDEO.GRP WINDOWS/COLLIDET.GRP WINDOWS/COLLIDE0.GRP"
badgrp=$(sed -n 's/^  \([A-Z][A-Z]*\)  .*/\1/p' "$root/tools/mkbadgrp.py")
names=${*:-$(cd "$root/tests/scripts/dde" && ls *.txt | sed 's/\.txt$//')}
status=0
for n in $names; do
    grp= nfiles="$files $(sed -n 's/^;get //p' "$root/tests/scripts/dde/$n.txt" | tr -d '\r')"
    case $n in grp*)
        mkdir -p "$out/$n"
        python3 "$root/tools/mkbadgrp.py" "$out/$n/grp"
        for g in $badgrp; do
            grp="$grp $out/$n/grp/$g.GRP=WINDOWS/$g.GRP"
            nfiles="$nfiles WINDOWS/$g.GRP"
        done ;;
    esac
    for who in ${DDESCEN_WHO:-orig w16}; do
        d=$out/$n/$who
        mkdir -p "$d"
        if [ $who = orig ]; then
            sed 's/$/\r/' "$root/tests/scripts/dde/$n.txt" > "$d/DDE.TXT"
            extra=
        else
            sed 's/^run PROGMAN.EXE$/run C:\\PROGMAN.EXE/; s/$/\r/' "$root/tests/scripts/dde/$n.txt" > "$d/DDE.TXT"
            extra=$root/build/dos/PROGMAN.EXE
        fi
        for f in $nfiles; do [ -f "$d/$(basename $f)" ] && mv "$d/$(basename $f)" "$d/$(basename $f).old"; done
        rm -f "$d/TK.LOG"
        REFRUN_FILES="$d/DDE.TXT $extra$grp" REFRUN_OUT=$d REFRUN_GET="$nfiles WINDOWS/TK.LOG" \
            REFRUN_TIMEOUT=120 "$root/tests/refrun.sh" "$root/build/dos/TDDE.EXE" /S > "$d/log.txt" || true
        # Windows did not end (a Program Manager hung, or left a box up):
        # the log so far, marked
        if [ ! -s "$d/log.txt" ] && [ -f "$d/TK.LOG" ]; then
            { tr -d '\r' < "$d/TK.LOG"; echo "(Windows did not end)"; } > "$d/log.txt"
        fi
    done
    result=ok
    if ! diff "$out/$n/orig/log.txt" "$out/$n/w16/log.txt" > "$out/$n/log.diff"; then
        result=FAIL
        echo "  $n: DDE answers differ:"
        sed 's/^/    /' "$out/$n/log.diff" | head -20
    fi
    for f in $nfiles; do
        b=$(basename "$f")
        if [ -f "$out/$n/orig/$b" ] || [ -f "$out/$n/w16/$b" ]; then
            cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" || { result=FAIL; echo "  $n: $b differs"; }
        fi
    done
    echo "== $n: $result"
    [ $result = ok ] || status=1
done
exit $status
