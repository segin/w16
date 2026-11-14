#!/bin/sh
# Compare w16's VGACOLOR.2GR (drivers/grabber/vgacolor.asm) with Windows
# 3.1's (black-box; ARCHITECTURE.md 9.11, docs/notes/grabber-handover.md).
#
# Two kinds of scenario, each run with Microsoft's grabber and with w16's
# in headless DOSBox-X (tests/worun.sh, scratch copies of the reference
# install):
#   grt       no Windows: GRT.COM (tests/dos/grt.asm) calls the grabber
#             directly in each video mode (00-13h) as DSWAP does. Compared:
#             what it answers (InquireGrab, GetVersion, GetInfo's GRABINFO,
#             GetBlock's sizes, errors and data, InitScreen's INT 10h
#             calls); and w16's RestoreScreen must give back every VGA
#             register, the DAC, the BIOS's video fields and all four
#             planes exactly (planes 0 and 1 in the CGA modes; 3.1's does
#             not in every mode: not compared).
#             Expected differences: GetBlock's AX after copying a CGA
#             bitmap (3.1's answers 0708h, w16's the size), the save
#             buffer's size (each grabber's own).
#   NAME      tests/scripts/grabber/NAME.txt, a worun.sh input script under
#             "WIN /S" with Microsoft's DSWAP, WSWAP, WINOLDAP (or w16's
#             with -O) and KERNEL: GV1.COM's screens (tests/dos/gv1.asm)
#             switched away from and back to, PrintScreen; CB1.EXE
#             (tests/apps/cb1.c) records the clipboard. Compared: the
#             screenshots (up to 64 pixels: a cursor, wide in 40-column modes), CB1's log (window
#             handles left out) and the bitmaps' bits. "# known: SHOT"
#             lines name screenshots where 3.1's grabber is known to be
#             wrong (they are reported, not failed).
#
#   usage: tests/grabber.sh [-o DIR] [-g FILE] [-O] [SCENARIO...]
#     -o DIR   keep everything in DIR (new)
#     -g FILE  the grabber to test (default build/dos/VGACOLOR.2GR)
#     -O       w16's WINOLDAP.MOD on both sides
# Default scenarios: grt and every script. Prints PASS/FAIL per scenario;
# exit code = failures. Needs dosbox-x, xdotool, ImageMagick and
# `make dos grprobes`.
set -eu
unset WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
dos=$root/build/dos
out= gr=$dos/VGACOLOR.2GR common=
while getopts o:g:O opt; do
    case $opt in
    o) out=$OPTARG ;;
    g) gr=$OPTARG ;;
    O) common="-f $dos/WINOLDAP.MOD=WINDOWS/SYSTEM/WINOLDAP.MOD" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
msgr=$ref/WINDOWS/SYSTEM/VGACOLOR.2GR
[ -f "$msgr" ] || { echo "no reference install at $ref" >&2; exit 2; }
for f in "$gr" "$dos/GRT.COM" "$dos/GV1.COM" "$dos/CB1.EXE"; do
    [ -f "$f" ] || { echo "missing $f (make dos grprobes)" >&2; exit 2; }
done
if [ -n "$out" ]; then
    [ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
    mkdir -p "$out"
    work=$(cd "$out" && pwd)
else
    work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
fi
sdir=$root/tests/scripts/grabber
names=$*
[ -n "$names" ] || names="grt $(cd "$sdir" && ls *.txt | sed 's/\.txt$//' | tr '\n' ' ')"
mkdir -p "$work/files"

# ---- grt
GRT_MODES="03:p 01: 00: 02: 07: 04:p 05: 06: 0D:p 0E: 10: 12:p 13:p 03:l"
grt_run() {   # grt_run DIR GRABBER
    {
        printf '@echo off\r\nmd C:\\TMP\r\n'
        for m in $GRT_MODES; do printf 'C:\\GRT.COM C:\\G.2GR %s %s\r\n' "${m%%:*}" "${m#*:}"; done
    } > "$work/files/GRTALL.BAT"
    printf 'sleep 100\nshot end\n' > "$work/files/grt.txt"
    "$root/tests/worun.sh" -o "$1" -t 150 -f "$2=G.2GR" -f "$dos/GRT.COM=GRT.COM" \
        -f "$work/files/GRTALL.BAT=GRTALL.BAT" -w 'C:\GRTALL.BAT' -x "$work/files/grt.txt" > /dev/null 2>&1 || true
    tr -d '\r' < "$1/c/GRT.LOG" > "$1/grt.log" 2>/dev/null || true
}
# what is compared: the answers, without the registers other than AX and
# CF, the save buffer's size, the obsolete or bookkeeping calls' AX; the
# INT 10h calls of InitScreen only
grt_iface() {
    awk '
        /^== GRT/ { sub(/ size .*/, ""); print; next }
        /^[A-Za-z]/ { fn = $1; keep = (fn ~ /^(GetInfo|GetBlock|InquireGrab|GetVersion|InitScreen)/); if (keep) print; next }
        /^  int10 / { if (fn == "InitScreen") print $1, $2, substr($3, 3), (substr($2, 1, 2) == "01" ? $4 : ""); next }
        /^  out / { if (!keep) next; if (fn == "GetBlock(data)" && $2 != "00FF" && $2 != "00FE") $2 = "(n)"; print $1, $2, $6, $7; next }
        /^  (info|req|data) / { if (keep) print; next }
    ' "$1"
}
grt_scenario() {
    d=$work/grt
    grt_run "$d/ms" "$msgr"
    grt_run "$d/w16" "$gr"
    bad=
    [ -s "$d/ms/grt.log" ] || bad="$bad ms-log"
    [ -s "$d/w16/grt.log" ] || bad="$bad w16-log"
    grt_iface "$d/ms/grt.log" > "$d/ms.iface"
    grt_iface "$d/w16/grt.log" > "$d/w16.iface"
    diff -u "$d/ms.iface" "$d/w16.iface" > "$d/iface.diff" || bad="$bad answers"
    for f in "$d"/ms/c/GRT*.BIN; do
        [ -f "$f" ] || continue
        cmp -s "$f" "$d/w16/c/$(basename "$f")" || bad="$bad $(basename "$f")"
    done
    n=$(grep -c '^== GRT' "$d/w16/grt.log" || true)
    e=$(grep -c '^== end' "$d/w16/grt.log" || true)
    [ "$n" = "$e" ] && [ "$n" -gt 0 ] || bad="$bad w16-runs($e/$n)"
    # (CGA modes: w16 keeps their 32 KB window: planes 0 and 1 in modes
    # 4 and 5, plane 0 in mode 6)
    awk '/^== GRT/ { m = $5 } /DIFFERS|differs |dac bytes differing/ {
            if ($0 ~ /dac bytes differing 0000/) next
            if (m ~ /^0[45]$/ && $0 ~ /plane 0[23] /) next
            if (m == "06" && $0 ~ /plane 0[123] /) next
            print m ": " $0 }' "$d/w16/grt.log" > "$d/w16.restore"
    [ ! -s "$d/w16.restore" ] || bad="$bad w16-restore"
    if [ -z "$bad" ]; then echo "PASS grt"; else
        echo "FAIL grt:$bad"
        sed 's/^/    /' "$d/iface.diff" | head -30
        sed 's/^/    /' "$d/w16.restore" | head -20
        failures=$((failures + 1))
    fi
}

# ---- Windows scenarios
run_one() {   # run_one DIR GRABBER SCRIPT TMAX
    # A: and B: on C:'s directory: DOSBox-X's second swap directory is on
    # A:, where DSWAP passes PrintScreen's data to WINOLDAP
    "$root/tests/worun.sh" -o "$1" -x "$3" -t "$4" -f "$2=WINDOWS/SYSTEM/VGACOLOR.2GR" \
        -f "$dos/GV1.COM=GV1.COM" -f "$dos/CB1.EXE=CB1.EXE" -f "$work/files/RUN.BAT=RUN.BAT" \
        -n 'windows:load=C:\CB1.EXE' -c "mount a $1/c;mount b $1/c;SET TEMP=C:\\WINDOWS\\TEMP" \
        $common > /dev/null 2>&1 || true
}
cblog() {
    [ -f "$1/c/CB.LOG" ] || { echo "(no CB.LOG)"; return; }
    tr -d '\r' < "$1/c/CB.LOG" | sed 's/ owner [0-9A-F]*//'
}
win_scenario() {
    n=$1 script=$sdir/$1.txt
    [ -f "$script" ] || { echo "no scenario $n" >&2; exit 2; }
    tmax=$(sed -n 's/^# tmax: //p' "$script"); [ -n "$tmax" ] || tmax=300
    sed -n 's/^# run: //p' "$script" | tr '\n' '\r' | sed 's/\r/\r\n/g' > "$work/files/RUN.BAT"
    known=$(sed -n 's/^# known: //p' "$script")
    d=$work/$n
    run_one "$d/ms" "$msgr" "$script" "$tmax"
    run_one "$d/w16" "$gr" "$script" "$tmax"
    bad= note=
    cblog "$d/ms" > "$d/ms.cb"; cblog "$d/w16" > "$d/w16.cb"
    diff -u "$d/ms.cb" "$d/w16.cb" > "$d/cb.diff" || bad="$bad clipboard"
    for f in "$d"/ms/c/CB*.BIN; do
        [ -f "$f" ] || continue
        cmp -s "$f" "$d/w16/c/$(basename "$f")" || bad="$bad $(basename "$f")"
    done
    mkdir -p "$d/diff"
    for f in "$d"/ms/*.png; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .png) g=$d/w16/$s.png
        [ -f "$g" ] || { bad="$bad $s(missing)"; continue; }
        px=$(magick compare -metric AE "$f" "$g" "$d/diff/$s.png" 2>&1 >/dev/null | sed 's/ .*//') || true
        px=${px%%.*}
        echo "$s $px" >> "$d/pixels.txt"
        case $px in ''|*[!0-9]*) bad="$bad $s(?)" ;;
        *) [ "$px" -le 64 ] && continue
           case " $known " in *" $s "*) note="$note $s($px)" ;; *) bad="$bad $s($px)" ;; esac ;;
        esac
    done
    [ -z "$note" ] || note=" (known 3.1 differences:$note)"
    if [ -z "$bad" ]; then echo "PASS $n$note"; else
        echo "FAIL $n:$bad$note"
        sed 's/^/    /' "$d/cb.diff" | head -20
        failures=$((failures + 1))
    fi
}

failures=0
for n in $names; do
    case $n in
    grt) grt_scenario ;;
    *) win_scenario "$n" ;;
    esac
done
echo "grabber: $failures failure(s)"
exit $failures
