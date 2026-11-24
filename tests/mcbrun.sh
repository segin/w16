#!/bin/sh
# mcbrun.sh OUTDIR MODE [EMU]: MCBCHK.EXE (tests/dos/mcbchk.c), the DOS memory
# arena as programs that measure memory use it (UMBs linked, strategies,
# the chain walked), on DOSBox-X's or DOSBox's own DOS (which have UMBs:
# the MCB at 9FFFh that links them ends conventional memory).
#   MODE plain  no Windows: MCBCHK straight from DOS
#        ms     in a DOS box (a windowed PIF from WIN.INI run=), Windows
#               3.1 with Microsoft's WIN386.EXE and KRNL386
#        w16    the same with w16's WIN386.EXE
#        tree   w16's install tree alone (build/w16, w16's KRNL386,
#               WIN386 and WINOA386), WIN /3
#   EMU x (default) DOSBox-X, dosbox plain DOSBox (its DOS has UMBs too).
#   (Arguments after EMU are ignored: the boot test names files there for
#   its reference cache's key.)
# Environment: MCBRUN_PROG=DIR:PROG runs C:\PROG\PROG (DIR copied to
# C:\PROG) in the box instead of MCBCHK, for trying a program of one's own
# (the run then ends at the time limit, MCBRUN_TIME, default 120 s);
# MCBRUN_COM1=1 COM1 (w16's VMM trace) in OUTDIR/com1.txt;
# MCBRUN_SHOT=1 a screenshot at the time limit (OUTDIR/shot.png);
# DROPIN_CYCLES (default max), DROPIN_CPUTYPE (default 386).
# OUTDIR/emu.log is DOSBox-X's own log (its "Corrupt MCB chain" ends the
# emulator). Prints MCA.LOG (or the unfinished MCA.TMP).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 mode=$2 emu=${3:-x}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c" "$o/MCA.LOG" "$o/emu.log"
ref=$root/.tools/ref/win31/cdrive
conf="[log]
logfile=$o/emu.log"
[ -n "${MCBRUN_COM1:-}" ] && conf="$conf
[serial]
serial1=file file:$o/com1.txt"
prog= pdir= wait=
if [ -n "${MCBRUN_PROG:-}" ]; then
    pdir=${MCBRUN_PROG%:*} prog=${MCBRUN_PROG##*:}
fi
case $mode in
plain|ms|w16) wd=WINDOWS ;;
tree) wd=W16 ;;
*) echo "mode: plain, ms, w16 or tree" >&2; exit 2 ;;
esac
set -- -o "$o/c" -t "${MCBRUN_TIME:-120}" -f "$root/build/dos/MCBCHK.EXE=$wd/MCBCHK.EXE"
if [ -n "$pdir" ]; then
    for f in "$pdir"/*; do
        [ -f "$f" ] && set -- "$@" -f "$f=PROG/$(basename "$f")"
    done
    run="C:\\PROG\\$prog"
else
    wait=$wd/MCA.LOG
    run="C:\\$wd\\MCBCHK.EXE"
fi
[ -n "$wait" ] && set -- "$@" -W "$wait"
[ "$emu" = dosbox ] || set -- "$@" -p 386
[ -n "${MCBRUN_SHOT:-}" ] && set -- "$@" -s "$o/shot.png"
if [ "$mode" = plain ]; then
    set -- "$@" -r -c "$run A"
else
    if [ -n "$pdir" ]; then
        python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/MCA.PIF" "title=PROG" \
            "program=$run" 'dir=C:\PROG' windowed=1 || exit 2
    else
        python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/MCA.PIF" "title=MCBCHK A" \
            "program=$run" params=A "dir=C:\\$wd" windowed=1 || exit 2
    fi
    set -- "$@" -f "$o/MCA.PIF=$wd/MCA.PIF" -i "WIN.INI:windows:run=C:\\$wd\\MCA.PIF" -c "WIN /3"
    case $mode in
    ms) set -- "$@" -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" ;;
    w16) set -- "$@" -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
            -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" ;;
    tree) set -- "$@" -T "$root/build/w16" -3 ;;
    esac
fi
DROPIN_FIXED=${MCBRUN_SHOT:+1} DROPIN_CONF=$conf DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_WSECS=3 \
    "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
l=$(find "$o/c" -iname MCA.LOG | head -1)
[ -n "$l" ] || l=$(find "$o/c" -iname MCA.TMP | head -1)
[ -n "$l" ] && tr -d '\r' < "$l" > "$o/MCA.LOG"
echo "== MCA.LOG"
cat "$o/MCA.LOG" 2>/dev/null || echo "(none)"
echo "== DOSBox-X: MCB messages"
grep -i 'mcb\|memory error' "$o/emu.log" 2>/dev/null | head -20 || true
exit 0
