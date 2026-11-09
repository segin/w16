#!/bin/sh
# Compare the WinHelp requests of the original Write and ours (probe
# WR11). Each script tests/scripts/writehelp/NAME.txt runs under SNAP on
# the reference Windows 3.1 twice, with the original WRITE.EXE and with
# build/dos/WRITE.EXE, while tests/apps/thelp.c stands in for WINHELP.EXE
# and logs each request (SNAP "mark N" lines label them). The requests
# (command, topic, help file, data) must be the same. The method is
# tests/helpscenarios.sh's (Program Manager).
#   usage: tests/writehelp.sh [NAME...]     output: build/ref/writehelp/
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
names=${*:-$(cd "$root/tests/scripts/writehelp" && ls *.txt | sed 's/\.txt$//')}
status=0
for n in $names; do
    for who in orig w16; do
        d=$root/build/ref/writehelp/$n/$who
        mkdir -p "$d"
        if [ $who = orig ]; then
            sed 's/$/\r/' "$root/tests/scripts/writehelp/$n.txt" > "$d/SNAP.TXT"
            extra=
        else
            sed 's/^run WRITE.EXE/run C:\\WRITE.EXE/; s/$/\r/' \
                "$root/tests/scripts/writehelp/$n.txt" > "$d/SNAP.TXT"
            extra=$root/build/dos/WRITE.EXE
        fi
        REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-400} \
        REFRUN_FILES="$d/SNAP.TXT $root/build/dos/THELP.EXE=WINDOWS/WINHELP.EXE $extra" \
            "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt" 2>&1 || true
        grep -a '^HELP \(msg 0477\|block\|text\)' "$d/log.txt" | awk '
            /msg 0477/ { mark = substr($4, 4); next }
            /block/    { printf "%smark %s cmd %s%s topic %s%s%s%s", sep, mark, $8, $7, $12, $11, $10, $9; sep = "\n"; next }
            /text/     { sub(/^HELP text @[0-9A-F]+ /, ""); printf " %s", $0 }
            END { print "" }' > "$d/requests.txt"
    done
    if cmp -s "$root/build/ref/writehelp/$n/orig/requests.txt" "$root/build/ref/writehelp/$n/w16/requests.txt"; then
        echo "== $n: ok ($(grep -c . "$root/build/ref/writehelp/$n/orig/requests.txt") requests)"
    else
        echo "== $n: DIFFERS"
        diff "$root/build/ref/writehelp/$n/orig/requests.txt" "$root/build/ref/writehelp/$n/w16/requests.txt" | sed 's/^/   /'
        status=1
    fi
done
exit $status
