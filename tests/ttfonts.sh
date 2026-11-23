#!/bin/sh
# Check w16's TrueType fonts (tools/ttgen.py; ARCHITECTURE.md 6.9.3) on
# Windows 3.1. In a scratch copy of the reference install (tests/refrun.sh)
# the twelve .TTF files are put in WINDOWS\SYSTEM and probe FN4
# (tests/apps/fn4.c) runs as the shell, once per family (three sessions
# at once): it makes each file's .FOT with
# CreateScalableFontResource (the portable way: SYSTEM\X.FOT; with a full
# path: C:\FOTB\X.FOB; the portable way again: C:\FOTC\X.FOC), adds the
# fonts, dumps them as it dumps Microsoft's faces, and lists the families.
# Then tools/ttcmp.py compares the dump with tests/fonts/fn4ref.txt
# (Arial, Times New Roman, Courier New on 3.1) and tools/mkfot.py's .FOT
# files with the ones 3.1 wrote.
#
#   usage: tests/ttfonts.sh [-d TTFDIR] [-o OUTDIR] [-m MODE]
#     -d  the fonts (default build/ttfonts, `python3 tools/ttgen.py`)
#     -o  keep the log and 3.1's .FOT files there (default: a temporary
#         directory)
#     -m  /S (default) or /3
#   -r  re-record tests/fonts/fn4ref.txt (Microsoft's faces and the ANSI
#       map test fonts of tools/ttmap.py; no w16 fonts)
# Exit status: the number of failed checks.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$root/build/ttfonts out= mode=/S record=
while getopts d:o:m:r opt; do
    case $opt in
    d) dir=$OPTARG ;; o) out=$OPTARG ;; m) mode=$OPTARG ;; r) record=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
exe=$root/build/dos/FN4.EXE
[ -f "$exe" ] || { echo "missing $exe (make build/dos/FN4.EXE)" >&2; exit 2; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
[ -n "$out" ] || out=$tmp/out
mkdir -p "$out"
if [ -n "$record" ]; then
    for v in "" A B; do python3 "$root/tools/ttmap.py" font "$tmp/TTMAP$v.TTF" $v; done
    REFRUN_TIMEOUT=7000 REFRUN_FILES="$tmp/TTMAP.TTF $tmp/TTMAPA.TTF $tmp/TTMAPB.TTF" \
        "$root/tests/refrun.sh" "$exe" "$mode" > "$tmp/log.txt" 2>&1 || true
    grep -q '^FN4 done' "$tmp/log.txt" || { echo "FN4 did not finish" >&2; exit 2; }
    sed -n '/^FN4 TrueType probe/,/^FN4 done/p' "$tmp/log.txt" > "$root/tests/fonts/fn4ref.txt"
    echo "recorded tests/fonts/fn4ref.txt"
    exit 0
fi
files= get=
for f in W16SANS W16SANBD W16SANIT W16SANBI W16SERIF W16SERBD W16SERIT W16SERBI \
         W16MONO W16MONBD W16MONIT W16MONBI; do
    [ -f "$dir/$f.TTF" ] || { echo "missing $dir/$f.TTF (tools/ttgen.py)" >&2; exit 2; }
    files="$files $dir/$f.TTF=WINDOWS/SYSTEM/$f.TTF"
    get="$get WINDOWS/SYSTEM/$f.FOT FOTB/$f.FOB FOTC/$f.FOC"
done
: > "$tmp/FN4NOREF"
# one session per family (3.1 did not get through all twelve faces in one:
# it hung near the end of the twelfth); the first also brings the .FOTs
for n in 1 2 3; do
    : > "$tmp/FN4SET$n"
    o=$out; [ $n = 1 ] || o=$tmp/out$n
    REFRUN_TIMEOUT=3000 REFRUN_FILES="$files $tmp/FN4NOREF $tmp/FN4SET$n" REFRUN_GET="$get" REFRUN_OUT="$o" \
        REFRUN_PRE="MD C:\\FOTB
MD C:\\FOTC" "$root/tests/refrun.sh" "$exe" "$mode" > "$tmp/fn4-$n.txt" 2>&1 &
done
wait
for n in 1 2 3; do
    grep -q '^FN4 done' "$tmp/fn4-$n.txt" || { echo "FN4 did not finish (family $n)" >&2; cp "$tmp/fn4-$n.txt" "$out/"; exit 2; }
    sed -n '/^FN4 TrueType probe/,/^FN4 done/p' "$tmp/fn4-$n.txt"
done > "$out/fn4-w16.txt"
status=0
python3 "$root/tools/ttcmp.py" "$root/tests/fonts/fn4ref.txt" "$out/fn4-w16.txt" || status=$((status + 1))
python3 "$root/tools/mkfot.py" --compare "$out" "$dir" || status=$((status + 1))
exit $status
