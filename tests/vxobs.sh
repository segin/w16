#!/bin/sh
# Observe WIN386.EXE black-box (ARCHITECTURE.md 12): scenarios run in
# headless DOSBox-X in scratch copies of the reference install, with
# Microsoft's WIN386.EXE (default) or w16's (-w). Prints, per scenario,
# what the probes recorded:
#   WC.LOG  - WC2 (screen text, exit code, video mode after a command) and
#             WC3 (an INT 2Fh spy between WIN.COM and WIN386: every INT 2Fh
#             call WIN386 and the System VM make in real/V86 mode, with
#             registers in and out) - the WIN.COM probes, reused;
#   VX1.LOG - VX1 (tests/dos/vx1.asm) as KRNL386.EXE: what the program
#             started in the System VM gets, V86 and protected mode.
#   usage: tests/vxobs.sh [-w] [-k] [-o DIR] [SCENARIO...]
#     -w  use build/win386/WIN386.EXE        -k  keep the scratch directories
#     -o  also copy each scenario's logs to DIR/<scenario>/
# Needs dosbox-x, `make dos wcprobes vxprobes` (WC2, WC3, VX1, VX2).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=${VXOBS_REF:-$root/.tools/ref/win31/cdrive}
[ -d "$ref" ] || ref=$(cd "$root/../../.." && pwd)/.tools/ref/win31/cdrive
dos=$root/build/dos
keep= w16= outdir=
while getopts wko: opt; do
    case $opt in
    w) w16=1 ;;
    k) keep=1 ;;
    o) outdir=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
work=$(mktemp -d)
[ -n "$keep" ] && echo "scratch: $work" || trap 'rm -rf "$work"' EXIT

# NAME|options|DOS commands (";" separates lines). Options: spy (WC3 wraps
# WIN386), vx1 (VX1 as KRNL386), mem=N, cpu=T, nohimem, rm=PATH,
# ini=SEC:KEY=VAL, ems=V (DOSBox-X [dos] ems=V), direct (no WIN.COM: run
# SYSTEM\WIN386.EXE from C:\WINDOWS).
scenarios='
boot|spy vx1|WIN /3;C:\WC2 !boot
direct|vx1|SYSTEM\WIN386;C:\WC2 !direct
nohimem|vx1 nohimem|SYSTEM\WIN386 > C:\O1.TXT;C:\WC2 !nohimem
cpu286|vx1 cpu=286|SYSTEM\WIN386 > C:\O1.TXT;C:\WC2 !cpu286
mem2|vx1 mem=2|WIN /3 > C:\O1.TXT;C:\WC2 !mem2
nokrnl|rm=WINDOWS/SYSTEM/KRNL386.EXE|WIN /3 > C:\O1.TXT;C:\WC2 !nokrnl
nodevice|vx1 ini=386Enh:device=nosuch.386|C:\VX2 eeee;WIN /3 > C:\O1.TXT;C:\WC2 !nodevice
nobuiltin|vx1 ini=386Enh:device=*nosuch|C:\VX2 eeee;WIN /3 > C:\O1.TXT;C:\WC2 !nobuiltin
emm386|vx1 ems=emm386|C:\VX2 eeee;WIN /3 > C:\O1.TXT;C:\WC2 !emm386
emsboard|vx1 ems=true|C:\VX2 eeee;WIN /3 > C:\O1.TXT;C:\WC2 !emsboard
noini|vx1 rm=WINDOWS/SYSTEM.INI|WIN /3 > C:\O1.TXT;C:\WC2 !noini
help|vx1|SYSTEM\WIN386 /? > C:\O1.TXT;C:\WC2 !help
'

run_one() {   # run_one DIR OPTIONS COMMANDS
    d=$1 opts=$2 cmds=$3
    mkdir -p "$d"
    cp -r "$ref" "$d/c"
    [ -n "$w16" ] && cp "$root/build/win386/WIN386.EXE" "$d/c/WINDOWS/SYSTEM/WIN386.EXE"
    cp "$dos/WC2.COM" "$dos/VX2.COM" "$d/c/"
    mem=16 cpu=386 xms=true ems=false
    for o in $opts; do
        case $o in
        spy) mv "$d/c/WINDOWS/SYSTEM/WIN386.EXE" "$d/c/WINDOWS/SYSTEM/WIN386.EX_"
             cp "$dos/WC3.COM" "$d/c/WINDOWS/SYSTEM/WIN386.EXE" ;;
        vx1) cp "$dos/VX1.COM" "$d/c/WINDOWS/SYSTEM/KRNL386.EXE" ;;
        mem=*) mem=${o#mem=} ;;
        cpu=*) cpu=${o#cpu=} ;;
        nohimem) xms=false ;;
        ems=*) ems=${o#ems=} ;;
        rm=*) rm -f "$d/c/${o#rm=}" ;;
        ini=*) python3 -I "$root/tests/scripts/iniset.py" "$d/c/WINDOWS/SYSTEM.INI" "${o#ini=}" ;;
        esac
    done
    {
        printf '[sdl]\noutput=surface\n[dosbox]\nquit warning=false\nmachine=svga_s3\nmemsize=%s\n' "$mem"
        printf '[cpu]\ncore=normal\ncputype=%s\ncycles=max\n' "$cpu"
        printf '[dos]\nxms=%s\nems=%s\n' "$xms" "$ems"
        printf '[mixer]\nnosound=true\n[speaker]\npcspeaker=false\n'
        printf '[log]\nlogfile=%s/trace.log\n' "$d"
        printf '[serial]\nserial1=file file:%s/com1.txt\n' "$d"     # w16's VMM traces here
        for c in ${VXOBS_LOG:-}; do printf '%s=true\n' "$c"; done   # e.g. "files exec"
        printf '[autoexec]\nmount c "%s/c"\nc:\ncd \\WINDOWS\nCLS\n' "$d"
        printf '%s\n' "$cmds" | tr ';' '\n'
        printf 'exit\n'
    } > "$d/dosbox.conf"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "${VXOBS_TIMEOUT:-90}" \
        dosbox-x -nogui -fastlaunch ${VXOBS_ARGS:-} -conf "$d/dosbox.conf" > "$d/dosbox.log" 2>&1 || true
}

names=$(printf '%s\n' "$scenarios" | sed -n 's/^\([a-z0-9]*\)|.*/\1/p')
[ $# -gt 0 ] && names=$*
for n in $names; do
    line=$(printf '%s\n' "$scenarios" | grep "^$n|") || { echo "no scenario $n" >&2; exit 2; }
    opts=$(printf '%s' "$line" | cut -d'|' -f2)
    cmds=$(printf '%s' "$line" | cut -d'|' -f3-)
    run_one "$work/$n" "$opts" "$cmds" &
done
wait
for n in $names; do
    echo "======== $n"
    for f in WC.LOG VX1.LOG; do
        [ -f "$work/$n/c/$f" ] && { echo "---- $f"; tr -d '\r' < "$work/$n/c/$f"; }
    done
    for o in "$work/$n"/c/O*.TXT; do
        [ -f "$o" ] || continue
        echo "---- $(basename "$o")"
        od -An -c "$o"
    done
    if [ -n "$outdir" ]; then
        mkdir -p "$outdir/$n"
        for f in "$work/$n"/c/WC.LOG "$work/$n"/c/VX1.LOG "$work/$n"/c/O*.TXT "$work/$n"/dosbox.log "$work/$n"/trace.log "$work/$n"/com1.txt; do
            [ -f "$f" ] && cp "$f" "$outdir/$n/"
        done
    fi
done
