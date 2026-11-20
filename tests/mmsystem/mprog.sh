#!/bin/sh
# Real 3.1 programs over MMSYSTEM (layer 3; ARCHITECTURE.md 23.4), with
# Microsoft's MMSYSTEM.DLL or w16's (and MMTASK.TSK), on the reference
# install with Windows 3.1's own Sound Blaster driver set up as
# tests/mmsystem/mrun.sh's MRUN_SND=sb2 (DOSBox as a Sound Blaster 16),
# driven by real keys (tests/inputrun.sh):
#   soundrec  Sound Recorder opens DING.WAV and plays it
#   sound     Control Panel's Sound applet (SND.CPL): its lists, then Test
#             with a sound picked
#   midimap   Control Panel's MIDI Mapper applet (MIDIMAP.DRV)
#   mplayer   Media Player (layer 4, MCI): its Device menu, CHIMES.WAV
#             opened (MCIWAVE.DRV) and played, CANYON.MID opened (MCISEQ)
#   usage: tests/mmsystem/mprog.sh OUTDIR ms|w16 PROG [-e REFDIR]
# -e REFDIR: the Microsoft run's directory (the w16 run waits for the same
# screens). OUTDIR gets the shots (0*.png).
# MPROG_SND=sb16w: w16's SB16.DRV (build/dos) as the wave, MIDI and aux
# driver instead of SNDBLST2 ([sb16.drv] port=220 int=5 dma=1 hdma=5
# mpuport=330; tests/sb16/sbscen.sh prog).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 mm=$2 prog=$3
shift 3
ref=
[ "${1:-}" = -e ] && ref=$2
tmp=$out.in
rm -rf "$tmp" "$out"
mkdir -p "$tmp"
for f in SNDBLST2 MSADLIB; do
    python3 -I "$root/tools/szdd.py" "$root/.tools/ref/win31/src/$f.DR_" "$tmp/$f.DRV"
done
cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$tmp/WIN.INI"
case $prog in
soundrec) run='C:\WINDOWS\DING.WAV' ;;     # run= starts the program each word names: the file opens Sound Recorder
sound|midimap) run=CONTROL.EXE ;;
mplayer) run=MPLAYER.EXE ;;
*) echo "PROG: soundrec, sound, midimap or mplayer" >&2; exit 2 ;;
esac
python3 "$root/tests/scripts/iniset.py" "$tmp/WIN.INI" "windows:run=$run" \
    'windows:CursorBlinkRate=65535'
if [ "${MPROG_SND:-sb2}" = sb16w ]; then
    set -- -r -f "$tmp/WIN.INI=WINDOWS/WIN.INI" -f "$root/build/dos/SB16.DRV=WINDOWS/SYSTEM/SB16.DRV" \
        -i 'drivers:wave=sb16.drv' -i 'drivers:midi=sb16.drv' -i 'drivers:aux=sb16.drv' \
        -i 'sb16.drv:port=220' -i 'sb16.drv:int=5' -i 'sb16.drv:dma=1' -i 'sb16.drv:hdma=5' \
        -i 'sb16.drv:mpuport=330'
else
    set -- -r -f "$tmp/WIN.INI=WINDOWS/WIN.INI" \
        -f "$tmp/SNDBLST2.DRV=WINDOWS/SYSTEM/SNDBLST2.DRV" -f "$tmp/MSADLIB.DRV=WINDOWS/SYSTEM/MSADLIB.DRV" \
        -i 'drivers:wave=sndblst2.drv' -i 'drivers:midi=sndblst2.drv' \
        -i 'sndblst.drv:port=220' -i 'sndblst.drv:int=5'
fi
[ "$mm" = w16 ] && set -- "$@" -f "$root/build/dos/MMSYSTEM.DLL=WINDOWS/SYSTEM/MMSYSTEM.DLL" \
    -f "$root/build/dos/MMTASK.TSK=WINDOWS/SYSTEM/MMTASK.TSK"
export DROPIN_CONF='[sblaster]
sbtype=sb16
sbbase=220
irq=5
dma=1
hdma=5
mpu401=intelligent
mpubase=330'
script=$root/tests/scripts/input/mm$prog.txt
if [ -n "$ref" ]; then
    set -- -e "$ref/boot.png" -E "$ref" -b 30 -t 300 "$script" "$out" "$@"
else
    set -- -s -b 30 -t 300 "$script" "$out" "$@"
fi
"$root/tests/inputrun.sh" "$@" > "$out.txt" 2>&1 || true
ls "$out"/0*.png 2>/dev/null || { echo "no shots ($out.txt)" >&2; exit 3; }
