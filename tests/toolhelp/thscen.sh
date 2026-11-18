#!/bin/sh
# TOOLHELP.DLL scenarios (ARCHITECTURE.md 22): the probe THP.EXE
# (tests/toolhelp/thp.c) on each system with each TOOLHELP.DLL
# (tests/toolhelp/thrun.sh), one boot per group of sections (a section
# that ends Windows costs only its group), the groups' logs joined into
# OUTDIR/LIB-SYSTEM/thp.log, then the logs compared (tests/toolhelp/
# thcmp.py) as listed with -c.
#   usage: tests/toolhelp/thscen.sh [-o OUTDIR] [-s "SYSTEMS"] [-l "LIBS"]
#                                   [-g "GROUPS"] [-j JOBS] [-c "A:B ..."] [-r RUNS]
# SYSTEMS: thrun.sh's (default "ref286 ref386 ref3 tree286 tree386");
# LIBS: ms w16 (default both); GROUPS: section letters per boot (default
# "e g l mtc s i r n x k"); -c pairs of runs to compare, LIB-SYSTEM:
# LIB-SYSTEM (default: each system's ms against its w16, and w16-tree286
# against ms-ref286, w16-tree386 against ms-ref386). -r "LIB-SYSTEM ...":
# these runs only, instead of every SYSTEM with every LIB.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$root/build/thscen-$(date +%Y%m%d-%H%M%S)
systems="ref286 ref386 ref3 tree286 tree386" libs="ms w16" runs=
groups="e g l mtc s i r n x k d" jobs=${THSCEN_JOBS:-12} pairs=
while getopts o:s:l:g:j:c:r: opt; do
    case $opt in
    o) out=$OPTARG ;; s) systems=$OPTARG ;; l) libs=$OPTARG ;;
    g) groups=$OPTARG ;; j) jobs=$OPTARG ;; c) pairs=$OPTARG ;; r) runs=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
if [ -z "$runs" ]; then
    for s in $systems; do for l in $libs; do runs="$runs $l-$s"; done; done
fi
mkdir -p "$out"
echo "thscen: $out"
for r in $runs; do
    l=${r%%-*} s=${r#*-}
    mkdir -p "$out/$r"
    for g in $groups; do
        while [ "$(jobs -p | wc -l)" -ge "$jobs" ]; do sleep 1; done
        THRUN_ARG=$g "$root/tests/toolhelp/thrun.sh" "$out/$r/$g" "$l" "$s" > "$out/$r/$g.txt" 2>&1 &
    done
done
wait
# a group that did not finish runs once more (logged in OUTDIR/retries.txt:
# a group that needs it every time is a failure to look into)
for r in $runs; do
    l=${r%%-*} s=${r#*-}
    for g in $groups; do
        grep -q '^THP end' "$out/$r/$g/thp.log" 2>/dev/null && continue
        echo "$r $g" >> "$out/retries.txt"
        while [ "$(jobs -p | wc -l)" -ge "$jobs" ]; do sleep 1; done
        THRUN_ARG=$g "$root/tests/toolhelp/thrun.sh" "$out/$r/$g" "$l" "$s" > "$out/$r/$g.txt" 2>&1 &
    done
done
wait
for r in $runs; do
    : > "$out/$r/thp.log"
    for g in $groups; do
        if [ -s "$out/$r/$g/thp.log" ]; then
            grep -v '^THP start\|^TOOLHELP loaded\|^exports\|^THP end' "$out/$r/$g/thp.log" >> "$out/$r/thp.log" || true
            grep -q '^THP end' "$out/$r/$g/thp.log" || echo "!! group $g did not finish" >> "$out/$r/thp.log"
        else
            echo "!! group $g: no log" >> "$out/$r/thp.log"
        fi
    done
done
if [ -z "$pairs" ]; then
    for s in $systems; do
        case $libs in *ms*w16*|*w16*ms*) pairs="$pairs ms-$s:w16-$s" ;; esac
    done
fi
status=0
for p in $pairs; do
    a=${p%%:*} b=${p#*:}
    [ -s "$out/$a/thp.log" ] && [ -s "$out/$b/thp.log" ] || continue
    python3 "$root/tests/toolhelp/thcmp.py" "$out/$a/thp.log" "$out/$b/thp.log" \
        > "$out/cmp-$a-$b.txt" || status=1
    echo "== $a : $b: $(tail -1 "$out/cmp-$a-$b.txt")"
done
exit $status
