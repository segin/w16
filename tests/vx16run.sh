#!/bin/sh
# vx16run.sh OUTDIR CONF: where the time goes in 386 Enhanced mode
# (ARCHITECTURE.md 12.14). VX16.EXE (tests/apps/vx16.c, WIN.INI run=)
# counts a busy loop, the clocks, timer events, traps, DOS calls, file
# reads and drawing by the real-time clock, in one of these setups:
#   ms       the reference install, WIN /3: Microsoft's WIN386 and KRNL386
#   w16      the same with w16's WIN386.EXE
#   std      the reference install, WIN /S (Standard mode)
#   tree     w16's install tree alone (tests/dropin.sh -T, KRNL386), WIN /3
#   treems   the same with Microsoft's WIN386.EXE in place of the tree's
#   treestd  the tree, WIN /S
# Environment: VX16_SECS (seconds a phase, default 4), VX16_ONLY (the
# phases, comma-separated: vx16.c), DROPIN_CYCLES (default max, as
# tests/qtplay.sh; "fixed 30000" for the emulated machine's speed alone),
# VX16_DOSBOX=x for DOSBox-X (default plain DOSBox, as qtplay.sh -T 386
# -E), VX16_EXTRA more dropin.sh arguments, VX16_COM1=1 COM1 to
# OUTDIR/com1.txt (DOSBox-X: w16's VMM trace with -i w16:vmmtrace=1).
# Prints OUTDIR/VX16.LOG (lines "VX16 NAME=COUNT").
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 conf=$2
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c"
ref=$root/.tools/ref/win31/cdrive
win=WINDOWS mode="WIN /3 /B"
case $conf in
ms|w16|std)
    set -- -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE"
    [ "$conf" = w16 ] && set -- "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
    [ "$conf" = std ] && mode="WIN /S /B" ;;
tree|treems|treestd)
    win=W16
    set -- -T "$root/build/w16" -3
    [ "$conf" = treems ] && set -- "$@" -f "$ref/WINDOWS/SYSTEM/WIN386.EXE=W16/SYSTEM/WIN386.EXE"
    [ "$conf" = treestd ] && mode="WIN /S /B" ;;
*) echo "vx16run.sh OUTDIR ms|w16|std|tree|treems|treestd" >&2; exit 2 ;;
esac
set -- "$@" -c "$mode" -t ${VX16_TIME:-300} -o "$o/c" -W VX16.LOG \
    -f "$root/build/dos/VX16.EXE=$win/VX16.EXE" \
    -i "WIN.INI:windows:run=C:\\$win\\VX16.EXE" \
    -i "WIN.INI:vx16:secs=${VX16_SECS:-4}" -i "WIN.INI:vx16:only=${VX16_ONLY:-}" ${VX16_EXTRA:-}
dconf=
if [ -n "${VX16_COM1:-}" ]; then
    VX16_DOSBOX=x
    dconf="[serial]
serial1=file file:$o/com1.txt"
    rm -f "$o/com1.txt"
fi
[ "${VX16_DOSBOX:-}" = x ] && set -- "$@" -p 386
DROPIN_CONF=$dconf DROPIN_NOREDIR=1 DROPIN_WSECS=3 "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
l=$(find "$o/c" -iname VX16.LOG | head -1)
[ -n "$l" ] && tr -d '\r' < "$l" > "$o/VX16.LOG"
cat "$o/VX16.LOG" 2>/dev/null || {
    echo "no VX16.LOG"; tail -5 "$o/dropin.txt"
    t=$(find "$o/c" -iname TK.LOG | head -1); [ -n "$t" ] && cat "$t"; }
exit 0
