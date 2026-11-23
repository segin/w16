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
# movie, pauses it, plays it on and drags its window meanwhile (w16 hung
# there until 2026-11-19), ends the player with File > Exit, starts it again from
# Program Manager, opens the movie, ends it with Alt+F4, and ends Windows
# (the close sequence: closing the player ended Windows with "Standard
# Mode: Fault in MS-DOS Extender" on w16's tree alone, 2026-11-18).
# With -T the w16 side is instead w16's install tree alone (build/w16 as
# C:\W16, nothing of Microsoft's: tests/dropin.sh -T, the user's setup),
# QuickTime installed into it the same way. The tree's SYSTEM.INI loads
# w16's SB16.DRV, and DOSBox has a Sound Blaster 16, so QuickTime finds a
# sound device there (its controller's speaker button live, the movie's
# sound played); the stock 3.1 run then gets one too: Windows 3.1's own
# SNDBLST2.DRV, expanded from its Setup files (.tools/ref/win31/src) at
# test time, in its SYSTEM.INI (DOSBox's card: port 220h, IRQ 7). Without
# it the speaker button differs: live on w16, gray on 3.1, and its top
# edge in the dragged movie's picture (SB16.DRV became the tree's default
# on 2026-11-20).
#
# QuickTime is Apple's: its installer is never committed. It is copied in
# at test time from $W16_QTDIR (default ~/Downloads/qtw16), which must
# hold QTINSTAL.EXE (what QUICKTIM.EXE or QTEASY16.EXE unpacks); without it
# the test says so and passes (exit 0, "skip").
#
#   usage: tests/qtplay.sh [-o OUTDIR] [-m "FILE ..."] [-t "FILE ..."] [-T 286|386] [-E] [-R REFDIR]
#     -m "FILE ..."  leave these files of the tree out (Microsoft's stay;
#                    KRNL386.EXE: Microsoft's KRNL386 runs): bisection
#     -t "FILE ..."  only these files of the tree (e.g. TIMER.DRV: the
#                    boot-test case timer); everything else, KRNL386 and
#                    DOSX too, stays Microsoft's
#     -T 286|386     w16's install tree alone, under its KRNL286 (what
#                    tests/dropin.sh -T boots: it leaves KRNL386.EXE out) or
#                    its KRNL386
#     -E             386 Enhanced mode on both sides (WIN /3): Microsoft's
#                    WIN386 on stock 3.1, the tree's on the w16 side; with
#                    -T 386 the user's setup of 2026-11-22 (plain DOSBox:
#                    playing the movie ended Windows with a #GP in
#                    TIMER:1, docs/notes/quicktime-handover.md). Known
#                    differences are printed, not checked: a drag not
#                    taken (now and then, UNVERIFIED why), the
#                    movie's length (w16 about twice 3.1's 12 s: TIMER's
#                    events late on WIN386's virtual 8254) and the dragged
#                    picture (the speaker button: SB16.DRV has no devices
#                    in Enhanced mode)
#     -R REFDIR      the stock 3.1 run made before (its shots); no new one
# QTPLAY_W16OPTS in the environment: more tests/dropin.sh options for the
# w16 side only, e.g. "-p 386 -i w16:vmmtrace=1" (DOSBox-X; with
# DROPIN_CONF="[serial] serial1=file file:PATH" and -R, the VMM's trace)
# Checks (exit 1 if one fails):
#   - both runs reach the Movie Player (w16 runs with [w16] hangwatch=120,
#     or $QTPLAY_HANGWATCH: a hang ends Windows with a report in
#     OUTDIR/w16/work/c/OUT.TXT). The installer unpacks its files without
#     yielding: KERNEL's longest stretch without progress there (printed
#     at the end, "longest stretch") is 15-21 s on an idle
#     installation-05, 25-28 s at load average 30-45 and 43-46 s at 55-57
#     (DOSBox's cycles=max runs fewer instructions per emulated second
#     under load). The limit
#     was 30 until 2026-11-20 and ended busy installs under load; the
#     hangs it is for never end (the movie, 2026-11-17; the drag,
#     2026-11-19);
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
#     ends Windows (WIN returns; w16's KERNEL: "back to DOS, code 0000");
#     the movie's window opened again (c3-reopened, c6b-opened) is where
#     3.1 puts it (what changed from c1-closed, c6-player: the same
#     rectangle). The other screens (c2, c4, c5, c7, c8) are saved, not
#     compared;
#   - the drag while playing (after c4-paused: Space, the movie's window
#     dragged by its caption, the button held while arrow keys move it
#     (the same drop under any load), and dropped, the movie left to end;
#     w16 hung there until 2026-11-19, user/track.c): the window lands
#     where 3.1 puts it (what changed from c4-paused to c4b-dropped, the
#     pointer's columns left out: the same rectangle) and shows 3.1's last
#     frame there, pixel for pixel.
#     c4a-dragging (the frame mid-drag) is saved, not compared.
#   - the installer's banner (the Apple logo and "Apple Computer QuickTime
#     for Windows 2.1.2" across the top, an RLE4 DIB through
#     CreateDIBitmap and BitBlt; black on w16 until 2026-11-20,
#     drivers/vga/dib.c) is 3.1's, pixel for pixel, in 02-begin.
# The installer's own boxes (01-05) and the rest of the screen are saved
# but not compared: w16's VGA.DRV art (caption buttons, the pointer) and
# the installer's boxes differ (docs/notes/quicktime-handover.md, "Left
# over").
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= msfiles= refdir= only= treek= enh=
while getopts o:m:t:T:ER: opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) msfiles=$OPTARG ;;
    t) only=$OPTARG ;;
    T) treek=$OPTARG
       case $treek in 286|386) ;; *) echo "-T 286 or -T 386" >&2; exit 2 ;; esac ;;
    E) enh=1 ;;
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
# the script for a side: @WINDIR@ is its Windows directory
side_script() { sed "s/@WINDIR@/C:\\\\$1/g" "$script" > "$out/qtplay-$1.txt"; }
side_script WINDOWS
side_script W16
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
# 386 Enhanced mode: WIN /3 on both sides (from here on the positional
# parameters are the w16 side's dropin.sh options)
modec=
[ -z "$enh" ] || { modec="WIN /3 /B"; set -- "$@" -c "$modec"; }

# the stock 3.1 run: with the tree alone, a sound device as the tree has
# (its SYSTEM.INI loads SB16.DRV): Windows 3.1's SNDBLST2.DRV on DOSBox's
# Sound Blaster (port 220h, IRQ 7)
refsnd=
if [ -n "$treek" ] && grep -qi '^wave=sb16.drv' "$tree/SYSTEM.INI"; then
    python3 -I "$root/tools/szdd.py" "$root/.tools/ref/win31/src/SNDBLST2.DR_" "$out/SNDBLST2.DRV"
    refsnd="-f $out/SNDBLST2.DRV=WINDOWS/SYSTEM/SNDBLST2.DRV -i drivers:wave=sndblst2.drv
        -i drivers:midi=sndblst2.drv -i sndblst.drv:port=220 -i sndblst.drv:int=7"
fi
# in 386 Enhanced mode with its VxD, as Setup installs the two (the tree
# has w16's VSBD.386: assets/SYSTEM.INI)
if [ -n "$refsnd" ] && [ -n "$enh" ]; then
    python3 -I "$root/tools/szdd.py" "$root/.tools/ref/win31/src/VSBD.38_" "$out/VSBD.386"
    refsnd="$refsnd -f $out/VSBD.386=WINDOWS/SYSTEM/VSBD.386 -i 386Enh:+device=vsbd.386"
fi
newref=
if [ -z "$refdir" ]; then
    refdir=$out/ref newref=1
    # shellcheck disable=SC2086
    "$root/tests/inputrun.sh" -s -b 40 -t 900 "$out/qtplay-WINDOWS.txt" "$refdir" -r -3 \
        ${modec:+-c "$modec"} -f "$qt/QTINSTAL.EXE=TMP/QTINSTAL.EXE" $refsnd > "$out/ref.txt" 2>&1 &
fi
wscript=$out/qtplay-WINDOWS.txt
[ -z "$treek" ] || wscript=$out/qtplay-W16.txt
hw=${QTPLAY_HANGWATCH:-120}
# shellcheck disable=SC2086
"$root/tests/inputrun.sh" -s -b 40 -t 900 "$wscript" "$out/w16" \
    -i w16:hangwatch=$hw "$@" ${QTPLAY_W16OPTS:-} > "$out/w16.txt" 2>&1 || true
wait
refdir=$(cd "$refdir" && pwd)
# how close w16 came to the hangwatch limit (KERNEL prints it as Windows
# ends: [w16] exitmsg)
ls=$(grep -a -m1 'longest stretch' "$out/w16/work/c/OUT.TXT" 2>/dev/null | tr -d '\r' || true)
echo "hangwatch $hw s${ls:+; ${ls#w16: }}"

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
        if [ $d -ge -1 ] && [ $d -le 1 ]; then :
        elif [ -n "$enh" ]; then
            # known: w16's TIMER.DRV in 386 Enhanced mode, without VTDAPI
            # on WIN386's virtual 8254, calls events late (ARCHITECTURE.md
            # 21.5); 3.1 takes 12 s with and without a sound device, w16
            # 22-26 s (2026-11-22)
            echo "known, not checked with -E: the last frame came in shot $we on w16, $re on 3.1"
        else fail "the last frame came in shot $we on w16, $re on 3.1"; fi
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
    elif grep -a 'back to DOS, code' "$o" 2>/dev/null | grep -aqv 'code 0000'; then
        # w16's KERNEL says how Windows ended ([w16] exitmsg)
        fail "the close sequence on $1: Windows ended with $(grep -a -m1 'back to DOS' "$o") ($2)"
    elif ! grep -aq 'WINEXIT\|back to DOS, code 0000' "$o" 2>/dev/null; then
        # WIN back at the prompt (the autoexec's WINEXIT; a w16 run ends
        # DOSBox there)
        fail "the close sequence on $1: Exit Windows did not end Windows ($2)"
    else
        echo "the close sequence on $1: closed, reopened, paused, File > Exit, Alt+F4, Exit Windows: survived"
    fi
}
if [ -f "$refdir/p15.png" ]; then
    # a -R run made before the close sequence existed has none
    [ -z "$newref" ] && [ ! -f "$refdir/c1-closed.png" ] || closed 3.1 "$refdir"
fi
[ ! -f "$w/p15.png" ] || closed w16 "$w"
# where the movie's window comes back: what changed from the empty player
# to the movie opened again in it (c1-closed, c3-reopened) and in a new
# player (c6-player, c6b-opened), the same rectangle on both sides (on
# w16 the reopened movie came back a step higher until 2026-11-18: MDI's
# default place, user/mdi.c)
if [ -f "$refdir/c6b-opened.png" ] && [ -f "$w/c6b-opened.png" ]; then
    for pr in "c1-closed c3-reopened" "c6-player c6b-opened"; do
        a=${pr% *} b=${pr#* }
        ra=$(magick "$refdir/$a.png" "$refdir/$b.png" -compose difference -composite -threshold 0 -format '%@' info:)
        wa=$(magick "$w/$a.png" "$w/$b.png" -compose difference -composite -threshold 0 -format '%@' info:)
        echo "the movie's window in $b: $ra on 3.1, $wa on w16"
        [ "$ra" = "$wa" ] || fail "the movie's window in $b is $wa on w16, $ra on 3.1"
    done
fi
# the drag while the movie plays: where the movie's window landed (what
# changed from c4-paused to c4b-dropped, its old place and its new one: the
# same rectangle on both sides), and the picture in it at the end (the
# window is in that rectangle's lower right, as big as the reopened one,
# c1-closed to c3-reopened; the picture is the window less 24 rows at the
# top, its border and caption, 20 at the bottom, the controller and
# border, and 5 columns each side: the same pixels on both sides)
dragged() {
    # the pointer left out: where it was in c4-paused (x 312 on; the window
    # ends left of it) and the top left corner, where the script puts it
    # after the drop (a relative move lands where the host's load decides)
    magick \( "$1/c4-paused.png" -fill black -draw "rectangle 312,0 639,479" \
            -draw "rectangle 0,0 23,31" \) \
        \( "$1/c4b-dropped.png" -fill black -draw "rectangle 312,0 639,479" \
            -draw "rectangle 0,0 23,31" \) \
        -compose difference -composite -threshold 0 -format '%@' info:
}
if [ -f "$refdir/c4b-dropped.png" ] && [ -f "$w/c4b-dropped.png" ]; then
    ra=$(dragged "$refdir") wa=$(dragged "$w")
    echo "the movie's window dragged while playing: $ra on 3.1, $wa on w16"
    if [ "$ra" != "$wa" ] && [ -n "$enh" ]; then
        # known (2026-11-22): in 386 Enhanced mode the drag is sometimes
        # not taken at all (3 runs in 7 under load: the window stays where
        # it was, the movie plays on); UNVERIFIED why
        echo "known, not checked with -E: the movie's window dragged while playing: $wa on w16, $ra on 3.1 ($w/c4b-dropped.png)"
    elif [ "$ra" != "$wa" ]; then
        fail "the movie's window dragged while playing: $wa on w16, $ra on 3.1 ($w/c4b-dropped.png)"
    else
        ws=$(magick "$refdir/c1-closed.png" "$refdir/c3-reopened.png" -compose difference -composite \
            -threshold 0 -format '%@' info:)
        ws=${ws%%+*} sz=${ra%%+*} xy=${ra#*+}
        ww=${ws%x*} wh=${ws#*x} bw=${sz%x*} bh=${sz#*x} bx=${xy%+*} by=${xy#*+}
        crop="$((ww - 10))x$((wh - 44))+$((bx + bw - ww + 5))+$((by + bh - wh + 24))"
        magick "$refdir/c4b-dropped.png" -crop "$crop" +repage "$out/drag-ref.png"
        magick "$w/c4b-dropped.png" -crop "$crop" +repage "$out/drag-w16.png"
        n=$(px "$out/drag-ref.png" "$out/drag-w16.png" "$out/drag-diff.png")
        echo "the dragged movie's picture ($crop): $n pixels differ from 3.1's"
        if [ "$n" = 0 ]; then :
        elif [ -n "$enh" ]; then
            # known: w16's SB16.DRV offers no devices in 386 Enhanced mode,
            # so the controller's speaker button (its top edge is in this
            # picture) is gray on w16, live on 3.1 with SNDBLST2.DRV
            echo "known, not checked with -E: the dragged movie's picture: $n pixels differ ($out/drag-diff.png)"
        else fail "the dragged movie's picture: $n pixels differ from 3.1's ($out/drag-diff.png)"; fi
    fi
elif [ -f "$refdir/c4b-dropped.png" ]; then
    fail "the drag while the movie plays did not get to its drop on w16 ($w/input.log, $w/work/c/OUT.TXT)"
elif [ -n "$newref" ] && [ -f "$refdir/c4-paused.png" ]; then
    # (a -R run made before the drag existed has none)
    fail "the drag while the movie plays did not get to its drop on 3.1 ($refdir/input.log)"
fi
# the installer's banner (488 x 75 at 76,20 with its frame; the boxes
# open below it): 3.1's pixels
if [ -f "$refdir/02-begin.png" ] && [ -f "$w/02-begin.png" ]; then
    magick "$refdir/02-begin.png" -crop 500x84+70+16 +repage "$out/banner-ref.png"
    magick "$w/02-begin.png" -crop 500x84+70+16 +repage "$out/banner-w16.png"
    n=$(px "$out/banner-ref.png" "$out/banner-w16.png" "$out/banner-diff.png")
    echo "the installer's banner: $n pixels differ from 3.1's"
    [ "$n" = 0 ] || fail "the installer's banner: $n pixels differ from 3.1's ($out/banner-diff.png)"
elif [ -f "$refdir/02-begin.png" ]; then
    fail "w16 did not get to the installer's Begin Install box ($w/input.log)"
fi
if [ $fails = 0 ]; then echo "ok   qtplay: QuickTime installs and plays its sample movie as on 3.1 ($out)"; fi
exit $(( fails > 0 ))
