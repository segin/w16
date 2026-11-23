#!/bin/sh
# vmtaskrun.sh OUTDIR CONF: VMTASK.386's API under Microsoft's WIN386 and
# w16's (ARCHITECTURE.md 15.12). VMT1.EXE (tests/apps/vmt1.c, WIN.INI
# run=) finds the device, then with no DOS box and after starting each
# box takes two snapshots VMTASK_SECS apart and checks them: the VMs' run
# times add up to the elapsed time, Windows idles with no box, a busy box
# (B: VX14 b, tests/dos/vx14.asm) gets at least half the time, a polling
# one (P: VX14 p, which VMPOLL finds idle) at most a tenth, the windowed
# boxes' SHELL_GetVMInfo flags have bit 2 and the full-screen one's (PF)
# not; a NOTE line gives the System VM's Get_VM_Exec_Time beside VMTASK's count.
#   CONF  ms    the reference install, WIN /3: Microsoft's KRNL386 and
#               WIN386, with w16's VMTASK.386 added
#         w16   Microsoft's KRNL386 under w16's WIN386.EXE, VMTASK.386 added
#         tree  w16's install tree alone (-T build/w16 -3), whose SYSTEM.INI
#               loads VMTASK.386
# Environment: VMTASK_PROGS (default B,P,PF: the boxes, in order),
# VMTASK_SECS (6), VMTASK_SETTLE (6); VMTASK_DOSBOX=x for DOSBox-X
# (default plain DOSBox); DROPIN_CYCLES (default fixed 30000: the same
# machine speed in every run); VMTASK_EXTRA more dropin.sh arguments.
# Prints OUTDIR/VMT1.LOG; exits 1 when a check failed or there is no log.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 conf=$2
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c"
ref=$root/.tools/ref/win31/cdrive
k386="$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE"
w386="$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
vmt="$root/build/dos/VMTASK.386=WINDOWS/SYSTEM/VMTASK.386"
win=WINDOWS
case $conf in
ms)   set -- -r -f "$k386" -f "$vmt" -i "386Enh:+device=vmtask.386" ;;
w16)  set -- -r -f "$k386" -f "$w386" -f "$vmt" -i "386Enh:+device=vmtask.386" ;;
tree) win=W16; set -- -T "$root/build/w16" -3 ;;
*) echo "vmtaskrun.sh OUTDIR ms|w16|tree" >&2; exit 2 ;;
esac
progs=
for b in $(echo "${VMTASK_PROGS:-B,P,PF}" | tr , ' '); do
    case $b in
    B*) mode=b ;;
    *)  mode=p ;;
    esac
    case $b in
    ?F*) win_=0 ;;
    *)   win_=1 ;;
    esac
    python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/$b.PIF" "title=$b" \
        "program=C:\\$win\\VX14.COM" "params=$mode" "dir=C:\\$win" "windowed=$win_" background=1 || exit 2
    set -- "$@" -f "$o/$b.PIF=$win/$b.PIF"
    progs="$progs${progs:+,}C:\\$win\\$b.PIF"
done
set -- "$@" -c "WIN /3 /B" -t ${VMTASK_TIME:-400} -o "$o/c" -W "$win/VMT1.LOG" \
    -f "$root/build/dos/VMT1.EXE=$win/VMT1.EXE" -f "$root/build/dos/VX14.COM=$win/VX14.COM" \
    -i "WIN.INI:windows:run=C:\\$win\\VMT1.EXE" -i "WIN.INI:vmt1:progs=$progs" \
    -i "WIN.INI:vmt1:secs=${VMTASK_SECS:-6}" -i "WIN.INI:vmt1:settle=${VMTASK_SETTLE:-6}" \
    ${VMTASK_EXTRA:-}
[ "${VMTASK_DOSBOX:-}" = x ] && set -- "$@" -p 386
DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_CYCLES=${DROPIN_CYCLES:-fixed 30000} \
    DROPIN_WSECS=3 "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
for f in VMT1.LOG TK.LOG; do
    l=$(find "$o/c" -iname "$f" | head -1)
    [ -n "$l" ] && tr -d '\r' < "$l" > "$o/$f"
done
if [ ! -f "$o/VMT1.LOG" ]; then
    echo "no VMT1.LOG"; tail -5 "$o/dropin.txt"; cat "$o/TK.LOG" 2>/dev/null
    exit 1
fi
cat "$o/VMT1.LOG"
grep -q '^CHECK FAIL' "$o/VMT1.LOG" && exit 1
grep -q '^VMT1 done' "$o/VMT1.LOG" || exit 1
exit 0
