#!/bin/sh
# Run TK6.EXE (the floating-point probe) on the reference Windows 3.1 in
# DOSBox-X (tests/refrun.sh with REFRUN_X), in Standard and 386 Enhanced
# mode, with a coprocessor and without one (the emulator), and write the
# logs to OUTDIR as TK6-win31-{87,em}-{S,3}.log; the same for TK6N-TK10.
#   usage: tests/tk6ref.sh OUTDIR
# Then: tools/obsref.py OUTDIR/TK6-win31-*.log > tests/apps/tk6ref.h
#       tools/obsref.py OUTDIR/TK6N-win31-*.log > tests/apps/tk6nref.h
#       tools/obsref.py OUTDIR/TK7-win31-*.log > tests/apps/tk7ref.h
#       (and so on for TK8, TK9, TK10; TK8T1-5 share tk8tref.h; TK67P1-12,
#       the WIN87EM audit probe, share tk67ref.h)
# TK6REF_PROGS: the programs to run (default: all of them). TK67P13, the
# coprocessor-exception part, runs on 86Box only (tests/86box-n4.sh).
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
# one program at a time, its four configurations at once
for p in ${TK6REF_PROGS:-TK6 TK6N TK7 TK8 TK8T1 TK8T2 TK8T3 TK8T4 TK8T5 TK9 TK10 TK67P1 TK67P2 TK67P3 TK67P4 TK67P5 TK67P6 TK67P7 TK67P8 TK67P9 TK67P10 TK67P11 TK67P12}; do
    # TK67P4, 10 and 11 start TK67H
    case $p in TK67P4|TK67P10|TK67P11) REFRUN_FILES=$root/build/dos/TK67H.EXE ;; *) REFRUN_FILES= ;; esac
    export REFRUN_FILES
    for fpu in true false; do
        tag=87; [ $fpu = true ] || tag=em
        for mode in S 3; do
            REFRUN_FPU=$fpu "$root/tests/refrun.sh" "$root/build/dos/$p.EXE" /$mode \
                > "$out/$p-win31-$tag-$mode.log" 2>&1 &
        done
    done
    wait
done
