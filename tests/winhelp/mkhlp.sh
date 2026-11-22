#!/bin/sh
# Build one of w16's own test help files with OpenWatcom's help compiler
# whc (open source; $WATCOM/binl64/whc or whc on PATH): tests/winhelp/
# NAME.hpj and its .rtf, with the pictures tests/winhelp/mkpics.py draws,
# compiled in a scratch directory; the result is OUT.
#   usage: tests/winhelp/mkhlp.sh NAME OUT
# Exits 3 when whc is not there (the callers then skip their test).
set -eu
name=$1 out=$2
here=$(cd "$(dirname "$0")" && pwd)
whc=${WATCOM:-/opt/watcom}/binl64/whc
[ -x "$whc" ] || whc=$(command -v whc 2>/dev/null) || { echo "mkhlp: no whc (OpenWatcom's help compiler)" >&2; exit 3; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp "$here/$name.hpj" "$here/$name.rtf" "$work/"
python3 -I "$here/mkpics.py" "$work"
(cd "$work" && "$whc" /q "$name.hpj" > whc.log 2>&1) || true
if [ ! -s "$work/$name.hlp" ] || grep -q . "$work/whc.log"; then
    echo "mkhlp: whc failed on $name.hpj:" >&2
    cat "$work/whc.log" >&2
    exit 1
fi
cp "$work/$name.hlp" "$out"
