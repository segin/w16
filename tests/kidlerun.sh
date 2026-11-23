#!/bin/sh
# kidlerun.sh OUTDIR CONF [BOX]: KERNEL's idle loop, black box
# (ARCHITECTURE.md 12.15). VX17.EXE (tests/apps/vx17.c, WIN.INI run=)
# counts the idle calls a waiting Windows makes in protected mode (INT 28h,
# INT 2Fh 1689h) and through DPMI 0300h, a 55 ms timer's promptness, its
# busy loop and message round trips, with no DOS box and then with one;
# in 386 Enhanced mode VXT.386 (tests/vxd/vxt.asm) reports from the VMM's
# side on COM1 (OUTDIR/com1.txt): each VM's run time, V86 INT 28h and
# 1689h, Release_Time_Slice, the Call_When_Idle callbacks.
#   CONF  ms      the reference install, WIN /3: Microsoft's KRNL386, WIN386
#         msk     the same with w16's KRNL386 (dropin.sh -3)
#         w16     Microsoft's KRNL386 under w16's WIN386.EXE
#         wk      w16's KRNL386 and w16's WIN386
#         tree    w16's install tree alone (-T build/w16 -3), WIN /3
#         std     the reference install, WIN /S: Microsoft's KRNL386, DOSX
#         std286  Microsoft's KRNL286 under Microsoft's DOSX
#         stdk    w16's KRNL386 under Microsoft's DOSX
#         stdk286 w16's KRNL286 under Microsoft's DOSX
#         treestd the tree, WIN /S: w16's KRNL386 and DOSX
#         tree286 the tree, WIN /S without KRNL386: w16's KRNL286 and DOSX
#   BOX   what each DOS box runs (tests/dos/vx14.asm): b (busy, counting;
#         default), p (polling INT 16h), w (waiting in INT 16h), cmd
#         (COMMAND.COM)
# Environment: KIDLE_PIF="key=value ..." tools/mkpif.py settings (default
# windowed=1 background=1); KIDLE_COUNTS (default 0,1), KIDLE_SECS (8),
# KIDLE_ONLY (vx17.c's phases); DROPIN_CYCLES (default fixed 30000: the
# same machine speed in every run); KIDLE_EXTRA more dropin.sh arguments
# (e.g. -i w16:...); KIDLE_NOVXT=1 without VXT.386; KIDLE_MS5=1 MS5.COM
# (tests/dos/ms5.asm) before WIN: VX17's rm28/rm89, what reaches real mode.
# Prints OUTDIR/VX17.LOG, then tools/kidlesum.py's sums of VXT's lines
# per phase.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 conf=$2 box=${3:-b}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c"
ref=$root/.tools/ref/win31/cdrive
k386="$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE"
w386="$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
win=WINDOWS mode="WIN /3 /B" enh=1
case $conf in
ms)     set -- -r -f "$k386" ;;
msk)    set -- -3 ;;
w16)    set -- -r -f "$k386" -f "$w386" ;;
wk)     set -- -3 -f "$w386" ;;
tree)   win=W16; set -- -T "$root/build/w16" -3 ;;
std)    set -- -r -f "$k386"; mode="WIN /S /B" enh= ;;
std286) set -- -r; mode="WIN /S /B" enh= ;;
stdk)   set -- -3; mode="WIN /S /B" enh= ;;
stdk286) set --; mode="WIN /S /B" enh= ;;
treestd) win=W16; set -- -T "$root/build/w16" -3; mode="WIN /S /B" enh= ;;
tree286) win=W16; set -- -T "$root/build/w16"; mode="WIN /S /B" enh= ;;
*) echo "kidlerun.sh OUTDIR ms|msk|w16|wk|tree|std|std286|stdk|stdk286|treestd|tree286 [b|p|w|cmd]" >&2; exit 2 ;;
esac
case $box in
cmd) prog=COMMAND.COM params= ;;
*) prog="C:\\$win\\VX14.COM" params=$box ;;
esac
# shellcheck disable=SC2086
python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/VX17.PIF" "title=VX14 $box" \
    "program=$prog" "params=$params" "dir=C:\\$win" ${KIDLE_PIF:-windowed=1 background=1} || exit 2
counts=${KIDLE_COUNTS:-0,1}
[ -z "$enh" ] && counts=0       # (no DOS boxes beside Standard mode's Windows)
if [ -n "${KIDLE_MS5:-}" ]; then
    mode="C:\\MS5.COM
$mode"
    set -- "$@" -f "$root/build/dos/MS5.COM=MS5.COM"
fi
set -- "$@" -c "$mode" -t ${KIDLE_TIME:-600} -o "$o/c" -W "$win/VX17.LOG" \
    -f "$root/build/dos/VX17.EXE=$win/VX17.EXE" -f "$root/build/dos/VX14.COM=$win/VX14.COM" \
    -f "$o/VX17.PIF=$win/VX17.PIF" \
    -i "WIN.INI:windows:run=C:\\$win\\VX17.EXE" \
    -i "WIN.INI:vx17:prog=C:\\$win\\VX17.PIF" \
    -i "WIN.INI:vx17:counts=$counts" -i "WIN.INI:vx17:secs=${KIDLE_SECS:-8}" \
    -i "WIN.INI:vx17:only=${KIDLE_ONLY:-}" -p 386 ${KIDLE_EXTRA:-}
conf=
if [ -n "$enh" ] && [ -z "${KIDLE_NOVXT:-}" ]; then
    set -- "$@" -f "$root/build/dos/VXT.386=$win/SYSTEM/VXT.386" -i "386Enh:+device=vxt.386"
fi
conf="[serial]
serial1=file file:$o/com1.txt"
rm -f "$o/com1.txt"
DROPIN_CONF=$conf DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_CYCLES=${DROPIN_CYCLES:-fixed 30000} \
    DROPIN_WSECS=3 "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
for f in VX17.LOG VX14.LOG TK.LOG; do
    l=$(find "$o/c" -iname "$f" | head -1)
    [ -n "$l" ] && tr -d '\r' < "$l" > "$o/$f"
done
cat "$o/VX17.LOG" 2>/dev/null || { echo "no VX17.LOG"; tail -5 "$o/dropin.txt"; cat "$o/TK.LOG" 2>/dev/null; }
[ -f "$o/com1.txt" ] && python3 "$root/tools/kidlesum.py" "$o/com1.txt"
exit 0
