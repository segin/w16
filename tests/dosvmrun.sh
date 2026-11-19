#!/bin/sh
# dosvmrun.sh NAME SCRIPT [w16]: a session with DOS boxes in 386 Enhanced
# mode (the input script, e.g. tests/scripts/input/dosbox386e.txt: the
# MS-DOS Prompt, Alt+Enter, VER, DIR, EXIT) on MS-DOS 4.00 (INPUTRUN_HDD)
# in DOSBox-X, with Microsoft's WIN386 (or w16's: w16) and Microsoft's
# KRNL386 and the rest. Shots and the work tree in build/tmp/NAME, COM1
# (under w16 the VMM's trace) in build/tmp/NAME.com1. DOSVM_EXTRA: more
# inputrun arguments (e.g. -i w16:vmmtrace=1). DOSVM_NOHDD=1: on DOSBox-X's own
# DOS (WIN's output not redirected: a DOS box would inherit it).
cd "$(dirname "$0")/.."
mkdir -p build/tmp
n=$1 scr=$2 w=${3:-}
o=build/tmp/$n
rm -rf "$o" "$o.com1"
K=.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
set -- -f $K=WINDOWS/SYSTEM/KRNL386.EXE ${DOSVM_EXTRA:-}
[ -n "$w" ] && set -- "$@" -f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE
HDD=1
[ -n "${DOSVM_NOHDD:-}" ] && HDD= && export DROPIN_NOREDIR=1
INPUTRUN_HDD=$HDD INPUTRUN_KEYDELAY=${INPUTRUN_KEYDELAY:-150} DROPIN_MACHINE=${DOSVM_MACHINE:-svga_s3} DROPIN_CONF="[serial]
serial1=file file:$PWD/$o.com1" INPUTRUN_STAY=1 tests/inputrun.sh -b ${DOSVM_BOOT:-90} -t ${DOSVM_TIME:-300} "$scr" "$o" -r -p 386 -c "WIN /3 /B" "$@" > "$o.txt" 2>&1
tail -n 3 "$o/input.log"
