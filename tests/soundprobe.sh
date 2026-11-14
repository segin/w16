#!/bin/sh
# The sound driver (SYSTEM.INI [boot] sound.drv=, module SOUND), w16's
# MMSOUND.DRV against Windows 3.1's (drivers/sound/README.md). Each probe
# runs with Windows 3.1's MMSOUND.DRV and with w16's
# (build/dos/MMSOUND.DRV), Microsoft's KRNL286, DOSX and USER around them,
# in DOSBox-X at a fixed 10000 cycles a millisecond (the beep is a loop:
# its length follows the emulated processor's speed) with the PC speaker
# on and the run's sound recorded (tests/dropin.sh DROPIN_CAPTURE):
#   sn1  SN1.EXE (tests/apps/sn1.c) as the shell calls every export: the
#        bytes each takes off the stack and the registers it leaves, the
#        results over its inputs, the queue, playing (counts, the 8254's
#        channels 0 and 2 and port 61h while notes play), the beeps
#        (DoBeep, MessageBeep with each type), every note, the lengths,
#        tempos, modes, transposition, SetVoiceSound, thresholds. The
#        logs without the "~" lines and fields must be identical, and the
#        recordings' tones (tools/wavtones.py) the same within
#        tools/tonecmp.py's limits.
#   queue  part Q: the queue's capacity, filled by a second program
#        (SNH.EXE, SN1 renamed) while SN1 waits
#   rec  SN1's beeps with SNDREC.DRV (tests/apps/sndrec.asm) as SOUND in
#        front of the driver under test (renamed SOUNT by
#        tools/nerename.py): every call made to the driver from the boot
#        to the end (USER's MessageBeep: DoBeep with the beep's type) must
#        be the same
#   usage: tests/soundprobe.sh [-o DIR] [-m] [sn1] [queue] [rec]   (default: all)
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
probes=${*:-sn1 queue rec}
[ -n "$out" ] || out=$root/build/soundprobe-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
ms=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/MMSOUND.DRV
w16=$root/build/dos/MMSOUND.DRV
dos=$root/build/dos
sides="ms w16"
[ -z "$msonly" ] || sides=ms
fails=0
# SNH.EXE: SN1 under the module name SNH (part Q's helper)
cp "$dos/SN1.EXE" "$out/SNH.EXE"
python3 "$root/tools/nerename.py" "$out/SNH.EXE" --module SNH > /dev/null

# the logs without "~" lines and fields
strip() {
    tr -d '\r' < "$1" | grep -av '^~' | grep -av '^OBS ~' | sed 's/ ~[^ ]*//g'
}
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
# run SIDE NAME PARTS [dropin.sh options]: SN1 as the shell, its log to
# NAME-SIDE.log, the recording's tones to NAME-SIDE.tones
run() {
    s=$1 n=$2 parts=$3; shift 3
    if [ "$s" = w16 ] && [ -z "${norepl:-}" ]; then set -- "$@" -f "$w16=WINDOWS/SYSTEM/MMSOUND.DRV"; fi
    DROPIN_CYCLES='fixed 10000' DROPIN_CAPTURE="$out/$n-$s.cap" \
        "$root/tests/dropin.sh" -r -p 386 -t 600 -o "$out/$n-$s" \
        -f "$dos/SN1.EXE=SN1.EXE" -f "$out/SNH.EXE=SNH.EXE" -i 'boot:shell=C:\SN1.EXE' \
        -i "w16:sn1parts=$parts" "$@" > "$out/$n-$s.txt" 2>&1 || true
    cp "$out/$n-$s/c/WINDOWS/TK.LOG" "$out/$n-$s.log" 2>/dev/null || : > "$out/$n-$s.log"
    grep -aq '^SN1 done' "$out/$n-$s.log" || { echo "FAIL $n $s: did not finish ($out/$n-$s.log)"; fails=$((fails + 1)); }
    python3 "$root/tools/wavtones.py" --split "$out/$n-$s.cap"/*.wav > "$out/$n-$s.tones" 2>/dev/null || : > "$out/$n-$s.tones"
}

for p in $probes; do
    case $p in
    sn1)
        for s in $sides; do run $s sn1 ABCDEFGHSP & done
        wait
        if [ -z "$msonly" ]; then
            cmpf sn1 "$out/sn1-ms.log" "$out/sn1-w16.log"
            if python3 "$root/tools/tonecmp.py" "$out/sn1-ms.tones" "$out/sn1-w16.tones" > "$out/sn1-tones.txt"; then
                echo "ok   sn1 sound: $(tail -1 "$out/sn1-tones.txt")"
            else
                echo "FAIL sn1 sound ($out/sn1-tones.txt):"
                head -20 "$out/sn1-tones.txt"
                fails=$((fails + 1))
            fi
        fi ;;
    queue)
        for s in $sides; do run $s queue Q & done
        wait
        [ -n "$msonly" ] || cmpf queue "$out/queue-ms.log" "$out/queue-w16.log" ;;
    rec)
        for s in $sides; do
            if [ $s = ms ]; then src=$ms; else src=$w16; fi
            cp "$src" "$out/SOUNT-$s.DLL"
            python3 "$root/tools/nerename.py" "$out/SOUNT-$s.DLL" --module SOUNT > /dev/null
            norepl=1 run $s rec E -f "$dos/SNDREC.DRV=WINDOWS/SYSTEM/MMSOUND.DRV" \
                -f "$out/SOUNT-$s.DLL=WINDOWS/SYSTEM/SOUNT.DLL" &
        done
        wait
        for s in $sides; do
            grep -aq '^REC .*DoBeep' "$out/rec-$s.log" ||
                { echo "FAIL rec $s: no records ($out/rec-$s.log)"; fails=$((fails + 1)); }
        done
        # (SNDREC is MMSOUND.DRV on both sides, the driver under test
        # SOUNT: norepl)
        [ -n "$msonly" ] || cmpf rec "$out/rec-ms.log" "$out/rec-w16.log" ;;
    *) echo "unknown probe $p" >&2; exit 2 ;;
    esac
done
echo "soundprobe: logs in $out"
exit $fails
