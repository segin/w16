#!/bin/sh
# Black-box observation of Write (probes WR*): run a SNAP.EXE script on the
# reference Windows 3.1 (Standard mode) and keep everything it produced.
#   usage: tests/wrobs.sh <script.txt> <outdir> [host files for C:\ ...]
# The script is a SNAP.EXE script (tests/apps/snap.c); "run WRITE.EXE"
# starts the original. WROBS_EXE=path runs ours instead: the script's
# "run WRITE.EXE" becomes "run C:\WRITE.EXE" with that file copied to C:\.
# Afterwards <outdir> holds the screenshots (*.BMP), every C:\*.WRI,
# C:\*.DOC, C:\*.TXT, C:\*.BKP, the files named in WROBS_GET or on the
# script's "; get:" lines (paths under C:\), SNAP's log (log.txt) and
# WIN.INI. "; ini:" lines set WIN.INI keys.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
script=$1 out=$2
shift 2
mkdir -p "$out"
if [ -n "${WROBS_EXE:-}" ]; then
    sed 's/^run WRITE.EXE/run C:\\WRITE.EXE/; s/$/\r/' "$script" > "$out/SNAP.TXT"
    set -- "$@" "$WROBS_EXE"
else
    sed 's/$/\r/' "$script" > "$out/SNAP.TXT"
fi
# the caret must not blink while screenshots are taken: a 65-second blink
# (Write draws its own caret at GetCaretBlinkTime intervals)
tr -d '\r' < "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" | sed 's/$/\r/' > "$out/WIN.INI.in"
python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "windows:CursorBlinkRate=${WROBS_BLINK:-65000}"
# WROBS_INI="section:key=value ..." sets more WIN.INI keys (e.g. colours)
for s in ${WROBS_INI:-}; do
    python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "$s"
done
# the script's "; ini: section:key=value" lines too (values may hold
# spaces), e.g. a printer (WR29)
sed -n 's/^; ini: *//p' "$script" | while IFS= read -r s; do
    python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "$s"
done
files="$out/SNAP.TXT $out/WIN.INI.in=WINDOWS/WIN.INI $*"
# what to bring back: the inputs' names (re-saved) plus the usual outputs
get="WINDOWS/WIN.INI ${WROBS_GET:-} $(sed -n 's/^; get: *//p' "$script" | tr '\n' ' ')"
for n in $(sed -n 's/^type C:\\\(.*\)$/\1/p' "$script" | tr a-z A-Z); do get="$get $n"; done
for f in "$@"; do get="$get $(basename "$f" | tr a-z A-Z)"; done
for f in "$@"; do b=$(basename "$f" | tr a-z A-Z); get="$get ${b%.*}.BKP"; done
REFRUN_FILES="$files" REFRUN_OUT=$out REFRUN_GET="$get" \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" "${WROBS_MODE:-/S}" > "$out/log.txt" || true
grep -v '^WIN ' "$out/log.txt" | head -40
