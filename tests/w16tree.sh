#!/bin/sh
# Boot build/w16 (make install-tree) as a whole: every file of the tree is
# laid over a scratch copy of the reference Windows 3.1 install (WIN.COM
# and PROGMAN.EXE into WINDOWS\, SYSTEM\* into WINDOWS\SYSTEM\, the README
# left out), KRNL386.EXE is removed, and "WIN /S /B" runs in headless
# DOSBox (tests/dropin.sh). Then the reference itself (Microsoft's files,
# KRNL286) the same way. Both screens are saved and compared.
#   usage: tests/w16tree.sh [-t SECS] [-o DIR]
# Passes when the w16 tree's boot completes (BOOTLOG.TXT reaches
# INITDONE=Final USER); prints how many pixels differ from the reference
# (w16's PROGMAN.EXE and fonts are not pixel-identical to Microsoft's:
# tests/pmscenarios.sh and tests/fonts.sh track those differences).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
tree=$root/build/w16
secs=60 out=$root/build/w16tree
while getopts t:o: opt; do
    case $opt in
    t) secs=$OPTARG ;;
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -f "$tree/WIN.COM" ] || { echo "no $tree (make install-tree)" >&2; exit 2; }
mkdir -p "$out"
set --
for f in "$tree"/*; do
    case $(basename "$f") in README.TXT|SYSTEM) continue ;; esac
    set -- "$@" -f "$f=WINDOWS/$(basename "$f")"
done
for f in "$tree"/SYSTEM/*; do
    set -- "$@" -f "$f=WINDOWS/SYSTEM/$(basename "$f")"
done
"$root/tests/dropin.sh" -k "$tree/SYSTEM/KRNL286.EXE" -X "$tree/SYSTEM/DOSX.EXE" \
    -i w16:watchdog=0 -t "$secs" -s "$out/w16.png" "$@" > "$out/w16.log" 2>&1 || true
"$root/tests/dropin.sh" -r -t "$secs" -s "$out/ref.png" > "$out/ref.log" 2>&1 || true
grep -a '^loaded:\|^USER:\|^shell:\|^boot:' "$out/w16.log" | cut -c1-200
px=$(compare -metric AE "$out/ref.png" "$out/w16.png" "$out/diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1 || true)
echo "pixels differing from Windows 3.1's own files: $px ($out/w16.png, $out/ref.png, $out/diff.png)"
if grep -q '^boot: complete' "$out/w16.log"; then
    echo "w16 tree: boots"
else
    echo "w16 tree: FAILED to boot"; exit 1
fi
