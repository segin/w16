#!/bin/sh
# ttshow.sh - w16's TrueType fonts (build/ttfonts; ARCHITECTURE.md 6.9.3)
# as Windows 3.1 draws them and as w16's GDI draws them (its rasteriser
# running the fonts' own instructions, gdi/ttf/): probe FN4's show pages
# (tests/apps/fn4.c: a line at each of 8, 9, 10, 11, 12, 14, 16, 18 and
# 24 pt, Microsoft's face above w16's) under SNAP, once in a scratch copy
# of the reference install with the twelve .TTFs in WINDOWS\SYSTEM, once
# on w16's install tree (build/w16, where make installs them). The lines
# in w16's faces (the second of each pair, found on 3.1's page) are
# compared pixel for pixel; the line above, Microsoft's face, is not (w16's
# tree has none of Microsoft's fonts).
#
#   usage: tests/ttshow.sh [-d TTFDIR] [-o OUTDIR] [-l LIMIT] [PAGE...]
#     -d  the fonts (default build/ttfonts)
#     -o  keep the screens there: ms-SN.png, w16-SN.png (default: a
#         temporary directory)
#     -l  the most pixels allowed to differ in a line, per cent of the
#         black pixels 3.1 draws there (default 10: w16's interpreter is
#         not yet 3.1's at every pixel)
#     PAGE  FN4 show pages (default 4-15: Arial, Arial Bold, ... Courier
#         New Bold Italic with their w16 faces)
# Prints a line per page: the worst line's differing pixels. Exit status:
# the number of pages over the limit (2 when a run gave no screens).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$root/build/ttfonts out= limit=10
while getopts d:o:l: opt; do
    case $opt in
    d) dir=$OPTARG ;; o) out=$OPTARG ;; l) limit=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
pages=${*:-4 5 6 7 8 9 10 11 12 13 14 15}
dos=$root/build/dos
for f in FN4.EXE SNAP.EXE; do
    [ -f "$dos/$f" ] || { echo "missing $dos/$f (make)" >&2; exit 2; }
done
[ -d "$root/build/w16" ] || { echo "missing build/w16 (make install-tree)" >&2; exit 2; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
[ -n "$out" ] || out=$tmp/out
mkdir -p "$out"
: > "$tmp/SNAP.TXT"
for p in $pages; do
    printf 'run C:\\FN4.EXE show %s\r\nwait 15000\r\nsnap C:\\S%s.BMP\r\nclose\r\nwait 3000\r\n' "$p" "$p" >> "$tmp/SNAP.TXT"
done
files="$dos/FN4.EXE $tmp/SNAP.TXT"
for f in "$dir"/*.TTF; do files="$files $f=WINDOWS/SYSTEM/$(basename "$f")"; done
REFRUN_TIMEOUT=1500 REFRUN_FILES="$files" REFRUN_OUT="$tmp/ms" \
    "$root/tests/refrun.sh" "$dos/SNAP.EXE" /S > "$tmp/ms.txt" 2>&1 &
# w16's tree: a session a page (after its first page FN4 removes the
# fonts it added, which on w16's tree are the installed ones), four at once
n=0
for p in $pages; do
    printf 'run C:\\FN4.EXE show %s\r\nwait 15000\r\nsnap C:\\S%s.BMP\r\n' "$p" "$p" > "$tmp/SNAP$p.TXT"
    "$root/tests/dropin.sh" -T "$root/build/w16" -t 600 -o "$tmp/w16-$p" \
        -i 'boot:shell=C:\SNAP.EXE' -f "$dos/SNAP.EXE=SNAP.EXE" -f "$dos/FN4.EXE=FN4.EXE" \
        -f "$tmp/SNAP$p.TXT=SNAP.TXT" > "$tmp/w16-$p.txt" 2>&1 &
    n=$((n + 1))
    [ $((n % 4)) = 0 ] && wait
done
wait
status=0
for p in $pages; do
    m=$tmp/ms/S$p.BMP w=$tmp/w16-$p/c/S$p.BMP
    if [ ! -f "$m" ] || [ ! -f "$w" ]; then
        echo "page $p: no screen ($([ -f "$m" ] || echo "3.1 ")$([ -f "$w" ] || echo w16))"
        status=2
        continue
    fi
    python3 "$root/tests/ttf/ttshowcmp.py" "$m" "$w" "$out/ms-S$p.png" "$out/w16-S$p.png" "$limit" "page $p" ||
        status=$((status + 1))
done
exit $status
