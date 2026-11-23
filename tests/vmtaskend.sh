#!/bin/sh
# vmtaskend.sh OUTDIR CONF [x]: the VM Task Manager's End VM, End Task,
# shortcut menu and IDs, driven through the keyboard and mouse
# (ARCHITECTURE.md 15.12). WIN.INI run= starts VMTH.EXE (tests/apps/
# vmth.c: a program whose window ignores WM_CLOSE) and VMT1.EXE
# (tests/apps/vmt1.c), which 25 s later logs the tasks' and VMs' IDs,
# starts a busy DOS box (B.PIF: VX14 b, windowed, minimised and not
# active), then VMTASK.EXE (at 0,0, Always on Top: the active window, also
# where w16's WINOA386 gives a box started minimised the focus), and after [vmt1] final
# seconds checks that VMTH has ended and only the System VM is left.
# tests/inputrun.sh then (shots in OUTDIR):
#   e0  the list with the IDs columns (compare with VMT1's log)
#   e1  Shift+F10's shortcut menu on the selected row
#   e2  End key (the last row, the box), Del: 3.1's Terminate warning
#   e3  Shift+Tab, Space (OK): the box ended
#   e4  the VMTH row selected (typing "vmth": the list's type-ahead), e5
#       Del: End Task's question, e6 Return: WM_CLOSE sent and ignored, 5 s
#       later the offer to end it by force, e7 Shift+Tab, Space: it has ended
#   e8  a right click on the first row: the Windows row's menu
#   v1  Task, Virtual Devices...; v2 Task, Drivers...
# Then OUTDIR/VMT1.LOG's checks; exits 1 if one failed.
#   CONF  tree  w16's install tree alone (-T build/w16 -3)
#         ms    the reference install, Microsoft's KRNL386 and WIN386, with
#               w16's VMTASK.386 and VMTASK.EXE
#   x     DOSBox-X (default plain DOSBox)
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 conf=$2 dx=${3:-}
mkdir -p "$o"
o=$(cd "$o" && pwd)
ref=$root/.tools/ref/win31/cdrive
win=W16
case $conf in
tree) set -- -T "$root/build/w16" -3 ;;
ms)   win=WINDOWS
      set -- -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
          -f "$root/build/dos/VMTASK.386=WINDOWS/SYSTEM/VMTASK.386" -i "386Enh:+device=vmtask.386" \
          -f "$root/build/dos/VMTASK.EXE=WINDOWS/VMTASK.EXE" ;;
*) echo "vmtaskend.sh OUTDIR tree|ms [x]" >&2; exit 2 ;;
esac
python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/B.PIF" "title=B" \
    "program=C:\\$win\\VX14.COM" "params=b" "dir=C:\\$win" windowed=1 background=1 || exit 2
set -- "$@" -c "WIN /3" -f "$o/B.PIF=$win/B.PIF" \
    -f "$root/build/dos/VMT1.EXE=$win/VMT1.EXE" -f "$root/build/dos/VMTH.EXE=$win/VMTH.EXE" \
    -f "$root/build/dos/VX14.COM=$win/VX14.COM" \
    -i "WIN.INI:windows:run=C:\\$win\\VMTH.EXE C:\\$win\\VMT1.EXE" \
    -i "WIN.INI:vmt1:launch=C:\\$win\\VMTASK.EXE" -i "WIN.INI:vmt1:wait=25" -i "WIN.INI:vmt1:secs=4" \
    -i "WIN.INI:vmt1:progs=C:\\$win\\B.PIF" -i "WIN.INI:vmt1:settle=10" -i "WIN.INI:vmt1:end=0" -i "WIN.INI:vmt1:late=1" -i "WIN.INI:vmt1:final=${VMTASK_FINAL:-150}" \
    -i "WIN.INI:vmt1:gone=VMTH" -i "WIN.INI:vmt1:vms=1" \
    -i "WIN.INI:VMTask:OnTop=${VMTASK_ONTOP:-0}" -i "WIN.INI:VMTask:Left=0" -i "WIN.INI:VMTask:Top=0"
[ "$dx" = x ] && set -- "$@" -p 386
{
    printf 'sleep 3\nshot e0\n'
    printf 'key shift+F10\nsleep 2\nshot e1\nkey Escape\nsleep 1\n'
    printf 'key End\nsleep 1\nkey Delete\nsleep 3\nshot e2\nkey shift+Tab\nkey space\nsleep 10\nshot e3\n'
    printf 'type vmth\n'
    printf 'sleep 2\nshot e4\nkey Delete\nsleep 3\nshot e5\nkey Return\nsleep 9\nshot e6\n'
    printf 'key shift+Tab\nkey space\nsleep 6\nshot e7\n'
    printf 'home\nmove 60 40\nclick 3\nsleep 2\nshot e8\nkey Escape\n'
    printf 'key alt+t\nkey i\nsleep 3\nshot v1\nkey Escape\nsleep 1\n'
    printf 'key alt+t\nkey r\nsleep 3\nshot v2\nkey Escape\nsleep 1\n'
    # (VMT1's final checks come VMTASK_FINAL s after its start)
    printf 'sleep %s\n' "${VMTASK_TAIL:-90}"
} > "$o/script.txt"
rm -rf "$o/run" "$o"/e?.png "$o"/v?.png
DROPIN_CYCLES=${DROPIN_CYCLES:-max} "$root/tests/inputrun.sh" -b "${VMTASK_BOOT:-75}" -t 600 \
    "$o/script.txt" "$o/run" "$@" > "$o/input.txt" 2>&1
cp "$o"/run/e?.png "$o"/run/v?.png "$o/" 2>/dev/null
# VMT1 checks after the script (its final wait)
l=$(find "$o/run/work" -iname VMT1.LOG 2>/dev/null | head -1)
[ -z "$l" ] && l=$(find "$o/run" -iname VMT1.LOG 2>/dev/null | head -1)
if [ -z "$l" ]; then echo "no VMT1.LOG"; tail -5 "$o/input.txt"; exit 1; fi
tr -d '\r' < "$l" > "$o/VMT1.LOG"
t=$(find "$o/run" -iname TK.LOG | head -1)
[ -n "$t" ] && tr -d '\r' < "$t" > "$o/TK.LOG"
grep -E '^(VMT1 (task|left|  vm)|CHECK)' "$o/VMT1.LOG"
grep -q '^CHECK FAIL' "$o/VMT1.LOG" && exit 1
grep -q 'has ended' "$o/VMT1.LOG" || exit 1
exit 0
