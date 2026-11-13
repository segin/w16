#!/bin/sh
# MOUSE.DRV's PowerEventProc (ordinal 7), observed black-box: MD2B.EXE
# (tests/apps/md2b.c) runs as the shell with MS3B.COM (tests/dos/ms3b.asm)
# loaded before WIN, on Microsoft's KRNL286, DOSX, USER (tests/dropin.sh
# -r). tests/apps/md2logs/b/notes.md has the findings.
#   phase 1 (dropin.sh, no input): PowerEventProc called with many codes in
#           each of its three parameter words, with the driver enabled and
#           disabled; the PS/2 BIOS (and APM, INT 2Fh) calls each one made
#   phase 2 (tests/inputrun.sh, tests/scripts/input/md2b.txt): MD2's event
#           procedure enabled, PowerEventProc called with [w16] md2codes
#           every md2every events while the mouse moves; events and the
#           BIOS's packets listed
#   phase 3 (dropin.sh, no input): POWER.DRV (expanded from the install
#           disks' POWER.DR_) added to [boot] drivers= as Setup's APM
#           machine types do ([power.drv] UseRealMode=1), MS4B.COM (an APM
#           BIOS of ours, tests/dos/ms4b.asm, scheduled power events) loaded
#           before MS3; MD2 logs WM_POWER and the MS3 records. For the w16
#           run, MOUSEPT.DRV (w16's driver reporting PowerEventProc calls,
#           their parameters and callers) is MOUSE.DRV.
#   phase 4: phase 3 with w16's driver only, as MOUSEPF.DRV: like MOUSEPT,
#           but PowerEventProc returns -1 (PWR_FAIL) to event 1: does
#           POWER.DRV then suspend all the same?
# Each phase with Windows 3.1's MOUSE.DRV (ms) and w16's (w16,
# build/dos/MOUSE.DRV; phase 3: build/dos/MOUSEPT.DRV).
#   usage: tests/md2bprobe.sh [-o DIR] [-c CODES] [-e EVERY] [phase...]
#     -c CODES  phase 2's codes, comma-separated (default 2,1,3,4,2)
#     -e EVERY  phase 2: call after this many events (default 3)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= codes=2,1,3,4,2 every=3
while getopts o:c:e: opt; do
    case $opt in
    o) out=$OPTARG ;;
    c) codes=$OPTARG ;;
    e) every=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
phases=${*:-1 2}
[ -n "$out" ] || out=$root/build/md2bprobe-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
printf 'C:\\MS3B.COM\r\nWIN /S /B\r\n' > "$out/RUN.BAT"
printf 'C:\\MS4B.COM\r\nC:\\MS3B.COM\r\nWIN /S /B\r\n' > "$out/RUN3.BAT"
for ph in $phases; do
    drvs="ms w16"
    [ "$ph" = 4 ] && drvs=w16
    for drv in $drvs; do
        set -- -f "$root/build/dos/MS3B.COM=MS3B.COM" -f "$root/build/dos/MD2B.EXE=MD2B.EXE" \
            -f "$out/RUN.BAT=RUN.BAT" -c 'C:\RUN.BAT' -i 'boot:shell=C:\MD2B.EXE' \
            -i "w16:md2phase=$(( ph > 3 ? 3 : ph ))" -i "w16:md2codes=$codes" -i "w16:md2every=$every" \
            -i w16:md2secs=55
        mdrv=$root/build/dos/MOUSE.DRV
        if [ "$ph" -ge 3 ]; then
            mdrv=$root/build/dos/MOUSEPT.DRV
            [ "$ph" = 4 ] && mdrv=$root/build/dos/MOUSEPF.DRV
            python3 -I "$root/tests/szdd.py" "$root/.tools/ref/win31/src/POWER.DR_" "$out/POWER.DRV"
            set -- "$@" -f "$out/RUN3.BAT=RUN.BAT" -f "$root/build/dos/MS4B.COM=MS4B.COM" \
                -f "$out/POWER.DRV=WINDOWS/SYSTEM/POWER.DRV" \
                -i 'boot:drivers=mmsystem.dll power.drv' -i power.drv:UseRealMode=1
        fi
        [ $drv = w16 ] && set -- "$@" -f "$mdrv=WINDOWS/SYSTEM/MOUSE.DRV"
        name=p$ph$drv
        if [ "$ph" != 2 ]; then
            "$root/tests/dropin.sh" -r -t $([ "$ph" = 1 ] && echo 60 || echo 100) -o "$out/$name" "$@" > "$out/$name.txt" 2>&1 || true
            tr -d '\r' < "$out/$name/c/WINDOWS/TK.LOG" > "$out/$name.log" 2>/dev/null || : > "$out/$name.log"
        else
            "$root/tests/inputrun.sh" -b 30 -t 120 "$root/tests/scripts/input/md2b.txt" "$out/$name" \
                -r "$@" > "$out/$name.txt" 2>&1 || true
            tr -d '\r' < "$out/$name/work/c/WINDOWS/TK.LOG" > "$out/$name.log" 2>/dev/null || : > "$out/$name.log"
        fi
        echo "$name: $(grep -c '^P ' "$out/$name.log") calls, $(grep -c '^E ' "$out/$name.log") events, $(grep -q '^MD2 done' "$out/$name.log" && echo done || echo 'NOT done')"
    done
done
echo "md2bprobe: logs in $out"
