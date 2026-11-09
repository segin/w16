#!/bin/sh
# Run the DX probes (tests/dos/dxN.asm) under Microsoft's DOSX and under
# w16's DOSX, and compare their logs.
#
# Each probe is a .COM image that runs in the KERNEL slot: a scratch copy
# of the reference install (.tools/ref/win31/cdrive) gets it as
# WINDOWS\SYSTEM\KRNL286.EXE, KRNL386.EXE is removed, and "WIN /S" runs
# in headless DOSBox: Microsoft's WIN.COM and WSWAP.EXE start the DOSX
# under test, which starts the probe (ARCHITECTURE.md 9.4, 9.6). For the
# w16 runs, build/dos/DOSX.EXE replaces WINDOWS\SYSTEM\DOSX.EXE.
#
#   usage: tests/dosxtest.sh [options] [PROBE...]   (default: all DX probes)
#     -m WHICH   ms, w16 or both (default both)
#     -a ARGS    extra arguments after "WIN /S" (the probe's command tail)
#     -c CPU     386 (DOSBox, the default) or 286 (DOSBox-X, an 80286)
#     -t SECS    time limit per run (default 60)
#     -o DIR     keep the logs in DIR (default build/dosxtest)
#     -q         print only the diff and the verdict
# DOSXTEST_DOSX=FILE: use FILE as w16's DOSX (e.g. build/dos/DOSXT.EXE).
# Prints each run's log (C:\DXn.LOG, then WIN's output and errorlevel),
# then the differences between the two DOSXes. Lines that are expected to
# differ are not filtered: the diff is for reading (ARCHITECTURE.md 9.6
# lists the known differences). Exits 0 when every run produced a log.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
which=both args= cpu=386 secs=60 out=$root/build/dosxtest quiet=
while getopts m:a:c:t:o:q opt; do
    case $opt in
    m) which=$OPTARG ;;
    a) args=$OPTARG ;;
    c) cpu=$OPTARG ;;
    t) secs=$OPTARG ;;
    o) out=$OPTARG ;;
    q) quiet=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
probes=${*:-$(cd "$root/build/dos" && ls DX[0-9]*.COM 2>/dev/null | sed 's/\.COM$//' | sort -V)}
[ -n "$probes" ] || { echo "no DX probes built (make dos)" >&2; exit 2; }
case $which in
both) hosts="ms w16" ;;
ms|w16) hosts=$which ;;
*) echo "-m: ms, w16 or both" >&2; exit 2 ;;
esac
mkdir -p "$out"
status=0

run() {   # run PROBE HOST -> $out/PROBE.HOST.log
    probe=$1 host=$2
    work=$(mktemp -d)
    cp -r "$ref" "$work/c"
    sys=$work/c/WINDOWS/SYSTEM
    rm -f "$sys/KRNL386.EXE"
    cp "$root/build/dos/$probe.COM" "$sys/KRNL286.EXE"
    if [ "$host" = w16 ]; then
        cp "${DOSXTEST_DOSX:-$root/build/dos/DOSX.EXE}" "$sys/DOSX.EXE"
    fi
    if [ "$cpu" = 286 ]; then
        emu="dosbox-x -nogui -fastlaunch" xdos="quit warning=false" cputype=286
    else
        emu="dosbox -noconsole" xdos= cputype=386
    fi
    cat > "$work/dosbox.conf" <<CONF
[sdl]
output=surface
[dosbox]
memsize=16
$xdos
[cpu]
core=normal
cputype=$cputype
cycles=max
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c "$work/c"
c:
cd \\WINDOWS
WIN /S $args > C:\\OUT.TXT
if errorlevel 1 echo ERRORLEVEL=NONZERO >> C:\\OUT.TXT
echo WINEXIT >> C:\\OUT.TXT
exit
CONF
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "$secs" \
        $emu -conf "$work/dosbox.conf" >/dev/null 2>&1 || true
    log=$(echo "$probe" | tr a-z A-Z).LOG
    {
        if [ -f "$work/c/$log" ]; then tr -d '\r' < "$work/c/$log"; else echo "(no $log)"; fi
        echo "== WIN output"
        if [ -f "$work/c/OUT.TXT" ]; then tr -d '\r' < "$work/c/OUT.TXT"; else echo "(none: timed out?)"; fi
    } > "$out/$probe.$host.log"
    [ -f "$work/c/$log" ] || status=1
    rm -rf "$work"
}

for p in $probes; do
    for h in $hosts; do
        run "$p" "$h"
        if [ -z "$quiet" ]; then
            echo "== $p under $h DOSX"
            cat "$out/$p.$h.log"
        fi
    done
    if [ "$which" = both ]; then
        echo "== $p: Microsoft's DOSX (-) versus w16's (+)"
        if cmp -s "$out/$p.ms.log" "$out/$p.w16.log"; then
            echo "(identical)"
        else
            diff -u "$out/$p.ms.log" "$out/$p.w16.log" | tail -n +3 || true
        fi
    fi
done
exit $status
