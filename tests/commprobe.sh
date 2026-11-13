#!/bin/sh
# The communications driver, w16's against Windows 3.1's (drivers/comm/
# README.md): CM1.EXE (tests/apps/cm1.c) runs as the shell of Windows 3.1
# (Microsoft's KRNL286, DOSX, USER) and drives COMM.DRV directly and
# through USER, with tests/commhost.py at the other end of COM1 (DOSBox's
# nullmodem serial port, a TCP connection to commhost on this machine).
# Two runs, one after the other:
#   ms     Windows 3.1's COMM.DRV
#   w16    w16's (build/dos/COMM.DRV)
# Each gives CM1's TK.LOG (NAME.log; NAME.cmp without the "OBS ~" lines)
# and commhost's record of what came down the cable and when the lines
# changed (NAME.host). Both must be the same for the two drivers.
#   usage: tests/commprobe.sh [-o DIR] [-x] [-k] [-r NAME]
#     -o DIR   keep everything in DIR (default build/commprobe-HHMMSS)
#     -x       DOSBox-X (tests/dropin.sh -p 386) instead of DOSBox, with
#              LPT1 written to DIR/NAME.lpt
#     -r NAME  only that run (ms or w16); nothing compared
#     -d FILE  the driver of the w16 run (default build/dos/COMM.DRV)
# Exit status 0 when the runs agree.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= usex= only= drv=$root/build/dos/COMM.DRV
while getopts o:xr:d: opt; do
    case $opt in
    o) out=$OPTARG ;;
    x) usex=1 ;;
    r) only=$OPTARG ;;
    d) drv=$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG") ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -n "$out" ] || out=$root/build/commprobe-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
cm1=$root/build/dos/CM1.EXE
# a free TCP port on this machine
freeport() {
    python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}
run() {
    name=$1; shift
    port=$(freeport)
    python3 "$root/tests/commhost.py" -t 400 -l "$out/$name.host" "$port" \
        > "$out/$name.hostout" 2>&1 &
    hpid=$!
    sleep 0.5
    conf="[serial]
serial1=nullmodem server:127.0.0.1 port:$port
serial2=dummy"
    x=
    if [ -n "$usex" ]; then
        x="-p 386"
        conf="$conf
[parallel]
parallel1=file append:$out/$name.lpt"
    fi
    DROPIN_CONF=$conf "$root/tests/dropin.sh" -r $x -t 360 -o "$out/$name" -c 'WIN /S' \
        -f "$cm1=CM1.EXE" -i 'boot:shell=C:\CM1.EXE' "$@" > "$out/$name.txt" 2>&1 || true
    kill "$hpid" 2>/dev/null || true
    wait "$hpid" 2>/dev/null || true
    tr -d '\r' < "$out/$name/c/WINDOWS/TK.LOG" > "$out/$name.log" 2>/dev/null || : > "$out/$name.log"
    grep -v '^OBS ~' "$out/$name.log" > "$out/$name.cmp" || true
}
case $only in
''|ms) run ms ;;
esac
case $only in
''|w16) run w16 -f "$drv=WINDOWS/SYSTEM/COMM.DRV" ;;
esac
[ -z "$only" ] || { echo "commprobe: $only run in $out"; exit 0; }
fails=0
grep -q '^CM1 done' "$out/ms.log" || { echo "FAIL: the run with Windows 3.1's COMM.DRV did not finish ($out/ms.log)"; fails=1; }
for k in log host; do
    a=$out/ms.$k b=$out/w16.$k
    [ $k = log ] && a=$out/ms.cmp b=$out/w16.cmp
    if cmp -s "$a" "$b"; then
        echo "ok   $k: w16's COMM.DRV the same as Windows 3.1's"
    else
        echo "FAIL $k: w16's COMM.DRV differs from Windows 3.1's:"
        diff "$a" "$b" | head -30
        fails=1
    fi
done
echo "commprobe: logs in $out"
exit $fails
