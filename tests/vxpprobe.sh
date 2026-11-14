#!/bin/sh
# VXP.386 (tests/vxd/vxp.asm, `make vxprobes`), w16's probe VxD, through a
# whole 386 Enhanced mode session (WIN /3 /B, then File > Exit Windows with
# keys: tests/scripts/input/exitwin.txt) in headless DOSBox-X (tests/
# inputrun.sh), with Microsoft's WIN386.EXE (ms) or w16's (w16), and
# Microsoft's KRNL386 and the rest. Prints the VXP lines of COM1: the
# control messages VXP got and the VMM services' results
# (ARCHITECTURE.md 12.7).
#   usage: tests/vxpprobe.sh ms|w16
# Output also in build/vxpprobe-ms|w16{.com1,.txt,/}.
set -u
cd "$(dirname "$0")/.."
w=$1
K=.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
o=$PWD/build/vxpprobe-$w
rm -rf "$o" "$o.com1"
set -- -f $K=WINDOWS/SYSTEM/KRNL386.EXE -f build/dos/VXP.386=WINDOWS/SYSTEM/VXP.386 -i "386Enh:+device=vxp.386"
[ "$w" = w16 ] && set -- "$@" -f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE
DROPIN_MACHINE=svga_s3 DROPIN_CONF="[serial]
serial1=file file:$o.com1" timeout 400 tests/inputrun.sh -s -b 200 -t 200 tests/scripts/input/exitwin.txt "$o" \
    -r -p 386 -c "WIN /3 /B" "$@" > "$o.txt" 2>&1
tail -n 3 "$o/input.log"
tr -d '\r' < "$o.com1" | grep -a 'VXP\|panic\|exit'
