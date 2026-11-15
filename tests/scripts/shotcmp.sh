#!/bin/sh
# shotcmp.sh DIR_A DIR_B [OUTDIR]: compare the screenshots two runs of
# tests/worun.sh took under the same names (ImageMagick compare, number of
# differing pixels; 0 = identical). With OUTDIR, difference images go there.
# Prints "name pixels" per screenshot; exit code = number that differ.
a=$1 b=$2 o=${3:-}
[ -n "$o" ] && mkdir -p "$o"
n=0
for f in "$a"/*.png; do
    s=$(basename "$f" .png)
    if [ ! -f "$b/$s.png" ]; then
        echo "$s missing"
        n=$((n + 1))
        continue
    fi
    t=null:
    [ -z "$o" ] || t=$o/$s.png
    d=$(compare -metric AE "$f" "$b/$s.png" "$t" 2>&1 >/dev/null | sed 's/ .*//')
    echo "$s $d"
    [ "$d" = 0 ] || n=$((n + 1))
done
exit $n
