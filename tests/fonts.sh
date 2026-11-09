#!/bin/sh
# Check w16's fonts (build/fonts/*.FON, `make fonts`) as drop-in replacements
# for Windows 3.1's (ARCHITECTURE.md 6.9). In scratch copies of the
# reference install (tests/refrun.sh) Microsoft's VGASYS.FON, VGAFIX.FON,
# VGAOEM.FON and SSERIFE.FON are replaced by ours, Windows is started
# headless and:
#   1. the font probe FN1 (tests/apps/fn1.c) runs in Standard mode and in
#      386 Enhanced mode; its log (every observable metric: TEXTMETRIC,
#      GetCharWidth 0-255, text extents, enumeration, CreateFont mapping,
#      dialog base units) must equal tests/fonts/fn1ref.txt, recorded from
#      Windows 3.1 with its own fonts;
#   2. with -s DIR: SNAP.EXE takes screenshots (tests/scripts/fonts/
#      screens.txt: Program Manager, a menu, File Run, Properties, Notepad,
#      About, Control Panel, its Fonts dialog) with Microsoft's fonts and with
#      ours; the window layout (SNAP's dump of every window's rectangle)
#      must be identical, and tools/layoutdiff.py checks that the pixels
#      that differ are text-shaped. FN2.EXE then shows every character of
#      our fonts (pages 1-3). Everything goes to DIR as PNG files.
#
#   usage: tests/fonts.sh [-r] [-q] [-s DIR]
#     -r  re-record tests/fonts/fn1ref.txt from Windows 3.1's own fonts
#         (both modes must agree)
#     -q  Standard mode only
#     -s DIR  also take and compare the screenshots
# Exit status: the number of failed checks.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dos=$root/build/dos
fonts=$root/build/fonts
ref=$root/tests/fonts/fn1ref.txt
record= quick= shots=
while getopts rqs: opt; do
    case $opt in
    r) record=1 ;;
    q) quick=1 ;;
    s) shots=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
for f in FN1.EXE FN2.EXE SNAP.EXE; do
    [ -f "$dos/$f" ] || { echo "missing $dos/$f (make dos)" >&2; exit 2; }
done
ours=
for f in VGASYS VGAFIX VGAOEM SSERIFE; do
    [ -f "$fonts/$f.FON" ] && ours="$ours $fonts/$f.FON=WINDOWS/SYSTEM/$f.FON"
done
[ -n "$ours" ] || { echo "no fonts in $fonts (make fonts)" >&2; exit 2; }
echo "w16 fonts:$(for f in $ours; do printf ' %s' "$(basename "${f%%=*}")"; done)"
modes="/S /3"
[ -n "$quick" ] && modes=/S
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
status=0

# raster fonts' elfFullName is not filled in by EnumFontFamilies (FN1 no
# longer prints it); the log ends at "FN1 done"
clean() { sed -n '/^FN1 font probe/,/^FN1 done/p' "$1"; }

if [ -n "$record" ]; then
    for m in $modes; do
        REFRUN_TIMEOUT=600 "$root/tests/refrun.sh" "$dos/FN1.EXE" $m > "$tmp/ref${m#/}.txt" 2>&1 || true
    done
    clean "$tmp/refS.txt" > "$tmp/s.txt"
    grep -q '^FN1 done' "$tmp/s.txt" || { echo "FN1 did not finish on Windows 3.1" >&2; exit 2; }
    if [ -z "$quick" ]; then
        clean "$tmp/ref3.txt" > "$tmp/e.txt"
        cmp -s "$tmp/s.txt" "$tmp/e.txt" || {
            echo "Standard and 386 Enhanced mode differ:"; diff "$tmp/s.txt" "$tmp/e.txt" | head -20; exit 2; }
    fi
    mkdir -p "$(dirname "$ref")"
    cp "$tmp/s.txt" "$ref"
    echo "recorded $ref ($(wc -l < "$ref") lines)"
fi

for m in $modes; do
    REFRUN_TIMEOUT=600 REFRUN_FILES="$ours" "$root/tests/refrun.sh" "$dos/FN1.EXE" $m \
        > "$tmp/ours.txt" 2>&1 || true
    clean "$tmp/ours.txt" > "$tmp/o.txt"
    if cmp -s "$ref" "$tmp/o.txt"; then
        echo "PASS metrics $m ($(grep -c '^FONT' "$ref") fonts, $(grep -c '^MAP' "$ref") mappings, $(grep -c '^ENUM' "$ref") enumerated)"
    else
        echo "FAIL metrics $m:"
        diff "$ref" "$tmp/o.txt" | head -40
        status=$((status + 1))
    fi
done

if [ -n "$shots" ]; then
    mkdir -p "$shots"
    sed 's/$/\r/' "$root/tests/scripts/fonts/screens.txt" > "$tmp/SNAP.TXT"
    for who in ref w16; do
        mkdir -p "$tmp/$who"
        files="$tmp/SNAP.TXT"
        [ $who = w16 ] && files="$files $ours"
        REFRUN_FILES="$files" REFRUN_OUT="$tmp/$who" "$root/tests/refrun.sh" \
            "$dos/SNAP.EXE" /S > "$tmp/$who/log.txt" 2>&1 || true
        # the window dumps (rectangles, styles, classes, text)
        grep -a -v -e '-> ' -e '^SNAP' -e '^WINEXIT' "$tmp/$who/log.txt" > "$tmp/$who/dump.txt" || true
        cp "$tmp/$who/log.txt" "$shots/snap-$who.txt"
    done
    if cmp -s "$tmp/ref/dump.txt" "$tmp/w16/dump.txt"; then
        echo "PASS window layout ($(grep -c . "$tmp/ref/dump.txt") lines of window dumps)"
    else
        echo "FAIL window layout:"
        diff "$tmp/ref/dump.txt" "$tmp/w16/dump.txt" | head -40
        status=$((status + 1))
    fi
    for f in "$tmp"/ref/*.BMP; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .BMP)
        low=$(echo "$s" | tr A-Z a-z)
        magick "$f" "$shots/$low-ref.png"
        if [ ! -f "$tmp/w16/$s.BMP" ]; then
            echo "FAIL $s: no screenshot with w16's fonts"; status=$((status + 1)); continue
        fi
        magick "$tmp/w16/$s.BMP" "$shots/$low-w16.png"
        magick "$f" "$tmp/w16/$s.BMP" +append "$shots/$low-side.png"
        if r=$(python3 "$root/tools/layoutdiff.py" "$f" "$tmp/w16/$s.BMP" "$shots/$low-diff.png"); then
            echo "PASS $s: $r"
        else
            echo "FAIL $s: $r"; status=$((status + 1))
        fi
    done
    # every character of our fonts, as Windows draws them (only when all
    # four are ours: Microsoft's glyphs are not shown)
    [ "$(echo $ours | wc -w)" = 4 ] || { echo "screenshots in $shots"; exit $status; }
    for p in 1 2 3; do
        printf 'run C:\\FN2.EXE %s\r\nwait 3000\r\nsnap C:\\FN2P%s.BMP\r\n' $p $p > "$tmp/SNAP.TXT"
        mkdir -p "$tmp/fn2"
        REFRUN_FILES="$tmp/SNAP.TXT $dos/FN2.EXE $ours" REFRUN_OUT="$tmp/fn2" \
            "$root/tests/refrun.sh" "$dos/SNAP.EXE" /S > /dev/null 2>&1 || true
        [ -f "$tmp/fn2/FN2P$p.BMP" ] && magick "$tmp/fn2/FN2P$p.BMP" "$shots/fn2-page$p.png"
    done
    echo "screenshots in $shots"
fi
exit $status
