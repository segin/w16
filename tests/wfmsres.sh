#!/bin/sh
# w16's WINFILE.EXE with Microsoft's strip of small pictures (bitmap 100)
# and icons (200, 201, 202, 204) instead of its own, for tests only (ARCHITECTURE.md 3, test-only
# combinations): screens taken with it differ from Microsoft's File Manager
# only where the drawing code does, not where w16's art differs. Compiles
# w16's winfile.rc with the reference install's bitmap 100 in place of
# w16's strip (tools/ne2bmp.py) and relinks File Manager's objects (from
# make). The output holds Microsoft's resources: it belongs in a scratch
# or build directory, never in the repository or the install tree.
#
#   usage: tests/wfmsres.sh OUT.EXE
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
outf=$1
mswf=$root/.tools/ref/win31/cdrive/WINDOWS/WINFILE.EXE
W=${WATCOM:-/opt/watcom}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# Microsoft's bitmap under w16's file name, found first by the resource
# compiler
python3 "$root/tools/ne2bmp.py" "$mswf" 100 "$tmp/wfstrip.bmp"
# and its icons: the program, a minimised directory window, a minimised
# Search Results window
python3 "$root/tools/ne2ico.py" "$mswf" 200 "$tmp/winfile.ico"
python3 "$root/tools/ne2ico.py" "$mswf" 201 "$tmp/tree.ico"
python3 "$root/tools/ne2ico.py" "$mswf" 202 "$tmp/dir.ico"
python3 "$root/tools/ne2ico.py" "$mswf" 204 "$tmp/treedir.ico"
(cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W wrc -q -r -bt=windows -i="$tmp" -i="$W/h/win" \
    -i=shell/winfile -i=build/shell/winfile -fo="$tmp/wf.res" shell/winfile/winfile.rc) > "$tmp/rc.txt" 2>&1 ||
    { cat "$tmp/rc.txt"; exit 1; }
case $outf in /*) ;; *) outf=$PWD/$outf ;; esac
# the objects make links (Makefile WF_C)
objs="build/shell/start.o build/shell/winfile/dosfn.o"
for c in $(sed -n 's/^WF_C *:= *//p' "$root/Makefile"); do objs="$objs build/shell/winfile/$c.o"; done
(cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W \
    wlink @shell/winfile/winfile.lnk name "$outf" libpath "$W/lib286/win" \
    option resource="$tmp/wf.res" option map="$tmp/wf.map" file { $objs }) \
    > "$tmp/link.txt" 2>&1 || { cat "$tmp/link.txt"; exit 1; }
python3 "$root/tools/nemark.py" "$outf" 3.10 > /dev/null
