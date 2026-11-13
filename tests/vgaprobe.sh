#!/bin/sh
# Run Windows 3.1 with a display driver under test: Microsoft's VGA.DRV or
# w16's (drivers/vga), optionally behind VGREC, the values-only recording
# stand-in (make vgaprobes), optionally with a probe program as the shell.
# A front end for tests/dropin.sh (Microsoft's KERNEL, -r, unless -k).
#
#   usage: tests/vgaprobe.sh [options] OUTDIR
#     -w         w16's VGA.DRV (build/dos/VGA.DRV) instead of Microsoft's
#     -D FILE    that file as the driver under test
#     -m         with -w/-D: link Microsoft's VGA.DRV resources into a scratch
#                copy of the driver under test instead of its own (the
#                pixels of the art are then the same on both sides, so
#                screens compare the drawing code; never kept)
#     -l         VGREC in front of the driver (renamed VGAREAL): C:\GD.LOG
#                to OUTDIR/GD.LOG (each call, its values, the bytes it popped)
#     -p FILE    a probe program as the shell (C:\PROBE.EXE); its TK.LOG
#                (if any) to OUTDIR/TK.LOG
#     -k         w16's KRNL286 instead of Microsoft's KERNEL
#     -t SECS    run time limit (default 60)
#     -s         a screenshot when Windows has settled: OUTDIR/screen.png
#     -g FILE    copy C:\FILE back to OUTDIR as well (repeatable)
#     -W FILE    end the run when C:\FILE exists (tests/dropin.sh -W)
#     -f SRC=DST, -i S:K=V, -c CMD  passed to tests/dropin.sh
# Environment: as tests/dropin.sh (DROPIN_SHOTS etc.).
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
drv= msres= log= probe= ourk= secs=60 shot= extra= gets=
while getopts wD:mlp:kt:sf:i:c:g:W: opt; do
    case $opt in
    w) drv=$root/build/dos/VGA.DRV ;;
    D) drv=$OPTARG ;;
    m) msres=1 ;;
    l) log=1 ;;
    p) probe=$OPTARG ;;
    k) ourk=1 ;;
    t) secs=$OPTARG ;;
    s) shot=1 ;;
    f) extra="$extra -f $OPTARG" ;;
    i) extra="$extra -i $OPTARG" ;;
    c) extra="$extra -c $OPTARG" ;;
    g) gets="$gets $OPTARG" ;;
    W) extra="$extra -W $OPTARG" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
out=$1
mkdir -p "$out"
msvga=$ref/WINDOWS/SYSTEM/VGA.DRV
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
W=${WATCOM:-/opt/watcom}
target=$msvga
if [ -n "$drv" ]; then
    target=$drv
    if [ -n "$msres" ]; then
        # the driver under test with Microsoft's resources: relink its
        # objects (build/vga/vga.lnk lists them) with a .RES made from
        # Microsoft's file (tools/ne2res.py)
        python3 "$root/tools/ne2res.py" "$msvga" "$tmp/ms.res"
        (cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W \
            wlink @build/vga/link.lnk name "$tmp/VGA.DRV" option resource="$tmp/ms.res" \
            option map="$tmp/vga.map") > "$tmp/link.txt" 2>&1 || { cat "$tmp/link.txt"; exit 1; }
        python3 "$root/tools/nemark.py" "$tmp/VGA.DRV" 3.10 > /dev/null
        target=$tmp/VGA.DRV
    fi
fi
files=
if [ -n "$log" ]; then
    cp "$target" "$tmp/VGAREAL.DLL"
    python3 "$root/tools/nerename.py" "$tmp/VGAREAL.DLL" --module VGAREAL > /dev/null
    # USER and GDI load the display driver's resources from the module
    # DISPLAY: VGREC carries the resources of the driver it stands for
    python3 "$root/tools/ne2res.py" "$target" "$tmp/drv.res"
    (cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W \
        wlink @build/gdi/gd1.lnk name "$tmp/VGREC.DRV" libpath $W/lib286/win \
        option resource="$tmp/drv.res" \
        file { build/gdi/gdlog.o build/gdi/gd1stubs.o build/gdi/vgreclog.o }) > /dev/null
    files="-f $tmp/VGREC.DRV=WINDOWS/SYSTEM/VGREC.DRV -f $tmp/VGAREAL.DLL=WINDOWS/SYSTEM/VGAREAL.DLL -i boot:display.drv=VGREC.DRV"
else
    [ "$target" = "$msvga" ] || files="-f $target=WINDOWS/SYSTEM/VGA.DRV"
fi
[ -z "$probe" ] || files="$files -f $probe=PROBE.EXE -i boot:shell=C:\\PROBE.EXE"
args=
[ -n "$ourk" ] || args=-r
[ -z "$shot" ] || args="$args -s $out/screen.png"
# shellcheck disable=SC2086
"$root/tests/dropin.sh" $args -t "$secs" -i w16:watchdog=0 $files $extra -o "$tmp/run" > "$out/run.txt" 2>&1 || true
for f in $gets; do
    [ -f "$tmp/run/c/$f" ] && cp "$tmp/run/c/$f" "$out/$f"
done
for f in GD.LOG TK.LOG WINDOWS/TK.LOG W16DBG.LOG WINDOWS/BOOTLOG.TXT; do
    [ -f "$tmp/run/c/$f" ] && tr -d '\r' < "$tmp/run/c/$f" > "$out/$(basename "$f")"
done
tail -6 "$out/run.txt"
