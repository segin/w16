#!/bin/sh
# Run a Win16 program under the reference Windows install (black-box
# observation only) in headless DOSBox, with the program as the shell so
# Windows exits when it does. Prints what the program wrote to stdout.
#   usage: tests/refrun.sh <program.exe> [mode] [ref-dir]
# mode is the WIN.COM switch: /S (Standard, the default) or /3 (386
# Enhanced). ref-dir defaults to .tools/ref/win31/cdrive (a completed
# Windows 3.1 Setup); .tools/ref/wfw311a/cdrive is WfW 3.11 (Enhanced only).
# Prints the program's TK.LOG followed by WINEXIT.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
exe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mode=${2:-/S}
ref=${3:-$root/.tools/ref/win31/cdrive}
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp -r "$ref" "$work/c"
name=$(basename "$exe" | tr a-z A-Z)
cp "$exe" "$work/c/$name"
sed -i "s/^shell=.*/shell=C:\\\\$name\r/" "$work/c/WINDOWS/SYSTEM.INI"
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
mount c "$work/c"
c:
cd \\WINDOWS
WIN $mode > C:\\OUT.TXT
echo WINEXIT >> C:\\OUT.TXT
exit
CONF
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${REFRUN_TIMEOUT:-120} dosbox -conf "$work/dosbox.conf" -noconsole >/dev/null 2>&1 || true
[ -f "$work/c/OUT.TXT" ] || { echo "no output (DOSBox failed or timed out)" >&2; exit 3; }
[ -f "$work/c/WINDOWS/TK.LOG" ] && tr -d '\r' < "$work/c/WINDOWS/TK.LOG"
tr -d '\r' < "$work/c/OUT.TXT"
