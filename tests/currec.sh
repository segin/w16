#!/bin/sh
# The display driver's cursor calls under real mouse motion: CURREC.DRV
# (tests/gdi/currec.asm, make vgaprobes) in front of the driver under test
# (renamed VGAREAL, carrying its resources, as tests/vgaprobe.sh -l does),
# CR1 (tests/apps/cr1.c) as the shell recording for SECS seconds while
# tests/inputrun.sh plays SCRIPT; C:\CR1.LOG (and C:\CR1.BIN with -F) to
# OUTDIR. Mouse trails (drivers/vga/README.md).
#
#   usage: tests/currec.sh [-s STACK] [-F] [-t SECS] [-p] SCRIPT OUTDIR [dropin.sh options...]
#     -s ms     all of Microsoft's Windows 3.1 (default)
#     -s vga    Microsoft's 3.1 with w16's VGA.DRV (build/dos/VGA.DRV)
#     -s tree   w16's install tree alone (build/w16 as C:\W16), KRNL386
#   w16's driver gets Microsoft's resources (tests/vgamsres.sh): the same
#   arrow on every stack, which tools/cr1stat.py counts in the frames
#     -F        frames too: CR1 keeps the screen's rows 100-299 at each
#               timer message when they changed (tools/vg9png.py)
#     -t SECS   CR1's recording time (default 20)
#     -p        Program Manager stays the shell (CR1 started by WIN.INI
#               run=, as a program without a window): the pointer then
#               crosses its windows
#   Environment: INPUTRUN_NOLOCK=1 as tests/inputrun.sh (the mouse not
#   captured). WIN.INI: MouseTrails=7, MouseSpeed=0.
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
stack=ms frames= secs=20 pm=
while getopts s:Ft:p opt; do
    case $opt in
    s) stack=$OPTARG ;;
    F) frames=" frames" ;;
    t) secs=$OPTARG ;;
    p) pm=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
script=$1 out=$2
shift 2
extra="$*"
[ -e "$out" ] && { echo "$out exists" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
W=${WATCOM:-/opt/watcom}
case $stack in
ms)   target=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/VGA.DRV sys=WINDOWS/SYSTEM; set -- -r ;;
vga)  "$root/tests/vgamsres.sh" "$out/msres.drv" > /dev/null
      target=$out/msres.drv sys=WINDOWS/SYSTEM; set -- -r ;;
tree) "$root/tests/vgamsres.sh" "$out/msres.drv" > /dev/null
      target=$out/msres.drv sys=W16/SYSTEM; set -- -T "$root/build/w16" -3 ;;
*) echo "unknown stack $stack" >&2; exit 2 ;;
esac
cp "$target" "$out/VGAREAL.DLL"
python3 "$root/tools/nerename.py" "$out/VGAREAL.DLL" --module VGAREAL > /dev/null
python3 "$root/tools/ne2res.py" "$target" "$out/drv.res"
(cd "$root" && PATH=$W/binl64:$PATH WATCOM=$W wlink @build/gdi/currec.lnk name "$out/CURREC.DRV" \
    libpath $W/lib286/win option resource="$out/drv.res" \
    file { build/gdi/currec.o build/gdi/crstubs.o }) > "$out/link.txt"
printf '%s%s' "$secs" "$frames" > "$out/CR1.IN"
if [ -n "$pm" ]; then start='-i WIN.INI:windows:run=C:\CR1.EXE'; else start='-i boot:shell=C:\CR1.EXE'; fi
INPUTRUN_FULL=1 "$root/tests/inputrun.sh" -b 30 -t $((secs + 70)) "$script" "$out/run" "$@" \
    -f "$out/CURREC.DRV=$sys/CURREC.DRV" -f "$out/VGAREAL.DLL=$sys/VGAREAL.DLL" \
    -f "$root/build/dos/CR1.EXE=CR1.EXE" -f "$out/CR1.IN=CR1.IN" \
    -i boot:display.drv=CURREC.DRV $start \
    -i WIN.INI:windows:MouseTrails=7 -i WIN.INI:windows:MouseSpeed=0 $extra > "$out/run.txt" 2>&1 || true
for f in CR1.LOG CR1.BIN; do
    [ -f "$out/run/work/c/$f" ] && cp "$out/run/work/c/$f" "$out/$f"
done
[ -f "$out/CR1.LOG" ] && tr -d '\r' < "$out/CR1.LOG" > "$out/cr1.txt" && head -1 "$out/cr1.txt"
