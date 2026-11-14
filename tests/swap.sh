#!/bin/sh
# Compare w16's WSWAP.EXE and DSWAP.EXE with Windows 3.1's (black-box;
# ARCHITECTURE.md 9.9).
#
# Each scenario (tests/scripts/swap/NAME.txt, a tests/worun.sh input script:
# key presses and screenshots) runs twice in headless DOSBox-X, in scratch
# copies of the reference install running "WIN /S": once with Microsoft's
# WSWAP.EXE and DSWAP.EXE, once with build/dos's. Everything else is the
# same on both sides: Microsoft's by default, w16's with the options below.
# WO2.EXE (tests/apps/wo2.c) runs from WIN.INI load= on both and records
# WIN.COM's switcher block at every switch. What is compared:
#   - the block snapshots, leaving out the linear address of WINOLDAP's
#     buffer (185h, where KERNEL put it) and 163h-168h (Windows 3.1's WSWAP
#     keeps its own XMS handle there; w16's has none);
#   - C:\WC.LOG, what a DOS program (WC1.COM) was given (but its PSP, its
#     environment's segment, the memory free and the vectors: w16's DSWAP
#     is smaller and hooks other ones);
#   - the swap files made in the swap directory (names, from DOSBox-X's
#     file log, in order) and that none is left at the end;
#   - the screenshots, pixel by pixel; on text screens the MS-DOS Prompt's
#     banner box (rows 1-5) is left out, since w16's says "w16" where 3.1's
#     says "Windows"; up to 40 pixels may differ (a cursor or caret).
#
#   usage: tests/swap.sh [-o DIR] [-w] [-O] [-x] [-W] [-k KRNL286.EXE | -K] [SCENARIO...]
#     -o DIR   keep everything in DIR (new): DIR/NAME/{ms,w16}/...
#     -w       w16's WIN.COM (build/dos/WIN.COM) on both sides
#     -O       w16's WINOLDAP.MOD on both sides
#     -x       w16's DOSX.EXE on both sides
#     -W       w16's WIN.COM, WINOLDAP.MOD and DOSX.EXE on w16's side only:
#              w16's stack against all of Microsoft's (Microsoft's WSWAP
#              does not run on w16's DOSX: it hands DOSX its memory
#              through an XMS interface of its own). Block 183h (the DOS
#              memory, which depends on WIN.COM's size) is left out too
#     -k FILE  this KRNL286.EXE on both sides; -K Microsoft's KRNL286
# Prints PASS/FAIL per scenario; exit code = failures. Needs dosbox-x,
# xdotool, ImageMagick and `make dos woprobes wcprobes`. Each scenario
# takes a few minutes.
set -eu
unset WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
dos=$root/build/dos
out= krnl= common= w16only= mask=
while getopts o:wOxWk:K opt; do
    case $opt in
    o) out=$OPTARG ;;
    w) common="$common -f $dos/WIN.COM=WINDOWS/WIN.COM" ;;
    O) common="$common -f $dos/WINOLDAP.MOD=WINDOWS/SYSTEM/WINOLDAP.MOD" ;;
    x) common="$common -f $dos/DOSX.EXE=WINDOWS/SYSTEM/DOSX.EXE" ;;
    W) w16only="-f $dos/WIN.COM=WINDOWS/WIN.COM -f $dos/WINOLDAP.MOD=WINDOWS/SYSTEM/WINOLDAP.MOD -f $dos/DOSX.EXE=WINDOWS/SYSTEM/DOSX.EXE"
       mask="-m 183:2" ;;
    k) krnl="-k $OPTARG" ;;
    K) krnl=-K ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
for f in "$dos/WSWAP.EXE" "$dos/DSWAP.EXE" "$dos/WO2.EXE" "$dos/WC1.COM"; do
    [ -f "$f" ] || { echo "missing $f (make dos woprobes wcprobes)" >&2; exit 2; }
done
if [ -n "$out" ]; then
    [ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
    mkdir -p "$out"
    work=$(cd "$out" && pwd)
else
    work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
fi
sdir=$root/tests/scripts/swap
names=$*
[ -n "$names" ] || names=$(cd "$sdir" && ls *.txt | sed 's/\.txt$//')

mkdir -p "$work/files"
printf '[wo2]\r\nhooks=fdt\r\n' > "$work/files/WO2.INI"
printf 'wait\r\n' > "$work/files/WCWAIT.TXT"
python3 "$root/tools/mkpif.py" "$ref/WINDOWS/_DEFAULT.PIF" "$work/files/PROBE.PIF" \
    'title=Probe Program' 'program=C:\WC1.COM' 'dir=C:\WINDOWS\SYSTEM' 'params=?'

run_one() {   # run_one DIR SIDE OPTIONS SCRIPT
    d=$1 side=$2 opts=$3 script=$4
    set -- -o "$d" -x "$script" -l -f "$dos/WO2.EXE=WO2.EXE" \
        -f "$work/files/WO2.INI=WO2.INI" -n 'windows:load=C:\WO2.EXE'
    if [ "$side" = w16 ]; then
        set -- "$@" -f "$dos/WSWAP.EXE=WINDOWS/SYSTEM/WSWAP.EXE" \
            -f "$dos/DSWAP.EXE=WINDOWS/SYSTEM/DSWAP.EXE" $w16only
    fi
    pre='SET TEMP=C:\WINDOWS\TEMP' tmax=300
    for o in $opts; do
        case $o in
        files=WC1) set -- "$@" -f "$dos/WC1.COM=WC1.COM" ;;
        wait) set -- "$@" -f "$work/files/WCWAIT.TXT=WCWAIT.TXT" ;;
        pif) set -- "$@" -f "$work/files/PROBE.PIF=PROBE.PIF" ;;
        notemp) pre= ;;
        t=*) tmax=${o#t=} ;;
        esac
    done
    [ -z "$pre" ] || set -- "$@" -c "$pre"
    # shellcheck disable=SC2086
    "$root/tests/worun.sh" "$@" -t "$tmax" $common $krnl > /dev/null 2>&1 || true
}

blocks() {
    [ -f "$1/c/WO2.LOG" ] || { echo "(no WO2.LOG)"; return; }
    # shellcheck disable=SC2086
    python3 "$root/tools/woblk.py" "$1/c/WO2.LOG" -m 185:4 -m 163:6 $mask | grep -v '185h then'
}

# the swap files made, in order, and those left at the end
swapfiles() {
    sed -n 's/.*FILES:file create attributes [0-9]* file \(.*~WOA[^ ]*\).*/create \1/p' \
        "$1/dosbox.log" 2>/dev/null | tr -d '\r'
    for f in "$1"/c/WINDOWS/TEMP/~* "$1"/c/~*; do
        [ -e "$f" ] && echo "left $(basename "$f")"
    done
    true
}

failures=0
for n in $names; do
    script=$sdir/$n.txt
    [ -f "$script" ] || { echo "no scenario $n" >&2; exit 2; }
    opts=$(sed -n 's/^# opts: //p' "$script")
    run_one "$work/$n/ms" ms "$opts" "$script"
    run_one "$work/$n/w16" w16 "$opts" "$script"
    bad=
    blocks "$work/$n/ms" > "$work/$n/ms.blocks"
    blocks "$work/$n/w16" > "$work/$n/w16.blocks"
    diff -u "$work/$n/ms.blocks" "$work/$n/w16.blocks" > "$work/$n/blocks.diff" || bad="$bad blocks"
    for s in ms w16; do
        # (where in memory the program is, how much is free and where the
        # vectors point differ by design: w16's DSWAP is smaller)
        if [ -f "$work/$n/$s/c/WC.LOG" ]; then tr -d '\r' < "$work/$n/$s/c/WC.LOG" |
            grep -v '^psp=\|^envseg=\|^vec ' | sed 's/parentenv=[0-9A-F]*//'; fi > "$work/$n/$s.wc"
        swapfiles "$work/$n/$s" > "$work/$n/$s.files"
    done
    diff -u "$work/$n/ms.wc" "$work/$n/w16.wc" > "$work/$n/wc.diff" || bad="$bad WC.LOG"
    diff -u "$work/$n/ms.files" "$work/$n/w16.files" > "$work/$n/files.diff" || bad="$bad files"
    mkdir -p "$work/$n/diff"
    for f in "$work/$n/ms"/*.png; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .png)
        g=$work/$n/w16/$s.png
        if [ ! -f "$g" ]; then bad="$bad $s(missing)"; continue; fi
        size=$(magick identify -format '%wx%h' "$f")
        size2=$(magick identify -format '%wx%h' "$g")
        if [ "$size" != "$size2" ]; then bad="$bad $s($size/$size2)"; continue; fi
        if [ "$size" = 720x400 ]; then          # text: the banner left out
            magick "$f" -fill black -draw 'rectangle 0,16 719,95' "$work/$n/diff/a.png"
            magick "$g" -fill black -draw 'rectangle 0,16 719,95' "$work/$n/diff/b.png"
        else
            cp "$f" "$work/$n/diff/a.png"
            cp "$g" "$work/$n/diff/b.png"
        fi
        px=$(magick compare -metric AE "$work/$n/diff/a.png" "$work/$n/diff/b.png" "$work/$n/diff/$s.png" 2>&1 >/dev/null | sed 's/ .*//') || true
        px=${px%%.*}
        echo "$s $px" >> "$work/$n/pixels.txt"
        case $px in ''|*[!0-9]*) bad="$bad $s(?)" ;; *) [ "$px" -le 40 ] || bad="$bad $s($px)" ;; esac
    done
    rm -f "$work/$n/diff/a.png" "$work/$n/diff/b.png"
    if [ -z "$bad" ]; then
        echo "PASS $n"
    else
        echo "FAIL $n:$bad"
        for f in blocks wc files; do
            [ ! -s "$work/$n/$f.diff" ] || sed 's/^/    /' "$work/$n/$f.diff" | head -30
        done
        failures=$((failures + 1))
    fi
done
echo "swap: $failures failure(s)"
exit $failures
