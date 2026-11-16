#!/bin/sh
# Run the OLE probe client OC1 with a script on the reference Windows 3.1
# (scratch copy, tests/refrun.sh) and collect its log C:\OC.LOG, and every
# C:\*.BIN, *.BMP, *.WRI, *.CRD it leaves, in <outdir>.
#   usage: tests/ole/ocrun.sh <script.txt> <outdir> [OLECLI.DLL]
# With an OLECLI.DLL, that file replaces SYSTEM\OLECLI.DLL in the copy.
# OCRUN_FILES: more host files for C:\ (refrun's REFRUN_FILES syntax).
# OCRUN_MODE: /S (default) or /3.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
script=$1 out=$2 dll=${3:-}
mkdir -p "$out"
sed 's/\r$//; s/$/\r/' "$script" > "$out/OC.TXT"
files="$out/OC.TXT $root/build/dos/OCSPY.DLL ${OCRUN_FILES:-}"
[ -n "$dll" ] && files="$files $dll=WINDOWS/SYSTEM/OLECLI.DLL"
get="OC.LOG"
for n in $(grep -o 'C:\\[A-Za-z0-9_]*\.[A-Za-z]*' "$script" | sed 's/^C:\\//' | sort -u); do
    get="$get $n"
done
REFRUN_SHELL=${OCRUN_SHELL:-PROGMAN.EXE} REFRUN_FILES="$files" REFRUN_OUT="$out" REFRUN_GET="$get ${OCRUN_GET:-}" \
    REFRUN_TIMEOUT=${OCRUN_TIMEOUT:-180} \
    "$root/tests/refrun.sh" "$root/build/dos/OC1.EXE" "${OCRUN_MODE:-/S}" > "$out/refrun.txt" || true
[ -f "$out/OC.LOG" ] && tr -d '\r' < "$out/OC.LOG" > "$out/oc.log"
exit 0
