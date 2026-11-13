#!/bin/sh
# Compare two Cardfile runs of the same SNAP script (tests/cfrun.sh): the
# original's in ORIG and w16's in W16. For each screenshot ORIG/NAME.BMP:
# the number of differing pixels (ImageMagick), with a diff image
# W16/NAME-diff.png; for each card file ORIG/NAME.CRD: same or differs.
#   usage: tests/cfcompare.sh ORIG W16
set -u
orig=$1 w16=$2
for f in "$orig"/*.BMP; do
    [ -f "$f" ] || continue
    n=$(basename "$f" .BMP)
    if [ ! -f "$w16/$n.BMP" ]; then
        echo "$n: missing"
        continue
    fi
    px=$(magick compare -metric AE "$f" "$w16/$n.BMP" "$w16/$n-diff.png" 2>&1 | cut -d' ' -f1)
    echo "$n: $px pixels"
done
for f in "$orig"/*.CRD; do
    [ -f "$f" ] || continue
    n=$(basename "$f")
    if [ ! -f "$w16/$n" ]; then
        echo "$n: missing"
    elif cmp -s "$f" "$w16/$n"; then
        echo "$n: same"
    else
        echo "$n: differs ($(cmp "$f" "$w16/$n" 2>&1 | head -1))"
    fi
done
