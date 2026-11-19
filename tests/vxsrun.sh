#!/bin/sh
# vxsrun.sh NAME SCRIPT [w16]: VXS.386 (tests/vxd/vxs.asm, `make
# vxprobes`), the DOS-box probe VxD, loaded by SYSTEM.INI [386Enh]
# device=vxs.386, then the input script (e.g. tests/scripts/input/
# dosbox386e.txt: the MS-DOS Prompt, Alt+Enter, VER, DIR, EXIT) on MS-DOS
# 4.00 (INPUTRUN_HDD) in DOSBox-X, with Microsoft's WIN386 (or w16's: w16)
# and Microsoft's KRNL386 and the rest. Shots and the work tree in
# build/tmp/NAME, COM1 (VXS's lines and, under w16, the VMM's trace) in
# build/tmp/NAME.com1. VXS_EXTRA: more inputrun arguments; VXS_FILE=VXSH.386:
# the variant that also logs a few VMM services.
cd "$(dirname "$0")/.."
mkdir -p build/tmp
n=$1 scr=$2 w=${3:-}
o=build/tmp/$n
rm -rf "$o" "$o.com1"
K=.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
set -- -f $K=WINDOWS/SYSTEM/KRNL386.EXE -f build/dos/${VXS_FILE:-VXS.386}=WINDOWS/SYSTEM/VXS.386 -i "386Enh:+device=vxs.386" ${VXS_EXTRA:-}
[ -n "$w" ] && set -- "$@" -f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE
INPUTRUN_HDD=1 INPUTRUN_KEYDELAY=${INPUTRUN_KEYDELAY:-150} DROPIN_MACHINE=${VXS_MACHINE:-svga_s3} DROPIN_CONF="[serial]
serial1=file file:$PWD/$o.com1" INPUTRUN_STAY=1 tests/inputrun.sh -b ${VXS_BOOT:-90} -t ${VXS_TIME:-300} "$scr" "$o" -r -p 386 -c "WIN /3 /B" "$@" > "$o.txt" 2>&1
tail -n 3 "$o/input.log"
