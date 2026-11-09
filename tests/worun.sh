#!/bin/sh
# Run Windows 3.1 Standard mode headless with a scripted keyboard, for the
# WINOLDAP work (ARCHITECTURE.md 9.7): DOS programs started from Windows.
#
# A scratch copy of the reference install (.tools/ref/win31/cdrive) runs
# "WIN /S" in DOSBox-X on a virtual X display (Xvfb; never the user's
# desktop). Real key presses go to the emulator window (xdotool), so the
# DOS session, Alt+Tab and Ctrl+Esc are driven as a user would.
#
#   usage: tests/worun.sh [options] -o DIR
#     -o DIR     output directory (new): screenshots, logs, the C: drive
#     -m FILE    WINOLDAP.MOD to use (default: Microsoft's)
#     -k FILE    KRNL286.EXE to drop in (KRNL386.EXE is deleted), e.g.
#                build/dos/KRNL286.EXE; -K: Microsoft's KRNL286
#     -f SRC=DST copy SRC to C:\DST (repeatable); DST "-" deletes C:\SRC
#     -i S:K=V   SYSTEM.INI key (repeatable); -n S:K=V the same in WIN.INI
#     -c CMD     DOS commands before WIN, ";"-separated (default none)
#     -w CMD     the Windows command line (default "WIN /S"; its output is not
#                redirected: the DOS sessions inherit WIN's standard handles)
#     -x FILE    the input script (default: just wait 40 s, screenshot)
#     -e EMU     dosbox-x (default) or dosbox
#     -l         DOSBox-X INT 21h and file logs (DIR/dosbox.log)
#     -t SECS    hard time limit (default 300)
# The script, one command per line ('#' comments):
#     sleep S          wait S seconds (decimals allowed)
#     key K...         xdotool key names: alt+Tab ctrl+Escape Return ...
#     type TEXT        type TEXT
#     shot NAME        screenshot DIR/NAME.png
# When the script ends the emulator is stopped. Afterwards DIR/c holds the
# drive (C:\WO.LOG is what the WO probes wrote), DIR/out.txt WIN's output.
set -eu
unset WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
out= mod= krnl= msk286= files= inis= winis= pre= wcmd='WIN /S' script= emu=dosbox-x logs= tmax=300
while getopts o:m:k:Kf:i:n:c:w:x:e:lt: opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) mod=$OPTARG ;;
    k) krnl=$OPTARG ;;
    K) msk286=1 ;;
    f) files="$files
$OPTARG" ;;
    i) inis="$inis
$OPTARG" ;;
    n) winis="$winis
$OPTARG" ;;
    c) pre=$OPTARG ;;
    w) wcmd=$OPTARG ;;
    x) script=$OPTARG ;;
    e) emu=$OPTARG ;;
    l) logs=1 ;;
    t) tmax=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -n "$out" ] || { echo "need -o DIR" >&2; exit 2; }
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
cp -r "$ref" "$out/c"
chmod -R u+w "$out/c"
sys=$out/c/WINDOWS/SYSTEM
if [ -n "$mod" ]; then cp "$mod" "$sys/WINOLDAP.MOD"; fi
if [ -n "$krnl" ]; then cp "$krnl" "$sys/KRNL286.EXE"; rm -f "$sys/KRNL386.EXE"; fi
if [ -n "$msk286" ]; then rm -f "$sys/KRNL386.EXE"; fi
printf '%s\n' "$files" | while IFS= read -r f; do
    [ -n "$f" ] || continue
    src=${f%%=*} dst=${f#*=}
    if [ "$dst" = - ]; then rm -f "$out/c/$src"; else mkdir -p "$(dirname "$out/c/$dst")"; cp "$src" "$out/c/$dst"; fi
done
printf '%s\n' "$inis" | while IFS= read -r f; do
    [ -z "$f" ] || python3 "$root/tests/scripts/iniset.py" "$out/c/WINDOWS/SYSTEM.INI" "$f"
done
printf '%s\n' "$winis" | while IFS= read -r f; do
    [ -z "$f" ] || python3 "$root/tests/scripts/iniset.py" "$out/c/WINDOWS/WIN.INI" "$f"
done
if [ -n "$script" ]; then cp "$script" "$out/script.txt"; else printf 'sleep 40\nshot end\n' > "$out/script.txt"; fi
{
    printf '[sdl]\noutput=surface\nshowmenu=false\n[dosbox]\nmemsize=16\n'
    if [ "$emu" = dosbox-x ]; then printf 'quit warning=false\nmachine=svga_s3\n'; fi
    printf '[cpu]\ncore=normal\ncputype=386\ncycles=max\n'
    printf '[mixer]\nnosound=true\n[speaker]\npcspeaker=false\n'
    if [ -n "$logs" ]; then printf '[log]\nlogfile=%s/dosbox.log\nint21=debug\nfileio=debug\nexec=debug\nfiles=debug\n' "$out"; fi
    printf '[autoexec]\nmount c "%s/c"\nc:\n' "$out"
    printf '%s\n' "$pre" | tr ';' '\n'
    printf 'cd \\WINDOWS\n%s\necho WINEXIT > C:\\OUT.TXT\nexit\n' "$wcmd"
} > "$out/dosbox.conf"
case $emu in
dosbox-x) run="dosbox-x -fastlaunch -conf $out/dosbox.conf${logs:+ -log-int21 -log-fileio}" ;;
*) run="dosbox -noconsole -conf $out/dosbox.conf" ;;
esac
cat > "$out/drive.sh" <<SH
#!/bin/sh
for t in 1 2 3 4 5; do       # the emulator sometimes cannot open the display
    SDL_AUDIODRIVER=dummy timeout $tmax $run >"$out/emu.txt" 2>&1 &
    pid=\$!
    sleep 2
    kill -0 \$pid 2>/dev/null && break
    grep -q "Couldn't open X11 display" "$out/emu.txt" || break
    sleep \$t
done
wid=
for i in 1 2 3 4 5 6 7 8 9 10; do
    wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
    [ -n "\$wid" ] && break
    sleep 1
done
while IFS= read -r line; do
    kill -0 \$pid 2>/dev/null || break      # the emulator has gone
    set -- \$line
    [ \$# -gt 0 ] || continue
    case \$1 in
    \#*) ;;
    sleep) sleep \$2 ;;
    key) shift; xdotool windowfocus --sync "\$wid" 2>/dev/null; xdotool key --delay 120 --window "\$wid" "\$@" ;;
    type) shift; xdotool windowfocus --sync "\$wid" 2>/dev/null; xdotool type --delay 120 --window "\$wid" "\$*" ;;
    keydown|keyup) c=\$1; shift; xdotool windowfocus --sync "\$wid" 2>/dev/null; xdotool \$c --window "\$wid" "\$@" ;;
    shot) import -window "\${wid:-root}" "$out/\$2.png" 2>/dev/null || echo "shot \$2 failed" ;;
    esac
done < "$out/script.txt"
kill \$pid 2>/dev/null
wait
SH
chmod +x "$out/drive.sh"
for try in 1 2 3 4 5; do   # parallel runs: pick a free display number
    n=$(( $(od -An -N2 -tu2 /dev/urandom) % 800 + 200 ))
    while [ -e /tmp/.X$n-lock ] || [ -e /tmp/.X11-unix/X$n ]; do n=$((n + 1)); done
    # our own Xvfb (no xvfb-run: its authority file setup failed now and
    # then with several runs at once); local connections only
    Xvfb :$n -screen 0 1024x768x24 -nolisten tcp > "$out/xvfb.txt" 2>&1 &
    xpid=$!
    i=0
    while [ ! -e /tmp/.X11-unix/X$n ] && [ $i -lt 50 ] && kill -0 $xpid 2>/dev/null; do
        sleep 0.2; i=$((i + 1))
    done
    DISPLAY=:$n "$out/drive.sh" || true
    kill $xpid 2>/dev/null; wait $xpid 2>/dev/null || true
    grep -q "Couldn't open X11 display\|X connection to .* broken" "$out/emu.txt" 2>/dev/null || break
    echo "worun: X display :$n failed, retrying" >&2
    rm -f "$out"/*.png; sleep $try
done
[ ! -f "$out/c/OUT.TXT" ] || tr -d '\r' < "$out/c/OUT.TXT" > "$out/out.txt"
[ ! -f "$out/c/WO.LOG" ] || tr -d '\r' < "$out/c/WO.LOG" > "$out/wo.log"
ls "$out"
