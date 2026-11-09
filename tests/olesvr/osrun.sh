#!/bin/sh
# Run one OLESVR probe script on the reference Windows 3.1 (black-box
# observation): OS2.EXE is the shell and runs tests/olesvr/scripts/
# NAME.os2 (copied as C:\OS2.TXT; NAME.os1, if there is one, becomes
# C:\OS1.TXT for the OS1 server), with OSSPY.DLL logging every DDE message
# to C:\OS.LOG, which ends up in OUTDIR/os.log (and is printed).
#   usage: tests/olesvr/osrun.sh NAME OUTDIR [svr=ms|w16] [cli=ms|w16] [mode]
# svr picks the OLESVR.DLL (Microsoft's or build/dos's), cli the
# OLECLI.DLL (build/dos/OLECLI.DLL, once the OLECLI agent's module exists).
# NAME.snap, if there is one, becomes C:\SNAP.TXT for SNAP.EXE (the OS2
# script starts it), whose screenshots (C:\*.BMP) land in OUTDIR.
# OSRUN_FILES: more REFRUN_FILES entries; OSRUN_GET: more files to fetch.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
name=$1 out=$2 svr=${3:-ms} cli=${4:-ms} mode=${5:-/S}
dos=$root/build/dos
s=$root/tests/olesvr/scripts
mkdir -p "$out"
sed 's/$/\r/' "$s/$name.os2" > "$out/OS2.TXT"
files="$out/OS2.TXT $dos/OSSPY.DLL=WINDOWS/OSSPY.DLL $dos/OS1.EXE"
if [ -f "$s/$name.os1" ]; then
    sed 's/$/\r/' "$s/$name.os1" > "$out/OS1.TXT"
    files="$files $out/OS1.TXT"
fi
if [ -f "$s/$name.snap" ]; then           # UI steps for SNAP.EXE (tests/apps/snap.c)
    sed 's/$/\r/' "$s/$name.snap" > "$out/SNAP.TXT"
    files="$files $out/SNAP.TXT $dos/SNAP.EXE"
fi
if [ "$svr" = w16 ]; then files="$files $dos/OLESVR.DLL=WINDOWS/SYSTEM/OLESVR.DLL"; fi
if [ "$cli" = w16 ]; then files="$files $dos/OLECLI.DLL=WINDOWS/SYSTEM/OLECLI.DLL"; fi
for f in ${OSRUN_FILES:-}; do files="$files $f"; done
rm -f "$out/OS.LOG" "$out/os.log" "$out"/*.BMP
get="OS.LOG ${OSRUN_GET:-}"
if [ -f "$s/$name.get" ]; then get="$get $(cat "$s/$name.get")"; fi   # files the scenario saves
REFRUN_FILES="$files" REFRUN_OUT=$out REFRUN_GET="$get" \
    REFRUN_TIMEOUT=${OSRUN_TIMEOUT:-180} "$root/tests/refrun.sh" "$dos/OS2.EXE" "$mode" \
    > "$out/refrun.txt" 2>&1 || true
if [ -f "$out/OS.LOG" ]; then
    tr -d '\r' < "$out/OS.LOG" > "$out/os.log"
    cat "$out/os.log"
else
    echo "no OS.LOG" >&2
    cat "$out/refrun.txt" >&2
    exit 3
fi
