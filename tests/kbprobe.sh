#!/bin/sh
# The keyboard driver's interface, w16's against Windows 3.1's
# (drivers/keyboard/README.md). Two probes, each run with Windows 3.1's
# KEYBOARD.DRV and with w16's (build/dos/KEYBOARD.DRV), Microsoft's
# KRNL286, DOSX and USER around them:
#   kb1  KB1.EXE (tests/apps/kb1.c) as the shell calls every export over
#        all its inputs: every character through AnsiToOem/OemToAnsi and
#        the Buff forms, VkKeyScan, OemKeyScan, MapVirtualKey, every scan
#        code through GetKeyNameText, every virtual key in 14 shift states
#        through ToAscii, Alt+keypad sequences, the stack use and the
#        registers each entry leaves, Enable/Disable and the INT 9 vector.
#   kb2  KB2.EXE (tests/apps/kb2.c) as the shell under real key input
#        (tests/scripts/input/kbprobe.txt through tests/inputrun.sh:
#        xdotool into DOSBox's emulated keyboard), with KBREC.DRV
#        (tests/apps/kbrec.asm) as KEYBOARD in front of the driver under
#        test (renamed KBTARGET by tools/nerename.py) and KB4.COM
#        (tests/dos/kb4.asm) loaded before WIN: the keyboard messages KB2
#        gets, every call made to the driver, every call the driver makes
#        to USER's event procedure (registers, interrupt flag, the BIOS
#        keyboard flags) and what reaches real mode (INT 9, INT 16h, INT
#        15h AH=4Fh).
# The logs, without the layout-dependent lines and fields ("~"), must be
# identical.
#   usage: tests/kbprobe.sh [-o DIR] [-m] [kb1] [kb2]   (default: both)
#     -m   Microsoft's driver only (to look at its logs)
# Exit status 0 when every comparison made agrees.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= msonly=
while getopts o:m opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) msonly=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
probes=${*:-kb1 kb2}
[ -n "$out" ] || out=$root/build/kbprobe-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
ms=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KEYBOARD.DRV
w16=$root/build/dos/KEYBOARD.DRV
dos=$root/build/dos
sides="ms w16"
[ -z "$msonly" ] || sides=ms
fails=0

# cmpf NAME A B: the logs without "~" lines and fields
strip() { tr -d '\r' < "$1" | grep -av '^~' | grep -av '^OBS ~' | sed 's/ ~[^ ]*//g'; }
cmpf() {
    strip "$2" > "$2.cmp"
    strip "$3" > "$3.cmp"
    if cmp -s "$2.cmp" "$3.cmp"; then
        echo "ok   $1: identical ($(wc -l < "$2.cmp") lines)"
    else
        echo "FAIL $1: differs ($2.cmp $3.cmp):"
        diff "$2.cmp" "$3.cmp" | head -30
        fails=$((fails + 1))
    fi
}

for p in $probes; do
    case $p in
    kb1)
        for s in $sides; do
            if [ $s = ms ]; then set --; else set -- -f "$w16=WINDOWS/SYSTEM/KEYBOARD.DRV"; fi
            "$root/tests/dropin.sh" -r -t 120 -W WINDOWS/TK.LOG -o "$out/kb1-$s" \
                -f "$dos/KB1.EXE=KB1.EXE" -i 'boot:shell=C:\KB1.EXE' "$@" > "$out/kb1-$s.txt" 2>&1 || true
            cp "$out/kb1-$s/c/WINDOWS/TK.LOG" "$out/kb1-$s.log" 2>/dev/null || : > "$out/kb1-$s.log"
            grep -aq '^KB1 done' "$out/kb1-$s.log" || { echo "FAIL kb1 $s: did not finish ($out/kb1-$s.log)"; fails=$((fails + 1)); }
        done
        [ -n "$msonly" ] || cmpf kb1 "$out/kb1-ms.log" "$out/kb1-w16.log" ;;
    kb2)
        printf 'C:\\KB4.COM\r\nWIN /S /B\r\n' > "$out/KBGO.BAT"
        for s in $sides; do
            if [ $s = ms ]; then src=$ms; else src=$w16; fi
            cp "$src" "$out/KBTARGET-$s.DLL"
            python3 "$root/tools/nerename.py" "$out/KBTARGET-$s.DLL" --module KBTARGET > /dev/null
            "$root/tests/inputrun.sh" -b 30 -t 160 "$root/tests/scripts/input/kbprobe.txt" "$out/kb2-$s" -r \
                -c KBGO -f "$out/KBGO.BAT=WINDOWS/KBGO.BAT" -f "$dos/KB4.COM=KB4.COM" \
                -f "$dos/KB2.EXE=KB2.EXE" -i 'boot:shell=C:\KB2.EXE' \
                -f "$dos/KBREC.DRV=WINDOWS/SYSTEM/KEYBOARD.DRV" -f "$out/KBTARGET-$s.DLL=WINDOWS/SYSTEM/KBTARGET.DLL" \
                > "$out/kb2-$s.txt" 2>&1 || true
            cp "$out/kb2-$s/work/c/WINDOWS/TK.LOG" "$out/kb2-$s.log" 2>/dev/null || : > "$out/kb2-$s.log"
            grep -aq '^KB2 done' "$out/kb2-$s.log" || { echo "FAIL kb2 $s: did not finish ($out/kb2-$s.log)"; fails=$((fails + 1)); }
        done
        [ -n "$msonly" ] || cmpf kb2 "$out/kb2-ms.log" "$out/kb2-w16.log" ;;
    *) echo "unknown probe $p" >&2; exit 2 ;;
    esac
done
echo "kbprobe: logs in $out"
exit $fails
