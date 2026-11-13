#!/bin/sh
# MD2 (tests/apps/md2a.c) as the shell of Windows 3.1, headless, with real
# input from SCRIPT (tests/inputrun.sh). Without -M, MOUSE is
# MS3 (tests/apps/ms3a.asm): a recording driver in front of MOUSX.DLL, a
# scratch copy of the real driver whose module name tools/nerename.py
# changed to MOUSX (NE header only), so that every call any module makes
# to MOUSE.DRV is recorded with its arguments and results.
#   usage: tests/md2aprobe.sh [-M] [-w] [-b SECS] [-t SECS] SCRIPT OUTDIR [dropin.sh options...]
#     -M      no MS3: MD2 calls MOUSE.DRV itself
#     -w      the real driver is w16's (build/dos/MOUSE.DRV), not Windows 3.1's
#     -d FILE the real driver is FILE
#     -b, -t  as for tests/inputrun.sh (defaults 30 and 90)
# Options after OUTDIR go to tests/dropin.sh, e.g. -i w16:md2mode=1,
# -i w16:md2run=POINT.EXE, -f FILE=WINDOWS/POINT.EXE, -c 'C:\RUN.BAT'.
# OUTDIR gets inputrun's files and md2.log (TK.LOG, CRs removed).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
shim=1 real=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/MOUSE.DRV boot=30 total=90
while getopts Mwd:b:t: opt; do
    case $opt in
    M) shim= ;;
    w) real=$root/build/dos/MOUSE.DRV ;;
    d) real=$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG") ;;
    b) boot=$OPTARG ;;
    t) total=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
script=$1 out=$2; shift 2
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
set -- -f "$root/build/dos/MD2A.EXE=MD2A.EXE" -i 'boot:shell=C:\MD2A.EXE' "$@"
if [ -n "$shim" ]; then
    cp "$real" "$out/MOUSX.DLL"
    python3 "$root/tools/nerename.py" "$out/MOUSX.DLL" --module MOUSX
    set -- -f "$root/build/dos/MS3A.DRV=WINDOWS/SYSTEM/MOUSE.DRV" \
        -f "$out/MOUSX.DLL=WINDOWS/SYSTEM/MOUSX.DLL" "$@"
else
    set -- -f "$real=WINDOWS/SYSTEM/MOUSE.DRV" "$@"
fi
"$root/tests/inputrun.sh" -b "$boot" -t "$total" "$script" "$out/run" -r "$@" \
    > "$out/inputrun.txt" 2>&1 || true
tr -d '\r' < "$out/run/work/c/WINDOWS/TK.LOG" > "$out/md2.log" 2>/dev/null || : > "$out/md2.log"
grep -q '^MD2 done' "$out/md2.log" && echo "md2aprobe: done, $out/md2.log" ||
    { echo "md2aprobe: MD2 did not finish ($out/md2.log)"; exit 1; }
