#!/bin/sh
# Run a SHELL.DLL probe (build/dos/SHn.EXE, tests/apps/shN.c) as the shell
# of a scratch copy of the reference Windows 3.1, in Standard and 386
# Enhanced mode, with Microsoft's SHELL.DLL (default) or w16's (-w).
#   usage: tests/shellprobe.sh [-w] [-m MODES] [-o DIR] [-f SRC=DST]... SHn
#     -w         drop in build/dos/SHELL.DLL as WINDOWS\SYSTEM\SHELL.DLL
#     -m MODES   the WIN switches to run (default "/S /3")
#     -o DIR     results (default build/shprobe/SHn-ref or SHn-w16, replaced)
#     -f SRC=DST copy SRC to C:\DST as well (repeatable)
#     -p CMD     a DOS command run before WIN (repeatable), e.g.
#                'DEL C:\WINDOWS\REG.DAT'
# Each mode's TK.LOG goes to DIR/<mode>.log (s.log, 3.log), and whatever the
# probe left at C:\ as R*.DAT (REG.DAT snapshots), *.BMP and SH*.TXT to
# DIR/<mode>/. Exit status: the number of FAIL lines.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
w16= modes="/S /3" out= files= pre=
while getopts wm:o:f:p: opt; do
    case $opt in
    w) w16=1 ;;
    m) modes=$OPTARG ;;
    o) out=$OPTARG ;;
    f) files="$files $OPTARG" ;;
    p) pre="$pre
$OPTARG" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
probe=${1:?probe name, e.g. SH1}
[ -n "$out" ] || out=$root/build/shprobe/$probe-$( [ -n "$w16" ] && echo w16 || echo ref)
rm -rf "$out"; mkdir -p "$out"
[ -n "$w16" ] && files="$files $root/build/dos/SHELL.DLL=WINDOWS/SYSTEM/SHELL.DLL"
get=
for n in 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20; do
    get="$get R$n.DAT"
done
get="$get WINDOWS/REG.DAT WINDOWS/WIN.INI SH.TXT"
fails=0
for m in $modes; do
    tag=$(echo "$m" | tr -d '/' | tr A-Z a-z)
    mkdir -p "$out/$tag"
    REFRUN_PRE="$pre" REFRUN_FILES="$files" REFRUN_OUT="$out/$tag" REFRUN_GET="$get" \
        "$root/tests/refrun.sh" "$root/build/dos/$probe.EXE" "$m" > "$out/$tag.log" 2>&1 || true
    n=$(grep -c '^FAIL' "$out/$tag.log" || true)
    echo "$probe $m$( [ -n "$w16" ] && echo ' (w16 SHELL)'): $n FAIL, log $out/$tag.log"
    fails=$((fails + n))
done
exit $fails
