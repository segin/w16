#!/bin/sh
# Run probe US27 (tests/apps/us27.c: USER's functions given a bad pointer)
# as the shell through all its cases, going on past the ones that end the
# task. Windows 3.1 ends the task for some bad pointers in a way the
# probe's fault hooks do not see (KERNEL's own Application Error, e.g.
# for a fault inside KERNEL that USER's call led to): the run then stops
# where it was. This script notes that case (the last "RUN Name.K" line
# of the log) as ended, and boots again from the next one, until the
# probe reaches its end. The logs are joined into OUT/us27.log, with
# "OBS Name.K.ok=0000" for each case that ended the task so (so that
# tools/obsref.py records it as ended) and a "KILLED Name.K" line.
#
#   usage: tests/us27run.sh OUT [dropin.sh options...]
#     e.g. tests/us27run.sh build/us27-ref286 -r -p 386x
#          tests/us27run.sh build/us27-ref386 -r -3 -p 386x
#          tests/us27run.sh build/us27-w16u -r -p 386x -f build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE
# Use -p 386x (DOSBox-X): DOSBox checks no segment limits at all (see
# tools/us27ref.py for what DOSBox-X checks).
# Every case is run (WIN.INI [US27] explore=1), also those us27ref.h says
# ended the task on 3.1; US27_EXPLORE=0 leaves those out. US27_EXE: the
# probe (default build/dos/US27.EXE); US27_T: each boot's time limit
# (default 300 s). A case counts as having ended the task when the log
# has not grown for DROPIN_WSECS seconds (default 20: a busy host can hold
# a write up for seconds).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1; shift
exe=${US27_EXE:-$root/build/dos/US27.EXE}
mkdir -p "$out"
: > "$out/us27.log"
# the probe's log is in its current directory: the Windows directory
# (C:\WINDOWS, or C:\W16 for an install tree booted alone, dropin.sh -T)
dir=WINDOWS
for a in "$@"; do [ "$a" = -T ] && dir=W16; done
skip= from= n=0
while :; do
    n=$((n + 1))
    [ $n -le 200 ] || { echo "us27run: too many restarts" >&2; exit 1; }
    rm -rf "$out/run"
    DROPIN_WSECS=${DROPIN_WSECS:-20} \
    "$root/tests/dropin.sh" "$@" -t "${US27_T:-300}" -o "$out/run" -W $dir/TK.LOG \
        -f "$exe=US27.EXE" -i 'boot:shell=C:\US27.EXE' -i "WIN.INI:US27:explore=${US27_EXPLORE:-1}" \
        ${skip:+-i "WIN.INI:US27:skip=$skip"} ${from:+-i "WIN.INI:US27:from=$from"} \
        > "$out/run$n.txt" 2>&1 || true
    log=$out/run/c/$dir/TK.LOG
    [ -f "$log" ] || { echo "us27run: no log (run $n, $out/run$n.txt)" >&2; exit 1; }
    tr -d '\r' < "$log" >> "$out/us27.log"
    if grep -q '^US27 end' "$log"; then
        break
    fi
    last=$(tr -d '\r' < "$log" | sed -n 's/^RUN //p' | tail -1)
    [ -n "$last" ] || { echo "us27run: stopped before any case (run $n, $out/run$n.txt)" >&2; exit 1; }
    echo "KILLED $last" >> "$out/us27.log"
    echo "OBS $last.ok=0000" >> "$out/us27.log"
    skip=${skip:+$skip,}$last from=$last
done
rm -rf "$out/run"
echo "us27run: $n boots, $(grep -c '^KILLED' "$out/us27.log" || true) cases ended the task ($out/us27.log)"
