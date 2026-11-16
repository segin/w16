#!/bin/sh
# DDEML in w16's install tree alone (build/w16, tests/dropin.sh -T, under
# its KRNL286 or, with -3, its KRNL386): DM.EXE (tests/ddeml/dm.c) as the
# shell runs tests/ddeml/scripts/tree.dmt (a DDEML server and client in
# two copies, then a DDEML client driving the tree's Program Manager)
# with w16's DDEML or, with LIB ms, Microsoft's (copied into the scratch
# tree for the test only). OUTDIR/os.log is the log (tests/olesvr/osspy.c;
# under w16's USER its hooks may not see DDE messages, so it holds the
# probe's calls and callbacks).
#   usage: tests/ddeml/ddtree.sh OUTDIR [ms|w16] [286|386]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 lib=${2:-w16} k=${3:-286}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/work" "$out/os.log"
sed 's/$/\r/' "$root/tests/ddeml/scripts/tree.dmt" > "$out/DM.TXT"
extra=
if [ "$lib" = ms ]; then extra="-f $ref/SYSTEM/DDEML.DLL=W16/SYSTEM/DDEML.DLL"; fi
if [ "$k" = 386 ]; then extra="$extra -3"; fi
# shellcheck disable=SC2086
"$root/tests/dropin.sh" -T "$root/build/w16" -t "${DDTREE_TIMEOUT:-150}" -o "$out/work" \
    -f "$dos/DM.EXE=W16/DM.EXE" -f "$dos/DM.EXE=DM.EXE" -f "$dos/OSSPY.DLL=W16/OSSPY.DLL" \
    -f "$out/DM.TXT=DM.TXT" -i 'boot:shell=DM.EXE' $extra > "$out/dropin.txt" 2>&1 || true
if [ -f "$out/work/c/OS.LOG" ]; then
    tr -d '\r' < "$out/work/c/OS.LOG" > "$out/os.log"
else
    echo "no OS.LOG ($out/dropin.txt)" >&2
    exit 3
fi
