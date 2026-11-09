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
#   win386     w16's WIN386.EXE, WIN /3 (386 Enhanced) must be 0
#              (tests/win386.sh: Microsoft's KRNL386 and the rest, against
#              Microsoft's WIN386.EXE; both must reach Final USER init).
#              Not in the default set yet: w16's boot is intermittent (an
#              occasional #GP in a hardware interrupt handler, ARCHITECTURE.md
#              12.3); run it by name: tests/boottest.sh win386
#   tree       build/w16 as a whole (tests/w16tree.sh): must boot
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
cases=${*:-kernel dosx wincom allw16 user userk gdi gdik usergdi tree}
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
    win386)
        if "$root/tests/win386.sh" -t "$secs" -o "$out/win386" > "$out/win386.txt" 2>&1 \
           && grep -q '^INITDONE=Final USER' "$out/win386/w16-bootlog.txt" \
           && grep -q '^INITDONE=Final USER' "$out/win386/ms-bootlog.txt"; then
            echo "ok   win386: 0 pixels"
        else
            echo "FAIL win386: see $out/win386.txt and $out/win386/"; fails=$((fails + 1))
        fi ;;
    tree)
        if "$root/tests/w16tree.sh" -t "$secs" -o "$out/tree" > "$out/tree.txt" 2>&1; then
            echo "ok   tree: boots ($(sed -n "s/.*Windows 3.1.s own files: \([0-9]*\).*/\1/p" "$out/tree.txt" | tail -1) pixels off stock 3.1, expected: w16 PROGMAN and fonts)"
        else
            echo "FAIL tree: see $out/tree.txt"; fails=$((fails + 1))
        fi ;;
    *) echo "unknown case $c" >&2; exit 2 ;;
    esac
done
echo "boottest: $fails failure(s); results in $out"
exit "$fails"
