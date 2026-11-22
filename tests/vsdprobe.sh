#!/bin/sh
# The Virtual Sound Device (VSD, device 000Bh; ARCHITECTURE.md 12.8):
# Windows 3.1's WIN386 (ms) or w16's (w16) in DOSBox-X with the PC speaker
# recorded (tests/dropin.sh DROPIN_CAPTURE) and the probe VxD VXV.386
# (tests/vxd/vxp.asm built with VSDPROBE: VSD_Get_Version, VSD_Bell at
# Init_Complete and Sys_VM_Init, whether ports 40h/42h/43h/61h are
# trapped, the DOS boxes' accesses when they are not) writing to COM1;
# the DOS-side probe SPK.COM (tests/dos/spk.asm) in the MS-DOS Prompt:
#   fg   one box full screen in front (DOSPRMPT.PIF, no background): a
#        tone; another, with Alt+Tab to Windows in the middle and back
#        (tests/scripts/input/vsdfg.txt)
#   bg   a box with background execution plays while Windows is in
#        front, where Alt+Q in Program Manager beeps (vsdbg.txt)
#   two  two such boxes, the first playing behind while the second plays
#        in front (vsdtwo.txt)
#   kbd  keys typed into a box that has interrupts off (SPK X; vsdkbd.txt)
#   hung Reboot's screens: VX8 hung, keys the screen has no use for,
#        Escape, Return; the plain screen (vsdhung.txt)
#   cadd Reboot's screen for a DOS program and keys it has no use for
#        (vsdcadd.txt)
#   gp   VX12 G's privileged instruction in a full-screen box (SHELL's
#        box), Ctrl+Alt+Del in a second box (tests/scripts/input/
#        vx12gc.txt)
#   NAME any other input script, tests/scripts/input/NAME.txt (VX12.COM
#        at hand): what beeps in it
#   usage: tests/vsdprobe.sh [-o DIR] [-k FILE] ms|w16 [fg] [bg] [two] [kbd] [hung] [cadd] [gp] [NAME]
#        (default: fg bg two)
#     -o DIR   the output directory (one scenario only)
#   VSDPROBE_NOVXV=1: without VXV.386 (ports 61h and 42h then trapped by
#   nobody); VSDPROBE_WIN386=FILE: that WIN386.EXE for w16's side
#     -k FILE  ignored (the boot test names the files a run depends on, for
#              its reference cache's key)
# Output in build/vsdprobe-SIDE-SCEN/: com1.txt (the VXP lines), spk.log
# (SPK's lines), tones.txt (tools/wavtones.py --split over the
# recording), and the run (input.log, the shots) in run/. Prints the
# three for each scenario. tools/vsdcmp.py compares two fg runs (the
# boot-test case vsd).
set -u
unset DISPLAY WAYLAND_DISPLAY
cd "$(dirname "$0")/.."
root=$PWD
odir=
while getopts o:k: opt; do
    case $opt in
    o) odir=$OPTARG ;;
    k) ;;
    *) sed -n '2,/^set -u/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
side=$1; shift
scens=${*:-fg bg two}
K=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
for sc in $scens; do
    o=${odir:-$root/build/vsdprobe-$side-$sc}
    rm -rf "$o"
    mkdir -p "$o"
    set -- -r -p 386 -c "WIN /3 /B" -f "$K=WINDOWS/SYSTEM/KRNL386.EXE" \
        -f "$root/build/dos/VXV.386=WINDOWS/SYSTEM/VXV.386" -i "386Enh:+device=vxv.386" \
        -f "$root/build/dos/SPK.COM=WINDOWS/SPK.COM"
    scr=$root/tests/scripts/input/vsd$sc.txt
    [ -f "$scr" ] || scr=$root/tests/scripts/input/$sc.txt
    [ -n "${VSDPROBE_NOVXV:-}" ] &&
        set -- -r -p 386 -c "WIN /3 /B" -f "$K=WINDOWS/SYSTEM/KRNL386.EXE" -f "$root/build/dos/SPK.COM=WINDOWS/SPK.COM"
    case $sc in
    kbd)                                # (VXV's trap of port 61h makes SPK X's wait far too long)
        set -- -r -p 386 -c "WIN /3 /B" -f "$K=WINDOWS/SYSTEM/KRNL386.EXE" -f "$root/build/dos/SPK.COM=WINDOWS/SPK.COM" ;;
    hung)
        cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$o/WIN.INI"
        python3 -I "$root/tests/scripts/iniset.py" "$o/WIN.INI" 'windows:run=C:\VX8.EXE' 'vx8:mode=2'
        set -- "$@" -f "$root/build/dos/VX8.EXE=VX8.EXE" -f "$o/WIN.INI=WINDOWS/WIN.INI" ;;
    gp) scr=$root/tests/scripts/input/vx12gc.txt
        set -- "$@" -f "$root/build/dos/VX12.COM=WINDOWS/VX12.COM" ;;
    fg|bg|two) ;;
    *)  set -- "$@" -f "$root/build/dos/VX12.COM=WINDOWS/VX12.COM" ;;
    esac
    if [ "$sc" = bg ] || [ "$sc" = two ]; then
        python3 "$root/tools/mkpif.py" "$root/.tools/ref/win31/cdrive/WINDOWS/DOSPRMPT.PIF" "$o/DOSPRMPT.PIF" background=1
        set -- "$@" -f "$o/DOSPRMPT.PIF=WINDOWS/DOSPRMPT.PIF"
    fi
    [ "$side" = w16 ] && set -- "$@" -f "${VSDPROBE_WIN386:-$root/build/win386/WIN386.EXE}=WINDOWS/SYSTEM/WIN386.EXE"
    DROPIN_CAPTURE=$o/cap DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 DROPIN_MACHINE=svga_s3 \
    DROPIN_CONF="[serial]
serial1=file file:$o/com1.raw" timeout 600 tests/inputrun.sh -s -b 200 -t 400 \
        "$scr" "$o/run" "$@" > "$o/run.txt" 2>&1
    tr -d '\r' < "$o/com1.raw" 2>/dev/null | grep -a 'VXP\|panic' > "$o/com1.txt"
    find "$o/run/work" -iname spk.log -exec cat {} \; 2>/dev/null | tr -d '\r' > "$o/spk.log"
    w=$(find "$o/cap" -name '*.wav' 2>/dev/null | head -1)
    if [ -n "$w" ]; then python3 "$root/tools/wavtones.py" --split "$w" > "$o/tones.txt" 2>&1
    else echo "no recording" > "$o/tones.txt"; fi
    echo "== $side $sc: $(tail -n 1 "$o/run/input.log" 2>/dev/null)"
    grep -a 'VSD\|61h\|Install_IO\|io counts' "$o/com1.txt"
    grep -a -c 'vio ' "$o/com1.txt" | sed 's/^/vio lines: /'
    cat "$o/spk.log" "$o/tones.txt"
done
