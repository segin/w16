#!/bin/sh
# Mouse stress test (drivers/mouse/README.md): long runs of MD3.EXE
# (tests/apps/md3.c) as the shell, which keeps Windows going to real mode
# (DOS file I/O, the idle loop's INT 28h), while the pointer is moved back
# and forth without a pause (xdotool, tests/inputrun.sh, DOSBox's
# autolock off); MD3 logs every stall of the pointer (no motion for 5 s)
# with the interrupt controllers' registers. For the build host: runs are
# long and several go at once (ARCHITECTURE.md 9.2).
#   usage: tests/mousestress.sh [options]
#     -D ms|w16   the DOS extender (default ms: Microsoft's DOSX)
#     -M ms|w16|lh  MOUSE.DRV (default ms; lh: MOUSELH.DRV, w16's driver
#                 enabling the device before it takes INT 74h, as 3.1's)
#     -U ms|w16   USER and GDI (default ms)
#     -K ms|w16   KRNL286 (default w16; -a needs w16's)
#     -a          [w16] idle1689=1: KERNEL's idle loop also makes INT 2Fh
#                 AX=1689h (as w16 did before; an accelerator)
#     -r MS       [w16] md3reenable=MS: MD3 calls the driver's Disable and
#                 Enable every MS milliseconds (every Enable is a chance
#                 for a packet to come while the driver sets itself up)
#     -L          load MS2 (tests/dos/ms2.asm) before WIN, its log wrapping:
#                 at a stall MD3 lists its last records (INT 15h C2xxh,
#                 INT 74h, the C207h handler's calls; values only)
#     -t SECS     each run's length (default 600)
#     -n RUNS     runs (default 4), -j JOBS at a time (default 4)
#     -o DIR      results (default build/mousestress-HHMMSS)
# Prints one line
# per run (stalls, the first one's time) and the totals; exit status 1 if
# any run stalled or did not finish.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dosx=ms mouse=ms user=ms kern=w16 accel=0 reen=0 ms2= secs=600 runs=4 jobs=4 out=
while getopts D:M:U:K:ar:Lt:n:j:o: opt; do
    case $opt in
    D) dosx=$OPTARG ;; M) mouse=$OPTARG ;; U) user=$OPTARG ;; K) kern=$OPTARG ;; a) accel=1 ;; r) reen=$OPTARG ;; L) ms2=1 ;;
    t) secs=$OPTARG ;; n) runs=$OPTARG ;; j) jobs=$OPTARG ;; o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -n "$out" ] || out=$root/build/mousestress-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
set -- -f "$root/build/dos/MD3.EXE=MD3.EXE" -i 'boot:shell=C:\MD3.EXE' \
    -i "w16:md3secs=$secs" -i "w16:idle1689=$accel" -i "w16:md3reenable=$reen"
[ "$dosx" = w16 ] && set -- "$@" -x
[ "$kern" = ms ] && set -- "$@" -r
if [ -n "$ms2" ]; then
    printf 'C:\\MS2.COM R\r\nWIN /S /B\r\n' > "$out/RUNL.BAT"
    set -- "$@" -c 'C:\RUNL.BAT' -f "$out/RUNL.BAT=RUNL.BAT" -f "$root/build/dos/MS2.COM=MS2.COM"
fi
[ "$mouse" = w16 ] && set -- "$@" -f "$root/build/dos/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV"
[ "$mouse" = lh ] && set -- "$@" -f "$root/build/dos/MOUSELH.DRV=WINDOWS/SYSTEM/MOUSE.DRV"
[ "$user" = w16 ] && set -- "$@" -f "$root/build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE" \
    -f "$root/build/dos/GDI.EXE=WINDOWS/SYSTEM/GDI.EXE"
# the input: the pointer over the window, then small motions back and
# forth for the whole run (inputrun.sh waits 0.2 s after each line: the
# pointer is at each place long enough for MD3's 250 ms samples)
scr=$out/input.txt
{
    echo "center"
    i=0
    # (more lines than the run needs: inputrun.sh ends DOSBox as soon as
    # the script is done)
    while [ $i -lt $((secs * 3 + 120)) ]; do
        echo "move 7 5"; echo "move -7 -5"
        i=$((i + 1))
    done
} > "$scr"
one() {
    k=$1; shift
    INPUTRUN_NOLOCK=1 "$root/tests/inputrun.sh" -b 20 -t $((secs + 90)) "$scr" "$out/run$k" "$@" \
        > "$out/run$k.txt" 2>&1 || true
    tr -d '\r' < "$out/run$k/work/c/WINDOWS/TK.LOG" > "$out/run$k.log" 2>/dev/null || : > "$out/run$k.log"
}
k=1
while [ $k -le "$runs" ]; do
    one $k "$@" &
    [ $((k % jobs)) = 0 ] && wait
    k=$((k + 1))
done
wait
bad=0 total=0
k=1
while [ $k -le "$runs" ]; do
    l=$(grep '^MD3 done' "$out/run$k.log" || true)
    s=$(grep -m1 '^STALL' "$out/run$k.log" || true)
    if [ -z "$l" ]; then
        echo "run$k: did not finish ${s:+($s)}"; bad=$((bad + 1))
    else
        n=$(echo "$l" | sed 's/.*stalls=\([0-9]*\).*/\1/')
        total=$((total + n))
        [ "$n" = 0 ] || bad=$((bad + 1))
        echo "run$k: $l ${s:+| $s}"
    fi
    k=$((k + 1))
done
echo "mousestress: dosx=$dosx mouse=$mouse user=$user kernel=$kern accel=$accel reenable=$reen secs=$secs: $bad of $runs runs stalled or unfinished, $total stalls ($out)"
[ $bad = 0 ]
