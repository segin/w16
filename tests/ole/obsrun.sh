#!/bin/sh
# Run OLE observation scripts (tests/ole/obs/*.txt) with OC1 on the
# reference Windows 3.1, a few at a time, each into <outdir>/<name>.
#   usage: tests/ole/obsrun.sh <outdir> [OLECLI.DLL|-] name...
# "-" (or nothing) keeps Microsoft's OLECLI. OBS_JOBS: parallel runs (3).
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1
dll=${2:--}
shift 2 || shift $#
[ "$dll" = - ] && dll=
names=${*:-$(cd "$root/tests/ole/obs" && ls *.txt | sed 's/\.txt$//')}
jobs=${OBS_JOBS:-3}
n=0
for name in $names; do
    "$root/tests/ole/ocrun.sh" "$root/tests/ole/obs/$name.txt" "$out/$name" $dll &
    n=$((n + 1))
    if [ $n -ge "$jobs" ]; then
        wait
        n=0
    fi
done
wait
