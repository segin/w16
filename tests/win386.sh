#!/bin/sh
# Boot 386 Enhanced mode with w16's WIN386.EXE (ARCHITECTURE.md 12) and see
# how far it gets, against Windows 3.1's own WIN386.EXE.
#
# Two runs in headless DOSBox-X (on a virtual X display, xvfb-run, never
# the user's desktop), each in a scratch copy of the reference install
# (.tools/ref/win31/cdrive): "WIN /3 /B" with build/win386/WIN386.EXE copied
# over WINDOWS\SYSTEM\WIN386.EXE, and the same with Microsoft's. After SECS
# seconds each takes a screenshot; the two are compared pixel for pixel.
# Microsoft's KRNL386, USER, GDI and drivers run in both.
#
#   usage: tests/win386.sh [-t SECS] [-o DIR] [-n] [-i S:K=V]...
#     -t SECS   time before the screenshot (default 45)
#     -o DIR    where the screenshots, boot logs and the VMM's trace go
#               (default build/win386-<timestamp>)
#     -n        only w16's run (no reference run, no comparison)
#     -i S:K=V  set key K in section S of SYSTEM.INI (repeatable), e.g.
#               w16:vmmtrace=1 for the VMM's detailed trace (COM1)
# Prints the VMM's trace (from COM1), KRNL386's BOOTLOG.TXT summary and the
# comparison; exit status 0 when the screens are identical.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
[ -d "$ref" ] || ref=$(cd "$root/../../.." && pwd)/.tools/ref/win31/cdrive
secs=45 out= norefrun= inis=
while getopts t:o:ni: opt; do
    case $opt in
    t) secs=$OPTARG ;;
    o) out=$OPTARG ;;
    n) norefrun=1 ;;
    i) inis="$inis $OPTARG" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
[ -f "$root/build/win386/WIN386.EXE" ] || { echo "build/win386/WIN386.EXE missing (make win386)" >&2; exit 2; }
[ -n "$out" ] || out=$root/build/win386-$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"

run() {   # run NAME (w16|ms)
    d=$(mktemp -d)
    cp -r "$ref" "$d/c"
    [ "$1" = w16 ] && cp "$root/build/win386/WIN386.EXE" "$d/c/WINDOWS/SYSTEM/WIN386.EXE"
    rm -f "$d/c/WINDOWS/BOOTLOG.TXT"
    for i in $inis; do
        python3 -I "$root/tests/scripts/iniset.py" "$d/c/WINDOWS/SYSTEM.INI" "$i"
    done
    cat > "$d/dosbox.conf" <<CONF
[sdl]
output=surface
[dosbox]
quit warning=false
machine=svga_s3
memsize=16
[cpu]
core=normal
cputype=386
cycles=max
[mixer]
nosound=true
[speaker]
pcspeaker=false
[serial]
serial1=file file:$d/com1.txt
[autoexec]
mount c "$d/c"
c:
cd \\WINDOWS
WIN /3 /B > C:\\OUT.TXT
echo WINEXIT >> C:\\OUT.TXT
exit
CONF
    cat > "$d/shot.sh" <<SH
#!/bin/sh
SDL_AUDIODRIVER=dummy timeout $((secs + 5)) dosbox-x -nogui -fastlaunch -conf "$d/dosbox.conf" >"$d/dosbox.log" 2>&1 &
pid=\$!
sleep 3
# DOSBox-X gone already: it found no display (another Xvfb took ours); retry
kill -0 \$pid 2>/dev/null || exit 7
sleep $((secs - 3))
wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
import -window "\${wid:-root}" "$d/s.png"
wait
SH
    chmod +x "$d/shot.sh"
    # a display number of our own (other tests may run Xvfb at the same
    # time and xvfb-run -a can collide): a free one, and up to 5 tries
    try=0
    while [ $try -lt 5 ]; do
        n=$((300 + ($$ * 7 + try * 131) % 4000))
        while [ -e /tmp/.X$n-lock ] || [ -e /tmp/.X11-unix/X$n ]; do n=$((n + 1)); done
        xvfb-run -n $n -s '-screen 0 1024x768x24' "$d/shot.sh" >>"$d/xvfb.log" 2>&1 || echo "try $try display $n: $?" >>"$d/xvfb.log"
        # DOSBox-X ran unless it found no X display
        [ -s "$d/dosbox.log" ] && ! grep -q "open X11 display" "$d/dosbox.log" && break
        rm -f "$d/s.png" "$d/dosbox.log"
        try=$((try + 1))
        sleep 1
    done
    [ -f "$d/s.png" ] && cp "$d/s.png" "$out/$1.png"
    [ -f "$d/c/WINDOWS/BOOTLOG.TXT" ] && tr -d '\r' < "$d/c/WINDOWS/BOOTLOG.TXT" > "$out/$1-bootlog.txt"
    [ -f "$d/c/OUT.TXT" ] && tr -d '\r' < "$d/c/OUT.TXT" > "$out/$1-out.txt"
    [ -f "$d/com1.txt" ] && cp "$d/com1.txt" "$out/$1-com1.txt"
    [ -f "$d/xvfb.log" ] && cp "$d/xvfb.log" "$out/$1-xvfb.log"
    [ -n "${WIN386_KEEP:-}" ] && echo "kept $d" || rm -rf "$d"
}

run w16
[ -n "$norefrun" ] || run ms

echo "== w16's WIN386: the VMM's trace (COM1)"
[ -f "$out/w16-com1.txt" ] && tr -d '\r' < "$out/w16-com1.txt" | cut -c1-300 | head -40
echo "== w16's WIN386: standard output"
[ -f "$out/w16-out.txt" ] && cat "$out/w16-out.txt" || echo "(none: still running at the time limit)"
for n in w16 ms; do
    [ -f "$out/$n-bootlog.txt" ] || continue
    echo "== $n: BOOTLOG.TXT: $(grep -c . "$out/$n-bootlog.txt") lines, last: $(tail -1 "$out/$n-bootlog.txt")"
done
status=1
if [ -z "$norefrun" ] && [ -f "$out/w16.png" ] && [ -f "$out/ms.png" ]; then
    diffpx=$(compare -metric AE "$out/w16.png" "$out/ms.png" "$out/diff.png" 2>&1 || true)
    echo "== screenshots: $out/w16.png $out/ms.png; differing pixels: $diffpx"
    [ "${diffpx%% *}" = 0 ] && status=0      # compare prints "0 (0)"
else
    echo "== screenshot: $out/w16.png"
fi
exit $status
