#!/bin/sh
# Notepad and Windows' end (the Notepad audit's M5, ARCHITECTURE.md 15.1):
# WM_QUERYENDSESSION posted to Notepad (SNAP's postwin) with text typed and
# one of its dialogs open, Microsoft's Notepad and w16's, on the reference
# 3.1. A system-modal box stops SNAP, so each case is a whole-screen
# capture from outside (tests/shellshot.sh: DOSBox-X on xvfb, Program
# Manager the shell), compared between the two Notepads (the caret's
# blink may differ: look at the diff). The boxes' text and look (system-
# modal) must match; Windows does not end with Save As up.
#   usage: tests/npqes.sh [-o DIR] [case...]
#   cases: saveas saved open find setup none clean (default: all)
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/ref/npqes/$(date +%Y%m%d-%H%M%S)
if [ "${1:-}" = -o ]; then out=$2; shift 2; fi
cases=${*:-saveas saved open find setup none clean}
steps() {   # the SNAP script of case $1
    printf '%s\n' 'run NOTEPAD.EXE' 'wait 3000'
    [ "$1" = clean ] || printf '%s\n' 'type abc'
    case $1 in
    saveas) printf '%s\n' 'menu f' 'char a' ;;
    open)   printf '%s\n' 'menu f' 'char o' ;;
    find)   printf '%s\n' 'menu s' 'char f' ;;
    setup)  printf '%s\n' 'menu f' 'char t' ;;
    clean)  printf '%s\n' 'menu f' 'char o' ;;
    saved)  printf '%s\n' 'menu f' 'char a' 'wait 1500' 'type C:\A.TXT' 'key 0D' 'wait 1500' \
                'menu f' 'char a' ;;
    esac
    printf '%s\n' 'wait 2000' 'postwin Notepad 11 0 0' 'wait 2000' 'dump' 'wait 60000'
}
status=0
for c in $cases; do
    for who in orig w16; do
        d=$out/$c/$who
        rm -rf "$d"; mkdir -p "$d"
        if [ $who = w16 ]; then
            steps "$c" | sed 's/^run NOTEPAD.EXE/run C:\\NOTEPAD.EXE/' > "$d/SNAP.TXT"
            "$root/tests/shellshot.sh" PROGMAN.EXE "$d/SNAP.TXT" "$d/screen.png" "${NPQES_SECS:-50}" \
                "$root/build/dos/NOTEPAD.EXE" > "$d/log.txt" 2>&1 || true
        else
            steps "$c" > "$d/SNAP.TXT"
            "$root/tests/shellshot.sh" PROGMAN.EXE "$d/SNAP.TXT" "$d/screen.png" "${NPQES_SECS:-50}" \
                > "$d/log.txt" 2>&1 || true
        fi
    done
done
for c in $cases; do
    a=$out/$c/orig/screen.png b=$out/$c/w16/screen.png
    if [ ! -f "$a" ] || [ ! -f "$b" ]; then
        echo "== $c: FAIL (no screenshot)"; status=1; continue
    fi
    px=$(magick compare -metric AE "$a" "$b" "$out/$c/diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
    # the Save As box says "w16" where 3.1's says "Windows" (2737 pixels)
    allow=0
    case $c in saveas|saved) allow=2737 ;; esac
    if [ "$px" -le "$allow" ]; then echo "== $c: ok ($px pixels)"; else echo "== $c: FAIL ($px pixels, $allow allowed)"; status=1; fi
done
echo "output: $out"
exit $status
