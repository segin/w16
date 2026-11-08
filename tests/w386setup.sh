#!/bin/sh
# Drive Windows/386 2.11's interactive Setup headlessly with KEYFEED.COM
# and print the text screens it showed (black-box observation).
#   usage: tests/w386setup.sh script.txt [cdrive]
# script.txt is a tools/kfcomp.py script. The disks' files (merged) are
# A:, cdrive (default .tools/ref/w386/cdrive, emptied first) is C:.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
src=$root/.tools/ref/w386/src
cdrive=${2:-$root/.tools/ref/w386/cdrive}
[ -f "$src/SETUP.EXE" ] || { echo "no Windows/386 2.11 files in $src" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
python3 "$root/tools/kfcomp.py" "$1" "$work/S.KFS"
cp "$root/build/dos/KEYFEED.COM" "$work/"
rm -rf "$cdrive"; mkdir -p "$cdrive"
cat > "$work/dosbox.conf" <<CONF
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
mount a "$src"
mount c "$cdrive"
mount d "$work"
a:
D:\\KEYFEED.COM D:\\S.KFS D:\\SCREENS.TXT A:\\SETUP.EXE
exit
CONF
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${W386_TIMEOUT:-120} \
    dosbox -conf "$work/dosbox.conf" -noconsole >/dev/null 2>&1 || true
[ -f "$work/SCREENS.TXT" ] || { echo "no screens recorded (timed out?)" >&2; exit 3; }
tr -d '\r' < "$work/SCREENS.TXT"
