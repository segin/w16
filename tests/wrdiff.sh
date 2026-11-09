#!/bin/sh
# The decoded difference between the original's and w16's saved .WRI files
# of a writescenarios.sh run: tests/wrdiff.sh <results dir> [scenario...]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$1
shift
names=${*:-$(cd "$dir" && ls -d WR* )}
for n in $names; do
    for f in "$dir/$n"/orig/*.WRI; do
        [ -f "$f" ] || continue
        b=$(basename "$f")
        [ -f "$dir/$n/w16/$b" ] || { echo "== $n/$b: not saved by w16"; continue; }
        python3 -I "$root/tools/wri.py" dump "$f" > "$dir/$n/$b.orig.txt"
        python3 -I "$root/tools/wri.py" dump "$dir/$n/w16/$b" > "$dir/$n/$b.w16.txt"
        echo "== $n/$b"
        diff "$dir/$n/$b.orig.txt" "$dir/$n/$b.w16.txt" || true
    done
done
