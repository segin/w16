#!/bin/sh
# QuickTime for Windows (16-bit, 2.1.2) installs and plays its sample movie,
# on stock Windows 3.1 and on w16 laid over it (docs/notes/
# quicktime-handover.md; ARCHITECTURE.md 10.7). The setup is the one a user
# reported a hang with: a scratch copy of the reference install with every
# file of w16's install tree (build/w16, `make install-tree`) copied over
# Microsoft's in WINDOWS and WINDOWS\SYSTEM, except the INI, GRP and README
# files (3.1's INI files stay, as do the MCI drivers and whatever the tree
# lacks; MMSYSTEM, TIMER.DRV and TOOLHELP are w16's now), started with
# WIN /S; WIN.COM, DOSX and KRNL386 are w16's (on a 386 DOSX starts
# KRNL386). tests/scripts/input/qtplay.txt runs the
# installer (C:\TMP\QTINSTAL.EXE) from Program Manager, answers its five
# boxes, lets "Play Sample" start the Movie Player with SAMPLE.MOV, and
# plays it with Space, a shot every 2 seconds; then closes and reopens the
# movie, pauses it, ends the player with File > Exit, starts it again from
# Program Manager, opens the movie, ends it with Alt+F4, and ends Windows
# (the close sequence: closing the player ended Windows with "Standard
# Mode: Fault in MS-DOS Extender" on w16's tree alone, 2026-11-18).
# With -T the w16 side is instead w16's install tree alone (build/w16 as
# C:\W16, nothing of Microsoft's: tests/dropin.sh -T, the user's setup),
# QuickTime installed into it the same way.
#
# QuickTime is Apple's: its installer is never committed. It is copied in
# at test time from $W16_QTDIR (default ~/Downloads/qtw16), which must
# hold QTINSTAL.EXE (what QUICKTIM.EXE or QTEASY16.EXE unpacks); without it
# the test says so and passes (exit 0, "skip").
#
#   usage: tests/qtplay.sh [-o OUTDIR] [-m "FILE ..."] [-t "FILE ..."] [-T 286|386] [-R REFDIR]
#     -m "FILE ..."  leave these files of the tree out (Microsoft's stay;
#                    KRNL386.EXE: Microsoft's KRNL386 runs): bisection
#     -t "FILE ..."  only these files of the tree (e.g. TIMER.DRV: the
#                    boot-test case timer); everything else, KRNL386 and
#                    DOSX too, stays Microsoft's
#     -T 286|386     w16's install tree alone, under its KRNL286 (what
#                    tests/dropin.sh -T boots: it leaves KRNL386.EXE out) or
#                    its KRNL386
#     -R REFDIR      the stock 3.1 run made before (its shots); no new one
# Checks (exit 1 if one fails):
#   - both runs reach the Movie Player (w16 runs with [w16] hangwatch=30:
#     a hang ends Windows with a report in OUTDIR/w16/work/c/OUT.TXT);
#   - the movie plays: some shot after Space differs from the one before
#     it (06-player);
#   - the movie's last frame (held: the last shot) is 3.1's, pixel for
#     pixel: on each side the area the movie changed (06-player against
#     the last shot, trimmed: the frame and the controller's slider) is
#     the same rectangle, and the frame (all but the controller's 16 rows)
#     is identical; the controller's pixels are counted, not checked;
#   - it takes as long: the first shot showing the last frame is within one
#     shot (2 s) of 3.1's (printed: "plays in about N s");
#   - the close sequence, on each side: Windows lives through it (no fault
#     report, no hangwatch report in OUT.TXT) to its last shot; Program
#     Manager is back the same after File > Exit as after Alt+F4 (c5-exit
#     and c7-closed equal, and not c4-paused, the player); Exit Windows
#     ends Windows (WIN returns; w16: "back to DOS, code 0000"). The
#     screens in between (c1-c8) are saved, not compared.
# The installer's own boxes (01-05) and the rest of the screen are saved
# but not compared: w16's VGA.DRV art (caption buttons, the pointer) and
# the installer's boxes differ (docs/notes/quicktime-handover.md, "Left
# over").
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= msfiles= refdir= only= treek=
while getopts o:m:t:T:R: opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) msfiles=$OPTARG ;;
    t) only=$OPTARG ;;
    T) treek=$OPTARG
       case $treek in 286|386) ;; *) echo "-T 286 or -T 386" >&2; exit 2 ;; esac ;;
    R) refdir=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
qt=${W16_QTDIR:-$HOME/Downloads/qtw16}
if [ ! -f "$qt/QTINSTAL.EXE" ]; then
    echo "skip qtplay: no QuickTime installer (\$W16_QTDIR/QTINSTAL.EXE, W16_QTDIR=$qt)"
    exit 0
fi
[ -n "$out" ] || out=$root/build/qtplay-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
tree=$root/build/w16
[ -f "$tree/WIN.COM" ] || { echo "no $tree (make install-tree)" >&2; exit 2; }
script=$root/tests/scripts/input/qtplay.txt
ms() {
    case " $msfiles " in *" $1 "*) return 0 ;; esac
    [ -n "$only" ] || return 1
    case " $only " in *" $1 "*) return 1 ;; esac
    return 0
}

# the w16 side: the tree over the reference install
set -- -f "$qt/QTINSTAL.EXE=TMP/QTINSTAL.EXE"
for f in "$tree"/*; do
    b=$(basename "$f")
    case $b in README.TXT|SYSTEM|*.INI|*.GRP) continue ;; esac
    ms "$b" || set -- "$@" -f "$f=WINDOWS/$b"
done
for f in "$tree"/SYSTEM/*; do
    b=$(basename "$f")
    case $b in *.INI) continue ;; esac
    ms "$b" || set -- "$@" -f "$f=WINDOWS/SYSTEM/$b"
done
k386=$tree/SYSTEM/KRNL386.EXE
ms KRNL386.EXE && k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
dx=$tree/SYSTEM/DOSX.EXE
ms DOSX.EXE && dx=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/DOSX.EXE
set -- "$@" -3 -K "$k386" -X "$dx"
if [ -n "$treek" ]; then
    # the tree alone as C:\W16, its own KERNEL and DOSX; the installer
    # puts QuickTime there
    set -- -f "$qt/QTINSTAL.EXE=TMP/QTINSTAL.EXE" -T "$tree"
    [ "$treek" = 286 ] || set -- "$@" -3
fi
w16opts="$*"

newref=
if [ -z "$refdir" ]; then
    refdir=$out/ref newref=1
    "$root/tests/inputrun.sh" -s -b 40 -t 900 "$script" "$refdir" -r -3 \
        -f "$qt/QTINSTAL.EXE=TMP/QTINSTAL.EXE" > "$out/ref.txt" 2>&1 &
fi
# shellcheck disable=SC2086
"$root/tests/inputrun.sh" -s -b 40 -t 900 "$script" "$out/w16" \
    -i w16:hangwatch=30 $w16opts > "$out/w16.txt" 2>&1 || true
wait
refdir=$(cd "$refdir" && pwd)

fails=0
fail() { echo "FAIL qtplay: $*"; fails=$((fails + 1)); }
px() { magick compare -metric AE "$1" "$2" "${3:-null:}" 2>&1 >/dev/null | cut -d' ' -f1 | cut -d. -f1; }
# the first shot after Space that equals the last one (the held last frame)
endshot() {
    last=$(ls "$1"/p[0-9]*.png 2>/dev/null | tail -1)
    [ -n "$last" ] || { echo; return; }
    for f in "$1"/p[0-9]*.png; do
        [ "$(px "$f" "$last")" = 0 ] && { basename "$f" .png; return; }
    done
}
plays() {
    [ -f "$1/06-player.png" ] || return 1
    for f in "$1"/p[0-9]*.png; do
        [ "$(px "$f" "$1/06-player.png")" = 0 ] || return 0
    done
    return 1
}
w=$out/w16
if [ ! -f "$refdir/p15.png" ]; then
    fail "the stock 3.1 run did not get to the end ($refdir/input.log)"
elif ! plays "$refdir"; then
    fail "the movie did not play on stock 3.1 ($refdir)"
elif [ ! -f "$w/06-player.png" ]; then
    why=$(grep -a -m1 'no boot progress\|exception' "$w/work/c/OUT.TXT" 2>/dev/null || true)
    fail "w16 did not get to the Movie Player${why:+ ($why)} ($w)"
else
    if ! plays "$w"; then
        fail "the movie did not play on w16 ($w)"
    elif [ ! -f "$w/p15.png" ]; then
        fail "w16's run did not get to the end ($w/input.log)"
    else
        # the movie's area on each side: what changed between 06-player and
        # the end (the frame, and the controller's slider under it); the
        # frame is all but the controller's 16 rows
        rb=$(magick "$refdir/06-player.png" "$refdir/p15.png" -compose difference -composite \
            -threshold 0 -format '%@' info:)
        wb=$(magick "$w/06-player.png" "$w/p15.png" -compose difference -composite \
            -threshold 0 -format '%@' info:)
        echo "the movie and its controller: $rb on 3.1, $wb on w16"
        if [ "$rb" != "$wb" ]; then
            fail "the movie's area is $wb on w16, $rb on 3.1"
        else
            sz=${rb%%+*} cw=${rb%%x*} chh=${sz#*x}
            fh=$((chh - 16))
            magick "$refdir/p15.png" -crop "$rb" +repage -crop "${cw}x$fh+0+0" +repage "$out/frame-ref.png"
            magick "$w/p15.png" -crop "$wb" +repage -crop "${cw}x$fh+0+0" +repage "$out/frame-w16.png"
            n=$(px "$out/frame-ref.png" "$out/frame-w16.png" "$out/frame-diff.png")
            [ "$n" = 0 ] || fail "the last frame: $n pixels differ from 3.1's ($out/frame-diff.png)"
            magick "$refdir/p15.png" -crop "$rb" +repage -crop "${cw}x16+0+$fh" +repage "$out/ctl-ref.png"
            magick "$w/p15.png" -crop "$wb" +repage -crop "${cw}x16+0+$fh" +repage "$out/ctl-w16.png"
            echo "the last frame: $n pixels differ; the controller: $(px "$out/ctl-ref.png" "$out/ctl-w16.png" "$out/ctl-diff.png") (not checked)"
        fi
        re=$(endshot "$refdir") we=$(endshot "$w")
        re=${re#p} we=${we#p}
        d=$(( ${re#0} - ${we#0} ))
        [ $d -ge -1 ] && [ $d -le 1 ] || fail "the last frame came in shot $we on w16, $re on 3.1"
        echo "plays in about $(( ${re#0} * 2 )) s on 3.1, $(( ${we#0} * 2 )) s on w16"
    fi
fi
# the close sequence (after the movie's end): side NAME DIR
closed() {
    o=$2/work/c/OUT.TXT
    why=$(grep -a -m1 'Fault in MS-DOS Extender\|exception\|no boot progress\|caused a' "$o" 2>/dev/null || true)
    if [ -n "$why" ]; then
        fail "the close sequence on $1: $why ($2)"
    elif [ ! -f "$2/c8-exitwin.png" ]; then
        fail "the close sequence on $1 did not get to its end ($2/input.log)"
    elif [ "$(px "$2/c5-exit.png" "$2/c7-closed.png")" != 0 ] ||
         [ "$(px "$2/c5-exit.png" "$2/c4-paused.png")" = 0 ]; then
        fail "the close sequence on $1: Program Manager not back the same after File > Exit and Alt+F4 ($2/c5-exit.png, c7-closed.png)"
    elif ! grep -aq WINEXIT "$o" 2>/dev/null; then
        fail "the close sequence on $1: Exit Windows did not end Windows ($2)"
    elif [ "$1" = w16 ] && ! grep -aq 'back to DOS, code 0000' "$o"; then
        fail "the close sequence on w16: Windows ended with $(grep -a -m1 'back to DOS' "$o" || echo 'no exit message') ($2)"
    else
        echo "the close sequence on $1: closed, reopened, paused, File > Exit, Alt+F4, Exit Windows: survived"
    fi
}
if [ -f "$refdir/p15.png" ]; then
    # a -R run made before the close sequence existed has none
    [ -z "$newref" ] && [ ! -f "$refdir/c1-closed.png" ] || closed 3.1 "$refdir"
fi
[ ! -f "$w/p15.png" ] || closed w16 "$w"
if [ $fails = 0 ]; then echo "ok   qtplay: QuickTime installs and plays its sample movie as on 3.1 ($out)"; fi
exit $(( fails > 0 ))
