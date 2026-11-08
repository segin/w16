#!/bin/sh
# Run a DOS command line headlessly in DOSBox with HDPMI16 loaded, and print
# what it wrote to stdout.
#   usage: tests/dosrun.sh <dir-mounted-as-C> <dos command line...>
# Output (including the exit code) is written to C:\OUT.TXT inside DOS and
# echoed here. Exits with the DOS program's ERRORLEVEL.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$(cd "$1" && pwd); shift
hdpmi="$root/.tools/hx/hxrt/BIN/HDPMI16.EXE"
[ -f "$hdpmi" ] || { echo "missing $hdpmi (run tools/fetch-hdpmi.sh)" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp "$hdpmi" "$work/"
conf="$work/dosbox.conf"
cat > "$conf" <<CONF
[sdl]
output=surface
[dosbox]
memsize=16
[cpu]
core=normal
cputype=386
cycles=max
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c "$dir"
mount d "$work"
c:
d:\\HDPMI16.EXE -r > NUL
$* > C:\\OUT.TXT
if errorlevel 1 goto fail
echo EXIT=0 >> C:\\OUT.TXT
goto done
:fail
echo EXIT=NONZERO >> C:\\OUT.TXT
:done
exit
CONF
rm -f "$dir/OUT.TXT"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 dosbox -conf "$conf" -noconsole >/dev/null 2>&1 || true
[ -f "$dir/OUT.TXT" ] || { echo "no output (DOSBox failed or timed out)" >&2; exit 3; }
tr -d '\r' < "$dir/OUT.TXT"
tr -d '\r' < "$dir/OUT.TXT" | grep -q 'EXIT=0$'
