#!/bin/sh
# vx10run.sh NAME SCRIPT [w16]: VX10 (tests/apps/vx10.c, `make vxprobes`),
# the DOS box API spy, started by WIN.INI run= in 386 Enhanced mode, then
# the input script (e.g. tests/scripts/input/dosbox386.txt: the MS-DOS
# Prompt), in plain DOSBox (tests/inputrun.sh), with Microsoft's WIN386
# (or w16's: w16; VX10X=1: DOSBox-X with the VMM's COM1 trace in
# build/tmp/NAME.com1); shots and the work tree in build/tmp/NAME; prints
# VX10's TK.LOG. VX10_EXTRA: more inputrun arguments; VX10_MACHINE: DOSBox-X's
# machine (default svga_s3).
cd "$(dirname "$0")/.."
mkdir -p build/tmp
n=$1 scr=$2 w=${3:-}
o=build/tmp/$n
rm -rf "$o" "$o.ini"
cp .tools/ref/win31/cdrive/WINDOWS/WIN.INI "$o.ini"
python3 -I tests/scripts/iniset.py "$o.ini" 'windows:run=C:\VX10.EXE' ${VX10_INI:-}
K=.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
set -- -f $K=WINDOWS/SYSTEM/KRNL386.EXE -f build/dos/VX10.EXE=VX10.EXE -f "$o.ini=WINDOWS/WIN.INI" ${VX10_EXTRA:-}
[ -n "$w" ] && set -- "$@" -f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE
if [ -n "${VX10X:-}" ]; then
    DROPIN_MACHINE=${VX10_MACHINE:-svga_s3} DROPIN_CONF="[serial]
serial1=file file:$PWD/$o.com1" INPUTRUN_STAY=1 tests/inputrun.sh -s -b 200 -t 300 "$scr" "$o" -r -p 386 -c "WIN /3 /B" "$@" > "$o.txt" 2>&1
else
    INPUTRUN_STAY=1 tests/inputrun.sh -s -b 60 -t 240 "$scr" "$o" -r -c "WIN /3 /B" "$@" > "$o.txt" 2>&1
fi
find "$o/work" -iname 'TK.LOG' -exec cat {} \; | tr -d '\r'
