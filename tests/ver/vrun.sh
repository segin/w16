#!/bin/sh
# Run the VER.DLL probe VRP.EXE (tests/ver/vrp.c) as the shell of a
# scratch copy of the reference Windows 3.1 (black-box observation;
# ARCHITECTURE.md, VER), with Microsoft's VER.DLL or w16's
# (build/dos/VER.DLL), on the files tests/ver/mkinputs.py makes (C:\VT,
# and D:\VT on a second drive; C:\VT\PATH is on the PATH).
# OUTDIR/vrp.log is its log.
#   usage: tests/ver/vrun.sh OUTDIR [ms|w16] [mode]
# mode: WIN's switch, /S (default) or /3.
# VRUN_ARG: the sections to run (C:\VRP.ARG; letters, vrp.c's header);
# VRUN_LZ=w16: w16's LZEXPAND.DLL too; VRUN_X=1: in DOSBox-X (which keeps
# read-only attributes); VRUN_EXTRA: more HOSTPATH=PATH
# files.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 lib=${2:-ms} mode=${3:-/S}
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/in" "$out/VRP.LOG" "$out/vrp.log" "$out/VRP.ARG"
python3 "$root/tests/ver/mkinputs.py" "$out/in"
files=
for f in $(cd "$out/in/c" && find . -type f | sed 's|^\./||'); do
    files="$files $out/in/c/$f=$f"
done
if [ "$lib" = w16 ]; then files="$files $dos/VER.DLL=WINDOWS/SYSTEM/VER.DLL"; fi
if [ "${VRUN_LZ:-}" = w16 ]; then files="$files $dos/LZEXPAND.DLL=WINDOWS/SYSTEM/LZEXPAND.DLL"; fi
files="$files ${VRUN_EXTRA:-}"
if [ -n "${VRUN_ARG:-}" ]; then
    printf '%s' "$VRUN_ARG" > "$out/VRP.ARG"
    files="$files $out/VRP.ARG"
fi
REFRUN_X=${VRUN_X:-} REFRUN_FILES="$files" REFRUN_OUT=$out REFRUN_GET="VRP.LOG" REFRUN_DDRIVE=$out/in/d \
    REFRUN_PRE='SET PATH=C:\VT\PATH;Z:\' \
    REFRUN_TIMEOUT=${VRUN_TIMEOUT:-300} "$root/tests/refrun.sh" "$dos/VRP.EXE" "$mode" \
    > "$out/refrun.txt" 2>&1 || true
if [ -f "$out/VRP.LOG" ]; then
    tr -d '\r' < "$out/VRP.LOG" > "$out/vrp.log"
    cat "$out/vrp.log"
else
    echo "no VRP.LOG" >&2
    cat "$out/refrun.txt" >&2
    exit 3
fi
