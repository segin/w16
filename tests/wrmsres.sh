#!/bin/sh
# w16's WRITE.EXE with Microsoft's ruler bitmaps (resources 102 and 202)
# instead of its own, for tests only (ARCHITECTURE.md 3, test-only
# combinations): screens taken with it differ from Microsoft's Write only
# where the drawing code does, not where w16's art differs. Compiles
# w16's write.rc with the reference install's bitmaps 102 and 202 in
# place of w16's art (tools/ne2bmp.py) and relinks Write's objects (from
# make). The output holds Microsoft's resources: it belongs in a scratch
# or build directory, never in the repository or the install tree.
#
#   usage: tests/wrmsres.sh OUT.EXE
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
outf=$1
mswr=$root/.tools/ref/win31/cdrive/WINDOWS/WRITE.EXE
W=${WATCOM:-/opt/watcom}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# Microsoft's bitmaps under w16's file names, found first by the resource
# compiler
python3 "$root/tools/ne2bmp.py" "$mswr" 102 "$tmp/rbuttons.bmp"
python3 "$root/tools/ne2bmp.py" "$mswr" 202 "$tmp/rmarks.bmp"
(cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W wrc -q -r -bt=windows -i="$tmp" -i="$W/h/win" \
    -i=shell/write -i=build/shell/write -fo="$tmp/wr.res" shell/write/write.rc) > "$tmp/rc.txt" 2>&1 ||
    { cat "$tmp/rc.txt"; exit 1; }
case $outf in /*) ;; *) outf=$PWD/$outf ;; esac
# the objects make links (Makefile WR_C)
objs="build/shell/start.o build/shell/write/i4.o"
for c in $(sed -n 's/^WR_C *:= *//p' "$root/Makefile"); do objs="$objs build/shell/write/$c.o"; done
(cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W \
    wlink @shell/write/write.lnk name "$outf" libpath "$W/lib286/win" \
    option resource="$tmp/wr.res" option map="$tmp/wr.map" file { $objs }) \
    > "$tmp/link.txt" 2>&1 || { cat "$tmp/link.txt"; exit 1; }
python3 "$root/tools/nemark.py" "$outf" 3.10 > /dev/null
