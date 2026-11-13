#!/bin/sh
# w16's VGA.DRV with Microsoft's resources instead of its own, for tests
# only: screens taken with it differ from Microsoft's driver's only where
# the drawing code does, not where w16's art differs from Microsoft's
# (drivers/vga/README.md). Relinks the driver's objects (build/vga/link.lnk,
# from make) with a .RES made from the reference install's VGA.DRV
# (tools/ne2res.py). The output holds Microsoft's resources: it belongs in
# a scratch or build directory, never in the repository or the install
# tree.
#
#   usage: tests/vgamsres.sh OUT.DRV
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
outf=$1
msvga=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/VGA.DRV
W=${WATCOM:-/opt/watcom}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
python3 "$root/tools/ne2res.py" "$msvga" "$tmp/ms.res"
case $outf in /*) ;; *) outf=$PWD/$outf ;; esac
(cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W \
    wlink @build/vga/link.lnk name "$outf" option resource="$tmp/ms.res" \
    option map="$tmp/vga.map") > "$tmp/link.txt" 2>&1 || { cat "$tmp/link.txt"; exit 1; }
python3 "$root/tools/nenoname.py" "$outf" 501-502 > /dev/null
python3 "$root/tools/nemark.py" "$outf" 3.10 > /dev/null
