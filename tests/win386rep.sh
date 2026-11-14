#!/bin/sh
# win386rep.sh SCRIPT N OUT [extra inputrun -f args...]: SCRIPT N times in parallel with
# w16's WIN386 plus one reference run; per run the pixel differences of each shot
# (shots named as in the reference run). REP_BOTH="-f A=B ...": inputrun
# arguments for the reference run too (e.g. a WIN.INI with run=VX8.EXE);
# REP_K386=FILE: the KRNL386.EXE of the w16 runs (default Microsoft's)
scr=$1 n=$2 o=$3; shift 3
cd "$(dirname "$0")/.."
K=.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
rm -rf "$o"; mkdir -p "$o"
INPUTRUN_STAY=1 tests/inputrun.sh -s -b 60 -t 300 $scr $o/ref -r -c "WIN /3 /B" -f $K=WINDOWS/SYSTEM/KRNL386.EXE ${REP_BOTH:-} > $o/ref.txt 2>&1 &
for i in $(seq 1 "$n"); do
    INPUTRUN_STAY=1 tests/inputrun.sh -s -b 60 -t 300 $scr $o/$i -r -c "WIN /3 /B" -f ${REP_K386:-$K}=WINDOWS/SYSTEM/KRNL386.EXE ${REP_BOTH:-} \
        -f build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE "$@" > $o/$i.txt 2>&1 &
    sleep 1
done
wait
for i in $(seq 1 "$n"); do
    printf 'run %s:' "$i"
    for f in $o/ref/[0-9]*.png; do
        s=$(basename $f .png)
        printf ' %s=%s' "$s" "$(magick compare -metric AE $o/$i/$s.png $f null: 2>&1 | cut -d' ' -f1)"
    done
    echo
done
