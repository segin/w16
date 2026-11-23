#!/bin/sh
# vmtaskshot.sh OUTDIR CONF [x]: screenshots of the VM Task Manager
# (VMTASK.EXE, ARCHITECTURE.md 15.12) with no DOS box, one busy box and a
# busy and an idle (polling) box. WIN.INI run= starts VMTASK.EXE and
# VMT1.EXE (tests/apps/vmt1.c), which starts the boxes (VX14 b and p,
# tests/dos/vx14.asm; windowed, Background, minimised) on a timeline:
# the first VMTASK_T0 seconds after it starts, the second VMTASK_SETTLE
# (30) + 6 later. tests/inputrun.sh takes the shots (OUTDIR/vmt0.png,
# vmt1.png, vmt2.png) VMTASK_BOOT (45) seconds after DOSBox starts, then
# 50 and 40 seconds later: a box is started 20 seconds or more before
# its shot (VMTASK.EXE's screen changes every second, so the shots are
# taken by the clock, not by a still screen). inputrun.sh's own files
# (the scratch drive, its log) are in OUTDIR/run.
#   CONF  tree    w16's install tree alone (-T build/w16 -3), WIN /3
#         ms      the reference install with Microsoft's KRNL386 and
#                 WIN386, w16's VMTASK.386 and VMTASK.EXE added
#         std     w16's tree in Standard mode (WIN /S): no VMs, the
#                 message and the Windows tasks (one shot, vmt0.png)
#         novxd   the tree in 386 Enhanced mode without VMTASK.386 (one
#                 shot)
#   x     DOSBox-X (default plain DOSBox)
# VMTASK.EXE runs Always on Top (WIN.INI [VMTask] OnTop=1): w16's
# WINOA386 opens a box started minimised as a window in front.
# DROPIN_CYCLES as dropin.sh (default max).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 conf=$2 dx=${3:-}
mkdir -p "$o"
o=$(cd "$o" && pwd)
ref=$root/.tools/ref/win31/cdrive
win=W16 mode="WIN /3" boxes=1
case $conf in
tree)  set -- -T "$root/build/w16" -3 ;;
std)   set -- -T "$root/build/w16" -3; mode="WIN /S"; boxes= ;;
novxd) set -- -T "$root/build/w16" -3 -i "386Enh:-device=vmtask.386"; boxes= ;;
ms)    win=WINDOWS
       set -- -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
           -f "$root/build/dos/VMTASK.386=WINDOWS/SYSTEM/VMTASK.386" -i "386Enh:+device=vmtask.386" \
           -f "$root/build/dos/VMTASK.EXE=WINDOWS/VMTASK.EXE" ;;
*) echo "vmtaskshot.sh OUTDIR tree|ms|std|novxd [x]" >&2; exit 2 ;;
esac
if [ "$conf" = novxd ]; then
    # (dropin's S:-K= removes every device= line: put the others back)
    grep -i '^device=' "$root/build/w16/SYSTEM.INI" | tr -d '\r' | grep -iv vmtask | while read -r l; do
        printf '%s\n' "386Enh:+$l"; done > "$o/devs.txt"
    while read -r l; do set -- "$@" -i "$l"; done < "$o/devs.txt"
fi
progs=
for b in B P; do
    case $b in B) m=b ;; P) m=p ;; esac
    python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/$b.PIF" "title=$b" \
        "program=C:\\$win\\VX14.COM" "params=$m" "dir=C:\\$win" windowed=1 background=1 || exit 2
    set -- "$@" -f "$o/$b.PIF=$win/$b.PIF"
    progs="$progs${progs:+,}C:\\$win\\$b.PIF"
done
run="C:\\$win\\VMTASK.EXE"
[ -n "$boxes" ] && run="$run C:\\$win\\VMT1.EXE"
set -- "$@" -c "$mode" \
    -f "$root/build/dos/VMT1.EXE=$win/VMT1.EXE" -f "$root/build/dos/VX14.COM=$win/VX14.COM" \
    -i "WIN.INI:windows:run=$run" -i "WIN.INI:vmt1:progs=$progs" \
    -i "WIN.INI:vmt1:wait=${VMTASK_T0:-60}" -i "WIN.INI:vmt1:settle=${VMTASK_SETTLE:-30}" \
    -i "WIN.INI:vmt1:secs=6" -i "WIN.INI:VMTask:OnTop=1"
[ "$dx" = x ] && set -- "$@" -p 386
if [ -n "$boxes" ]; then
    printf 'shot vmt0\nsleep 50\nshot vmt1\nsleep 40\nshot vmt2\n' > "$o/script.txt"
else
    printf 'shot vmt0\n' > "$o/script.txt"
fi
rm -rf "$o"/vmt?.png "$o/run"
"$root/tests/inputrun.sh" -b "${VMTASK_BOOT:-45}" -t 400 "$o/script.txt" "$o/run" "$@" > "$o/input.txt" 2>&1
cp "$o"/run/vmt?.png "$o/" 2>/dev/null
ls "$o"/vmt?.png 2>/dev/null || { tail -5 "$o/input.txt"; exit 1; }
