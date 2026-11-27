#!/bin/sh
# vmtasksoak.sh OUTDIR [tree|ms]: the VM Task Manager left running for many
# refreshes while programs and DOS boxes come and go (ARCHITECTURE.md
# 15.12). VMTSOAK.EXE (tests/apps/vmtsoak.c, WIN.INI run=) starts
# VMTASK.EXE at its fastest interval (0.5 s) with the IDs and the
# history shown, then for each of VMTASK_CYCLES cycles (default 40)
# starts and closes one of Clock, Notepad, Calculator, Cardfile and Write
# (minimised) and, every other cycle, a busy DOS box (B.PIF: VX14 b) that
# it ends through VMTASK.386 (Close_VM and Nuke_VM in turn); after each
# cycle it logs the free memory and resources, the global heap's blocks,
# the tasks and the VM Task Manager's own blocks, and at the end checks
# that the VM Task Manager still runs and nothing grew past a small
# margin. KERNEL's output (a fault report) is in OUTDIR/OUT.TXT.
#   tree  w16's install tree alone (-T build/w16 -3; default)
#   ms    the reference install, Microsoft's KRNL386 and WIN386, with w16's
#         VMTASK.386 and VMTASK.EXE (and the reference's programs)
# Environment: VMTASK_APPS (the programs, default "CLOCK NOTEPAD CALC
# CARDFILE WRITE"; empty: none), VMTASK_BOX=0 (no DOS boxes),
# VMTASK_LAUNCH=0 (no VM Task Manager: what the rest leaves behind by
# itself); VMTASK_DOSBOX=x for DOSBox-X (default plain DOSBox);
# VMTASK_CYCLES; VMTASK_TIME (the time limit, default 900 s);
# DROPIN_CYCLES (default max). Prints OUTDIR/VMTSOAK.LOG; exits 1 when a
# check failed or there is no log.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 conf=${2:-tree}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c"
ref=$root/.tools/ref/win31/cdrive
case $conf in
tree) win=W16; set -- -T "${VMTASK_TREE:-$root/build/w16}" -3 ;;
ms)   win=WINDOWS
      set -- -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
          -f "$root/build/dos/VMTASK.386=WINDOWS/SYSTEM/VMTASK.386" -i "386Enh:+device=vmtask.386" \
          -f "$root/build/dos/VMTASK.EXE=WINDOWS/VMTASK.EXE" ;;
*) echo "vmtasksoak.sh OUTDIR [tree|ms]" >&2; exit 2 ;;
esac
python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/B.PIF" "title=B" \
    "program=C:\\$win\\VX14.COM" "params=b" "dir=C:\\$win" windowed=1 background=1 || exit 2
apps=
for a in ${VMTASK_APPS-CLOCK NOTEPAD CALC CARDFILE WRITE}; do apps="$apps${apps:+,}C:\\$win\\$a.EXE"; done
launch="C:\\$win\\VMTASK.EXE" box="C:\\$win\\B.PIF"
[ "${VMTASK_LAUNCH:-1}" = 0 ] && launch=
[ "${VMTASK_BOX:-1}" = 0 ] && box=
set -- "$@" -c "WIN /3 /B" -t "${VMTASK_TIME:-900}" -o "$o/c" -W "$win/VMTSOAK.LOG" \
    -f "$o/B.PIF=$win/B.PIF" -f "$root/build/dos/VMTSOAK.EXE=$win/VMTSOAK.EXE" \
    -f "$root/build/dos/VX14.COM=$win/VX14.COM" \
    -i "WIN.INI:windows:run=C:\\$win\\VMTSOAK.EXE" \
    -i "WIN.INI:vmtsoak:launch=$launch" -i "WIN.INI:vmtsoak:apps=$apps" \
    -i "WIN.INI:vmtsoak:box=$box" -i "WIN.INI:vmtsoak:cycles=${VMTASK_CYCLES:-40}" \
    -i "WIN.INI:VMTask:Interval=500" -i "WIN.INI:VMTask:IDs=1" -i "WIN.INI:VMTask:Graph=1" \
    -i "WIN.INI:VMTask:Left=0" -i "WIN.INI:VMTask:Top=0" ${VMTASK_EXTRA:-}
[ "${VMTASK_DOSBOX:-}" = x ] && set -- "$@" -p 386
DROPIN_CYCLES=${DROPIN_CYCLES:-max} DROPIN_WSECS=3 "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
for f in VMTSOAK.LOG TK.LOG OUT.TXT; do
    l=$(find "$o/c" -maxdepth 3 -iname "$f" | head -1)
    [ -n "$l" ] && tr -d '\r' < "$l" > "$o/$f"
done
if [ ! -f "$o/VMTSOAK.LOG" ]; then
    echo "no VMTSOAK.LOG"; tail -40 "$o/dropin.txt"; cat "$o/TK.LOG" 2>/dev/null
    exit 1
fi
cat "$o/VMTSOAK.LOG"
grep -q '^CHECK FAIL' "$o/VMTSOAK.LOG" && exit 1
grep -q '^VMTSOAK done' "$o/VMTSOAK.LOG" || exit 1
exit 0
