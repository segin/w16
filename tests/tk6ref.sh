#!/bin/sh
# Run TK6.EXE (the floating-point probe) on the reference Windows 3.1 in
# DOSBox-X (tests/refrun.sh with REFRUN_X), in Standard and 386 Enhanced
# mode, with a coprocessor and without one (the emulator), and write the
# logs to OUTDIR as TK6-win31-{87,em}-{S,3}.log; the same for TK6N and TK7.
#   usage: tests/tk6ref.sh OUTDIR
# Then: tools/obsref.py OUTDIR/TK6-win31-*.log > tests/apps/tk6ref.h
#       tools/obsref.py OUTDIR/TK6N-win31-*.log > tests/apps/tk6nref.h
#       tools/obsref.py OUTDIR/TK7-win31-*.log > tests/apps/tk7ref.h
# DOSBox-X rather than DOSBox: DOSBox's FPU keeps no exception flags in
# the status word and can not be switched off. Neither raises floating-
# point exceptions (IRQ 13), so with a coprocessor unmasked exceptions are
# not observable there.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
mkdir -p "$out"
REFRUN_X=1
export REFRUN_X
for fpu in true false; do
    tag=87; [ $fpu = true ] || tag=em
    for mode in S 3; do
        for p in TK6 TK6N TK7 TK8; do
            REFRUN_FPU=$fpu "$root/tests/refrun.sh" "$root/build/dos/$p.EXE" /$mode \
                > "$out/$p-win31-$tag-$mode.log" 2>&1 &
        done
    done
done
wait
