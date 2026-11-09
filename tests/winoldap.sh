#!/bin/sh
# Compare w16's WINOLDAP.MOD with Windows 3.1's (black-box; ARCHITECTURE.md 9.7).
#
# Each scenario (tests/scripts/winoldap/NAME.txt, a tests/worun.sh input
# script: key presses and screenshots) runs twice, one after the other, in headless
# DOSBox-X, in scratch copies of the reference install running "WIN /S" on
# Microsoft's KERNEL, WSWAP, DSWAP and DOSX: once with Microsoft's
# WINOLDAP.MOD, once with build/dos/WINOLDAP.MOD. WO2.EXE (tests/apps/wo2.c)
# runs in both from WIN.INI load= and records WIN.COM's switcher block as
# Windows starts, at every switch to a DOS program and when Windows comes
# back. What is compared:
#   - the block snapshots (the linear address of WINOLDAP's buffer, at
#     block+185h, left out: it is where KERNEL put the buffer);
#   - C:\WC.LOG, what the DOS program WC1.COM was given (tests/dos/wc1.asm);
#   - the screenshots, pixel by pixel, with the bottom-left corner of the
#     Windows screens (where the DOS programs' icons are: w16 draws its own,
#     ARCHITECTURE.md 6.8) masked, and up to 40 pixels allowed on the text
#     screens (the blinking cursor).
#
#   usage: tests/winoldap.sh [-o DIR] [-m WINOLDAP.MOD] [-k KRNL286.EXE] [SCENARIO...]
#     -o DIR   keep everything in DIR (new): DIR/NAME/{ms,w16}/*.png ...
#     -m MOD   the WINOLDAP.MOD to test (default build/dos/WINOLDAP.MOD)
#     -k FILE  run both on this KRNL286.EXE (e.g. build/dos/KRNL286.EXE)
#     -K       run both on Microsoft's KRNL286 (KRNL386.EXE deleted)
# Prints PASS/FAIL per scenario; exit code = failures. Needs dosbox-x,
# xvfb-run, xdotool, ImageMagick and `make dos woprobes`.
set -eu
unset WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
dos=$root/build/dos
out= mod=$dos/WINOLDAP.MOD krnl=
while getopts o:m:k:K opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) mod=$OPTARG ;;
    k) krnl="-k $OPTARG" ;;
    K) krnl=-K ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
for f in "$mod" "$dos/WO2.EXE" "$dos/WC1.COM"; do
    [ -f "$f" ] || { echo "missing $f (make dos woprobes)" >&2; exit 2; }
done
if [ -n "$out" ]; then
    [ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
    mkdir -p "$out"
    work=$(cd "$out" && pwd)
else
    work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
fi
sdir=$root/tests/scripts/winoldap
names=$*
[ -n "$names" ] || names=$(cd "$sdir" && ls *.txt | sed 's/\.txt$//')

# files the scenarios use
mkdir -p "$work/files"
printf '[wo2]\r\nhooks=fdt\r\n' > "$work/files/WO2.INI"
printf 'echo batch here\r\nC:\\WC1.COM batarg\r\n' > "$work/files/T.BAT"
printf 'wait\r\n' > "$work/files/WCWAIT.TXT"
python3 "$root/tools/mkpif.py" "$ref/WINDOWS/_DEFAULT.PIF" "$work/files/PROBE.PIF" \
    'title=Probe Program' 'program=C:\WC1.COM' 'dir=C:\WINDOWS\SYSTEM' 'params=?'

run_one() {   # run_one DIR WINOLDAP OPTIONS SCRIPT
    d=$1 m=$2 opts=$3 script=$4
    set -- -o "$d" -m "$m" -x "$script" -f "$dos/WO2.EXE=WO2.EXE" \
        -f "$work/files/WO2.INI=WO2.INI" -n 'windows:load=C:\WO2.EXE'
    pre='SET TEMP=C:\WINDOWS\TEMP'
    for o in $opts; do
        case $o in
        files=WC1) set -- "$@" -f "$dos/WC1.COM=WC1.COM" ;;
        wait) set -- "$@" -f "$work/files/WCWAIT.TXT=WCWAIT.TXT" ;;
        bat) set -- "$@" -f "$work/files/T.BAT=T.BAT" ;;
        pif) set -- "$@" -f "$work/files/PROBE.PIF=PROBE.PIF" ;;
        rm=*) set -- "$@" -f "${o#rm=}=-" ;;
        notemp) pre= ;;
        esac
    done
    [ -z "$pre" ] || set -- "$@" -c "$pre"
    # shellcheck disable=SC2086
    "$root/tests/worun.sh" "$@" $krnl > /dev/null 2>&1 || true
}

# the block snapshots, without the buffer's linear address
blocks() {
    [ -f "$1/c/WO2.LOG" ] || { echo "(no WO2.LOG)"; return; }
    python3 "$root/tools/woblk.py" "$1/c/WO2.LOG" -m 185:4 | grep -v '185h then'
}

failures=0
for n in $names; do
    script=$sdir/$n.txt
    [ -f "$script" ] || { echo "no scenario $n" >&2; exit 2; }
    opts=$(sed -n 's/^# opts: //p' "$script")
    run_one "$work/$n/ms" "$ref/WINDOWS/SYSTEM/WINOLDAP.MOD" "$opts" "$script"
    run_one "$work/$n/w16" "$mod" "$opts" "$script"
    bad=
    blocks "$work/$n/ms" > "$work/$n/ms.blocks"
    blocks "$work/$n/w16" > "$work/$n/w16.blocks"
    if ! diff -u "$work/$n/ms.blocks" "$work/$n/w16.blocks" > "$work/$n/blocks.diff"; then
        bad="$bad blocks"
    fi
    for s in ms w16; do
        if [ -f "$work/$n/$s/c/WC.LOG" ]; then tr -d '\r' < "$work/$n/$s/c/WC.LOG"; fi > "$work/$n/$s.wc"
    done
    diff -u "$work/$n/ms.wc" "$work/$n/w16.wc" > "$work/$n/wc.diff" || bad="$bad WC.LOG"
    mkdir -p "$work/$n/diff"
    for f in "$work/$n/ms"/*.png; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .png)
        g=$work/$n/w16/$s.png
        if [ ! -f "$g" ]; then bad="$bad $s(missing)"; continue; fi
        size=$(identify -format '%wx%h' "$f")
        if [ "$size" = 640x480 ]; then
            magick "$f" -fill black -draw 'rectangle 0,400 199,479' "$work/$n/diff/a.png"
            magick "$g" -fill black -draw 'rectangle 0,400 199,479' "$work/$n/diff/b.png"
            limit=0
        else
            cp "$f" "$work/$n/diff/a.png"
            cp "$g" "$work/$n/diff/b.png"
            limit=40
        fi
        px=$(compare -metric AE "$work/$n/diff/a.png" "$work/$n/diff/b.png" "$work/$n/diff/$s.png" 2>&1 >/dev/null | sed 's/ .*//') || true
        px=${px%%.*}
        echo "$s $px" >> "$work/$n/pixels.txt"
        case $px in ''|*[!0-9]*) bad="$bad $s(?)" ;; *) [ "$px" -le $limit ] || bad="$bad $s($px)" ;; esac
    done
    rm -f "$work/$n/diff/a.png" "$work/$n/diff/b.png"
    if [ -z "$bad" ]; then
        echo "PASS $n"
    else
        echo "FAIL $n:$bad"
        [ ! -s "$work/$n/blocks.diff" ] || sed 's/^/    /' "$work/$n/blocks.diff" | head -40
        [ ! -s "$work/$n/wc.diff" ] || sed 's/^/    /' "$work/$n/wc.diff" | head -20
        failures=$((failures + 1))
    fi
done
echo "winoldap: $failures failure(s)"
exit $failures
