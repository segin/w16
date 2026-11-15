#!/bin/sh
# Run one SNAP script on the reference Windows 3.1 (black-box observation):
# Cardfile's scenarios and probes (CF*). Files the script creates under
# C:\ (*.BMP screenshots, *.CRD card files) and the listed extras are
# copied to OUTDIR; TK.LOG goes to OUTDIR/log.txt.
#   usage: tests/cfrun.sh SCRIPT OUTDIR [extra files copied to C:\ ...]
# CFRUN_MODE=/3 runs 386 Enhanced mode (default /S). CFRUN_GET adds paths
# under C:\ to copy back (e.g. WINDOWS/WIN.INI). An extra file given as
# src=PATH/UNDER/C is copied there. CFRUN_OURS=1 runs w16's Cardfile
# (build/dos/CARDFILE.EXE, copied to C:\) wherever the script says
# "run CARDFILE.EXE".
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
script=$1 out=$2
shift 2
rm -rf "$out"
mkdir -p "$out"
ours=
if [ -n "${CFRUN_OURS:-}" ]; then
    ours=$root/build/dos/CARDFILE.EXE
    sedx='s/^run CARDFILE.EXE/run C:\\CARDFILE.EXE/'
else
    sedx=
fi
# scripts are UTF-8; Windows reads ANSI
iconv -f utf-8 -t latin1 "$script" | sed "$sedx" | sed 's/$/\r/' > "$out/SNAP.TXT"
# every C:\NAME.CRD/.BMP/... the script mentions comes back
get=$(grep -o -i 'C:\\[A-Z0-9_]*\.[A-Z0-9]*' "$script" | sed 's/^C:\\//' | sort -u | tr '\n' ' ')
REFRUN_FILES="$out/SNAP.TXT $ours $*" REFRUN_OUT=$out REFRUN_GET="$get ${CFRUN_GET:-} SNAP.TXT" \
    REFRUN_TIMEOUT=${CFRUN_TIMEOUT:-180} \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" "${CFRUN_MODE:-/S}" > "$out/log.txt" || true
for f in "$out"/*.BMP; do
    [ -f "$f" ] && magick "$f" "${f%.BMP}.png" 2>/dev/null
done
echo "output: $out"
