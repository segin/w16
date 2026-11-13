#!/bin/sh
# Run GD4 (tests/apps/gd4.c: GDI's heaps, what each object costs, how many
# fit, the GDI-resources percentage) as the shell of Windows 3.1, with
# Microsoft's GDI (-r) or with w16's (the default), and print its TK.LOG.
#
#   usage: tests/gdiheap.sh [-r] [-g GDI.EXE] [-x] [-k] [-m MODE] [-p PROBE.EXE]
#     -r          Microsoft's GDI (the reference)
#     -g FILE     the GDI to drop in (default build/dos/GDI.EXE)
#     -x          [w16] ExtendedGdiHeap=1 in SYSTEM.INI
#     -k          with KRNL286 (REFRUN_KRNL286=1, Microsoft's)
#     -m MODE     /S (default) or /3
#     -p FILE     the probe (default build/dos/GD4.EXE)
# Microsoft's KERNEL, USER, drivers and TOOLHELP are used in every case.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
gdi=$root/build/dos/GDI.EXE probe=$root/build/dos/GD4.EXE
useref= ext= mode=/S
while getopts rg:xkm:p: opt; do
    case $opt in
    r) useref=1 ;;
    g) gdi=$OPTARG ;;
    x) ext=1 ;;
    k) export REFRUN_KRNL286=1 ;;
    m) mode=$OPTARG ;;
    p) probe=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp -r "$ref" "$work/c"
[ -n "$useref" ] || cp "$gdi" "$work/c/WINDOWS/SYSTEM/GDI.EXE"
if [ -n "$ext" ]; then
    printf '[w16]\r\nExtendedGdiHeap=1\r\n' >> "$work/c/WINDOWS/SYSTEM.INI"
fi
REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-600} "$root/tests/refrun.sh" "$probe" "$mode" "$work/c"
