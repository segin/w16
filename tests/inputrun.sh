#!/bin/sh
# Real mouse and keyboard input into a booted Windows 3.1, headless: the
# scratch install comes from tests/dropin.sh (same options for which parts
# are w16's), DOSBox runs on a virtual X display (xvfb-run, never the
# user's desktop) and xdotool sends X input events to its window, so the
# input arrives the way a user's does: through DOSBox's emulated mouse and
# keyboard, MOUSE.DRV and KEYBOARD.DRV at interrupt time, USER's event
# procedures. Used to chase interactive hangs that SNAP.EXE (which posts
# messages) cannot reach.
#
#   usage: tests/inputrun.sh [-b SECS] [-t SECS] SCRIPT OUTDIR [dropin.sh options...]
#     -b SECS  boot time before the script starts (default 25)
#     -t SECS  total run time (default 120)
# SCRIPT lines (blank and ';' lines ignored):
#     home              move the pointer far up and left (it stops at 0,0)
#     move DX DY        relative pointer motion (X pixels; see the shots
#                       for where Windows put it)
#     jiggle N          N quick small pointer motions back and forth (a
#                       stream of mouse interrupts)
#     click [N]         press and release button N (1 left, 3 right)
#     down N / up N     press or release button N
#     key KEYS          xdotool key names, e.g. alt+f Escape Return
#     type TEXT         type text
#     sleep S           wait S seconds (fractions allowed)
#     shot NAME         screenshot OUTDIR/NAME.png
# INPUTRUN_NOLOCK=1 in the environment: autolock=false (no click needed
# for the mouse).
# OUTDIR (new) gets the shots, the scratch drive (c/), DOSBox's log and the
# script's own log (input.log).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
boot=25 total=120
while getopts b:t: opt; do
    case $opt in
    b) boot=$OPTARG ;;
    t) total=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
out=$2; shift 2
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
# the scratch install, prepared by dropin.sh (a 1-second run we discard)
"$root/tests/dropin.sh" -t 1 -o "$out/work" -i w16:watchdog=0 "$@" > /dev/null 2>&1 || true
work=$out/work
[ -f "$work/dosbox.conf" ] || { echo "dropin.sh did not prepare $work (bad options?)" >&2; exit 2; }
rm -f "$work/c/WINDOWS/BOOTLOG.TXT" "$work/c/OUT.TXT"
# INPUTRUN_NOLOCK=1: DOSBox's autolock=false, as an interactive session
# may have it: the mouse moves without a click capturing it first
if [ -n "${INPUTRUN_NOLOCK:-}" ]; then
    sed -i 's/^\[sdl\]$/[sdl]\nautolock=false/' "$work/dosbox.conf"
fi
cat > "$work/run.sh" <<SH
#!/bin/sh
cd "$work"
SDL_AUDIODRIVER=dummy timeout -k 10 $total dosbox -noconsole -conf "$work/dosbox.conf" > "$out/dosbox.log" 2>&1 &
sleep 3
wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
echo "window \$wid" >> "$out/input.log"
xdotool windowactivate --sync "\$wid" 2>/dev/null || xdotool windowfocus "\$wid" 2>/dev/null || true
# the pointer over the window's middle, then lock it in (a click captures
# the mouse in DOSBox)
eval \$(xdotool getwindowgeometry --shell "\$wid")
xdotool mousemove --window "\$wid" \$((WIDTH / 2)) \$((HEIGHT / 2))
sleep $boot
shot() { import -window "\$wid" "$out/\$1.png"; echo "shot \$1" >> "$out/input.log"; }
tr -d '\r' < "$script" | while read -r cmd a b; do
    case "\$cmd" in
    ''|';'*) continue ;;
    home) for i in 1 2 3 4 5 6 7 8; do xdotool mousemove_relative -- -100 -100; sleep 0.05; done ;;
    move) xdotool mousemove_relative -- "\$a" "\$b" ;;
    jiggle) i=0; while [ \$i -lt "\$a" ]; do xdotool mousemove_relative -- 7 5; xdotool mousemove_relative -- -7 -5; i=\$((i + 1)); done ;;
    click) xdotool click "\${a:-1}" ;;
    down) xdotool mousedown "\$a" ;;
    up) xdotool mouseup "\$a" ;;
    key) xdotool key --window "\$wid" \$a \$b ;;
    type) xdotool type --window "\$wid" "\$a \$b" ;;
    sleep) sleep "\$a" ;;
    shot) shot "\$a" ;;
    *) echo "unknown command \$cmd" >> "$out/input.log" ;;
    esac
    echo "\$cmd \$a \$b" >> "$out/input.log"
    sleep 0.2
done
wait
SH
chmod +x "$work/run.sh"
xvfb-run -a -s '-screen 0 1024x768x24' "$work/run.sh" || true
echo "== OUT.TXT"; tr -d '\r' < "$work/c/OUT.TXT" 2>/dev/null || echo "(none)"
echo "== input.log"; cat "$out/input.log"
