#!/bin/sh
# Black-box observation of Windows Help (probes WH*): run a SNAP.EXE script
# on the reference Windows 3.1 (Standard mode) and keep what it produced
# (ARCHITECTURE.md 15.10).
#   usage: tests/whobs.sh <script.txt> <outdir> [host files for C:\ ...]
# The script is a SNAP.EXE script (tests/apps/snap.c); "run WINHELP.EXE
# FILE" starts the original from C:\WINDOWS. WHOBS_EXE=path runs that file
# instead, copied over C:\WINDOWS\WINHELP.EXE.
# Script directives:
#   ; ini: section:key=value       a WIN.INI key
#   ; get: PATH                    a file under C:\ to bring back
#   ; put: SRC=DEST                a file of the checkout as C:\DEST
#   ; hlp: NAME                    tests/winhelp/NAME.hpj built by whc
#   ; owhlp: NAME.hlp              OpenWatcom's $WATCOM/binw/NAME.hlp
# Afterwards <outdir> holds the screenshots (*.png), SNAP's log (log.txt),
# WIN.INI and WINHELP.BMK as the session left them, and the files asked for.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
script=$1 out=$2
shift 2
mkdir -p "$out"
sed 's/$/\r/' "$script" > "$out/SNAP.TXT"
files="$out/SNAP.TXT"
if [ -n "${WHOBS_EXE:-}" ]; then
    cp "$WHOBS_EXE" "$out/WINHELP.EXE"
    files="$files $out/WINHELP.EXE=WINDOWS/WINHELP.EXE"
fi
ref=$root/.tools/ref/win31/cdrive
tr -d '\r' < "$ref/WINDOWS/WIN.INI" | sed 's/$/\r/' > "$out/WIN.INI.in"
python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "windows:CursorBlinkRate=${WHOBS_BLINK:-65000}"
sed -n 's/^; ini: *//p' "$script" | while IFS= read -r s; do
    python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "$s"
done
files="$files $out/WIN.INI.in=WINDOWS/WIN.INI"
for p in $(sed -n 's/^; put: *//p' "$script"); do
    files="$files $root/${p%%=*}=${p#*=}"
done
# "; hlp: NAME": w16's own test help file tests/winhelp/NAME.hpj, built by
# OpenWatcom's whc (tests/winhelp/mkhlp.sh) as C:\WINDOWS\NAME.HLP;
# "; owhlp: NAME.hlp": one of OpenWatcom's own help files ($WATCOM/binw).
# Either missing: exit 3 (skipped).
for n in $(sed -n 's/^; hlp: *//p' "$script"); do
    u=$(echo "$n" | tr a-z A-Z)
    "$root/tests/winhelp/mkhlp.sh" "$n" "$out/$u.HLP" || exit 3
    files="$files $out/$u.HLP=WINDOWS/$u.HLP"
done
for n in $(sed -n 's/^; owhlp: *//p' "$script"); do
    f=${WATCOM:-/opt/watcom}/binw/$n
    [ -f "$f" ] || { echo "whobs: no $f" >&2; exit 3; }
    files="$files $f=WINDOWS/$(echo "$n" | tr a-z A-Z)"
done
get="WINDOWS/WIN.INI WINDOWS/WINHELP.BMK ${WHOBS_GET:-} $(sed -n 's/^; get: *//p' "$script" | tr '\n' ' ')"
REFRUN_FILES="$files $*" REFRUN_OUT=$out REFRUN_GET="$get" \
    REFRUN_TIMEOUT=${WHOBS_TIMEOUT:-180} \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" "${WHOBS_MODE:-/S}" > "$out/log.txt" || true
for f in "$out"/*.BMP; do
    [ -f "$f" ] && magick "$f" "${f%.BMP}.png" && rm -f "$f"
done
tail -n 3 "$out/log.txt"
