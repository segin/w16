#!/bin/sh
# Program Manager DDE regression against the original: each script in
# tests/scripts/dde/*.txt (for TDDE.EXE; its first line is
# "run PROGMAN.EXE") runs once with the original and once with w16's
# Program Manager on the reference Windows 3.1. The logged DDE answers
# and the resulting group files and PROGMAN.INI must be identical.
#   usage: tests/ddescenario.sh [script...]    (default: all)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/ref/ddescen
files="WINDOWS/PROGMAN.INI WINDOWS/MAIN.GRP WINDOWS/ACCESSOR.GRP WINDOWS/GAMES.GRP WINDOWS/STARTUP.GRP WINDOWS/DDETEST.GRP WINDOWS/DDEKEEP.GRP"
names=${*:-$(cd "$root/tests/scripts/dde" && ls *.txt | sed 's/\.txt$//')}
status=0
for n in $names; do
    for who in orig w16; do
        d=$out/$n/$who
        mkdir -p "$d"
        if [ $who = orig ]; then
            sed 's/$/\r/' "$root/tests/scripts/dde/$n.txt" > "$d/DDE.TXT"
            extra=
        else
            sed 's/^run PROGMAN.EXE$/run C:\\PROGMAN.EXE/; s/$/\r/' "$root/tests/scripts/dde/$n.txt" > "$d/DDE.TXT"
            extra=$root/build/dos/PROGMAN.EXE
        fi
        for f in $files; do [ -f "$d/$(basename $f)" ] && mv "$d/$(basename $f)" "$d/$(basename $f).old"; done
        REFRUN_FILES="$d/DDE.TXT $extra" REFRUN_OUT=$d REFRUN_GET="$files" REFRUN_TIMEOUT=120 \
            "$root/tests/refrun.sh" "$root/build/dos/TDDE.EXE" /S > "$d/log.txt"
    done
    result=ok
    if ! diff "$out/$n/orig/log.txt" "$out/$n/w16/log.txt" > "$out/$n/log.diff"; then
        result=FAIL
        echo "  $n: DDE answers differ:"
        sed 's/^/    /' "$out/$n/log.diff" | head -20
    fi
    for f in $files; do
        b=$(basename "$f")
        if [ -f "$out/$n/orig/$b" ] || [ -f "$out/$n/w16/$b" ]; then
            cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" || { result=FAIL; echo "  $n: $b differs"; }
        fi
    done
    echo "== $n: $result"
    [ $result = ok ] || status=1
done
exit $status
