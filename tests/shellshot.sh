#!/bin/sh
# Screenshot the reference Windows 3.1 with a given shell, after SNAP
# (started from WIN.INI load=) has run a script. For screens SNAP cannot
# capture itself, such as system-modal boxes, which stop every other task:
# the picture is taken from outside, in DOSBox-X on a virtual X display
# (xvfb-run), never the user's desktop.
#   usage: tests/shellshot.sh SHELL SCRIPT OUT.PNG [seconds] [extra files...]
# SHELL is the SYSTEM.INI shell= value, e.g. PROGMAN.EXE (the original) or
# C:\PROGMAN.EXE (copy ours in as an extra file).
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
shell=$1 script=$2 out=$3 secs=${4:-30}
shift 3; [ $# -gt 0 ] && shift
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp -r "$root/.tools/ref/win31/cdrive" "$work/c"
cp "$root/build/dos/SNAP.EXE" "$work/c/"
sed 's/$/\r/' "$script" > "$work/c/SNAP.TXT"
for f in "$@"; do cp "$f" "$work/c/$(basename "$f" | tr a-z A-Z)"; done
sed -i "s/^shell=.*/shell=$(printf '%s' "$shell" | sed 's/\\/\\\\/g')\r/" "$work/c/WINDOWS/SYSTEM.INI"
sed -i 's/^load=.*/load=C:\\SNAP.EXE\r/' "$work/c/WINDOWS/WIN.INI"
cat > "$work/x.conf" <<CONF
[dosbox]
quit warning=false
machine=svga_s3
memsize=16
[cpu]
cputype=386
cycles=max
[mixer]
nosound=true
[autoexec]
mount c "$work/c"
c:
cd \\WINDOWS
WIN /S
CONF
cat > "$work/shot.sh" <<SH
#!/bin/sh
SDL_AUDIODRIVER=dummy timeout $((secs + 10)) dosbox-x -conf "$work/x.conf" -nogui -fastlaunch >/dev/null 2>&1 &
sleep $secs
import -window root "$work/s.png"
SH
chmod +x "$work/shot.sh"
xvfb-run -a -s '-screen 0 1024x768x24' "$work/shot.sh"
magick "$work/s.png" -crop 640x480+0+0 +repage "$out" 2>/dev/null || cp "$work/s.png" "$out"
