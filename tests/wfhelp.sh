#!/bin/sh
# Compare the WinHelp requests of the original File Manager and ours. Each
# script tests/scripts/winfilehelp/NAME.txt runs under SNAP on the
# reference Windows 3.1 twice, with the original WINFILE.EXE and with
# build/dos/WINFILE.EXE (over C:\WINDOWS\WINFILE.EXE), while
# tests/apps/thelp.c stands in for WINHELP.EXE and logs each request (SNAP
# "mark N" lines label them). The requests (command, topic, help file)
# must be the same; tests/writehelp.sh's method.
#   usage: tests/wfhelp.sh [NAME...]     output: build/ref/wfhelp/
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
names=${*:-$(cd "$root/tests/scripts/winfilehelp" && ls *.txt | sed 's/\.txt$//')}
status=0
for n in $names; do
    for who in orig w16; do
        d=$root/build/ref/wfhelp/$n/$who
        mkdir -p "$d"
        sed 's/$/\r/' "$root/tests/scripts/winfilehelp/$n.txt" > "$d/SNAP.TXT"
        extra=
        [ $who = w16 ] && extra=$root/build/dos/WINFILE.EXE=WINDOWS/WINFILE.EXE
        REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-600} \
        REFRUN_FILES="$d/SNAP.TXT $root/build/dos/THELP.EXE=WINDOWS/WINHELP.EXE $extra" \
            "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt" 2>&1 &
    done
    wait
    for who in orig w16; do
        d=$root/build/ref/wfhelp/$n/$who
        grep -a '^HELP \(msg 0477\|block\|text\)' "$d/log.txt" | awk '
            /msg 0477/ { mark = substr($4, 4); next }
            /block/    { cmd = $8 $7
                         # the data of HELP_INDEX, HELP_HELPONHELP and
                         # HELP_QUIT is unused: 3.1 leaves stack contents
                         # there; strings for the key commands
                         t = (cmd == "0101" || cmd == "0105" || cmd == "0003" ||
                              cmd == "0004" || cmd == "0002") ? "-" : $12 $11 $10 $9
                         printf "%smark %s cmd %s topic %s", sep, mark, cmd, t; sep = "\n"; next }
            /text/     { sub(/^HELP text @[0-9A-F]+ /, ""); if ($0 ~ /\.HLP"$/) printf " %s", $0 }
            END { print "" }' > "$d/requests.txt"
    done
    if cmp -s "$root/build/ref/wfhelp/$n/orig/requests.txt" "$root/build/ref/wfhelp/$n/w16/requests.txt"; then
        echo "== $n: ok ($(grep -c . "$root/build/ref/wfhelp/$n/orig/requests.txt") requests)"
    else
        echo "== $n: DIFFERS"
        diff "$root/build/ref/wfhelp/$n/orig/requests.txt" "$root/build/ref/wfhelp/$n/w16/requests.txt" | sed 's/^/   /' | head -40
        status=1
    fi
done
exit $status
