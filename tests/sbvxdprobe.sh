#!/bin/sh
# The Sound Blaster's VxD in 386 Enhanced mode (ARCHITECTURE.md 24.5):
# Windows 3.1 with its own Sound Blaster driver and VxD (ms: SNDBLST2.DRV,
# VSBD.386 as Setup installs them, expanded from the reference's Setup
# files) or w16's WIN386 with SB16.DRV and w16's VxD (w16), in DOSBox-X as
# a Sound Blaster 16 (220h, IRQ 5, DMA 1 and 5) with the sound recorded;
# the probe VxD VXB.386 (tests/vxd/vxm.c built with SBPROBE: the IRQs'
# status, the card's ports, the VxD's API calls) writing to COM1; an input
# script (default sbbox: SBD.COM in the MS-DOS Prompt while Windows has the
# card; with tests/sb16/SCRIPT.sbp, SBP.EXE is the shell and plays it).
#   usage: tests/sbvxdprobe.sh [-o DIR] [-k FILE] ms|w16 [SCRIPT]
#   SBVXD_NOVXB=1: without the probe; SBVXD_INI="sec:key=val ...": more
#   SYSTEM.INI settings; SBVXD_WIN386=FILE: that WIN386.EXE for w16's side
# Output: com1.txt, sbd.log, tones.txt, run/ (the shots).
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
sc=${1:-sbbox}
o=${odir:-$root/build/sbvxd-$side-$sc}
rm -rf "$o"
mkdir -p "$o/drv"
o=$(cd "$o" && pwd)
K=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
set -- -r -p 386 -c "WIN /3 /B" -f "$K=WINDOWS/SYSTEM/KRNL386.EXE" \
    -f "$root/build/dos/SBD.COM=WINDOWS/SBD.COM"
# the probe first: its DDB_Next then leads to the card's VxD (same init order)
[ -n "${SBVXD_NOVXB:-}" ] ||
    set -- "$@" -f "$root/build/dos/VXB.386=WINDOWS/SYSTEM/VXB.386" -i "386Enh:+device=vxb.386"
if [ "$side" = ms ]; then
    python3 -I "$root/tools/szdd.py" "$root/.tools/ref/win31/src/SNDBLST2.DR_" "$o/drv/SNDBLST2.DRV"
    python3 -I "$root/tools/szdd.py" "$root/.tools/ref/win31/src/VSBD.38_" "$o/drv/VSBD.386"
    set -- "$@" -f "$o/drv/SNDBLST2.DRV=WINDOWS/SYSTEM/SNDBLST2.DRV" -f "$o/drv/VSBD.386=WINDOWS/SYSTEM/VSBD.386" \
        -i 'drivers:wave=sndblst2.drv' -i 'drivers:midi=sndblst2.drv' \
        -i 'sndblst.drv:port=220' -i 'sndblst.drv:int=5' -i '386Enh:+device=vsbd.386'
else
    set -- "$@" -f "$root/build/dos/SB16.DRV=WINDOWS/SYSTEM/SB16.DRV" \
        -f "$root/build/dos/VSBD.386=WINDOWS/SYSTEM/VSBD.386" \
        -i 'drivers:wave=sb16.drv' -i 'drivers:midi=sb16.drv' -i 'drivers:aux=sb16.drv' \
        -i 'sb16.drv:port=220' -i 'sb16.drv:int=5' -i 'sb16.drv:dma=1' -i 'sb16.drv:hdma=5' \
        -i 'sb16.drv:mpuport=330' -i '386Enh:+device=vsbd.386' \
        -f "${SBVXD_WIN386:-$root/build/win386/WIN386.EXE}=WINDOWS/SYSTEM/WIN386.EXE" \
        -f "$root/build/dos/WINOA386.MOD=WINDOWS/SYSTEM/WINOA386.MOD"
fi
# a script for SBP.EXE (tests/sb16/sbp.c) as the shell: tests/sb16/SCRIPT.sbp
if [ -f "$root/tests/sb16/$sc.sbp" ]; then
    set -- "$@" -f "$root/build/dos/SBP.EXE=SBP.EXE" -f "$root/tests/sb16/$sc.sbp=SBP.TXT" \
        -i 'boot:shell=C:\SBP.EXE'
fi
for kv in ${SBVXD_INI:-}; do set -- "$@" -i "$kv"; done
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
rate=44100" timeout 600 tests/inputrun.sh -s -b ${SBVXD_BOOT:-200} -t 400 \
    "$root/tests/scripts/input/$sc.txt" "$o/run" "$@" > "$o/run.txt" 2>&1
tr -d '\r' < "$o/com1.raw" 2>/dev/null | grep -a -v '^$' > "$o/com1.txt"
tr -d '\r' < "$o/run/work/c/SBP.LOG" > "$o/sbp.log" 2>/dev/null
find "$o/run/work" -iname sbd.log -exec cat {} \; 2>/dev/null | tr -d '\r' > "$o/sbd.log"
w=$(find "$o/cap" -name '*.wav' 2>/dev/null | head -1)
if [ -n "$w" ]; then python3 "$root/tools/wavtones.py" --split "$w" > "$o/tones.txt" 2>&1
else echo "no recording" > "$o/tones.txt"; fi
echo "== $side $sc: $(tail -n 1 "$o/run/input.log" 2>/dev/null)"
cat "$o/sbd.log" "$o/sbp.log" 2>/dev/null
