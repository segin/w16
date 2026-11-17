#!/bin/sh
# LZEXPAND in w16's install tree alone (build/w16, tests/dropin.sh -T,
# under its KRNL286 or, with 386, its KRNL386): the probe LZP.EXE
# (tests/lzexpand/lzp.c) as the shell, with w16's LZEXPAND.DLL or, with
# LIB ms, Microsoft's (copied into the scratch tree for the test only).
# The probe imports VER.DLL (its VerInstallFile section): the tree's own,
# w16's (ARCHITECTURE.md 20). OUTDIR/lzp.log is the log.
#   usage: tests/lzexpand/lztree.sh OUTDIR [ms|w16] [286|386]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 lib=${2:-w16} k=${3:-286}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/work" "$out/in" "$out/SYS" "$out/lzp.log"
python3 "$root/tests/lzexpand/mkinputs.py" "$out/in"
: > "$out/KEEP"
set -- -f "$dos/LZP.EXE=W16/LZP.EXE" -f "$dos/LZP.EXE=LZP.EXE" \
    -f "$out/SYS/SYSFILE.TX_=W16/SYSTEM/SYSFILE.TX_" -f "$out/KEEP=LZOUT/KEEP"
for f in $(cd "$out/in" && find . -type f | sed 's|^\./||'); do
    set -- "$@" -f "$out/in/$f=LZ/$f"
done
if [ "$lib" = ms ]; then set -- "$@" -f "$ref/SYSTEM/LZEXPAND.DLL=W16/SYSTEM/LZEXPAND.DLL"; fi
if [ "$k" = 386 ]; then set -- "$@" -3; fi
"$root/tests/dropin.sh" -T "$root/build/w16" -t "${LZTREE_TIMEOUT:-300}" -o "$out/work" \
    -i 'boot:shell=LZP.EXE' "$@" > "$out/dropin.txt" 2>&1 || true
if [ -f "$out/work/c/LZP.LOG" ]; then
    tr -d '\r' < "$out/work/c/LZP.LOG" > "$out/lzp.log"
else
    echo "no LZP.LOG ($out/dropin.txt)" >&2
    exit 3
fi
