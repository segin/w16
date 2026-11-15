#!/bin/sh
# Compare w16's VGA.3GR with Microsoft's (docs/notes/grabber-handover.md
# "VGA.3GR", drivers/grabber/README.md): each scenario of tests/grabber3/
# runs under Windows 3.1 in 386 Enhanced mode (everything Microsoft's but
# the grabber) once with Microsoft's VGA.3GR and once with w16's
# (tests/grabber3.sh), then every screenshot is compared pixel by pixel
# (tests/scripts/imgdiff.py) and the clipboard records of CB1 line by line.
#
#   usage: tests/grabber3cmp.sh [-j JOBS] [-n RUNS] [-k] -o DIR [SCENARIO ...]
#     -j JOBS   runs at once (default 2; more only on a build host)
#     -n RUNS   pairs per scenario (default 1): intermittent differences
#     -k        keep the run directories of passing pairs (default: only
#               the screenshots and logs of failing ones are kept)
#     SCENARIO  names in tests/grabber3/ (default: all)
# The caret blinks: WIN.INI CursorBlinkRate is set to 30000 ms in both runs
# so that it is in the same state when the screenshots are taken.
# Prints one line per comparison ("ok", "FAIL") and a summary; exit status
# 1 when anything differs.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
jobs=2 runs=1 out= keep=
while getopts j:n:o:k opt; do
    case $opt in
    j) jobs=$OPTARG ;;
    n) runs=$OPTARG ;;
    o) out=$OPTARG ;;
    k) keep=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -n "$out" ] || { echo "need -o DIR" >&2; exit 2; }
[ -e "$out" ] && { echo "$out exists" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
scen="$*"
[ -n "$scen" ] || scen=$(ls "$root/tests/grabber3" | sed -n 's/\.txt$//p')
make -C "$root" -s build/dos/VGA.3GR build/dos/GR3.3GR build/dos/GV1.COM \
    build/dos/CB1.EXE build/dos/GT3.COM >/dev/null
# the run list: scenario, which grabber, run number
list=$out/list
: > "$list"
for s in $scen; do
    i=1
    while [ $i -le "$runs" ]; do
        echo "$s ms $i" >> "$list"
        echo "$s w16 $i" >> "$list"
        i=$((i + 1))
    done
done
one() {   # SCENARIO ms|w16 N
    g=
    [ "$2" = w16 ] && g="-g $root/build/dos/VGA.3GR"
    # shellcheck disable=SC2086
    "$root/tests/grabber3.sh" $g -x "$root/tests/grabber3/$1.txt" \
        -n windows:CursorBlinkRate=30000 -o "$out/$1-$2-$3" > "$out/$1-$2-$3.log" 2>&1 || true
}
# run them, $jobs at a time
n=0
while read -r s g i; do
    one "$s" "$g" "$i" &
    n=$((n + 1))
    if [ $n -ge "$jobs" ]; then wait; n=0; fi
done < "$list"
wait
fail=0 total=0
while read -r s g i; do
    [ "$g" = ms ] || continue
    a=$out/$s-ms-$i b=$out/$s-w16-$i
    ok=1
    for f in "$a"/*.png; do
        [ -e "$f" ] || continue
        case $f in *-diff.png) continue ;; esac
        n=$(basename "$f" .png)
        total=$((total + 1))
        r=$(python3 "$root/tests/scripts/imgdiff.py" "$f" "$b/$n.png" "$b/$n-diff.png") || true
        if [ "$r" = same ]; then echo "ok   $s/$i $n"; else echo "FAIL $s/$i $n: $r"; fail=$((fail + 1)); ok=; fi
    done
    total=$((total + 1))
    # CB1's records without its timing lines
    grep -v 'later)$' "$a/cb.log" > "$a/cb.cmp" 2>/dev/null || true
    grep -v 'later)$' "$b/cb.log" > "$b/cb.cmp" 2>/dev/null || true
    if [ -s "$a/cb.cmp" ] && cmp -s "$a/cb.cmp" "$b/cb.cmp"; then
        echo "ok   $s/$i clipboard"
    else
        echo "FAIL $s/$i clipboard"
        diff "$a/cb.cmp" "$b/cb.cmp" | head -20 || true
        fail=$((fail + 1)); ok=
    fi
    if [ -n "$ok" ] && [ -z "$keep" ]; then
        rm -rf "$a/c" "$b/c"
    fi
done < "$list"
echo "grabber3cmp: $((total - fail)) of $total same"
[ $fail -eq 0 ]
