#!/bin/sh
# WINHELP's .HLP reader against every help file at hand (part of make
# test; ARCHITECTURE.md 15.10): build/hlpdump -q must walk each one
# through, every topic and every item, with nothing left over. The files:
# OpenWatcom's own (open source, $WATCOM/binw/*.hlp: 41 files made by its
# help compiler whc, among them the 2-3 MB clib.hlp and cmix.hlp) and the
# reference Windows 3.1 install's (made by Microsoft's compiler). Neither
# is in the repository; whichever is missing is skipped.
#   usage: tests/winhelp/hlpcheck.sh HLPDUMP
set -u
dump=$1
root=$(cd "$(dirname "$0")/../.." && pwd)
n=0 bad=0
for f in "${WATCOM:-/opt/watcom}"/binw/*.hlp "$root"/.tools/ref/win31/cdrive/WINDOWS/*.HLP; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    "$dump" -q "$f" || { echo "hlpcheck: FAIL $f"; bad=$((bad + 1)); }
done
if [ $n = 0 ]; then
    echo "hlpcheck: no help files (OpenWatcom's binw or the reference install): skipped"
    exit 0
fi
echo "hlpcheck: $n help files, $bad failed"
[ $bad = 0 ]
