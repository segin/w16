#!/bin/sh
# Boot regression: boot real Windows 3.1 (the reference install) on w16's
# parts and compare each screen with the same boot on Microsoft's, pixel
# for pixel. `make test` does not boot Windows; run this after every
# merge (`make boot-test`).
#
# Cases (each a headless DOSBox boot through tests/dropin.sh, Program
# Manager as the shell, the screen taken after SECS seconds):
#   kernel     w16's KRNL286                       must be 0 pixels off
#   dosx       w16's KRNL286 on w16's DOSX         must be 0
#   wincom     w16's WIN.COM + KRNL286             must be 0
#   allw16     w16's WIN.COM + DOSX + KRNL286      must be 0
#   user       w16's USER on Microsoft's KRNL286   must be 0
#   userk      w16's USER on w16's KRNL286         must be 0
#   gdi        w16's GDI on Microsoft's KRNL286    must be 0
#   gdik       w16's GDI on w16's KRNL286          must be 0
#   usergdi    w16's USER and GDI on w16's KRNL286 must be 0
#   winoldap   w16's WINOLDAP.MOD on Microsoft's KRNL286: WIN.INI run= starts
#              a PIF (COMMAND.COM /C ECHO HI), the DOS program runs full
#              screen and Windows comes back; against Microsoft's WINOLDAP
#              doing the same                       must be 0
#   winoldapk  the same on w16's KRNL286            must be 0
#   tree       build/w16 as a whole (tests/w16tree.sh): must boot
#   allw16full every w16 part at once: WIN.COM, DOSX, KRNL286, USER, GDI,
#              PROGMAN, WINOLDAP, WIN87EM and the four fonts: must boot
#              (no pixel comparison: w16's PROGMAN and fonts differ)
#   input      real mouse and keyboard input (tests/inputrun.sh: xdotool on
#              the virtual display, through the drivers' event procedures)
#              all through the boot and after, then a menu by keyboard:
#              w16's USER and GDI on w16's KRNL286 against all-Microsoft
#              under the same input; the screens after must be identical
# The reference for each is Microsoft's KRNL286 (dropin.sh -r), DOSX,
# WIN.COM and USER. A boot whose BOOTLOG.TXT does not reach
# "INITDONE=Final USER" fails whatever the pixels say.
#
#   usage: tests/boottest.sh [-t SECS] [-o DIR] [CASE...]   (default: all)
# Screens, diffs and logs go to DIR (default build/boottest-HHMMSS, a
# new directory each run). Exit status: number of failed cases.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
secs=45 out=
while getopts t:o: opt; do
    case $opt in
    t) secs=$OPTARG ;;
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -n "$out" ] || out=$root/build/boottest-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
cases=${*:-kernel dosx wincom allw16 user userk gdi gdik usergdi winoldap winoldapk tree allw16full input}
user=$root/build/dos/USER.EXE
gdi=$root/build/dos/GDI.EXE
fails=0

# boot NAME [dropin.sh options...]: one boot, screenshot NAME.png, log NAME.txt
boot() {
    name=$1; shift
    "$root/tests/dropin.sh" -t "$secs" -i w16:watchdog=0 -s "$out/$name.png" "$@" \
        > "$out/$name.txt" 2>&1 || true
}
# booted NAME: the boot completed and Windows was still running when the
# screen was taken (KERNEL prints "w16: back to DOS" when Windows ends)
booted() { grep -q '^boot: complete' "$out/$1.txt" && ! grep -q 'back to DOS' "$out/$1.txt"; }

# check CASE W16 REF: both must boot; the screens must be identical
check() {
    c=$1 w=$2 r=$3
    if ! booted "$w"; then
        echo "FAIL $c: w16 boot did not complete (see $out/$w.txt)"; fails=$((fails + 1)); return
    fi
    if ! booted "$r"; then
        echo "FAIL $c: reference boot did not complete (see $out/$r.txt)"; fails=$((fails + 1)); return
    fi
    n=$(magick compare -metric AE "$out/$w.png" "$out/$r.png" "$out/$c-diff.png" 2>&1 >/dev/null || true)
    n=${n%% *}
    case $n in
    0) echo "ok   $c: 0 pixels" ;;
    *) echo "FAIL $c: $n pixels differ ($out/$c-diff.png)"; fails=$((fails + 1)) ;;
    esac
}

refdone=
ref() { [ -n "$refdone" ] || { boot ref -r; refdone=1; }; }

# the WINOLDAP cases: a PIF for COMMAND.COM /C ECHO HI, started by WIN.INI run=
woa=$root/build/dos/WINOLDAP.MOD
woaref=
woafiles() {
    [ -f "$out/HI.PIF" ] && return
    refwin=$root/.tools/ref/win31/cdrive/WINDOWS
    python3 "$root/tools/mkpif.py" "$refwin/_DEFAULT.PIF" "$out/HI.PIF" \
        'title=Echo Hi' 'program=COMMAND.COM' 'params=/C ECHO HI'
    cp "$refwin/WIN.INI" "$out/WIN.INI"
    python3 "$root/tests/scripts/iniset.py" "$out/WIN.INI" 'windows:run=C:\HI.PIF'
}
woaref() {
    [ -n "$woaref" ] && return
    woafiles
    boot woaref -r -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI"
    woaref=1
}

for c in $cases; do
    case $c in
    kernel) ref; boot kernel; check kernel kernel ref ;;
    dosx)   ref; boot dosx -x; check dosx dosx ref ;;
    wincom) ref; boot wincom -w; check wincom wincom ref ;;
    allw16) ref; boot allw16 -w -x; check allw16 allw16 ref ;;
    user)   ref; boot user -r -f "$user=WINDOWS/SYSTEM/USER.EXE"
            check user user ref ;;
    userk)  ref; boot userk -f "$user=WINDOWS/SYSTEM/USER.EXE"
            check userk userk ref ;;
    gdi)    ref; boot gdi -r -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
            check gdi gdi ref ;;
    gdik)   ref; boot gdik -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
            check gdik gdik ref ;;
    usergdi) ref; boot usergdi -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
            check usergdi usergdi ref ;;
    winoldap)  woaref; boot winoldap -r -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI" \
                   -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD"
               check winoldap winoldap woaref ;;
    winoldapk) woaref; boot winoldapk -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI" \
                   -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD"
               check winoldapk winoldapk woaref ;;
    tree)
        if "$root/tests/w16tree.sh" -t "$secs" -o "$out/tree" > "$out/tree.txt" 2>&1; then
            echo "ok   tree: boots ($(sed -n "s/.*Windows 3.1.s own files: \([0-9]*\).*/\1/p" "$out/tree.txt" | tail -1) pixels off stock 3.1, expected: w16 PROGMAN and fonts)"
        else
            echo "FAIL tree: see $out/tree.txt"; fails=$((fails + 1))
        fi ;;
    allw16full)
        fon=$root/build/fonts
        boot allw16full -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD" \
            -f "$root/build/dos/WIN87EM.DLL=WINDOWS/SYSTEM/WIN87EM.DLL" \
            -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
            -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON"
        if booted allw16full; then echo "ok   allw16full: boots"
        else echo "FAIL allw16full: did not boot (see $out/allw16full.txt)"; fails=$((fails + 1)); fi ;;
    input)
        scr=$root/tests/scripts/input/bootinput.txt
        "$root/tests/inputrun.sh" -b 1 -t 150 "$scr" "$out/input-ref" -r > "$out/input-ref.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -b 1 -t 150 "$scr" "$out/input-w16" \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/input-w16.txt" 2>&1 || true
        r=ok
        for s in 01-booted 02-menu 03-closed; do
            if [ ! -f "$out/input-w16/$s.png" ] || [ ! -f "$out/input-ref/$s.png" ]; then
                r="no $s screen"; break
            fi
            n=$(magick compare -metric AE "$out/input-w16/$s.png" "$out/input-ref/$s.png" "$out/input-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ ($out/input-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   input: 0 pixels"
        else echo "FAIL input: $r"; fails=$((fails + 1)); fi ;;
    *) echo "unknown case $c" >&2; exit 2 ;;
    esac
done
echo "boottest: $fails failure(s); results in $out"
exit "$fails"
