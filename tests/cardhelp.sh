#!/bin/sh
# Compare the WinHelp requests of the original Cardfile and w16's, as
# tests/helpscenarios.sh does for Program Manager: each script
# tests/scripts/cfhelp/NAME.txt runs on the reference Windows 3.1 with
# the original CARDFILE.EXE and with build/dos/CARDFILE.EXE, while
# tests/apps/thelp.c stands in for WINHELP.EXE and logs each request
# ("mark N" lines label them). The requests must be the same.
#   usage: tests/cardhelp.sh [NAME...]     output: build/ref/cfhelp/
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
names=${*:-$(cd "$root/tests/scripts/cfhelp" && ls *.txt | sed 's/\.txt$//')}
status=0
run() {   # who name
    d=$root/build/ref/cfhelp/$2/$1
    rm -rf "$d"; mkdir -p "$d"
    if [ "$1" = orig ]; then
        sed 's/$/\r/' "$root/tests/scripts/cfhelp/$2.txt" > "$d/SNAP.TXT"
        extra=
    else
        sed 's/^run CARDFILE.EXE/run C:\\CARDFILE.EXE/; s/$/\r/' \
            "$root/tests/scripts/cfhelp/$2.txt" > "$d/SNAP.TXT"
        extra=$root/build/dos/CARDFILE.EXE
    fi
    REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-400} \
    REFRUN_FILES="$d/SNAP.TXT $root/build/dos/THELP.EXE=WINDOWS/WINHELP.EXE $extra" \
        "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt" 2>&1 || true
    grep -a '^HELP \(msg 0477\|block\|text\)' "$d/log.txt" | awk '
        /msg 0477/ { mark = substr($4, 4); next }
        /block/    { printf "%smark %s cmd %s%s topic %s%s%s%s", sep, mark, $8, $7, $12, $11, $10, $9; sep = "\n"; next }
        /text/     { sub(/^HELP text @[0-9A-F]+ /, ""); printf " %s", $0 }
        END { print "" }' > "$d/requests.txt"
}
for n in $names; do
    run orig "$n" &
    run w16 "$n"
    wait
    o=$root/build/ref/cfhelp/$n/orig/requests.txt w=$root/build/ref/cfhelp/$n/w16/requests.txt
    if cmp -s "$o" "$w"; then
        echo "== $n: ok ($(grep -c . "$o") requests)"
    else
        echo "== $n: DIFFERS"
        diff "$o" "$w" | sed 's/^/   /'
        status=1
    fi
done
exit $status
