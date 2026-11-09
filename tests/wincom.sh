#!/bin/sh
# Compare w16's WIN.COM with Windows 3.1's (black-box; ARCHITECTURE.md 9.5).
#
# Each scenario runs twice in headless DOSBox-X, in scratch copies of the
# reference install (.tools/ref/win31/cdrive): once with Microsoft's
# WIN.COM, once with build/dos/WIN.COM. What is compared:
#   - the text screen, exit code and video mode after each WIN (WC2.COM);
#   - what WIN.COM writes to standard output (redirected to C:\O*.TXT);
#   - what the programs it starts were given: command tail, FCBs,
#     environment, video mode, vectors (WC1.COM standing in for WSWAP.EXE,
#     DOSX.EXE, WIN386.EXE or KRNL386.EXE);
#   - the switcher block WSWAP reads (WC4.COM as WSWAP.EXE).
# Values that depend on where things are in memory (segments, offsets of
# WIN.COM's handler and data, free memory) are left out of the comparison.
#
#   usage: tests/wincom.sh [-k] [-v] [SCENARIO...]
#     -k  keep the scratch directories (printed)
#     -v  show both outputs, not just the differences
# Scenario names: see the list below (default: all). Needs dosbox-x and
# `make dos wcprobes`. Prints PASS/FAIL per scenario; exit code = failures.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
dos=$root/build/dos
keep= verbose=
while getopts kv opt; do
    case $opt in
    k) keep=1 ;;
    v) verbose=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
for f in WIN.COM WC1.COM WC2.COM WC4.COM WC7.COM WC8.COM; do
    [ -f "$dos/$f" ] || { echo "missing $dos/$f (make dos wcprobes)" >&2; exit 2; }
done
work=$(mktemp -d)
[ -n "$keep" ] && echo "scratch: $work" || trap 'rm -rf "$work"' EXIT

# ---- scenarios: NAME|options|DOS commands (";" separates lines) ----
# options: standin (WC1 as WSWAP, DOSX, WIN386), krnl (WC1 as KRNL386:
# Microsoft's WSWAP, DOSX, WIN386 run), wc4 (WC4 as WSWAP), mem=N (MB),
# cpu=T, nohimem (no XMS), dos=V (DOS version), rm=PATH (delete a file),
# ini=SEC:KEY=VAL (SYSTEM.INI), tsr=N (WC7 N first: Windows "running").
# WC2 "!..." resets the video mode to 3 after recording.
scenarios='
switches|standin|WIN /? > C:\O1.TXT;C:\WC2 !help;WIN /R > C:\O2.TXT;C:\WC2 !real;WIN /S;C:\WC2 !S;WIN /3;C:\WC2 !3;WIN;C:\WC2 !none;WIN /2 /B X  y;C:\WC2 !2 B X y;WIN calc;C:\WC2 !calc
switches2|standin|WIN /3 x;C:\WC2 !a;WIN -3;C:\WC2 !b;WIN /3 /D:F;C:\WC2 !c;WIN /3/D:F;C:\WC2 !d;WIN /x /s /3;C:\WC2 !e;WIN x /3;C:\WC2 !f;WIN /3 /R;C:\WC2 !g;WIN /S/3;C:\WC2 !h;WIN /3 /S;C:\WC2 !i;WIN /3 /2;C:\WC2 !j;WIN :;C:\WC2 !k;WIN /S /?;C:\WC2 !l;WIN -s -3 x;C:\WC2 !m
rc|standin|echo 5> C:\WCRC.TXT;WIN /3;C:\WC2 !rc5 3;WIN /S;C:\WC2 !rc5 S;echo 66 7> C:\WCRC.TXT;WIN /3;C:\WC2 !rc66 7
places|standin|CD \;WINDOWS\WIN /3 a;C:\WC2 !root;MD C:\W2;COPY C:\WINDOWS\WIN.COM C:\W2\WIN.COM > NUL;CD \W2;SET PATH=C:\WINDOWS\;WIN /3;C:\WC2 !path;WIN /S;C:\WC2 !pathS
noxms|standin nohimem|WIN > C:\O1.TXT;C:\WC2 !none;WIN /3 > C:\O2.TXT;C:\WC2 !3;WIN /? > C:\O3.TXT;C:\WC2 !help
noext|standin mem=1|WIN > C:\O1.TXT;C:\WC2 !none;WIN /S > C:\O2.TXT;C:\WC2 !S
cpu8086|standin cpu=8086|WIN > C:\O1.TXT;C:\WC2 !none;WIN /3 > C:\O2.TXT;C:\WC2 !3;WIN /? > C:\O3.TXT;C:\WC2 !help
cpu286|standin cpu=286|WIN;C:\WC2 !none;WIN /3 > C:\O1.TXT;C:\WC2 !3;WIN /? > C:\O2.TXT;C:\WC2 !help
mem2|standin mem=2|WIN;C:\WC2 !none;WIN /3;C:\WC2 !3;WIN /? > C:\O1.TXT;C:\WC2 !help
xms1020|standin mem=3|C:\WC8 577 > NUL;WIN /? > C:\O1.TXT;C:\WC2 !help;WIN;C:\WC2 !none
xms1024|standin mem=3|C:\WC8 576 > NUL;WIN /? > C:\O1.TXT;C:\WC2 !help;WIN;C:\WC2 !none
dos30|standin dos=3.0|WIN > C:\O1.TXT;C:\WC2 !none
dos310|standin dos=3.1|WIN;C:\WC2 !none
nowswap|standin rm=WINDOWS/SYSTEM/WSWAP.EXE|WIN /S > C:\O1.TXT;C:\WC2 !S;WIN /S x > C:\O2.TXT;C:\WC2 !S x
nodosx|standin rm=WINDOWS/SYSTEM/DOSX.EXE|WIN /S > C:\O1.TXT;C:\WC2 !S;WIN;C:\WC2 !none
nowin386|standin rm=WINDOWS/SYSTEM/WIN386.EXE|WIN /3 > C:\O1.TXT;C:\WC2 !3;WIN;C:\WC2 !none
running1|standin tsr=1|WIN > C:\O1.TXT;C:\WC2 !none
running2|standin tsr=2|WIN /S > C:\O1.TXT;C:\WC2 !S
running3|standin tsr=3|WIN > C:\O1.TXT;C:\WC2 !none
running5|standin tsr=5|WIN /3 > C:\O1.TXT;C:\WC2 !3
switcher4|standin tsr=6|WIN;C:\WC2 !none
block|wc4|WIN /S;WIN /S /B X  y;MD C:\TMP;SET TEMP=C:\TMP;WIN /S z
blockini|wc4 ini=NonWindowsApp:SwapDisk=C:\WINDOWS|WIN /S
block286|wc4 cpu=286|WIN /S
chain|krnl|echo 7> C:\WCRC.TXT;WIN /S /B X  y;C:\WC2 !S7;WIN /3 a b;C:\WC2 !3 7;echo 66 9> C:\WCRC.TXT;WIN /S c;C:\WC2 !S 66 9;echo 66 8> C:\WCRC.TXT;WIN /3 d;C:\WC2 !3 66 8
'

run_one() {   # run_one DIR WINCOM OPTIONS COMMANDS
    d=$1 wincom=$2 opts=$3 cmds=$4
    mkdir -p "$d"
    cp -r "$ref" "$d/c"
    cp "$wincom" "$d/c/WINDOWS/WIN.COM"
    cp "$dos/WC2.COM" "$dos/WC7.COM" "$dos/WC8.COM" "$d/c/"
    mem=16 cpu=386 xms=true ver=
    for o in $opts; do
        case $o in
        standin) for f in WSWAP DOSX WIN386; do cp "$dos/WC1.COM" "$d/c/WINDOWS/SYSTEM/$f.EXE"; done ;;
        krnl) cp "$dos/WC1.COM" "$d/c/WINDOWS/SYSTEM/KRNL386.EXE" ;;
        wc4) cp "$dos/WC4.COM" "$d/c/WINDOWS/SYSTEM/WSWAP.EXE" ;;
        mem=*) mem=${o#mem=} ;;
        cpu=*) cpu=${o#cpu=} ;;
        nohimem) xms=false ;;
        dos=*) ver="ver=${o#dos=}" ;;
        rm=*) rm -f "$d/c/${o#rm=}" ;;
        ini=*) python3 "$root/tests/scripts/iniset.py" "$d/c/WINDOWS/SYSTEM.INI" "${o#ini=}" ;;
        tsr=*) cmds="C:\\WC7 ${o#tsr=};$cmds" ;;
        esac
    done
    {
        printf '[sdl]\noutput=surface\n[dosbox]\nquit warning=false\nmachine=svga_s3\nmemsize=%s\n' "$mem"
        printf '[cpu]\ncore=normal\ncputype=%s\ncycles=max\n' "$cpu"
        printf '[dos]\nxms=%s\n%s\n' "$xms" "$ver"
        printf '[mixer]\nnosound=true\n[speaker]\npcspeaker=false\n'
        printf '[autoexec]\nmount c "%s/c"\nc:\ncd \\WINDOWS\nCLS\n' "$d"
        printf '%s\n' "$cmds" | tr ';' '\n'
        printf 'exit\n'
    } > "$d/dosbox.conf"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "${WINCOM_TIMEOUT:-90}" \
        dosbox-x -nogui -fastlaunch -conf "$d/dosbox.conf" >/dev/null 2>&1 || true
    # the record, without what depends on memory layout
    {
        [ -f "$d/c/WC.LOG" ] && tr -d '\r' < "$d/c/WC.LOG" | sed \
            -e 's/^psp=.*parentprog=[^ ]* \(top=[0-9A-F]*\).*/\1/' \
            -e 's/^\(envseg\)=.*/\1/' \
            -e 's/^parenttail=80 .*/parenttail=(WIN386 reuses its PSP)/' \
            -e 's/ parentenv=[0-9A-F]*//' \
            -e 's/ 22=[0-9A-F:]*//' -e 's/ 2F=0[0-9A-F:]*//' \
            -e 's/^\(4A05\/5 ax bx cx dx si di es:\).*/\1/' \
            -e 's/^\(4B05 ax es:bx\).*/\1/' -e 's/^\(instance list at\).*/\1/' \
            -e 's/^\(  0000: 0300000000000000 000000000000\)[0-9A-F]* /\1(list) /' \
            -e 's/^|\(.*\)\\5CWC2 !.*/|\1\\5CWC2 .../' |
        # vectors into programs loaded after WIN.COM (WSWAP, DOSX) move
        # with WIN.COM's size; under WIN386 (a virtual machine) they point
        # into WIN386: only those into the BIOS and DOS are compared
        awk '/^vec /{v=$0; next}
             /^i2f1600=/{if ($1 == "i2f1600=0A03") v = "vec (in a WIN386 VM)"
                         else { n = split(v, a, " "); v = "vec"
                                for (i = 2; i <= n; i++) {
                                    if (a[i] !~ /=(F000|C000|0701|C401):/) sub(/=.*/, "=(loaded)", a[i])
                                    v = v " " a[i] } }
                         print v}
             {print}'
        for o in "$d"/c/O*.TXT; do
            [ -f "$o" ] || continue
            echo "== $(basename "$o")"
            od -An -c "$o"
        done
    } > "$d/result.txt"
}

failures=0
names=$(printf '%s\n' "$scenarios" | sed -n 's/^\([a-z0-9]*\)|.*/\1/p')
[ $# -gt 0 ] && names=$*
for n in $names; do
    line=$(printf '%s\n' "$scenarios" | grep "^$n|") || { echo "no scenario $n" >&2; exit 2; }
    opts=$(printf '%s' "$line" | cut -d'|' -f2)
    cmds=$(printf '%s' "$line" | cut -d'|' -f3-)
    run_one "$work/$n/ms" "$ref/WINDOWS/WIN.COM" "$opts" "$cmds" &
    run_one "$work/$n/w16" "$dos/WIN.COM" "$opts" "$cmds" &
    wait
    if [ ! -s "$work/$n/ms/result.txt" ]; then
        echo "FAIL $n (no record from Windows 3.1's WIN.COM)"
        failures=$((failures + 1))
    elif diff -u "$work/$n/ms/result.txt" "$work/$n/w16/result.txt" > "$work/$n/diff.txt"; then
        echo "PASS $n"
    else
        echo "FAIL $n"
        sed 's/^/    /' "$work/$n/diff.txt"
        failures=$((failures + 1))
    fi
    [ -n "$verbose" ] && { echo "---- $n (Windows 3.1)"; cat "$work/$n/ms/result.txt"; }
done
echo "wincom: $failures failure(s)"
exit $failures
