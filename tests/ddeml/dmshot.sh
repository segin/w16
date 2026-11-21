#!/bin/sh
# Run a DDEML probe script (tests/ddeml/scripts/NAME.dm, DM.EXE as the
# shell, as tests/ddeml/dmrun.sh) on a virtual X display, with screenshots
# and keys from an input script (tests/inputrun.sh): for what a probe
# cannot see itself, such as a system-modal box, which stops every other
# task (the DDEML termination dialog). Never the user's desktop.
#   usage: tests/ddeml/dmshot.sh NAME INPUT OUTDIR [ms|w16] [secs]
# INPUT: tests/inputrun.sh script lines (sleep, shot NAME, key KEYS, ...).
# OUTDIR (new) gets the shots, input.log and os.log (the probe's log).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
name=$1 input=$2 out=$3 lib=${4:-ms} secs=${5:-120}
dos=$root/build/dos
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
sed 's/$/\r/' "$root/tests/ddeml/scripts/$name.dm" > "$tmp/DM.TXT"
set -- -r -c "WIN /S" -t "$secs" -i "boot:shell=C:\\DM.EXE" \
    -f "$dos/DM.EXE=DM.EXE" -f "$tmp/DM.TXT=DM.TXT" -f "$dos/OSSPY.DLL=WINDOWS/OSSPY.DLL"
if [ "$lib" = w16 ]; then set -- "$@" -f "$dos/DDEML.DLL=WINDOWS/SYSTEM/DDEML.DLL"; fi
"$root/tests/inputrun.sh" -b 3 -t "$secs" "$input" "$out" "$@" > "$tmp/run.txt" 2>&1 || true
if [ -f "$out/work/c/OS.LOG" ]; then
    tr -d '\r' < "$out/work/c/OS.LOG" > "$out/os.log"
fi
cat "$out/os.log" 2>/dev/null || cat "$tmp/run.txt"
