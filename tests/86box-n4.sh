#!/bin/sh
# 86box-n4.sh - the hardware-coprocessor test of WIN87EM (TESTING.md,
# "hardware FPU"): TK67P13 (tests/apps/tk67.c: __fpMath 7 and FISTP with
# invalid unmasked, an unmasked zero divide and IRQ 13) under the
# reference Windows 3.1 on 86Box's 386DX with a 387 (tests/86box.sh),
# once with Microsoft's WIN87EM.DLL and once with w16's
# (build/dos/WIN87EM.DLL), in Standard and 386 Enhanced mode; the four
# logs go to OUTDIR (default build/86box-n4) and w16's observations of
# __fpMath 7 and FISTP (h7, hfist) are compared with Microsoft's. Exit
# status 0 when they agree. The zero divide's lines (hdiv) are shown, not
# compared: with Microsoft's WIN87EM the program writes nothing after it
# (no handler call; ARCHITECTURE.md 9.2), w16's calls the handler.
#   usage: tests/86box-n4.sh [OUTDIR]
# Optional: needs 86Box (tests/86box.sh); not part of make test. The four
# runs go at once (about two minutes).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build/86box-n4}
mkdir -p "$out"
make -C "$root" build/dos/WIN87EM.DLL build/dos/TK67P13.EXE >/dev/null || exit 2
# (Microsoft's run writes nothing after the zero divide: B86_IDLE ends it)
B86_IDLE=40
export B86_IDLE
for m in S 3; do
    B86_MODE=/$m "$root/tests/86box.sh" "$root/build/dos/TK67P13.EXE" \
        > "$out/ms-$m.log" 2>&1 &
    B86_MODE=/$m B86_WIN87EM=$root/build/dos/WIN87EM.DLL \
        "$root/tests/86box.sh" "$root/build/dos/TK67P13.EXE" > "$out/w16-$m.log" 2>&1 &
done
wait
rc=0
for m in S 3; do
    grep -q 'hw.div0' "$out/ms-$m.log" || { echo "86box-n4: no complete run with Microsoft's WIN87EM ($m)"; rc=2; continue; }
    grep '^OBS [^~]' "$out/ms-$m.log" | grep -v '\.hdiv\.\|\.hnote' > "$out/ms-$m.obs"
    grep '^OBS [^~]' "$out/w16-$m.log" | grep -v '\.hdiv\.\|\.hnote' > "$out/w16-$m.obs"
    if diff "$out/ms-$m.obs" "$out/w16-$m.obs" > "$out/diff-$m.txt"; then
        echo "86box-n4 /$m: w16's WIN87EM as Microsoft's"
    else
        echo "86box-n4 /$m: differences (Microsoft's <, w16's >):"
        cat "$out/diff-$m.txt"
        rc=1
    fi
    echo "86box-n4 /$m: the zero divide: Microsoft's: $(sed -n '/hw.div0/,$p' "$out/ms-$m.log" | sed 1d | tr '\n' ' ')"
    echo "86box-n4 /$m: the zero divide: w16's: $(sed -n '/hw.div0/,$p' "$out/w16-$m.log" | sed 1d | tr '\n' ' ')"
done
exit $rc
