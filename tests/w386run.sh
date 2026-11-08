#!/bin/sh
# Run a program under the reference Windows/386 2.11 (installed by
# tests/w386setup.sh with tests/scripts/w386-install.txt) in headless
# DOSBox: WIN386 PROGRAM. The program must end the session itself
# (ExitWindows). Prints the program's TK.LOG.
#   usage: tests/w386run.sh PROGRAM.EXE
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/w386/cdrive
[ -f "$ref/WIN386/WIN386.EXE" ] || { echo "no Windows/386 install in $ref" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp -r "$ref" "$work/c"
name=$(basename "$1" | tr a-z A-Z)
cp "$1" "$work/c/$name"
cat > "$work/dosbox.conf" <<CONF
[sdl]
output=surface
[dosbox]
memsize=16
[cpu]
core=normal
cputype=386
cycles=max
[dos]
ems=false
xms=false
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c "$work/c"
c:
ver set 3 30
cd \\WIN386
WIN386 C:\\$name > C:\\OUT.TXT
echo WINEXIT >> C:\\OUT.TXT
exit
CONF
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${W386_TIMEOUT:-90} \
    dosbox -conf "$work/dosbox.conf" -noconsole >/dev/null 2>&1 || true
for f in "$work/c/WIN386/TK.LOG" "$work/c/TK.LOG"; do
    [ -f "$f" ] && tr -d '\r' < "$f"
done
[ -f "$work/c/OUT.TXT" ] && tr -d '\r' < "$work/c/OUT.TXT" || echo "(no OUT.TXT: timed out)"
