#!/bin/sh
# Compare the WinHelp requests of the original Program Manager and ours.
# Each script tests/scripts/help/NAME.txt is run by SNAP on the reference
# Windows 3.1 (Standard mode) twice, with the original PROGMAN.EXE and
# with build/dos/PROGMAN.EXE, while tests/apps/thelp.c stands in for
# WINHELP.EXE and logs each request (SNAP "mark N" lines label them).
# The requests (command, topic, help file, data) must be the same.
#   usage: tests/helpscenarios.sh [NAME...]     output: build/ref/help/
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
names=${*:-$(cd "$root/tests/scripts/help" && ls *.txt | sed 's/\.txt$//')}
status=0
for n in $names; do
    for who in orig w16; do
        d=$root/build/ref/help/$n/$who
        mkdir -p "$d"
        if [ $who = orig ]; then
            sed 's/$/\r/' "$root/tests/scripts/help/$n.txt" > "$d/SNAP.TXT"
            extra=
        else
            sed 's/^run PROGMAN.EXE$/run C:\\PROGMAN.EXE/; s/$/\r/' \
                "$root/tests/scripts/help/$n.txt" > "$d/SNAP.TXT"
            extra=$root/build/dos/PROGMAN.EXE
        fi
        REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-400} \
        REFRUN_FILES="$d/SNAP.TXT $root/build/dos/THELP.EXE=WINDOWS/WINHELP.EXE $extra" \
            "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt" 2>&1 || true
        # one line per request: mark, command, topic, then the block's text
        grep -a '^HELP \(msg 0477\|block\|text\)' "$d/log.txt" | awk '
            /msg 0477/ { mark = substr($4, 4); next }
            /block/    { printf "%smark %s cmd %s%s topic %s%s%s%s", sep, mark, $8, $7, $12, $11, $10, $9; sep = "\n"; next }
            /text/     { sub(/^HELP text @[0-9A-F]+ /, ""); printf " %s", $0 }
            END { print "" }' > "$d/requests.txt"
    done
    if cmp -s "$root/build/ref/help/$n/orig/requests.txt" "$root/build/ref/help/$n/w16/requests.txt"; then
        echo "== $n: ok ($(grep -c . "$root/build/ref/help/$n/orig/requests.txt") requests)"
    else
        echo "== $n: DIFFERS"
        diff "$root/build/ref/help/$n/orig/requests.txt" "$root/build/ref/help/$n/w16/requests.txt" | sed 's/^/   /'
        status=1
    fi
done
exit $status
