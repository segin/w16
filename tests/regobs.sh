#!/bin/sh
# Black-box observation of the Registration Info Editor (probes RE*): run a
# SNAP.EXE script on the reference Windows 3.1 (Standard mode) and keep
# what it produced (ARCHITECTURE.md 15.8).
#   usage: tests/regobs.sh <script.txt> <outdir> [host files for C:\ ...]
# The script is a SNAP.EXE script (tests/apps/snap.c); "run REGEDIT.EXE"
# starts the original from C:\WINDOWS. REGOBS_EXE=path runs that file
# instead, copied over C:\WINDOWS\REGEDIT.EXE. REGOBS_SHELL=path puts that
# SHELL.DLL in WINDOWS\SYSTEM.
# Script directives:
#   ; ini: section:key=value       a WIN.INI key
#   ; sysini: section:key=value    a SYSTEM.INI key
#   ; get: PATH                    a file under C:\ to bring back
#   ; put: SRC=DEST                a file of the checkout as C:\DEST
# Afterwards <outdir> holds the screenshots (*.BMP), SNAP's log (log.txt),
# WIN.INI and REG.DAT as the session left them, and the files asked for.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
script=$1 out=$2
shift 2
mkdir -p "$out"
sed 's/$/\r/' "$script" > "$out/SNAP.TXT"
files="$out/SNAP.TXT"
if [ -n "${REGOBS_EXE:-}" ]; then
    cp "$REGOBS_EXE" "$out/REGEDIT.EXE"
    files="$files $out/REGEDIT.EXE=WINDOWS/REGEDIT.EXE"
fi
if [ -n "${REGOBS_TRACE:-}" ]; then
    # probe RE15: a scratch copy of the program with its SHELL imports
    # renamed to XHELL (tests/apps/xhell.c), which logs every registration
    # call to C:\XHELL.LOG and passes it on
    src=${REGOBS_EXE:-$root/.tools/ref/win31/cdrive/WINDOWS/REGEDIT.EXE}
    cp "$src" "$out/REGEDIT.EXE"
    python3 -I "$root/tools/nerename.py" "$out/REGEDIT.EXE" --import SHELL=XHELL > /dev/null
    files="$files $out/REGEDIT.EXE=WINDOWS/REGEDIT.EXE $root/build/dos/XHELL.DLL=WINDOWS/SYSTEM/XHELL.DLL"
    REGOBS_GET="${REGOBS_GET:-} XHELL.LOG"
fi
if [ -n "${REGOBS_SHELL:-}" ]; then
    files="$files $REGOBS_SHELL=WINDOWS/SYSTEM/SHELL.DLL"
fi
# carets must not blink while screenshots are taken
ref=$root/.tools/ref/win31/cdrive
tr -d '\r' < "$ref/WINDOWS/WIN.INI" | sed 's/$/\r/' > "$out/WIN.INI.in"
python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "windows:CursorBlinkRate=${REGOBS_BLINK:-65000}"
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
# "; put: SRC=DEST": a file of the checkout as C:\DEST
for p in $(sed -n 's/^; put: *//p' "$script"); do
    files="$files $root/${p%%=*}=${p#*=}"
done
get="WINDOWS/WIN.INI WINDOWS/REG.DAT ${REGOBS_GET:-} $(sed -n 's/^; get: *//p' "$script" | tr '\n' ' ')"
REFRUN_FILES="$files $*" REFRUN_OUT=$out REFRUN_GET="$get" \
    REFRUN_TIMEOUT=${REGOBS_TIMEOUT:-180} \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" "${REGOBS_MODE:-/S}" > "$out/log.txt" || true
for f in "$out"/*.BMP; do
    [ -f "$f" ] && magick "$f" "${f%.BMP}.png" && rm -f "$f"
done
tail -n 3 "$out/log.txt"
