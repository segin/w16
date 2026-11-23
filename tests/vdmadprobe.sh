#!/bin/sh
# The Virtual DMA Device (VDMAD, device 0004h; ARCHITECTURE.md 12.8):
# Windows 3.1's WIN386 (ms) or w16's (w16) in DOSBox-X as a Sound Blaster
# 16 (220h, IRQ 5, DMA 1 and 5) with the sound recorded (tests/dropin.sh
# DROPIN_CAPTURE), the probe VxD VXM.386 (tests/vxd/vxm.c: VDMAD's
# services, the ports it traps, a channel callback on channels 1 and 5
# that logs each virtual state and what VDMAD_Default_Handler programmed)
# writing to COM1, and the DOS program SBD.COM (tests/dos/sbd.asm) in the
# MS-DOS Prompt (tests/scripts/input/vdmadbox.txt): the DMA registers read
# back, single-cycle and auto-initialised 8-bit and 16-bit playback.
#   usage: tests/vdmadprobe.sh [-o DIR] [-k FILE] [-R] ms|w16 [SCRIPT]
#     -o DIR   the output directory (default build/vdmadprobe-SIDE)
#     -R       VXMR.386 instead (it reserves 8 pages of buffer space)
#     -k FILE  ignored (the boot test's reference cache key)
#   SCRIPT: tests/scripts/input/SCRIPT.txt (default vdmadbox)
#   VDMADPROBE_NOVXM=1: without the probe VxD; VDMADPROBE_WIN386=FILE:
#   that WIN386.EXE for w16's side; VDMADPROBE_INI="sec:key=val ...":
#   more SYSTEM.INI settings
# Output: com1.txt (the VXM lines and the VMM's), sbd.log (SBD's lines),
# tones.txt (tools/wavtones.py --split over the recording), run/ (the
# input run, its shots).
set -u
unset DISPLAY WAYLAND_DISPLAY
cd "$(dirname "$0")/.."
root=$PWD
odir= vxm=VXM
while getopts o:k:R opt; do
    case $opt in
    o) odir=$OPTARG ;;
    R) vxm=VXMR ;;
    k) ;;
    *) sed -n '2,/^set -u/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
side=$1; shift
sc=${1:-vdmadbox}
o=${odir:-$root/build/vdmadprobe-$side}
rm -rf "$o"
mkdir -p "$o"
o=$(cd "$o" && pwd)
K=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
set -- -r -p 386 -c "WIN /3 /B" -f "$K=WINDOWS/SYSTEM/KRNL386.EXE" \
    -f "$root/build/dos/SBD.COM=WINDOWS/SBD.COM"
[ -n "${VDMADPROBE_NOVXM:-}" ] ||
    set -- "$@" -f "$root/build/dos/$vxm.386=WINDOWS/SYSTEM/VXM.386" -i "386Enh:+device=vxm.386"
for kv in ${VDMADPROBE_INI:-}; do set -- "$@" -i "$kv"; done
[ "$side" = w16 ] && set -- "$@" -f "${VDMADPROBE_WIN386:-$root/build/win386/WIN386.EXE}=WINDOWS/SYSTEM/WIN386.EXE"
DROPIN_CAPTURE=$o/cap DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 DROPIN_MACHINE=svga_s3 \
DROPIN_CONF="[serial]
serial1=file file:$o/com1.raw
[sblaster]
sbtype=sb16
sbbase=220
irq=5
dma=1
hdma=5
[mixer]
rate=44100" timeout 600 tests/inputrun.sh -s -b 200 -t 400 \
    "$root/tests/scripts/input/$sc.txt" "$o/run" "$@" > "$o/run.txt" 2>&1
tr -d '\r' < "$o/com1.raw" 2>/dev/null | grep -a -v '^$' > "$o/com1.txt"
find "$o/run/work" -iname sbd.log -exec cat {} \; 2>/dev/null | tr -d '\r' > "$o/sbd.log"
w=$(find "$o/cap" -name '*.wav' 2>/dev/null | head -1)
if [ -n "$w" ]; then python3 "$root/tools/wavtones.py" --split "$w" > "$o/tones.txt" 2>&1
else echo "no recording" > "$o/tones.txt"; fi
echo "== $side $sc: $(tail -n 1 "$o/run/input.log" 2>/dev/null)"
grep -a -c '^VXM' "$o/com1.txt" | sed 's/^/VXM lines: /'
cat "$o/sbd.log" "$o/tones.txt"
