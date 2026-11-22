#!/bin/sh
# Black-box observation of the PIF Editor (probes PE*): run a SNAP.EXE
# script on the reference Windows 3.1 and keep what it produced
# (ARCHITECTURE.md 15.11).
#   usage: tests/pifobs.sh <script.txt> <outdir> [host files for C:\ ...]
# The script is a SNAP.EXE script (tests/apps/snap.c); "run PIFEDIT.EXE"
# starts the original from C:\WINDOWS. PIFOBS_EXE=path runs that file
# instead, copied over C:\WINDOWS\PIFEDIT.EXE. PIFOBS_MODE=/3 runs 386
# Enhanced mode (default /S, Standard mode).
# Script directives:
#   ; ini: section:key=value       a WIN.INI key
#   ; sysini: section:key=value    a SYSTEM.INI key
#   ; get: PATH                    a file under C:\ to bring back
#   ; put: SRC=DEST                a file of the checkout as C:\DEST (SRC
#                                  may name the reference install's own
#                                  files under .tools: they are copied at
#                                  test time, never into the repository)
#   ; mode: /3                     the WIN.COM switch, as PIFOBS_MODE
# Afterwards <outdir> holds the screenshots (*.png), SNAP's log (log.txt),
# WIN.INI as the session left it, and the files asked for.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
script=$1 out=$2
shift 2
mkdir -p "$out"
sed 's/$/\r/' "$script" > "$out/SNAP.TXT"
files="$out/SNAP.TXT"
if [ -n "${PIFOBS_EXE:-}" ]; then
    cp "$PIFOBS_EXE" "$out/PIFEDIT.EXE"
    files="$files $out/PIFEDIT.EXE=WINDOWS/PIFEDIT.EXE"
fi
# carets must not blink while screenshots are taken
ref=$root/.tools/ref/win31/cdrive
tr -d '\r' < "$ref/WINDOWS/WIN.INI" | sed 's/$/\r/' > "$out/WIN.INI.in"
python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "windows:CursorBlinkRate=${PIFOBS_BLINK:-65000}"
sed -n 's/^; ini: *//p' "$script" | while IFS= read -r s; do
    python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "$s"
done
files="$files $out/WIN.INI.in=WINDOWS/WIN.INI"
if grep -q '^; sysini:' "$script"; then
    tr -d '\r' < "$ref/WINDOWS/SYSTEM.INI" | sed 's/$/\r/' > "$out/SYSTEM.INI.in"
    sed -n 's/^; sysini: *//p' "$script" | while IFS= read -r s; do
        python3 -I "$root/tests/scripts/iniset.py" "$out/SYSTEM.INI.in" "$s"
    done
    files="$files $out/SYSTEM.INI.in=WINDOWS/SYSTEM.INI"
fi
for p in $(sed -n 's/^; put: *//p' "$script"); do
    files="$files $root/${p%%=*}=${p#*=}"
done
# "; var: NAME SRC [OFF=HEX ...]": SRC (of the checkout, or the reference
# install under .tools) with those bytes changed, as C:\NAME
# (tests/scripts/pifedit/pifvar.py; made here, under the output directory)
sed -n 's/^; var: *//p' "$script" | while read -r name src edits; do
    # shellcheck disable=SC2086
    python3 -I "$root/tests/scripts/pifedit/pifvar.py" "$root/$src" "$out/$name.in" $edits
done
for name in $(sed -n 's/^; var: *\([^ ]*\).*/\1/p' "$script"); do
    files="$files $out/$name.in=$name"
done
mode=${PIFOBS_MODE:-$(sed -n 's/^; mode: *//p' "$script" | head -1)}
get="WINDOWS/WIN.INI $(sed -n 's/^; get: *//p' "$script" | tr '\n' ' ')"
REFRUN_FILES="$files $*" REFRUN_OUT=$out REFRUN_GET="$get" \
    REFRUN_TIMEOUT=${PIFOBS_TIMEOUT:-180} \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" "${mode:-/S}" > "$out/log.txt" || true
for f in "$out"/*.BMP; do
    [ -f "$f" ] && magick "$f" "${f%.BMP}.png" && rm -f "$f"
done
tail -n 3 "$out/log.txt"
