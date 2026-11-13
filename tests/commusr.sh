#!/bin/sh
# USER's communications functions (OpenComm ... EnableCommNotification),
# w16's USER against Windows 3.1's: a probe (default CM2.EXE,
# tests/apps/cm2.c; or CM1.EXE, tests/apps/cm1.c) runs as the shell with
# tests/commhost.py at the far end of COM1 and COM2 (DOSBox's nullmodem
# ports, as tests/commprobe.sh), once on Microsoft's USER ("ms") and once
# on w16's ("w16"). With COMMREC (tests/apps/commrec.c) in front of the
# communications driver, every call USER makes to the driver is recorded
# too (C:\COMMREC.LOG): the driver is a scratch copy renamed MCOM
# (tools/nerename.py: its module name only) installed as SYSTEM\MCOM.DLL,
# COMMREC as SYSTEM\COMM.DRV. Compared: the probe's TK.LOG without its
# "OBS ~" lines, COMMREC's log with its pointers and window handles
# reduced to what is not layout (tests/scripts/commrec.py), and
# commhost's records.
#   usage: tests/commusr.sh [-o DIR] [-e EXE] [-n] [-d DRV] [-a] [-r NAME] [-p N]
#     -o DIR   keep everything in DIR (default build/commusr-HHMMSS)
#     -e EXE   the probe (default build/dos/CM2.EXE)
#     -n       no COMMREC: the driver as COMM.DRV itself
#     -d DRV   the driver (default Windows 3.1's SYSTEM\COMM.DRV;
#              build/dos/COMM.DRV is w16's)
#     -a       the w16 run on every w16 part (WIN.COM, DOSX, KRNL286, USER,
#              GDI) instead of w16's USER on Microsoft's
#     -r NAME  only that run (ms or w16); nothing compared
#     -p N     CM1's parts (SYSTEM.INI [w16] cm1parts, as commprobe.sh -p)
# Exit status 0 when the runs agree.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= exe=$root/build/dos/CM2.EXE rec=1 drv=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/COMM.DRV
all= only= parts=15
while getopts o:e:nd:ar:p: opt; do
    case $opt in
    o) out=$OPTARG ;;
    e) exe=$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG") ;;
    n) rec= ;;
    d) drv=$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG") ;;
    a) all=1 ;;
    r) only=$OPTARG ;;
    p) parts=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -n "$out" ] || out=$root/build/commusr-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
name=$(basename "$exe" | tr a-z A-Z)
# the driver: as COMM.DRV, or renamed MCOM behind COMMREC
if [ -n "$rec" ]; then
    cp "$drv" "$out/MCOM.DLL"
    chmod u+w "$out/MCOM.DLL"
    python3 "$root/tools/nerename.py" "$out/MCOM.DLL" --module MCOM
    drvfiles="-f $out/MCOM.DLL=WINDOWS/SYSTEM/MCOM.DLL -f $root/build/dos/COMMREC.DRV=WINDOWS/SYSTEM/COMM.DRV"
else
    drvfiles="-f $drv=WINDOWS/SYSTEM/COMM.DRV"
fi
freeport() {
    python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}
run() {
    run=$1; shift
    port=$(freeport)
    python3 "$root/tests/commhost.py" -t 700 -l "$out/$run.host" "$port" > "$out/$run.hostout" 2>&1 &
    hpid=$!
    port2=$(freeport)
    python3 "$root/tests/commhost.py" -t 700 -l "$out/$run.host2" "$port2" > "$out/$run.hostout2" 2>&1 &
    hpid2=$!
    sleep 0.5
    conf="[serial]
serial1=nullmodem server:127.0.0.1 port:$port
serial2=nullmodem server:127.0.0.1 port:$port2"
    # shellcheck disable=SC2086
    DROPIN_CONF=$conf "$root/tests/dropin.sh" -t 600 -o "$out/$run" -c 'WIN /S' \
        -f "$exe=$name" -i "boot:shell=C:\\$name" -i "w16:cm1parts=$parts" $drvfiles "$@" \
        > "$out/$run.txt" 2>&1 || true
    kill "$hpid" "$hpid2" 2>/dev/null || true
    wait "$hpid" "$hpid2" 2>/dev/null || true
    tr -d '\r' < "$out/$run/c/WINDOWS/TK.LOG" > "$out/$run.log" 2>/dev/null || : > "$out/$run.log"
    grep -v '^OBS ~' "$out/$run.log" > "$out/$run.cmp" || true
    if [ -n "$rec" ]; then
        tr -d '\r' < "$out/$run/c/COMMREC.LOG" > "$out/$run.rec" 2>/dev/null || : > "$out/$run.rec"
        python3 "$root/tests/scripts/commrec.py" "$out/$run.log" "$out/$run.rec" > "$out/$run.reccmp"
    fi
}
case $only in
''|ms) run ms -r ;;
esac
case $only in
''|w16)
    if [ -n "$all" ]; then
        run w16 -w -x -f "$root/build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE" \
            -f "$root/build/dos/GDI.EXE=WINDOWS/SYSTEM/GDI.EXE"
    else
        run w16 -r -f "$root/build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE"
    fi ;;
esac
[ -z "$only" ] || { echo "commusr: $only run in $out"; exit 0; }
fails=0
for r in ms w16; do
    grep -q -E '^(CM1|CM2) done' "$out/$r.log" || { echo "FAIL: the $r run did not finish ($out/$r.log)"; fails=1; }
done
kinds="cmp host host2"
[ -n "$rec" ] && kinds="$kinds reccmp"
for k in $kinds; do
    if cmp -s "$out/ms.$k" "$out/w16.$k"; then
        echo "ok   $k: w16's USER the same as Windows 3.1's"
    else
        echo "FAIL $k: w16's USER differs from Windows 3.1's:"
        diff "$out/ms.$k" "$out/w16.$k" | head -30
        fails=1
    fi
done
echo "commusr: logs in $out"
exit $fails
