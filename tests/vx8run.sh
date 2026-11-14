#!/bin/sh
# vx8run.sh NAME MODE SCRIPT [w16]: VX8 (tests/apps/vx8.c, `make vxprobes`)
# started by WIN.INI run= with [vx8] mode=MODE, in plain DOSBox
# (tests/inputrun.sh) with Microsoft's WIN386 (or w16's: w16), the input
# script run (e.g. tests/scripts/input/vx8hung.txt: Ctrl+Alt+Del, Escape,
# Ctrl+Alt+Del, Return, a shot after each); shots and the work tree in
# build/tmp/NAME; prints VX8's TK.LOG.
cd "$(dirname "$0")/.."
mkdir -p build/tmp
n=$1 mode=$2 scr=$3 w=${4:-}
o=build/tmp/$n
rm -rf "$o" "$o.ini"
cp .tools/ref/win31/cdrive/WINDOWS/WIN.INI "$o.ini"
python3 -I tests/scripts/iniset.py "$o.ini" 'windows:run=C:\VX8.EXE' "vx8:mode=$mode"
K=.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
set -- -f $K=WINDOWS/SYSTEM/KRNL386.EXE -f build/dos/VX8.EXE=VX8.EXE -f "$o.ini=WINDOWS/WIN.INI"
[ -n "$w" ] && set -- "$@" -f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE
INPUTRUN_STAY=1 tests/inputrun.sh -s -b 60 -t 200 "$scr" "$o" -r -c "WIN /3 /B" "$@" > "$o.txt" 2>&1
find "$o/work" -iname 'TK.LOG' -exec cat {} \; | tr -d '\r'
