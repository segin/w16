#!/bin/sh
# Run one scenario of 386 Enhanced mode with a windowed DOS prompt, for the
# 386 display grabber work (VGA.3GR: drivers/grabber/vga3gr/,
# docs/notes/grabber-handover.md "VGA.3GR", ARCHITECTURE.md 12.9).
#
# Microsoft's Windows 3.1 (a scratch copy of the reference install, via
# tests/worun.sh) runs "WIN /3" headless; the scenario script starts
# C:\WINPR.PIF, a copy of DOSPRMPT.PIF made at test time with the
# full-screen bit (byte 1C9h, 08h) cleared, so the DOS prompt is windowed.
# CB1.EXE (the clipboard recorder) runs from WIN.INI load=; GV1.COM (fixed
# screens per video mode) and GT3.COM are on C:.
#
#   usage: tests/grabber3.sh [options] -x SCRIPT -o DIR
#     -g FILE   the 386 grabber under test (default Microsoft's VGA.3GR;
#               e.g. build/dos/VGA.3GR, w16's)
#     -r        record: GR3 (tests/apps/gr3.asm) is installed as VGA.3GR in
#               front of the grabber under test, renamed GRABTGT (module
#               name only, tools/nerename.py, in a scratch copy); C:\GR3.LOG
#               is decoded into DIR/gr3.txt
#     -u        with -r: the grabber's imports of GDI, USER and KERNEL are
#               also recorded (USTRACE forwarders GDX, USEX, KERNEX:
#               tools/mkinterpose.py; the copy's import names are changed
#               by tools/nerename.py); decoded into DIR/trace.txt
#     -f, -i, -n, -t  as for tests/worun.sh
# Combining Microsoft's grabber with w16's recorder happens only in the
# scratch directory at test time; nothing of it is kept or inspected
# beyond the values passed across the interfaces (ARCHITECTURE.md 3).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive/WINDOWS
grab=$ref/SYSTEM/VGA.3GR rec= us= out= script= extra= tmax=400
while getopts g:ruo:x:f:i:n:t: opt; do
    case $opt in
    g) grab=$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG") ;;
    r) rec=1 ;;
    u) us=1 rec=1 ;;
    o) out=$OPTARG ;;
    x) script=$OPTARG ;;
    f) extra="$extra -f $OPTARG" ;;
    i) extra="$extra -i $OPTARG" ;;
    n) extra="$extra -n $OPTARG" ;;
    t) tmax=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -n "$out" ] && [ -n "$script" ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
[ -e "$out" ] && { echo "$out exists" >&2; exit 2; }
gen=$out.gen
rm -rf "$gen"
mkdir -p "$gen"
make -C "$root" -s build/dos/GR3.3GR build/dos/GV1.COM build/dos/CB1.EXE build/dos/GT3.COM >/dev/null
[ -z "$us" ] || make -C "$root" -s build/dos/USTRACE.DLL >/dev/null
python3 -c "
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
d[0x1c9] &= ~8
open(sys.argv[2], 'wb').write(d)" "$ref/DOSPRMPT.PIF" "$gen/WINPR.PIF"
files="-f $gen/WINPR.PIF=WINPR.PIF -f $root/build/dos/GV1.COM=GV1.COM"
files="$files -f $root/build/dos/GT3.COM=GT3.COM -f $root/build/dos/CB1.EXE=CB1.EXE"
if [ -n "$rec" ]; then
    cp "$grab" "$gen/GRABTGT.DLL"
    chmod u+w "$gen/GRABTGT.DLL"
    if [ -n "$us" ]; then
        specs=$root/.tools/wine/specs
        g() {   # NAME TARGET REALFILE MODID SPEC
            python3 "$root/tools/mkinterpose.py" --name "$1" --target "$2" --real "$3" \
                --modid "$4" --spec "$specs/$5.spec" --ret all "$gen" >/dev/null
            nasm -f obj -o "$gen/$1.o" "$gen/$1.asm"
            env WATCOM=/opt/watcom /opt/watcom/binl64/wlink @"$gen/$1.lnk" \
                name "$gen/$1.DLL" file "$gen/$1.o" >/dev/null
            files="$files -f $gen/$1.DLL=WINDOWS/SYSTEM/$1.DLL"
        }
        g USEX USER "$ref/SYSTEM/USER.EXE" 0 user
        g GDX GDI "$ref/SYSTEM/GDI.EXE" 1 gdi
        g KERNEX KERNEL "$ref/SYSTEM/KRNL386.EXE" 8 kernel
        python3 "$root/tools/nerename.py" "$gen/GRABTGT.DLL" --module GRABTGT \
            --import GDI=GDX --import USER=USEX --import KERNEL=KERNEX
        files="$files -f $root/build/dos/USTRACE.DLL=WINDOWS/SYSTEM/USTRACE.DLL"
        extra="$extra -i ustrace:cap=0"
    else
        python3 "$root/tools/nerename.py" "$gen/GRABTGT.DLL" --module GRABTGT
    fi
    files="$files -f $gen/GRABTGT.DLL=WINDOWS/SYSTEM/GRABTGT.DLL"
    files="$files -f $root/build/dos/GR3.3GR=WINDOWS/SYSTEM/VGA.3GR"
else
    files="$files -f $grab=WINDOWS/SYSTEM/VGA.3GR"
fi
# shellcheck disable=SC2086
"$root/tests/worun.sh" -o "$out" -w 'WIN /3' -x "$script" -t "$tmax" \
    -n windows:load=C:\\CB1.EXE $files $extra >/dev/null
if [ -f "$out/c/GR3.LOG" ]; then
    cp "$out/c/GR3.LOG" "$out/"
    python3 "$root/tools/gr3dump.py" "$out/GR3.LOG" > "$out/gr3.txt" || true
fi
if [ -n "$us" ]; then
    cp "$out/c/USTRACE.BIN" "$out/c/USCOUNT.BIN" "$out/c/USSEGS.TXT" "$out/" 2>/dev/null || true
    python3 "$root/tools/ustrace.py" "$out" > "$out/trace.txt" || true
fi
[ ! -f "$out/c/CB.LOG" ] || tr -d '\r' < "$out/c/CB.LOG" > "$out/cb.log"
rm -rf "$gen"
ls "$out"
