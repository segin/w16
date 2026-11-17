#!/bin/sh
# Run the LZEXPAND probe LZP.EXE (tests/lzexpand/lzp.c) as the shell of a
# scratch copy of the reference Windows 3.1 (black-box observation;
# ARCHITECTURE.md, LZEXPAND), with Microsoft's LZEXPAND.DLL or w16's
# (build/dos/LZEXPAND.DLL), on the files tests/lzexpand/mkinputs.py makes
# (w16's own compressor; put in C:\LZ). OUTDIR/lzp.log is its log.
#   usage: tests/lzexpand/lzrun.sh OUTDIR [ms|w16] [mode]
# mode: WIN's switch, /S (default) or /3.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 lib=${2:-ms} mode=${3:-/S}
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/in" "$out/SYS" "$out/LZP.LOG" "$out/lzp.log"
python3 "$root/tests/lzexpand/mkinputs.py" "$out/in"
files=
for f in $(cd "$out/in" && find . -type f | sed 's|^\./||'); do
    files="$files $out/in/$f=LZ/$f"
done
: > "$out/KEEP"
files="$files $out/SYS/SYSFILE.TX_=WINDOWS/SYSTEM/SYSFILE.TX_ $out/KEEP=LZOUT/KEEP"
if [ "$lib" = w16 ]; then files="$files $dos/LZEXPAND.DLL=WINDOWS/SYSTEM/LZEXPAND.DLL"; fi
# LZRUN_EXTRA: more files, HOSTPATH=PATH/UNDER/C entries
files="$files ${LZRUN_EXTRA:-}"
# LZRUN_ARG: the probe's argument (C:\LZP.ARG), e.g. x: the exploration
if [ -n "${LZRUN_ARG:-}" ]; then
    printf '%s' "$LZRUN_ARG" > "$out/LZP.ARG"
    files="$files $out/LZP.ARG"
fi
REFRUN_FILES="$files" REFRUN_OUT=$out REFRUN_GET="LZP.LOG" \
    REFRUN_TIMEOUT=${LZRUN_TIMEOUT:-180} "$root/tests/refrun.sh" "$dos/LZP.EXE" "$mode" \
    > "$out/refrun.txt" 2>&1 || true
if [ -f "$out/LZP.LOG" ]; then
    tr -d '\r' < "$out/LZP.LOG" > "$out/lzp.log"
    cat "$out/lzp.log"
else
    echo "no LZP.LOG" >&2
    cat "$out/refrun.txt" >&2
    exit 3
fi
