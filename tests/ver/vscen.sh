#!/bin/sh
# VER.DLL scenarios (ARCHITECTURE.md, VER): the probe VRP.EXE
# (tests/ver/vrp.c) runs as the shell of the reference Windows 3.1
# with Microsoft's VER.DLL and with w16's (tests/ver/vrun.sh),
# in each mode, and the two logs are compared section by section
# (tests/ver/vcmp.py; nothing masked).
#   usage: tests/ver/vscen.sh [-o OUTDIR] [-m "MODES"] [-n] [SECTION...]
# -m the WIN switches to run (default "/S /3"); -n Microsoft's only.
# SECTION: compare only these sections ("find", "install", ...).
# OUTDIR defaults to a fresh build/vscen-<date>-<time>.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$root/build/vscen-$(date +%Y%m%d-%H%M%S)
modes="/S /3" libs="ms w16"
while getopts o:m:n opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) modes=$OPTARG ;;
    n) libs=ms ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
mkdir -p "$out"
echo "vscen: $out"
for m in $modes; do
    for l in $libs; do
        d=$out/${l}$(echo "$m" | tr -d '/')
        VRUN_TIMEOUT=${VRUN_TIMEOUT:-600} "$root/tests/ver/vrun.sh" "$d" "$l" "$m" \
            > /dev/null 2>&1 &
    done
done
wait
status=0
for m in $modes; do
    k=$(echo "$m" | tr -d '/')
    ref=$out/ms$k/vrp.log
    if [ ! -s "$ref" ]; then echo "== $m: FAIL (no log from Microsoft's)"; status=1; continue; fi
    grep -q '^VRP end' "$ref" || { echo "== $m: FAIL (Microsoft's run did not finish)"; status=1; }
    [ "$libs" = ms ] && continue
    w=$out/w16$k/vrp.log
    if [ ! -s "$w" ]; then echo "== $m: FAIL (no log from w16's)"; status=1; continue; fi
    python3 "$root/tests/ver/vcmp.py" "$ref" "$w" "$out/cmp$k" "$@" | sed "s|^|== $m |" \
        > "$out/cmp$k.txt" || true
    cat "$out/cmp$k.txt"
    grep -q 'FAIL' "$out/cmp$k.txt" && status=1
done
exit $status
