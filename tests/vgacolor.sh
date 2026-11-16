#!/bin/sh
# The display driver's colours, exhaustively: Windows 3.1's VGA.DRV against
# w16's (drivers/vga/README.md). Each probe runs as the shell
# (tests/vgaprobe.sh), once on each driver, and the files they write must
# be byte for byte the same:
#   VG3  GetNearestColor (ColorInfo) of all 2^24 colours, on the screen and
#        on a monochrome bitmap: NEAR.BIN, NEARM.BIN
#   VG4  the patterns of solid brushes (the dithering) on a colour and on a
#        monochrome bitmap, a checksum per colour, for all 2^24 colours in
#        PARTS ranges of reds (default 16): VG4.BIN
# Heavy: many DOSBoxes for a long time; run it on the build host.
#
#   usage: tests/vgacolor.sh [-j JOBS] [-p PARTS] [-n] OUTDIR
#     -j JOBS   DOSBoxes at once (default 4)
#     -p PARTS  VG4 ranges of reds, one DOSBox each (default 16)
#     -n        only VG3 (the nearest colours)
# make vgaprobes and build/dos/VGA.DRV first. Prints one line per check and
# "vgacolor: N difference(s)".
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
jobs=4 parts=16 only3=
while getopts j:p:n opt; do
    case $opt in
    j) jobs=$OPTARG ;; p) parts=$OPTARG ;; n) only3=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
out=$1
mkdir -p "$out"
dos=$root/build/dos
# the runs: NAME DRIVER-OPTION PROBE ...
list=$out/runs.txt
: > "$list"
for side in ms w16; do
    opt=-; [ $side = w16 ] && opt=-w
    echo "vg3-$side $opt VG3" >> "$list"
    if [ -z "$only3" ]; then
        i=0
        while [ $i -lt "$parts" ]; do
            r0=$((i * 256 / parts)) r1=$(((i + 1) * 256 / parts - 1))
            echo "vg4-$side-$i $opt VG4 $r0 $r1" >> "$list"
            i=$((i + 1))
        done
    fi
done
run() {
    name=$1 opt=$2 probe=$3
    d=$out/$name
    rm -rf "$d"
    mkdir -p "$d"
    if [ "$probe" = VG3 ]; then
        "$root/tests/vgaprobe.sh" $opt -p "$dos/VG3.EXE" -g NEAR.BIN -g NEARM.BIN -W NEAR.END \
            -t 900 "$d" > /dev/null 2>&1 || true
    else
        printf '%s %s\r\n' "$4" "$5" > "$d/VG4.IN"
        "$root/tests/vgaprobe.sh" $opt -p "$dos/VG4.EXE" -f "$d/VG4.IN=VG4.IN" -g VG4.BIN \
            -W VG4.END -t 7200 "$d" > /dev/null 2>&1 || true
    fi
}
# shellcheck disable=SC2034
while read -r name opt probe a b; do
    while [ "$(jobs -p | wc -l)" -ge "$jobs" ]; do sleep 2; done
    [ "$opt" = - ] && opt=
    run "$name" "$opt" "$probe" "$a" "$b" &
done < "$list"
wait
diffs=0
check() {
    if [ ! -s "$out/$1/$3" ] || [ ! -s "$out/$2/$3" ]; then
        echo "FAIL $1 / $2: $3 missing"; diffs=$((diffs + 1))
    elif cmp -s "$out/$1/$3" "$out/$2/$3"; then
        echo "ok   $3 ($1 = $2, $(wc -c < "$out/$1/$3") bytes)"
    else
        echo "FAIL $1 / $2: $3 differs"; diffs=$((diffs + 1))
    fi
}
check vg3-ms vg3-w16 NEAR.BIN
check vg3-ms vg3-w16 NEARM.BIN
if [ -z "$only3" ]; then
    i=0
    while [ $i -lt "$parts" ]; do
        check "vg4-ms-$i" "vg4-w16-$i" VG4.BIN
        i=$((i + 1))
    done
fi
echo "vgacolor: $diffs difference(s)"
[ $diffs -eq 0 ]
