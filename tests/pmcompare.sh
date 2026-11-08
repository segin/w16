#!/bin/sh
# Compare w16's PROGMAN.EXE with the original Program Manager on the
# reference Windows 3.1: run the same SNAP script against both, then count
# differing pixels per screenshot (ImageMagick) and write diff images.
#   usage: tests/pmcompare.sh [outdir]      (default build/ref/cmp)
# Black-box observation only: screenshots of the running programs.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build/ref/cmp}
rm -rf "$out"; mkdir -p "$out/orig" "$out/w16"
script() {
    printf '%s\r\n' "run $1" 'wait 3000' 'snap C:\MAIN.BMP' \
        'menu f' 'wait 300' 'snap C:\MFILE.BMP' 'key 1B' 'key 1B' \
        'menu o' 'wait 300' 'snap C:\MOPT.BMP' 'key 1B' 'key 1B' \
        'menu w' 'wait 300' 'snap C:\MWIN.BMP' 'key 1B' 'key 1B' \
        'menu h' 'wait 300' 'snap C:\MHELP.BMP' 'key 1B' 'key 1B' \
        'menu f' 'char r' 'wait 500' 'snap C:\DRUN.BMP' 'key 1B' 'wait 300' \
        'key 28' 'wait 300' 'snap C:\SELNEXT.BMP'
}
script PROGMAN.EXE > "$out/orig/SNAP.TXT"
script 'C:\PROGMAN.EXE' > "$out/w16/SNAP.TXT"
REFRUN_FILES="$out/orig/SNAP.TXT" REFRUN_OUT="$out/orig" \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$out/orig/log.txt"
REFRUN_FILES="$out/w16/SNAP.TXT $root/build/dos/PROGMAN.EXE" REFRUN_OUT="$out/w16" \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$out/w16/log.txt"
for f in "$out"/orig/*.BMP; do
    n=$(basename "$f" .BMP)
    [ -f "$out/w16/$n.BMP" ] || { echo "$n: missing in w16 run"; continue; }
    d=$(magick compare -metric AE "$f" "$out/w16/$n.BMP" "$out/$n-diff.png" 2>&1 || true)
    echo "$n: $d pixels differ"
done
