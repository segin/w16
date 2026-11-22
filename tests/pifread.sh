#!/bin/sh
# pifread.sh - how WINOLDAP.MOD (Standard mode) and WINOA386.MOD (386
# Enhanced mode) read a .PIF and what they do with it: Windows 3.1's
# against w16's (ARCHITECTURE.md 9.8, 12.11; black-box).
#
# Each fixture (tests/scripts/pifread/mkpifs.py: C:\WC1.COM, "Probe
# Program", C:\WINDOWS\SYSTEM, "stdargs"/"enhargs", one setting changed)
# runs from WIN.INI run= in a scratch copy of the reference install
# (tests/worun.sh, DOSBox-X, its INT 21h and file logs on) with Microsoft's
# module or w16's. Kept for each run, in OUT/NAME-WHO-MODE/:
#   reads    the PIF's opens, seeks and reads (DOSBox-X's file log)
#   wc       what WC1.COM was given: its command tail and directory
#   blocks   Standard mode: WIN.COM's switcher block as WINOLDAP filled
#            it (WO2.EXE's snapshots, tools/woblk.py; tests/winoldap.sh)
#   shell    386 Enhanced mode: VXS.386's records of WINOA386's SHELL
#            calls (function 3's registers and block; 5, 6, 7, 15h)
#   *.png    start (WC1 running: it waits 4 s), after (Windows again)
#
#   usage: tests/pifread.sh [-m std|386] [-w | -c] [-j JOBS] OUT [FIXTURE...]
#     -m MODE  std (WIN /S, WINOLDAP.MOD) or 386 (WIN /3, WINOA386.MOD)
#     -w       w16's module (build/dos/WINOLDAP.MOD or WINOA386.MOD) in
#              place of Microsoft's; the rest is 3.1's (KERNEL, WIN386)
#     -c       both, and compare: reads, wc, blocks and shell must be
#              equal, the screens within 40 pixels (Windows' screens with
#              the icons' corner masked: w16's icon is its own); PASS or
#              FAIL per fixture, the exit code the failures
#     -W       with -w or -c: w16's WIN386.EXE too on w16's side (no
#              SHELL record there: VXS.386 does not run on it)
#     -j JOBS  runs at once (default 4)
# FIXTURE: names from mkpifs.py's LIST (default all). Needs `make dos
# woprobes vxprobes`. DOSBox-X runs: on the remote host only (TESTING.md).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
mode=386 whos=ms jobs=4 w386=
while getopts m:wcWj: opt; do
    case $opt in
    m) mode=$OPTARG ;;
    w) whos=w16 ;;
    c) whos="ms w16" ;;
    W) w386=1 ;;
    j) jobs=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
out=$1; shift
mkdir -p "$out"
out=$(cd "$out" && pwd)
fx=$out/fixtures
[ -f "$fx/LIST" ] || python3 -I "$root/tests/scripts/pifread/mkpifs.py" "$fx" "$ref/WINDOWS/DOSPRMPT.PIF"
names=$*
[ -n "$names" ] || names=$(cut -d' ' -f1 "$fx/LIST")
printf 'wait\r\n' > "$out/WCWAIT.TXT"
printf 'C:\\WC1.COM batargs %%1\r\n' > "$out/WC1.BAT"
printf '[wo2]\r\nhooks=fdt\r\n' > "$out/WO2.INI"
# (Return only for the Parameters dialog: elsewhere it opened Program
# Manager's selected item, File Manager, whose directory changes then
# reached a windowed DOS program's: DOSBox's DOS keeps one current
# directory for all VMs)
printf 'sleep 32\nshot boot\nsleep 5\nshot start\nsleep 20\nshot after\n' > "$out/script.txt"
printf 'sleep 32\nshot boot\nsleep 5\nshot start\nkey Return\nsleep 20\nshot after\n' > "$out/scriptq.txt"

one() {
    n=$1 who=$2
    d=$out/$n-$who-$mode
    rm -rf "$d"
    sc=$out/script.txt
    case $n in *q386) sc=$out/scriptq.txt ;; esac
    set -- -o "$d" -l -t 200 -x "$sc" \
        -f "$root/build/dos/WC1.COM=WC1.COM" -f "$root/build/dos/WC1.COM=WC1.EXE" \
        -f "$out/WC1.BAT=WC1.BAT" -f "$out/WCWAIT.TXT=WCWAIT.TXT"
    if [ "$n" = nopif ]; then
        set -- "$@" -n 'windows:run=C:\WC1.COM'
    else
        set -- "$@" -f "$fx/$(echo "$n" | tr a-z A-Z).PIF=PROBE.PIF" -n 'windows:run=C:\PROBE.PIF'
    fi
    if [ "$mode" = 386 ]; then
        set -- "$@" -w 'WIN /3'
        # (w16's WIN386 after WIN.COM's logo: /B, so that the display is
        # Windows' again; UNVERIFIED why it stays on the logo without)
        [ $who = ms ] || [ -z "$w386" ] || set -- "$@" -w 'WIN /3 /B'
        # (VXS.386 needs Hook_Device_PM_API, which w16's VMM does not
        # have: with -W, w16's side has no SHELL record)
        [ $who = w16 ] && [ -n "$w386" ] || set -- "$@" -f "$root/build/dos/VXS.386=WINDOWS/SYSTEM/VXS.386" \
            -i '386Enh:+device=vxs.386'
        [ $who = ms ] || set -- "$@" -f "$root/build/dos/WINOA386.MOD=WINDOWS/SYSTEM/WINOA386.MOD"
        [ $who = ms ] || [ -z "$w386" ] || set -- "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
        WORUN_CONF="[serial]
serial1=file file:$d.com1" "$root/tests/worun.sh" "$@" > "$d.txt" 2>&1 || true
    else
        set -- "$@" -c 'SET TEMP=C:\WINDOWS\TEMP' -f "$root/build/dos/WO2.EXE=WO2.EXE" \
            -f "$out/WO2.INI=WO2.INI" -n 'windows:load=C:\WO2.EXE'
        [ $who = ms ] || set -- "$@" -m "$root/build/dos/WINOLDAP.MOD"
        "$root/tests/worun.sh" "$@" > "$d.txt" 2>&1 || true
    fi
    [ ! -f "$d.com1" ] || mv "$d.com1" "$d/com1.txt"
    summarize "$d"
}

# the PIF's opens, seeks and reads; WC.LOG's tail and directory; SHELL
summarize() {
    d=$1
    python3 -I "$root/tests/scripts/pifread/pifreads.py" "$d/dosbox.log" PROBE.PIF > "$d/reads" 2>&1 || true
    if [ -f "$d/c/WC.LOG" ]; then
        tr -d '\r' < "$d/c/WC.LOG" | grep -a -E "tail=|cwd=|video mode=|top=" > "$d/wc" || true
    else
        echo "(no WC.LOG)" > "$d/wc"
    fi
    if [ -f "$d/com1.txt" ]; then
        python3 -I "$root/tests/scripts/pifread/shellcalls.py" "$d/com1.txt" > "$d/shell" 2>&1 || true
    fi
    if [ -f "$d/c/WO2.LOG" ]; then
        python3 "$root/tools/woblk.py" "$d/c/WO2.LOG" -m 185:4 | grep -v '185h then' > "$d/blocks" || true
    fi
    rm -rf "$d/c/WINDOWS" "$d/dosbox.log.gz"
    gzip -f "$d/dosbox.log" 2>/dev/null || true
}

running=0
for n in $names; do
    for who in $whos; do
        # PIFREAD_KEEP=1: Microsoft's runs already in OUT are kept
        if [ $who = ms ] && [ -n "${PIFREAD_KEEP:-}" ] && [ -f "$out/$n-ms-$mode/wc" ]; then continue; fi
        one "$n" $who &
        running=$((running + 1))
        if [ $running -ge "$jobs" ]; then wait; running=0; fi
    done
done
wait
fails=0
for n in $names; do
    printf '== %s (%s)\n' "$n" "$(grep "^$n " "$fx/LIST" | cut -d' ' -f2-)"
    if [ "$whos" != "ms w16" ]; then
        d=$out/$n-$whos-$mode
        for f in wc reads blocks shell; do [ ! -f "$d/$f" ] || sed 's/^/  /' "$d/$f"; done
        continue
    fi
    a=$out/$n-ms-$mode b=$out/$n-w16-$mode bad=
    for f in wc reads blocks shell; do
        [ -f "$a/$f" ] && [ -f "$b/$f" ] || continue
        diff -u "$a/$f" "$b/$f" > "$b/$f.diff" 2>&1 || bad="$bad $f"
    done
    for f in "$a"/*.png; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .png) g=$b/$(basename "$f")
        [ -f "$g" ] || { bad="$bad $s(missing)"; continue; }
        m=
        [ "$(magick identify -format '%wx%h' "$f")" != 640x480 ] || m="-fill black -draw 'rectangle 0,400 199,479'"
        eval magick "\"$f\"" $m "\"$b/a.png\""
        eval magick "\"$g\"" $m "\"$b/b.png\""
        px=$(magick compare -metric AE "$b/a.png" "$b/b.png" "$b/$s-diff.png" 2>&1 >/dev/null || true)
        px=${px%%[. ]*}
        rm -f "$b/a.png" "$b/b.png"
        case $px in ''|*[!0-9]*) bad="$bad $s(?)" ;; *) [ "$px" -le 40 ] || bad="$bad $s($px)" ;; esac
    done
    if [ -z "$bad" ]; then echo "PASS $n"; else
        echo "FAIL $n:$bad"; fails=$((fails + 1))
        cat "$b"/*.diff 2>/dev/null | grep '^[-+][^-+]' | head -12 | sed 's/^/    /'
    fi
done
[ "$whos" != "ms w16" ] || echo "pifread: $fails failure(s)"
exit $fails
