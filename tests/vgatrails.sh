#!/bin/sh
# Mouse trails (drivers/vga/README.md, "Mouse trails"): VG9
# (tests/apps/vg9.c, make vgaprobes) as the shell of Windows 3.1, with
# Microsoft's VGA.DRV and with w16's, and the two compared.
#
#   usage: tests/vgatrails.sh [-p MODE] [-D DRV] [-i S:K=V] OUTDIR
#     -p MODE  VG9's mode (C:\VG9.IN): det (default; the comparison: every
#              case's operations with interrupts off, so the frames do not
#              depend on timing), boot, escape, more, late, or full (every
#              observation phase; timings differ between runs, see vg9.c)
#     -D DRV   the w16 side's driver (default build/dos/VGA.DRV); it gets
#              Microsoft's resources (tests/vgaprobe.sh -m), so that the
#              cursors' art is the same on both sides
#     -i S:K=V passed to tests/dropin.sh for both runs (an INI setting, e.g.
#              WIN.INI:windows:MouseTrails=5 for -p boot)
#   OUTDIR/ms and OUTDIR/w16 get each run's TK.LOG and VG9.BIN; the logs
#   (without the frames' times) and every frame must be identical: exit
#   status 0, else 1 (tools/vg9png.py --cmp; differing frames as images in
#   OUTDIR/diff). 3.1's driver sometimes writes MouseTrails with a leading
#   space (README.md: UNVERIFIED when): "ini= 7" is compared as "ini=7".
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
mode=det drv=$root/build/dos/VGA.DRV ini=
while getopts p:D:i: opt; do
    case $opt in
    p) mode=$OPTARG ;;
    D) drv=$OPTARG ;;
    i) ini="$ini -i $OPTARG" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
out=$1
mkdir -p "$out"
out=$(cd "$out" && pwd)
[ "$mode" = full ] && mode=all
printf '%s' "$mode" > "$out/VG9.IN"
for side in ms w16; do
    rm -rf "${out:?}/$side"
    if [ $side = ms ]; then set --; else set -- -D "$drv" -m; fi
    # shellcheck disable=SC2086
    "$root/tests/vgaprobe.sh" "$@" $ini -p "$root/build/dos/VG9.EXE" -f "$out/VG9.IN=VG9.IN" \
        -g VG9.BIN -t 400 "$out/$side" > "$out/$side.txt" 2>&1 || true
done
r=0
for side in ms w16; do
    grep -q '^end' "$out/$side/TK.LOG" 2>/dev/null || { echo "vgatrails: $side: VG9 did not finish ($out/$side)"; r=1; }
done
[ $r = 0 ] || exit 1
sed 's/ ms=[0-9]*//; s/ini= /ini=/' "$out/ms/TK.LOG" > "$out/ms.log"
sed 's/ ms=[0-9]*//; s/ini= /ini=/' "$out/w16/TK.LOG" > "$out/w16.log"
if ! diff "$out/ms.log" "$out/w16.log" > "$out/log.diff"; then
    echo "vgatrails: the logs differ ($out/log.diff)"
    r=1
fi
python3 "$root/tools/vg9png.py" --cmp "$out/ms/VG9.BIN" "$out/w16/VG9.BIN" "$out/diff" > "$out/frames.txt" || r=1
grep -v '^ok' "$out/frames.txt" || true
[ $r = 0 ] && echo "vgatrails: identical ($(grep -c '^ok' "$out/frames.txt") cases, $(grep -c '^frame' "$out/ms/TK.LOG") frames)"
exit $r
