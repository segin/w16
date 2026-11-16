#!/bin/sh
# Run one DDEML probe script on the reference Windows 3.1 (black-box
# observation; ARCHITECTURE.md, DDEML): DM.EXE (tests/ddeml/dm.c) is the
# shell and runs tests/ddeml/scripts/NAME.dm (as C:\DM.TXT), with
# OSSPY.DLL logging every DDE message and every probe line to C:\OS.LOG,
# which ends up in OUTDIR/os.log (and is printed).
#   usage: tests/ddeml/dmrun.sh NAME OUTDIR [cli=ms|w16] [srv=ms|w16] [mode]
# cli is the DDEML.DLL the DM.EXE copies use (Microsoft's or
# build/dos/DDEML.DLL); srv the library behind DMX.EXE (copies a script
# starts with "startx"): a copy of Microsoft's or w16's DDEML renamed to
# module DDEMX (tools/nerename.py), with DMX.EXE importing it, so that
# two DDEML libraries can be compared against each other in one session.
# These renamed copies are made at test time in OUTDIR only (test-only
# combination, ARCHITECTURE.md 3).
# NAME.get: more files (paths under C:\) to fetch into OUTDIR.
# DMRUN_FILES: more REFRUN_FILES entries.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
name=$1 out=$2 cli=${3:-ms} srv=${4:-ms} mode=${5:-/S}
dos=$root/build/dos
ref=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/DDEML.DLL
s=$root/tests/ddeml/scripts
mkdir -p "$out"
sed 's/$/\r/' "$s/$name.dm" > "$out/DM.TXT"
lib() { if [ "$1" = w16 ]; then echo "$dos/DDEML.DLL"; else echo "$ref"; fi; }
cp "$(lib "$srv")" "$out/DDEMX.DLL"
python3 "$root/tools/nerename.py" "$out/DDEMX.DLL" --module DDEMX > /dev/null
cp "$dos/DM.EXE" "$out/DMX.EXE"
python3 "$root/tools/nerename.py" "$out/DMX.EXE" --module DX --import DDEML=DDEMX > /dev/null
files="$out/DM.TXT $dos/OSSPY.DLL=WINDOWS/OSSPY.DLL $out/DMX.EXE $out/DDEMX.DLL=WINDOWS/SYSTEM/DDEMX.DLL"
if [ "$cli" = w16 ]; then files="$files $dos/DDEML.DLL=WINDOWS/SYSTEM/DDEML.DLL"; fi
for f in ${DMRUN_FILES:-}; do files="$files $f"; done
rm -f "$out/OS.LOG" "$out/os.log" "$out"/*.BMP
get="OS.LOG"
if [ -f "$s/$name.get" ]; then get="$get $(cat "$s/$name.get")"; fi
REFRUN_FILES="$files" REFRUN_OUT=$out REFRUN_GET="$get" \
    REFRUN_TIMEOUT=${DMRUN_TIMEOUT:-180} "$root/tests/refrun.sh" "$dos/DM.EXE" "$mode" \
    > "$out/refrun.txt" 2>&1 || true
if [ -f "$out/OS.LOG" ]; then
    tr -d '\r' < "$out/OS.LOG" > "$out/os.log"
    cat "$out/os.log"
else
    echo "no OS.LOG" >&2
    cat "$out/refrun.txt" >&2
    exit 3
fi
