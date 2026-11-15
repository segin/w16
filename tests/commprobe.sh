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
# changed (NAME.host; NAME.host2 for COM2, also a nullmodem port with a
# commhost that only records). All must be the same for the two drivers.
#   usage: tests/commprobe.sh [-o DIR] [-x] [-r NAME] [-d FILE] [-p N]
#     -o DIR   keep everything in DIR (default build/commprobe-HHMMSS)
#     -x       DOSBox-X (tests/dropin.sh -p 386) instead of DOSBox, with
#              LPT1 a printer that writes to DIR/NAME.lpt (compared too)
#     -r NAME  only that run (ms or w16); nothing compared
#     -d FILE  the driver of the w16 run (default build/dos/COMM.DRV)
#     -p N     only CM1's parts in the bit mask N (1 A, 2 B, 4 C, 8 D;
#              SYSTEM.INI [w16] cm1parts; default all)
# Exit status 0 when the runs agree.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= usex= only= drv=$root/build/dos/COMM.DRV parts=15
while getopts o:xr:d:p: opt; do
    case $opt in
    o) out=$OPTARG ;;
    x) usex=1 ;;
    r) only=$OPTARG ;;
    p) parts=$OPTARG ;;
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
    python3 "$root/tests/commhost.py" -t 700 -l "$out/$name.host" -T "$out/$name.hosttime" "$port" \
        > "$out/$name.hostout" 2>&1 &
    hpid=$!
    port2=$(freeport)
    python3 "$root/tests/commhost.py" -t 700 -l "$out/$name.host2" -T "$out/$name.hosttime2" "$port2" \
        > "$out/$name.hostout2" 2>&1 &
    hpid2=$!
    sleep 0.5
    conf="[serial]
serial1=nullmodem server:127.0.0.1 port:$port
serial2=nullmodem server:127.0.0.1 port:$port2"
    x=
    if [ -n "$usex" ]; then
        x="-p 386"
        conf="$conf
[parallel]
parallel1=file append:$out/$name.lpt"
    fi
    DROPIN_CONF=$conf "$root/tests/dropin.sh" -r $x -t 600 -o "$out/$name" -c 'WIN /S' \
        -f "$cm1=CM1.EXE" -i 'boot:shell=C:\CM1.EXE' -i "w16:cm1parts=$parts" "$@" > "$out/$name.txt" 2>&1 || true
    kill "$hpid" "$hpid2" 2>/dev/null || true
    wait "$hpid" "$hpid2" 2>/dev/null || true
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
kinds="log host host2"
if [ -n "$usex" ]; then
    kinds="$kinds lpt"
    touch "$out/ms.lpt" "$out/w16.lpt"
fi
for k in $kinds; do
    a=$out/ms.$k b=$out/w16.$k
    [ $k = log ] && a=$out/ms.cmp b=$out/w16.cmp
    if [ $k = host ]; then
        # Windows 3.1's driver sometimes loses the second byte of a write
        # that starts the transmitter again (its first byte goes alone,
        # the next one does not arrive): commhost's "#W150;" then comes
        # as "#150;", in most runs on a busy host. w16's driver loses
        # nothing; the record is compared with that byte put back
        sed 's/^#\([0-9]\)/#W\1/' "$a" > "$a.norm"
        sed 's/^#\([0-9]\)/#W\1/' "$b" > "$b.norm"
        a=$a.norm b=$b.norm
    fi
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
