#!/bin/sh
# ttview.sh - run TTVIEW.EXE (tests/apps/ttview.c: a TrueType viewer that
# rasterises with w16's fixed-point gdi/ttf/ttfx.c itself, a Windows 2.x
# program) on each system asked for, and check what it drew against the
# host's drawing of the same page (build/host/ttvhost, the same code):
# the page both hinted (w16's autohinter, gdi/ttf/ttfhint.c) and unhinted
# bit for bit, and the screen's client area showing exactly the one shown
# (tests/ttf/ttvcmp.py). TTVIEW gets "/T [/R n] [/U] [/2] [/G] C:\FONT.TTF"
# through C:\TTVIEW.CMD, writes C:\TTVIEW.PG (hinted), TTVIEWU.PG
# (unhinted), .SCR and .LOG and ends Windows.
#
#   usage: tests/ttview.sh [-f FONT.TTF] [-r N] [-o DIR] [-s] [-u] [-2] [-g] SYSTEM...
#     SYSTEM   w386    Windows/386 2.11 (tests/w386run.sh: MS-DOS 4.00,
#                      real-mode Windows under WIN386)
#              std     Windows 3.1 Standard mode (WIN /S)
#              enh     Windows 3.1 386 Enhanced mode (WIN /3)
#              w16     w16's install tree (build/w16), Standard mode
#              286     Windows 3.1 Standard mode on an emulated 80286
#                      (DOSBox-X cputype=286) at TTV_CYCLES (default
#                      "fixed 2000", roughly a 12 MHz 80286), for timing
#     -f FONT  the font (default tests/fixtures/fonts/LiberationSans-Regular.ttf)
#     -r N     render each page N times (TTVIEW /R: timing)
#     -o DIR   results (default build/ttview): DIR/SYSTEM/TTVIEW.{PG,SCR,LOG},
#              TTVIEWU.PG, screen.png (the capture), host.pg, host-u.pg
#     -u       show the unhinted page (TTVIEW /U; default the hinted)
#     -2       page 2, the specimen at 8-24 pixels to the em (TTVIEW /2)
#     -g       show the specimen as Windows draws it with its own TrueType
#              rasteriser (TTVIEW /G, 3.1 and later; implies -2): screen.png
#              is Windows' rendering, not compared; runs TTVIEW3.EXE, the
#              same marked 3.10 (3.1 adds no TrueType font for a 2.x program)
#     -s       also run each without /T and take screenshots of TTVIEW's
#              window (DIR/SYSTEM/shot*.png, view-*.png)
# Needs the reference installs (.tools/ref/win31, .tools/ref/w386, MS-DOS
# 4.00 built: TESTING.md) and make build/dos/TTVIEW.EXE build/host/ttvhost
# build/w16. Exit status 1 if any system's page or screen differs.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
font=$root/tests/fixtures/fonts/LiberationSans-Regular.ttf
out=$root/build/ttview reps=1 shots= opts= shown=h page2=
while getopts f:r:o:su2g opt; do
    case $opt in
    f) font=$OPTARG ;; r) reps=$OPTARG ;; o) out=$OPTARG ;; s) shots=1 ;;
    u) opts="$opts /U" shown=u ;; 2) opts="$opts /2" page2=-2 ;;
    g) opts="$opts /G" shown=g page2=-2 exe3=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || set -- w386 std enh w16
exe=$root/build/dos/TTVIEW.EXE
# /G: the 3.10-marked copy (3.1 adds no TrueType font for a 2.x program)
[ -n "${exe3:-}" ] && exe=$root/build/dos/TTVIEW3.EXE
mkdir -p "$out"
"$root/build/host/ttvhost" $page2 "$font" "$out/host.pg"
"$root/build/host/ttvhost" -u $page2 "$font" "$out/host-u.pg"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
rc=0
for sys in "$@"; do
    d=$out/$sys
    rm -rf "$d"
    mkdir -p "$d"
    for mode in test view; do
        [ $mode = view ] && [ -z "$shots" ] && continue
        if [ $mode = test ]; then
            printf '/T /R %s%s C:\\TTFONT.TTF\r\n' "$reps" "$opts" > "$work/TTVIEW.CMD"
        else
            printf '%s C:\\TTFONT.TTF\r\n' "$opts" > "$work/TTVIEW.CMD"
        fi
        case $sys in
        w386)
            if [ $mode = test ]; then
                W386_GET="TTVIEW.PG TTVIEWU.PG TTVIEW.SCR TTVIEW.LOG" W386_OUT=$d W386_TIMEOUT=${TTV_TIMEOUT:-120} \
                    "$root/tests/w386run.sh" "$exe" "$font=TTFONT.TTF" "$work/TTVIEW.CMD=TTVIEW.CMD" \
                    > "$d/run.txt" 2>&1 || true
            else
                W386_SHOT=5 W386_SHOTDIR=$d W386_TIMEOUT=40 \
                    "$root/tests/w386run.sh" "$exe" "$font=TTFONT.TTF" "$work/TTVIEW.CMD=TTVIEW.CMD" \
                    > "$d/view.txt" 2>&1 || true
            fi ;;
        std|enh|w16|286)
            # through WIN.INI run= and real input: 3.1 warns about a 2.x
            # program first (tests/scripts/input/ttview.txt presses OK);
            # as the shell it would not start at all
            case $sys in
            std) set -- -r -c 'WIN /S /B' ;;
            enh) set -- -r -3 -c 'WIN /3 /B' ;;
            w16) set -- -T "$root/build/w16" -c 'WIN /S /B' ;;
            286) set -- -r -p 286 -c 'WIN /S /B' ;;
            esac
            [ $sys = 286 ] && export DROPIN_CYCLES="${TTV_CYCLES:-fixed 2000}"
            slow=$([ $sys = 286 ] && echo 1 || echo)
            rm -rf "$work/in"
            # the script, its last wait long enough for a slow render (the
            # screen holds still while TTVIEW renders): on the 80286, 300 s
            wait=$([ -n "$slow" ] && echo '1200 300' || echo '120 3')
            sed "s/^settle 120 3$/settle $wait/" \
                "$root/tests/scripts/input/ttview.txt" > "$work/ttview.txt"
            # /T: then until TTVIEW has ended Windows (it renders the page
            # twice, hinted and not: the screen can hold still longer than
            # the settle's wait while it does)
            [ $mode = test ] && echo "waitexit $([ -n "$slow" ] && echo 1400 || echo 160)" >> "$work/ttview.txt"
            "$root/tests/inputrun.sh" -s -b "$([ -n "$slow" ] && echo 900 || echo 90)" \
                -t "${TTV_TIMEOUT:-$([ -n "$slow" ] && echo 1500 || echo 180)}" \
                "$work/ttview.txt" "$work/in" "$@" \
                -f "$exe=TTVIEW.EXE" -f "$font=TTFONT.TTF" -f "$work/TTVIEW.CMD=TTVIEW.CMD" \
                -i 'WIN.INI:windows:run=C:\TTVIEW.EXE' > "$d/$mode.txt" 2>&1 || true
            if [ $mode = test ]; then
                for f in TTVIEW.PG TTVIEWU.PG TTVIEW.SCR TTVIEW.LOG; do
                    [ -f "$work/in/work/c/$f" ] && cp "$work/in/work/c/$f" "$d/"
                done
            fi
            for f in "$work"/in/*.png; do
                [ -f "$f" ] && cp "$f" "$d/$mode-$(basename "$f")"
            done
            unset DROPIN_CYCLES ;;
        *) echo "ttview: no system $sys" >&2; exit 2 ;;
        esac
    done
    echo "== $sys"
    if [ -f "$d/TTVIEW.LOG" ] && [ -f "$d/TTVIEW.PG" ] && [ -f "$d/TTVIEWU.PG" ] && [ -f "$d/TTVIEW.SCR" ]; then
        tr -d '\r' < "$d/TTVIEW.LOG"
        python3 "$root/tests/ttf/ttvcmp.py" "$out/host.pg" "$d/TTVIEW.PG" "$out/host-u.pg" "$d/TTVIEWU.PG" \
            "$d/TTVIEW.SCR" $shown "$d/screen.png" || rc=1
    else
        echo "FAIL: TTVIEW wrote no results ($d/run.txt)"
        rc=1
    fi
done
exit $rc
