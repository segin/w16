#!/bin/sh
# VER.DLL in w16's install tree alone (build/w16, tests/dropin.sh -T,
# under its KRNL286 or, with 386, its KRNL386): the probe VRP.EXE
# (tests/ver/vrp.c) as the shell, with w16's VER.DLL or, with LIB ms,
# Microsoft's (copied into the scratch tree for the test only). The
# tree is put at C:\WINDOWS (tests/dropin.sh's DROPIN_TREEDIR), so that
# the log compares with the reference install's as it is: the probe's
# files (tests/ver/mkinputs.py) go to C:\VT, C:\WINDOWS and D:\VT;
# C:\VT\PATH is on the PATH. OUTDIR/vrp.log is the log.
# The boot test (case ver, parts tree286 and tree386) compares LIB ms's
# log with tests/ver/vrun.sh's of 3.1, and LIB w16's with LIB ms's.
#   usage: tests/ver/vtree.sh OUTDIR [ms|w16] [286|386]
# VTREE_ARG: the sections to run (C:\VRP.ARG; vrp.c's header).
# VTREE_FILES: more files for the tree, HOSTPATH=PATH (as dropin's -f).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 lib=${2:-w16} k=${3:-286}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/work" "$out/in" "$out/vrp.log" "$out/VRP.ARG"
python3 "$root/tests/ver/mkinputs.py" "$out/in"
set -- -f "$dos/VRP.EXE=VRP.EXE"
for f in $(cd "$out/in/c" && find . -type f | sed 's|^\./||'); do
    set -- "$@" -f "$out/in/c/$f=$f"
done
if [ -n "${VTREE_ARG:-}" ]; then
    printf '%s' "$VTREE_ARG" > "$out/VRP.ARG"
    set -- "$@" -f "$out/VRP.ARG=VRP.ARG"
fi
if [ "$lib" = ms ]; then set -- "$@" -f "$ref/SYSTEM/VER.DLL=WINDOWS/SYSTEM/VER.DLL"; fi
# VTREE_FILES: more HOSTPATH=PATH files (e.g. Microsoft's LZEXPAND.DLL)
for f in ${VTREE_FILES:-}; do set -- "$@" -f "$f"; done
if [ "$k" = 386 ]; then set -- "$@" -3; fi
# the probe started as C:\VRP.EXE, as tests/refrun.sh starts it (its
# directory is one OpenFile searches)
DROPIN_TREEDIR=WINDOWS DROPIN_DDRIVE=$out/in/d "$root/tests/dropin.sh" -T "$root/build/w16" \
    -t "${VTREE_TIMEOUT:-400}" -o "$out/work" -c 'SET PATH=C:\VT\PATH;Z:\
WIN /S /B' -i 'boot:shell=C:\VRP.EXE' "$@" > "$out/dropin.txt" 2>&1 || true
if [ -f "$out/work/c/VRP.LOG" ]; then
    tr -d '\r' < "$out/work/c/VRP.LOG" > "$out/vrp.log"
else
    echo "no VRP.LOG ($out/dropin.txt)" >&2
    exit 3
fi
