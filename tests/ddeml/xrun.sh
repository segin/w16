#!/bin/sh
# Run DDEML probe scripts (tests/ddeml/scripts, the x*.dm explorations
# too) in several library combinations and show how each log differs
# from Microsoft's (ms-ms), after tests/ddeml/dmnorm.py. For looking at
# a behaviour while matching it; tests/ddemlscen.sh is the test.
#   usage: tests/ddeml/xrun.sh OUTDIR "COMBOS" NAME...
#   e.g.   tests/ddeml/xrun.sh build/xp "w16-ms ms-w16" xun2 xun4+x
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 combos=$2
shift 2
mkdir -p "$out"
for n in "$@"; do
    for c in ms-ms $combos; do echo "$n $c"; done
done | xargs -P "${DDEMLSCEN_JOBS:-6}" -L 1 sh -c \
    'c=$3; "$0/tests/ddeml/dmrun.sh" "$2" "$1/$2-$c" "${c%-*}" "${c#*-}" > /dev/null 2>&1 || true' \
    "$root" "$out"
for n in "$@"; do
    for c in ms-ms $combos; do
        d=$out/$n-$c
        if [ -s "$d/os.log" ]; then
            python3 "$root/tests/ddeml/dmnorm.py" "$d/os.log" > "$d/os.cmp"
        else
            : > "$d/os.cmp"
        fi
    done
    for c in $combos; do
        echo "== $n $c"
        diff "$out/$n-ms-ms/os.cmp" "$out/$n-$c/os.cmp" || true
    done
done
