#!/bin/sh
# The MCI probes (layer 4; MMP's section Q, tests/mmsystem/mmci.c) on
# Microsoft's MMSYSTEM.DLL and on w16's, each script's pair of logs
# compared by tests/mmsystem/mcmp.py (ARCHITECTURE.md 23). A case is
# NAME:SCRIPT:KIND, SCRIPT a file in tests/mmsystem/mci/, KIND plain (the
# test driver MCT.DRV) or shim (also Windows 3.1's Sound Blaster driver,
# MCIWAVE and MCISEQ behind logging drivers: tests/mmsystem/mcirun.sh).
#   usage: tests/mmsystem/mciscen.sh [-o OUTDIR] [-m] [CASE...]
#     -m   Microsoft's MMSYSTEM only
# Exit status 0 when every pair agrees.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out= msonly=
while getopts o:m opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) msonly=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
cases=${*:-e1:explore1.txt:plain e2:explore2.txt:plain e4:explore4.txt:plain \
e6:explore6.txt:plain e8:explore8.txt:plain e3:explore3.txt:plain \
e5:explore5.txt:shim e7:explore7.txt:shim}
[ -n "$out" ] || out=$root/build/mciscen-$(date +%H%M%S)
mkdir -p "$out"
sides="ms w16"
[ -n "$msonly" ] && sides=ms
for c in $cases; do
    name=${c%%:*} r=${c#*:}
    script=${r%%:*} kind=${r#*:}
    for s in $sides; do
        "$root/tests/mmsystem/mcirun.sh" "$out/$name-$s" $s "$root/tests/mmsystem/mci/$script" $kind \
            > "$out/$name-$s.txt" 2>&1 &
    done
done
wait
[ -n "$msonly" ] && exit 0
fails=0
for c in $cases; do
    n=${c%%:*}
    a=$out/$n-ms/mmp.log b=$out/$n-w16/mmp.log
    if [ ! -s "$a" ] || [ ! -s "$b" ]; then
        echo "FAIL $n: no log"; fails=$((fails + 1)); continue
    fi
    if python3 "$root/tests/mmsystem/mcmp.py" "$a" "$b" > "$out/cmp-$n.txt" 2>&1; then
        echo "ok   $n: $(tail -n 1 "$out/cmp-$n.txt")"
    else
        echo "FAIL $n: $(grep -m1 FAIL "$out/cmp-$n.txt") ($out/cmp-$n.txt)"; fails=$((fails + 1))
    fi
done
exit $fails
