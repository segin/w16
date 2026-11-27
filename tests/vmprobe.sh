#!/bin/sh
# VX18 (tests/apps/vx18.c) as the shell in 386 Enhanced mode, under Windows
# 3.1's WIN386 and under w16's (build/win386/WIN386.EXE over it), with
# SYSTEM.INI's [386Enh] paging settings varied (ARCHITECTURE.md 12.17):
#   default   no paging keys: the temporary WIN386.SWP in the Windows directory
#   nopaging  Paging=no
#   max       MaxPagingFileSize=2048
#   pfile     PagingFile=C:\PAGE.SWP
#   pdrive    PagingDrive=C
#   mindisk   MinUserDiskSpace=4000000 (more than the disk has free)
#   overcommit PageOverCommit=2 (the linear address space: 0500h's fourth)
# Each scenario's observations (TK.LOG's OBS lines) under both must be the
# same, but for the lines named in $skip (the free-memory figures, which
# depend on the two WIN386s' own sizes).
#
#   usage: tests/vmprobe.sh [-o OUTDIR] [-r | -w] [SCENARIO ...]
#     -r  Windows 3.1's WIN386 only (print its observations)
#     -w  w16's only
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
out= only=
while getopts o:rw opt; do
    case $opt in
    o) out=$OPTARG ;; r) only=ref ;; w) only=w16 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
scen=${*:-default nopaging max pfile pdrive mindisk overcommit}
[ -n "$out" ] || out=$(mktemp -d)
mkdir -p "$out"
probe=$root/build/dos/VX18.EXE
w386=$root/build/win386/WIN386.EXE
[ -f "$probe" ] || { echo "FAIL vmprobe: no $probe (make vxprobes)"; exit 1; }
# observations that depend on each WIN386's own memory use, not on paging
skip='flags|dpmi_0500_0[0-8]|dpmi_0500_1[01]|free|disk'

ini() {
    case $1 in
    default) echo ;;
    nopaging) echo '386Enh:Paging=no' ;;
    max) echo '386Enh:MaxPagingFileSize=2048' ;;
    pfile) echo '386Enh:PagingFile=C:\PAGE.SWP' ;;
    pdrive) echo '386Enh:PagingDrive=C' ;;
    mindisk) echo '386Enh:MinUserDiskSpace=4000000' ;;
    overcommit) echo '386Enh:PageOverCommit=2' ;;
    esac
}

fails=0
for s in $scen; do
    for w in ref w16; do
        [ -z "$only" ] || [ "$only" = $w ] || continue
        f=
        [ $w = w16 ] && f="$w386=WINDOWS/SYSTEM/WIN386.EXE"
        REFRUN_INI=$(ini "$s") REFRUN_FILES=$f REFRUN_TIMEOUT=240 \
            "$root/tests/refrun.sh" "$probe" /3 > "$out/$s-$w.log" 2>&1 || true
        grep '^OBS' "$out/$s-$w.log" > "$out/$s-$w.obs" || true
    done
    if [ -n "$only" ]; then
        echo "== $s ($only)"; cat "$out/$s-$only.obs"; continue
    fi
    if ! grep -q '^VX18 done' "$out/$s-ref.log" || ! grep -q '^VX18 done' "$out/$s-w16.log"; then
        echo "FAIL vmprobe $s: VX18 did not finish ($out/$s-ref.log, $s-w16.log)"; fails=$((fails + 1)); continue
    fi
    grep -Ev "~($skip)=" "$out/$s-ref.obs" > "$out/$s-ref.cmp" || true
    grep -Ev "~($skip)=" "$out/$s-w16.obs" > "$out/$s-w16.cmp" || true
    if diff "$out/$s-ref.cmp" "$out/$s-w16.cmp" > "$out/$s.diff" 2>&1; then
        echo "ok   vmprobe $s"
    else
        echo "FAIL vmprobe $s: $(grep -c '^[<>]' "$out/$s.diff") line(s) differ from 3.1's ($out/$s.diff)"
        fails=$((fails + 1))
    fi
done
[ $fails = 0 ]
