#!/bin/sh
# Run observation scripts of this directory on the reference 3.1, two at a
# time (tests/wfobs.sh), results in build/ref/wfobs/NAME/.
#   usage: tests/scripts/winfile/obs/obs.sh NAME...    (e.g. WF5 WF6)
# On a remote host: tools/remote-run.sh -g build/ref/wfobs sh
# tests/scripts/winfile/obs/obs.sh WF5
set -u
root=$(cd "$(dirname "$0")/../../../.." && pwd)
n=0
for s in "$@"; do
    # "; big: NAME SIZE" lines: a file of SIZE bytes brought to C:\NAME
    extra=
    for b in $(sed -n 's/^; big: *\([^ ]*\) .*/\1/p' "$root/tests/scripts/winfile/obs/$s.txt"); do
        n=$(sed -n "s/^; big: *$b *//p" "$root/tests/scripts/winfile/obs/$s.txt")
        mkdir -p "$root/build/ref/wfobs/$s-in"
        head -c "$n" /dev/zero > "$root/build/ref/wfobs/$s-in/$b"
        extra="$extra $root/build/ref/wfobs/$s-in/$b"
    done
    WFOBS_LINES=0 sh "$root/tests/wfobs.sh" "$root/tests/scripts/winfile/obs/$s.txt" \
        "$root/build/ref/wfobs/$s" $extra > /dev/null &
    n=$((n + 1))
    [ $((n % 2)) = 0 ] && wait
done
wait
for s in "$@"; do
    echo "== $s: $(ls "$root/build/ref/wfobs/$s" | grep -c BMP) screens"
done
