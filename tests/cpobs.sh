#!/bin/sh
# Control Panel probe runner: runs a SNAP.EXE script (one command per line,
# see tests/apps/snap.c) as the shell of a scratch copy of the reference
# Windows 3.1 and collects the screenshots, the INI files and the script's
# log into OUT. Extra files (src=path/under/C, e.g. our CONTROL.EXE over
# Microsoft's) come from CPOBS_FILES.
#   usage: tests/cpobs.sh SCRIPT OUT [mode]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
script=$1
out=$2
mkdir -p "$out"
sed -e "s/^snap \([^:]*\)\r*$/snap C:\\\\\1/" -e 's/\r*$/\r/' "$script" > "$out/SNAP.TXT"
REFRUN_FILES="$out/SNAP.TXT ${CPOBS_FILES:-}" REFRUN_OUT=$out \
REFRUN_GET="${CPOBS_GET:-WINDOWS/CONTROL.INI WINDOWS/WIN.INI WINDOWS/SYSTEM.INI}" \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" "${3:-/S}" > "$out/log.txt"
for f in "$out"/*.BMP; do
    [ -f "$f" ] && magick "$f" "${f%.BMP}.png"
done
true
