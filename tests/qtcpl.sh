#!/bin/sh
# QuickTime for Windows 2.1.2's Control Panel (QTW16.CPL), its Files tab:
# the "QuickTime Files" list, its last whole row clicked and the list
# scrolled by its down arrow (tests/scripts/input/qtcpl.txt), on stock
# Windows 3.1 and on w16's install tree alone as C:\W16 (KRNL286; the
# user's setup), QuickTime installed into each by its own installer. The
# window spy US32S (tests/apps/us32s.c, loaded by WIN.INI load=) logs the
# panel's windows to TK.LOG whenever they change.
# Until 2026-11-20 w16 made that list one row taller than 3.1 (whole items
# from the height asked for, not from the height the system font's items
# had made it: user/listbox.c, probe US32), it ran under the static below
# it, and the scroll drew its last row over the static and the list's
# border.
#
# QuickTime is Apple's: its installer is never committed; it comes from
# $W16_QTDIR (default ~/Downloads/qtw16), and without it the test says so
# and passes ("skip").
#
#   usage: tests/qtcpl.sh [-o OUTDIR] [-R REFDIR]
#     -R REFDIR  the stock 3.1 run made before (its shots and TK.LOG)
# Checks (exit 1 if one fails):
#   - both runs get to the Files tab and the scroll (c8-arrow);
#   - the list (ID 1068) and the static under it (the file's description)
#     have 3.1's rectangles, and the list has 3.1's top item, selection
#     and scroll position after the scroll (the spy's last snapshot);
#   - after the scroll no pixel of the highlight colour lies between the
#     list's bottom and the static's (on both sides);
#   - the panel's window and its own children (tab control, pages,
#     buttons) have 3.1's rectangles and styles (until 2026-11-20 w16 made it 8
#     pixels wider and higher: its pages are child dialog templates with
#     a caption and a font, which w16 gave a dialog frame's size;
#     user/dlg.c, probe US33's dlgf).
# The shots are saved, not compared (C:\WINDOWS against C:\W16 in the
# list, w16's fonts).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= refdir=
while getopts o:R: opt; do
    case $opt in
    o) out=$OPTARG ;;
    R) refdir=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
qt=${W16_QTDIR:-$HOME/Downloads/qtw16}
if [ ! -f "$qt/QTINSTAL.EXE" ]; then
    echo "skip qtcpl: no QuickTime installer (\$W16_QTDIR/QTINSTAL.EXE, W16_QTDIR=$qt)"
    exit 0
fi
[ -n "$out" ] || out=$root/build/qtcpl-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
tree=$root/build/w16
[ -f "$tree/WIN.COM" ] || { echo "no $tree (make install-tree)" >&2; exit 2; }
spy=$root/build/dos/US32S.EXE
[ -f "$spy" ] || { echo "no $spy (make dos)" >&2; exit 2; }
script=$root/tests/scripts/input/qtcpl.txt
sed 's/@PICK@/End Left/' "$script" > "$out/qtcpl-ms.txt"
sed 's/@PICK@/End/' "$script" > "$out/qtcpl-w16.txt"

if [ -z "$refdir" ]; then
    refdir=$out/ref
    "$root/tests/inputrun.sh" -s -b 40 -t 900 "$out/qtcpl-ms.txt" "$refdir" -r -3 \
        -f "$qt/QTINSTAL.EXE=TMP/QTINSTAL.EXE" -f "$spy=US32S.EXE" \
        -i 'WIN.INI:windows:load=C:\US32S.EXE' > "$out/ref.txt" 2>&1 &
fi
"$root/tests/inputrun.sh" -s -b 40 -t 900 "$out/qtcpl-w16.txt" "$out/w16" \
    -f "$qt/QTINSTAL.EXE=TMP/QTINSTAL.EXE" -T "$tree" -f "$spy=US32S.EXE" \
    -i 'WIN.INI:windows:load=C:\US32S.EXE' -i w16:hangwatch=30 > "$out/w16.txt" 2>&1 || true
wait
refdir=$(cd "$refdir" && pwd)

fails=0
fail() { echo "FAIL qtcpl: $*"; fails=$((fails + 1)); }
# the spy's log: the last snapshot, the list's and the static's lines
spylog() {
    for f in "$1/work/c/TK.LOG" "$1/work/c/W16/TK.LOG" "$1/work/c/WINDOWS/TK.LOG"; do
        [ -f "$f" ] && { tr -d '\r' < "$f"; return; }
    done
}
last() {   # DIR: the last snapshot's lines
    spylog "$1" | awk '/^SPY snapshot/ { n = "" } { n = n $0 "\n" } END { printf "%s", n }'
}
for side in ref w16; do
    d=$refdir
    [ $side = w16 ] && d=$out/w16
    if [ ! -f "$d/c8-arrow.png" ]; then
        fail "$side: did not get to the scroll ($d/input.log)"
        continue
    fi
    last "$d" > "$out/$side.spy"
    # the list, its state, and the static right under it (ID 1160)
    grep -A1 ' id 1068 ' "$out/$side.spy" | sed 's/"[^"]*"$//' > "$out/$side.list" || true
    grep ' id 1160 ' "$out/$side.spy" | sed 's/"[^"]*"$//' >> "$out/$side.list" || true
    # the panel itself: its window, and its own children (the tabs, the
    # pages, the buttons) with their styles, without their texts and the
    # tab control's class name, which holds an instance handle. Left out:
    # the Help button's WS_CLIPSIBLINGS (ID E145h; 3.1 has it, w16 not:
    # not explained yet, ARCHITECTURE.md 10.4, US33 cs)
    sed -n 's/^SPY snapshot [0-9]*: \(top at .*\) client at.*/\1/p' "$out/$side.spy" > "$out/$side.frame"
    grep '^SPY 0 ' "$out/$side.spy" | sed 's/"[^"]*"$//; s/Afx:[^ ]*/Afx/; s/ st 54\([0-9a-f]* .* id -7867 \)/ st 50\1/' \
        >> "$out/$side.frame" || true
done
if [ $fails = 0 ]; then
    if ! cmp -s "$out/ref.frame" "$out/w16.frame"; then
        fail "the panel's window or its tabs, pages and buttons differ from 3.1's: $(diff "$out/ref.frame" "$out/w16.frame" | grep '^>' | tr '\n' ' ')"
    else
        echo "the panel's window, tabs, pages and buttons as on 3.1: $(head -1 "$out/ref.frame")"
    fi
    if [ ! -s "$out/ref.list" ]; then
        fail "3.1: the spy saw no Files list ($out/ref.spy)"
    elif ! cmp -s "$out/ref.list" "$out/w16.list"; then
        fail "the Files list or the static under it differ from 3.1's: $(tr '\n' ' ' < "$out/w16.list") against $(tr '\n' ' ' < "$out/ref.list")"
    else
        echo "the Files list and the static under it as on 3.1: $(tr '\n' ' ' < "$out/ref.list")"
    fi
    # under the list, down to the static's bottom: no highlight (the
    # spy's places are in the panel's client area; its snapshot line says
    # where that is on the screen)
    lb=$(sed -n '1s/.* at \([0-9]*\),\([0-9]*\) \([0-9]*\)x\([0-9]*\) .*/\1 \2 \3 \4/p' "$out/ref.list")
    st=$(sed -n '3s/.* at \([0-9]*\),\([0-9]*\) [0-9]*x\([0-9]*\) .*/\2 \3/p' "$out/ref.list")
    if [ -n "$lb" ] && [ -n "$st" ]; then
        # shellcheck disable=SC2086
        set -- $lb $st
        for side in ref w16; do
            d=$refdir
            [ $side = w16 ] && d=$out/w16
            org=$(sed -n 's/^SPY snapshot .* client at \([0-9]*\),\([0-9]*\)$/\1 \2/p' "$out/$side.spy" | head -1)
            [ -n "$org" ] || { fail "$side: no snapshot line ($out/$side.spy)"; continue; }
            x=$(( $1 + ${org% *} )) y=$(( $2 + $4 + ${org#* } )) w=$3 h=$(( $5 + $6 - $2 - $4 ))
            n=$(magick "$d/c8-arrow.png" -crop "${w}x$h+$x+$y" +repage -fill white +opaque '#000080' \
                -fill black -opaque '#000080' -negate -format '%[fx:int(mean*w*h+0.5)]' info:)
            [ "$n" = 0 ] || fail "$side: $n pixels of the highlight under the Files list after the scroll (${w}x$h+$x+$y in $d/c8-arrow.png)"
        done
    fi
fi
if [ $fails = 0 ]; then echo "ok   qtcpl: QuickTime's Files list as on 3.1, nothing drawn under it ($out)"; fi
exit $(( fails > 0 ))
