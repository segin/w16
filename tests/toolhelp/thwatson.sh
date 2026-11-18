#!/bin/sh
# Dr. Watson (Windows 3.1's DRWATSON.EXE, a real TOOLHELP.DLL caller:
# NotifyRegister, InterruptRegister, StackTraceCSIPFirst/Next,
# TerminateApp ...) catching a GP fault of the probe's child THC.EXE
# (tests/toolhelp/thp.c, section w), as the shell of a scratch Windows:
#   ms    3.1 (Microsoft's KRNL286, tests/dropin.sh -r) with its TOOLHELP
#   w16   the same with w16's TOOLHELP.DLL
#   tree  w16's install tree alone (its KRNL286, its TOOLHELP) at
#         C:\WINDOWS, Dr. Watson copied in from the reference install
# Screenshots after DROPIN_SHOTS seconds (default "25 40") and at the
# end; OUTDIR/drwatson.log is Dr. Watson's log, OUTDIR/thp.log the
# probe's. Black-box observation only.
#   usage: tests/toolhelp/thwatson.sh OUTDIR [ms|w16|tree]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 how=${2:-ms}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/work" "$out"/*.png "$out/drwatson.log" "$out/thp.log"
printf 'w' > "$out/THP.ARG"
set -- -f "$dos/THP.EXE=THP.EXE" -f "$dos/THC.EXE=THC.EXE" -f "$out/THP.ARG=THP.ARG" \
    -i 'boot:shell=C:\THP.EXE'
case $how in
ms)   set -- "$@" -r ;;
w16)  set -- "$@" -r -f "$dos/TOOLHELP.DLL=WINDOWS/SYSTEM/TOOLHELP.DLL" ;;
tree) set -- "$@" -T "$root/build/w16" -f "$ref/DRWATSON.EXE=WINDOWS/DRWATSON.EXE"
      export DROPIN_TREEDIR=WINDOWS ;;
esac
DROPIN_FIXED=1 DROPIN_SHOTS=${DROPIN_SHOTS:-25 40} "$root/tests/dropin.sh" -t "${THWATSON_TIMEOUT:-60}" \
    -o "$out/work" -s "$out/end.png" -c 'WIN /S /B' "$@" > "$out/run.txt" 2>&1 || true
for f in WINDOWS/DRWATSON.LOG THP.LOG; do
    [ -f "$out/work/c/$f" ] && tr -d '\r' < "$out/work/c/$f" > "$out/$(basename "$f" | tr A-Z a-z)"
done
ls "$out"
