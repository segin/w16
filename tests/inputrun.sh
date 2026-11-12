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
#     center            put the X pointer over the middle of DOSBox's window
#                       (with INPUTRUN_NOLOCK=1 DOSBox only sees motion while
#                       the X pointer is over its window: after "home" it may
#                       have left it, and further motion is lost)
#     move DX DY        relative pointer motion (X pixels; see the shots
#                       for where Windows put it)
#     jiggle N          N quick small pointer motions back and forth (a
#                       stream of mouse interrupts)
#     click [N]         press and release button N (1 left, 3 right)
#     down N / up N     press or release button N
#     key KEYS          xdotool key names, e.g. alt+f Escape Return
#     type TEXT         type text
#     sleep S           wait S seconds (fractions allowed)
#     waitpixel X Y RRGGBB MAX     wait until the pixel at X,Y has that
#     waitnotpixel X Y RRGGBB MAX  colour (has not), at most MAX seconds
#     markcolor RRGGBB  note where that colour's pixels are now
#     waitmoved RRGGBB MIN MAX  wait until two shots in a row have MIN
#                       pixels of it elsewhere (another window's active
#                       caption), at most MAX seconds
#     waitcount RRGGBB X0 Y0 X1 Y1 MIN MAX  wait until two shots in a row
#                       have MIN pixels of that colour outside the rectangle
#     settle MAX        wait until the screen holds still (two shots 0.7 s
#                       apart equal), at most MAX seconds: a started program
#                       has drawn its window (on a loaded machine "sleep"
#                       can be too short and the keys go to another window)
#     shot NAME         screenshot OUTDIR/NAME.png
# With dropin.sh's -p CPU among the options the run is in DOSBox-X.
# INPUTRUN_NOLOCK=1 in the environment: autolock=false (no click needed
# for the mouse). INPUTRUN_STAY=1: DOSBox stays at the DOS prompt after
# Windows ends.
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
# INPUTRUN_STAY=1: DOSBox stays at the DOS prompt when Windows ends (the
# autoexec's final exit left out), so a script can see the prompt
if [ -n "${INPUTRUN_STAY:-}" ]; then
    sed -i '/^exit$/d' "$work/dosbox.conf"
fi
if [ -n "${INPUTRUN_NOLOCK:-}" ]; then
    sed -i 's/^\[sdl\]$/[sdl]\nautolock=false/' "$work/dosbox.conf"
fi
# INPUTRUN_EARLY=1: the script's -b counts from when DOSBox's window
# appears, not from 3 seconds after DOSBox starts
# INPUTRUN_CAPTURE=1: DOSBox's own screenshots (key ctrl+F5) go to
# OUTDIR/capture: DOSBox's image of the emulated screen, not of the X
# window (a stale X window shows only in the shots)
if [ -n "${INPUTRUN_CAPTURE:-}" ]; then
    mkdir -p "$out/capture"
    printf '[dosbox]\ncaptures=%s\n' "$out/capture" >> "$work/dosbox.conf"
fi
# dropin.sh -p CPU prepared a DOSBox-X configuration ("quit warning" is
# DOSBox-X's): run DOSBox-X then
emu="dosbox -noconsole"
grep -q '^quit warning' "$work/dosbox.conf" && emu="dosbox-x -nogui -fastlaunch"
cat > "$work/run.sh" <<SH
#!/bin/sh
cd "$work"
SDL_AUDIODRIVER=dummy timeout -k 10 $total $emu -conf "$work/dosbox.conf" > "$out/dosbox.log" 2>&1 &
if [ -n "${INPUTRUN_EARLY:-}" ]; then
    # as soon as the window is there (-b then counts from that)
    i=0; while [ \$i -lt 100 ] && ! xdotool search --name DOSBox > /dev/null 2>&1; do sleep 0.05; i=\$((i + 1)); done
else
    sleep 3
fi
wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
echo "window \$wid" >> "$out/input.log"
xdotool windowactivate --sync "\$wid" 2>/dev/null || xdotool windowfocus "\$wid" 2>/dev/null || true
# the pointer over the window's middle, then lock it in (a click captures
# the mouse in DOSBox)
eval \$(xdotool getwindowgeometry --shell "\$wid")
xdotool mousemove --window "\$wid" \$((WIDTH / 2)) \$((HEIGHT / 2))
sleep $boot
shot() { timeout 30 import -window "\$wid" "$out/\$1.png"; echo "shot \$1" >> "$out/input.log"; }
# settle MAX: wait until the screen holds still (two screenshots 0.7 s
# apart the same), at most MAX seconds
settle() {
    t0=\$(date +%s)
    timeout 30 import -window "\$wid" "$out/.settle-a.png"
    while :; do
        sleep 0.7
        timeout 30 import -window "\$wid" "$out/.settle-b.png"
        n=\$(magick compare -metric AE "$out/.settle-a.png" "$out/.settle-b.png" null: 2>&1)
        if [ "\${n%% *}" = 0 ]; then
            echo "settled after \$((\$(date +%s) - t0)) s" >> "$out/input.log"; break
        fi
        if [ \$((\$(date +%s) - t0)) -ge "\$1" ]; then
            echo "not settled in \$1 s" >> "$out/input.log"; break
        fi
        mv "$out/.settle-b.png" "$out/.settle-a.png"
    done
    rm -f "$out/.settle-a.png" "$out/.settle-b.png"
}
# waitpixel X Y RRGGBB MAX / waitnotpixel X Y RRGGBB MAX: until the pixel
# at X,Y of a screenshot is (is not) that colour, at most MAX seconds
waitpix() {
    want=\$1 x=\$2 y=\$3 c=\$4 max=\$5 t0=\$(date +%s)
    while :; do
        timeout 30 import -window "\$wid" "$out/.wait.png"
        got=\$(python3 "$root/tests/scripts/shotpix.py" pixel "$out/.wait.png" "\$x" "\$y")
        if { [ "\$want" = is ] && [ "\$got" = "\$c" ]; } || { [ "\$want" = not ] && [ "\$got" != "\$c" ]; }; then
            echo "pixel \$x,\$y \$got after \$((\$(date +%s) - t0)) s" >> "$out/input.log"; break
        fi
        if [ \$((\$(date +%s) - t0)) -ge "\$max" ]; then
            echo "pixel \$x,\$y still \$got after \$max s" >> "$out/input.log"; break
        fi
        sleep 0.5
    done
    rm -f "$out/.wait.png"
}
# markcolor RRGGBB: the box of that colour's pixels now; waitmoved RRGGBB
# MIN MAX: until two screenshots in a row have at least MIN pixels of it
# in another box (an active caption elsewhere: the program started from a
# dialog is active), at most MAX seconds
markcolor() {
    timeout 30 import -window "\$wid" "$out/.wait.png"
    mark=\$(python3 "$root/tests/scripts/shotpix.py" bbox "$out/.wait.png" "\$1")
    mark=\${mark#* }
    echo "mark \$1 \$mark" >> "$out/input.log"
    rm -f "$out/.wait.png"
}
waitmoved() {
    c=\$1 min=\$2 max=\$3 t0=\$(date +%s) ok=0
    while :; do
        timeout 30 import -window "\$wid" "$out/.wait.png"
        r=\$(python3 "$root/tests/scripts/shotpix.py" bbox "$out/.wait.png" "\$c")
        n=\${r%% *} box=\${r#* }
        if [ "\${n:-0}" -ge "\$min" ] && [ "\$box" != "\$mark" ]; then ok=\$((ok + 1)); else ok=0; fi
        if [ \$ok -ge 2 ]; then
            echo "moved: \$r after \$((\$(date +%s) - t0)) s" >> "$out/input.log"; break
        fi
        if [ \$((\$(date +%s) - t0)) -ge "\$max" ]; then
            echo "not moved: \$r after \$max s" >> "$out/input.log"; break
        fi
        sleep 0.5
    done
    rm -f "$out/.wait.png"
}
# waitcount RRGGBB X0 Y0 X1 Y1 MIN MAX: until two screenshots in a row have
# at least MIN pixels of that colour outside the rectangle (a window's
# active caption that is not Program Manager's), at most MAX seconds
waitcount() {
    c=\$1 x0=\$2 y0=\$3 x1=\$4 y1=\$5 min=\$6 max=\$7 t0=\$(date +%s) ok=0
    while :; do
        timeout 30 import -window "\$wid" "$out/.wait.png"
        n=\$(python3 "$root/tests/scripts/shotpix.py" count "$out/.wait.png" "\$c" "\$x0" "\$y0" "\$x1" "\$y1")
        if [ "\${n:-0}" -ge "\$min" ]; then ok=\$((ok + 1)); else ok=0; fi
        if [ \$ok -ge 2 ]; then
            echo "count \$n after \$((\$(date +%s) - t0)) s" >> "$out/input.log"; break
        fi
        if [ \$((\$(date +%s) - t0)) -ge "\$max" ]; then
            echo "count still \$n after \$max s" >> "$out/input.log"; break
        fi
        sleep 0.5
    done
    rm -f "$out/.wait.png"
}
tr -d '\r' < "$script" | while read -r cmd a b; do
    case "\$cmd" in
    ''|';'*) continue ;;
    home) for i in 1 2 3 4 5 6 7 8; do xdotool mousemove_relative -- -100 -100; sleep 0.05; done ;;
    center) xdotool mousemove --window "\$wid" \$((WIDTH / 2)) \$((HEIGHT / 2)) ;;
    move) xdotool mousemove_relative -- "\$a" "\$b" ;;
    jiggle) i=0; while [ \$i -lt "\$a" ]; do xdotool mousemove_relative -- 7 5; xdotool mousemove_relative -- -7 -5; i=\$((i + 1)); done ;;
    click) xdotool click "\${a:-1}" ;;
    down) xdotool mousedown "\$a" ;;
    up) xdotool mouseup "\$a" ;;
    key) xdotool key --window "\$wid" \$a \$b ;;
    type) xdotool type --window "\$wid" "\$a \$b" ;;
    sleep) sleep "\$a" ;;
    shot) shot "\$a" ;;
    settle) settle "\$a" ;;
    waitpixel) waitpix is \$a \$b ;;
    waitnotpixel) waitpix not \$a \$b ;;
    waitcount) waitcount \$a \$b ;;
    markcolor) markcolor \$a ;;
    waitmoved) waitmoved \$a \$b ;;
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
