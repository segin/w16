#!/bin/sh
# Probe US4's screens (tests/apps/us4.c) on Windows 3.1 and on w16's USER:
# US4 runs as the shell (tests/refrun.sh, Standard mode), saving each
# screen as a BMP; then every pair is compared pixel by pixel.
#   usage: tests/userscreens.sh [-k] OUTDIR
#     -k   keep an existing reference run in OUTDIR/ref (only rerun w16's)
# OUTDIR/ref and OUTDIR/w16 get the BMPs and logs; OUTDIR/N-diff.png the
# differences; the count per screen is printed.
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
keep=
[ "${1:-}" = -k ] && { keep=1; shift; }
out=${1:?usage: tests/userscreens.sh [-k] OUTDIR}
mkdir -p "$out"
out=$(cd "$out" && pwd)
make -C "$root" -s build/dos/US4.EXE build/dos/USER.EXE >/dev/null
if [ -z "$keep" ] || [ ! -d "$out/ref" ]; then
    rm -rf "$out/ref"; mkdir -p "$out/ref"
    REFRUN_OUT="$out/ref" REFRUN_TIMEOUT=150 "$root/tests/refrun.sh" "$root/build/dos/US4.EXE" /S \
        > "$out/ref/log.txt" 2>&1 || true
fi
rm -rf "$out/w16"; mkdir -p "$out/w16"
REFRUN_FILES="$root/build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE" REFRUN_OUT="$out/w16" \
    REFRUN_TIMEOUT=150 "$root/tests/refrun.sh" "$root/build/dos/US4.EXE" /S \
    > "$out/w16/log.txt" 2>&1 || true
for f in "$out"/ref/US4_*.BMP; do
    [ -f "$f" ] || continue
    n=$(basename "$f" .BMP)
    if [ ! -f "$out/w16/$n.BMP" ]; then echo "$n: missing"; continue; fi
    d=$(magick compare -metric AE "$f" "$out/w16/$n.BMP" "$out/$n-diff.png" 2>&1 || true)
    echo "$n: $d"
done
