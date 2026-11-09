#!/bin/sh
# Calculator regression against the original (Windows 3.1's CALC.EXE) on
# the reference Windows 3.1, in DOSBox-X with a coprocessor (plain
# DOSBox's coprocessor emulation gets the last bits wrong; set
# CALC_FPU=false for Microsoft's emulator instead).
#
#   screens  every tests/scripts/calc/*.txt SNAP script runs with the
#            original and with build/dos/CALC.EXE; no screenshot may
#            differ by more pixels than tests/scripts/calc.baseline allows
#            (our own icon art; CALC_BASELINE=update rewrites it).
#   results  CA1 (tests/apps/ca1.c) runs every tests/scripts/calcca/*.txt
#            and the random scripts tools/calcgen.py makes (seed CALC_SEED,
#            default 1, CALC_LINES lines per kind, default 400) with both
#            calculators; tools/calccmp.py compares every result (display,
#            clipboard after Copy, indicators). Allowed differences are
#            listed in tests/scripts/calcca.known ("script line-text").
#   help     the WinHelp requests (tests/apps/thelp.c stands in for
#            WINHELP.EXE) must be the same.
#   usage: tests/calcscenarios.sh [screens|results|help ...]   (default: all)
#   output: build/ref/calc-scenarios/<timestamp>/
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
parts=${*:-screens results help}
out=$root/build/ref/calc-scenarios/$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"
status=0
export REFRUN_X=1 REFRUN_FPU=${CALC_FPU:-true}

# run a SNAP or CA1 script with the original (who=orig) or ours (w16)
run_snap() {        # script who dir
    if [ "$2" = orig ]; then
        sed 's/$/\r/' "$1" > "$3/SNAP.TXT"
        extra=
    else
        sed 's/^run CALC.EXE$/run C:\\CALC.EXE/; s/$/\r/' "$1" > "$3/SNAP.TXT"
        extra=$root/build/dos/CALC.EXE
    fi
    REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-300} REFRUN_FILES="$3/SNAP.TXT $extra ${4:-}" REFRUN_OUT="$3" \
        "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$3/log.txt" 2>&1 || true
}

run_ca1() {         # script who dir
    if [ "$2" = orig ]; then
        sed 's/$/\r/' "$1" > "$3/CA.TXT"
        extra=
    else
        sed 's/^run CALC.EXE$/run C:\\CALC.EXE/; s/$/\r/' "$1" > "$3/CA.TXT"
        extra=$root/build/dos/CALC.EXE
    fi
    REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-900} REFRUN_FILES="$3/CA.TXT $extra" REFRUN_OUT="$3" REFRUN_GET=CA.LOG \
        "$root/tests/refrun.sh" "$root/build/dos/CA1.EXE" /S > "$3/log.txt" 2>&1 || true
    tr -d '\r' < "$3/CA.LOG" > "$3/ca.txt" 2>/dev/null || : > "$3/ca.txt"
}

for part in $parts; do
case $part in
screens)
    base=$root/tests/scripts/calc.baseline
    [ -f "$base" ] || : > "$base"
    newbase=$(mktemp)
    for f in "$root"/tests/scripts/calc/*.txt; do
        n=$(basename "$f" .txt)
        for who in orig w16; do
            mkdir -p "$out/screens/$n/$who"
            run_snap "$f" $who "$out/screens/$n/$who" &
        done
        wait
        result=ok shots=
        for s in "$out/screens/$n"/orig/*.BMP; do
            [ -f "$s" ] || continue
            b=$(basename "$s" .BMP)
            [ -f "$out/screens/$n/w16/$b.BMP" ] || { result=FAIL; echo "  $n: $b missing"; continue; }
            px=$(magick compare -metric AE "$s" "$out/screens/$n/w16/$b.BMP" \
                 "$out/screens/$n/$b-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
            shots="$shots $b=$px"
            echo "$n/$b $px" >> "$newbase"
            allowed=$(awk -v k="$n/$b" '$1 == k { print $2 }' "$base")
            if [ -z "$allowed" ]; then
                echo "  $n: $b has no baseline ($px pixels differ)"
                [ "${CALC_BASELINE:-}" = update ] || result=FAIL
            elif [ "$px" -gt "$allowed" ]; then
                result=FAIL; echo "  $n: $b differs by $px pixels, baseline $allowed"
            elif [ "$px" -lt "$allowed" ]; then
                echo "  $n: $b improved: $px pixels (baseline $allowed)"
            fi
        done
        echo "== screens $n: $result (pixels:$shots)"
        [ $result = ok ] || status=1
    done
    if [ "${CALC_BASELINE:-}" = update ]; then
        awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
            | sort > "$base.tmp" && mv "$base.tmp" "$base"
        echo "baseline updated: $base"
    fi
    rm -f "$newbase"
    ;;
results)
    mkdir -p "$out/results"
    python3 -I "$root/tools/calcgen.py" "$out/results/gen" "${CALC_SEED:-1}" "${CALC_LINES:-400}"
    total=0 bad=0
    known=$root/tests/scripts/calcca.known
    for f in "$root"/tests/scripts/calcca/*.txt "$out"/results/gen/*.txt; do
        [ -f "$f" ] || continue
        n=$(basename "$f" .txt)
        for who in orig w16; do
            mkdir -p "$out/results/$n/$who"
            run_ca1 "$f" $who "$out/results/$n/$who" &
        done
        wait
        python3 -I "$root/tools/calccmp.py" "$out/results/$n/orig/ca.txt" "$out/results/$n/w16/ca.txt" \
            -n 1000 > "$out/results/$n/cmp.txt" || true
        c=$(sed -n 's/^compared \([0-9]*\) results, \([0-9]*\) differ$/\1 \2/p' "$out/results/$n/cmp.txt")
        set -- $c
        # differences listed as known are allowed
        allowed=0
        if [ -f "$known" ]; then
            allowed=$(grep -v '^compared\|^logs have' "$out/results/$n/cmp.txt" | cut -c1-34 | sed 's/ *$//' |
                while read -r l; do grep -qxF "$n $l" "$known" && echo x; done | wc -l)
        fi
        total=$((total + ${1:-0})) bad=$((bad + ${2:-0}))
        echo "== results $n: ${1:-0} compared, ${2:-0} differ ($allowed known)"
        grep -q 'logs have' "$out/results/$n/cmp.txt" && { echo "   $(grep 'logs have' "$out/results/$n/cmp.txt")"; status=1; }
        [ "${2:-0}" -le "$allowed" ] || status=1
    done
    echo "== results: $total compared, $bad differ"
    ;;
help)
    mkdir -p "$out/help"
    for who in orig w16; do
        mkdir -p "$out/help/$who"
        run_snap "$root/tests/scripts/calc-help.txt" $who "$out/help/$who" \
            "$root/build/dos/THELP.EXE=WINDOWS/WINHELP.EXE" &
    done
    wait
    for who in orig w16; do
        grep -a '^HELP \(msg 0477\|block\)' "$out/help/$who/log.txt" |
            sed 's/^HELP block [0-9A-F]* /HELP block /' > "$out/help/$who/requests.txt"
    done
    if cmp -s "$out/help/orig/requests.txt" "$out/help/w16/requests.txt"; then
        echo "== help: ok ($(grep -c block "$out/help/orig/requests.txt") requests)"
    else
        echo "== help: DIFFERS"
        diff "$out/help/orig/requests.txt" "$out/help/w16/requests.txt" | sed 's/^/   /'
        status=1
    fi
    ;;
esac
done
echo "output: $out"
exit $status
