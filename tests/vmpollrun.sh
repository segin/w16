#!/bin/sh
# vmpollrun.sh OUTDIR WHAT [w16]: Windows' share of the processor beside
# idle (or busy) DOS boxes in 386 Enhanced mode (VMPOLL, ARCHITECTURE.md
# 12.12). VX13.EXE (tests/apps/vx13.c, WIN.INI run=) starts DOS boxes one
# PIF at a time (minimised: Windows keeps the focus) and measures a busy
# loop and message round trips by the real-time clock with 0, 1, 2 and 4
# boxes; with Microsoft's WIN386.EXE, or w16's (third argument w16), and
# Microsoft's KRNL386 and the rest of the reference install.
#   WHAT  what each box runs: cmd (COMMAND.COM at its prompt), or a
#         VX14.COM mode (tests/dos/vx14.asm): p (INT 16h AH=01h polling),
#         w (INT 16h AH=00h), r (INT 2Fh 1680h between polls), d (INT 21h
#         AH=0Bh), b (busy, counting), s (busy, writing to the screen)
# Environment: VMPOLL_PIF="key=value ..." more tools/mkpif.py settings
# (default windowed=1 background=1: Windows 3.1 runs a box that is not
# Background only while it has the focus); VMPOLL_COUNTS (default
# 0,1,2,4); VMPOLL_SECS (8), VMPOLL_SETTLE (15); VMPOLL_DOSBOX=x for
# DOSBox-X (default plain DOSBox); DROPIN_CYCLES (default fixed 30000, so
# that the emulated machine's speed is the same in every run);
# VMPOLL_EXTRA more dropin.sh arguments.
# (A third argument other than w16, e.g. ms, and any after it are ignored:
# the boot test names the probes there for its reference cache's key.)
# VMPOLL_VXT=1: the scheduling probe VxD VXT.386 (tests/vxd/vxt.asm) too,
# its lines in OUTDIR/com1.txt; VMPOLL_COM1=1: COM1 alone there (w16's
# VMM trace); both run DOSBox-X. VMPOLL_SHOTS=N: a run of N screenshots
# 20 s apart instead (OUTDIR/shots).
# Results: OUTDIR/VX13.LOG (lines "VX13 boxes=N loop=L msgs=M") and, for
# b and s, OUTDIR/VX14.LOG; both printed.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 what=$2 w=${3:-}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c"
ref=$root/.tools/ref/win31/cdrive
case $what in
cmd) prog=COMMAND.COM params= ;;
*) prog='C:\WINDOWS\VX14.COM' params=$what ;;
esac
# shellcheck disable=SC2086
python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/VX13.PIF" "title=VX14 $what" \
    "program=$prog" "params=$params" 'dir=C:\WINDOWS' ${VMPOLL_PIF:-windowed=1 background=1} || exit 2
set -- -r -c "WIN /3 /B" -t ${VMPOLL_TIME:-600} -o "$o/c" -W WINDOWS/VX13.LOG \
    -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
    -f "$root/build/dos/VX13.EXE=WINDOWS/VX13.EXE" -f "$root/build/dos/VX14.COM=WINDOWS/VX14.COM" \
    -f "$o/VX13.PIF=WINDOWS/VX13.PIF" \
    -i 'WIN.INI:windows:run=C:\WINDOWS\VX13.EXE' \
    -i "WIN.INI:vx13:counts=${VMPOLL_COUNTS:-0,1,2,4}" -i "WIN.INI:vx13:secs=${VMPOLL_SECS:-8}" \
    -i "WIN.INI:vx13:settle=${VMPOLL_SETTLE:-15}" ${VMPOLL_EXTRA:-}
conf=
if [ -n "${VMPOLL_VXT:-}" ]; then
    # the scheduling probe VxD (tests/vxd/vxt.asm): its COM1 lines in
    # OUTDIR/com1.txt (DOSBox-X)
    VMPOLL_DOSBOX=x
    set -- "$@" -f "$root/build/dos/VXT.386=WINDOWS/SYSTEM/VXT.386" -i "386Enh:+device=vxt.386"
    conf="[serial]
serial1=file file:$o/com1.txt"
    rm -f "$o/com1.txt"
elif [ -n "${VMPOLL_COM1:-}" ]; then
    # COM1 (under w16's WIN386 the VMM's trace) in OUTDIR/com1.txt (DOSBox-X)
    VMPOLL_DOSBOX=x
    conf="[serial]
serial1=file file:$o/com1.txt"
    rm -f "$o/com1.txt"
fi
[ "${VMPOLL_DOSBOX:-}" = x ] && set -- "$@" -p 386
[ "$w" = w16 ] && set -- "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
if [ -n "${VMPOLL_SHOTS:-}" ]; then
    # VMPOLL_SHOTS=N: instead, a run of N shots 20 seconds apart (tests/
    # inputrun.sh, OUTDIR/shots), to see what the boxes show
    i=0
    while [ $i -lt "$VMPOLL_SHOTS" ]; do i=$((i + 1)); printf 'sleep 20\nshot %02d\n' $i; done > "$o/shots.txt"
    shift 9     # -r -c CMD -t SECS -o DIR -W FILE: inputrun's own or not for it
    DROPIN_CONF=$conf DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_CYCLES=${DROPIN_CYCLES:-fixed 30000} \
        "$root/tests/inputrun.sh" -s -b 300 -t 900 "$o/shots.txt" "$o/shots" -r -c "WIN /3 /B" "$@" > "$o/dropin.txt" 2>&1
    rm -rf "$o/c"
    cp -r "$o/shots/work" "$o/c" 2>/dev/null
else
DROPIN_CONF=$conf DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_CYCLES=${DROPIN_CYCLES:-fixed 30000} \
    DROPIN_WSECS=3 "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
fi
for f in VX13.LOG VX14.LOG TK.LOG; do
    l=$(find "$o/c" -iname "$f" | head -1)
    [ -n "$l" ] && tr -d '\r' < "$l" > "$o/$f"
done
cat "$o/VX13.LOG" 2>/dev/null || { echo "no VX13.LOG"; tail -5 "$o/dropin.txt"; cat "$o/TK.LOG" 2>/dev/null; }
[ -f "$o/VX14.LOG" ] && cat "$o/VX14.LOG"
exit 0
