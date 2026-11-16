#!/bin/sh
# OLE in w16's install tree alone (build/w16, dropin.sh -T, under its
# KRNL386): OS2.EXE as the shell runs tests/olesvr/scripts/tree.os2t
# (an OLE client through w16's OLECLI embedding a Paintbrush picture), with
# w16's OLESVR or, with SVR=ms, Microsoft's. Paintbrush and REG.DAT are the
# reference's, for the test only (w16 has neither). OUTDIR/os.log is the
# log (tests/olesvr/osspy.c; under w16's USER its hooks see no DDE, so it
# holds OS2's calls, callbacks and OLESVR's windows).
#   usage: tests/olesvr/ostree.sh OUTDIR [ms|w16]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 svr=${2:-w16}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
dos=$root/build/dos
mkdir -p "$out"
rm -rf "$out/work" "$out/os.log"
sed 's/$/\r/' "$root/tests/olesvr/scripts/tree.os2t" > "$out/OS2.TXT"
extra=
if [ "$svr" = ms ]; then extra="-f $ref/SYSTEM/OLESVR.DLL=W16/SYSTEM/OLESVR.DLL"; fi
# shellcheck disable=SC2086
"$root/tests/dropin.sh" -T "$root/build/w16" -3 -t "${OSTREE_TIMEOUT:-150}" -o "$out/work" \
    -f "$ref/PBRUSH.EXE=W16/PBRUSH.EXE" -f "$ref/PBRUSH.DLL=W16/PBRUSH.DLL" -f "$ref/REG.DAT=W16/REG.DAT" \
    -f "$dos/OS2.EXE=W16/OS2.EXE" -f "$dos/OSSPY.DLL=W16/OSSPY.DLL" -f "$out/OS2.TXT=OS2.TXT" \
    -i 'boot:shell=OS2.EXE' $extra > "$out/dropin.txt" 2>&1 || true
if [ -f "$out/work/c/OS.LOG" ]; then
    tr -d '\r' < "$out/work/c/OS.LOG" > "$out/os.log"
else
    echo "no OS.LOG ($out/dropin.txt)" >&2
    exit 3
fi
