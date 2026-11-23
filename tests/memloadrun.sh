#!/bin/sh
# memloadrun.sh OUTDIR MODE [w16]: MEMLOAD.EXE (tests/dos/memload.c), XMS
# and EMS workloads as real programs use the memory, in DOS boxes in 386
# Enhanced mode (V86MMGR, ARCHITECTURE.md 12.13), under Microsoft's
# WIN386.EXE or w16's (third argument w16), Microsoft's KRNL386 and the
# rest of the reference install, plain DOSBox (its own EMS gives the
# frame).
#   MODE x, e, 3: one windowed box running MEMLOAD X, E or 3 (tag A);
#        c: three windowed Background boxes started together (WIN.INI
#        run=), MEMLOAD C A, C B, C C, each both workloads ROUNDS times
#        (default 3) with its own data, checking it stays intact while
#        the others run (the run ends when all three logs are there)
#   f: two boxes, B waiting (INT 2Fh 1680h between tries) for a file A
#        makes after a while: does a box see another's new file?
# Environment: MEMLOAD_PIF more tools/mkpif.py settings; MEMLOAD_ROUNDS;
# MEMLOAD_INI="S:K=V ..." SYSTEM.INI settings; MEMLOAD_DOSBOX=x for DOSBox-X; MEMLOAD_COM1=1 COM1 (w16's VMM trace) in
# OUTDIR/com1.txt (DOSBox-X); MEMLOAD_PLAINCONF more dosbox.conf lines
# (e.g. a nullmodem serial port for plain DOSBox); DROPIN_CYCLES (default fixed 30000).
# Prints the logs (OUTDIR/MLA.LOG ...). (A third argument other than w16
# and any after it are ignored: the boot test names files there for its
# reference cache's key.)
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 mode=$2 w=${3:-}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c" "$o"/ML?.LOG
ref=$root/.tools/ref/win31/cdrive
case $mode in
c) tags="A B C" wf=WINDOWS/MLA.LOG,WINDOWS/MLB.LOG,WINDOWS/MLC.LOG ;;
f) tags="A B" wf=WINDOWS/MLA.LOG,WINDOWS/MLB.LOG ;;   # B waits for a file A makes later
*) tags=A wf=WINDOWS/MLA.LOG ;;
esac
set -- -r -c "WIN /3 /B" -t ${MEMLOAD_TIME:-600} -o "$o/c" -W "$wf" \
    -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
    -f "$root/build/dos/MEMLOAD.EXE=WINDOWS/MEMLOAD.EXE"
params() {
    case $mode:$1 in
    f:A) echo "t A ${MEMLOAD_ROUNDS:-200}" ;;
    f:B) echo "w B A" ;;
    *) echo "$mode $1 ${MEMLOAD_ROUNDS:-3}" ;;
    esac
}
run=
for t in $tags; do
    # shellcheck disable=SC2086
    python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/ML$t.PIF" "title=MEMLOAD $t" \
        'program=C:\WINDOWS\MEMLOAD.EXE' "params=$(params $t)" 'dir=C:\WINDOWS' \
        windowed=1 background=1 ${MEMLOAD_PIF:-} || exit 2
    set -- "$@" -f "$o/ML$t.PIF=WINDOWS/ML$t.PIF"
    run="$run C:\\WINDOWS\\ML$t.PIF"
done
set -- "$@" -i "WIN.INI:windows:run=${run# }"
conf=${MEMLOAD_PLAINCONF:-}
if [ -n "${MEMLOAD_COM1:-}" ]; then
    # COM1 (w16's VMM trace) in OUTDIR/com1.txt: DOSBox-X
    MEMLOAD_DOSBOX=x
    conf="[serial]
serial1=file file:$o/com1.txt"
fi
for i in ${MEMLOAD_INI:-}; do set -- "$@" -i "$i"; done
[ "${MEMLOAD_DOSBOX:-}" = x ] && set -- "$@" -p 386
[ "$w" = w16 ] && set -- "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
DROPIN_CONF=$conf DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_CYCLES=${DROPIN_CYCLES:-fixed 30000} \
    DROPIN_WSECS=5 "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
for t in $tags; do
    l=$(find "$o/c" -iname "ML$t.LOG" | head -1)
    # (unfinished: the partial log, ML?.TMP)
    [ -n "$l" ] || l=$(find "$o/c" -iname "ML$t.TMP" | head -1)
    [ -n "$l" ] && tr -d '\r' < "$l" > "$o/ML$t.LOG"
    echo "== ML$t.LOG"
    cat "$o/ML$t.LOG" 2>/dev/null || echo "(none)"
done
exit 0
