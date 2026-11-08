#!/bin/sh
# Run a Win16 program under the reference Windows install (black-box
# observation only) in headless DOSBox, with the program as the shell so
# Windows exits when it does. Prints what the program wrote to stdout.
#   usage: tests/refrun.sh <program.exe> [mode] [ref-dir]
# mode is the WIN.COM switch: /S (Standard, the default) or /3 (386
# Enhanced). ref-dir defaults to .tools/ref/win31/cdrive (a completed
# Windows 3.1 Setup); .tools/ref/wfw311a/cdrive is WfW 3.11 (Enhanced only).
# Prints the program's TK.LOG followed by WINEXIT.
# Environment: REFRUN_FILES = extra host files copied to C:\ (upper-cased
# names); REFRUN_OUT = directory that receives every C:\*.BMP afterwards;
# REFRUN_GET = paths under C:\ (e.g. WINDOWS/MAIN.GRP) also copied there;
# REFRUN_PRE = DOS commands (one per line) run before WIN, e.g. SET PATH=...
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
exe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mode=${2:-/S}
ref=${3:-$root/.tools/ref/win31/cdrive}
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp -r "$ref" "$work/c"
name=$(basename "$exe" | tr a-z A-Z)
cp "$exe" "$work/c/$name"
for f in ${REFRUN_FILES:-}; do
    case $f in
    *=*) cp "${f%%=*}" "$work/c/${f#*=}" ;;          # src=path/under/C
    *)   cp "$f" "$work/c/$(basename "$f" | tr a-z A-Z)" ;;
    esac
done
if [ -n "${REFRUN_SHELL:-}" ]; then
    # REFRUN_SHELL=PROG: PROG is the shell and our program starts from
    # WIN.INI's load= line (to test a program as the Windows shell)
    sed -i "s/^shell=.*/shell=${REFRUN_SHELL}\r/" "$work/c/WINDOWS/SYSTEM.INI"
    sed -i "s/^load=.*/load=C:\\\\$name\r/" "$work/c/WINDOWS/WIN.INI"
else
    sed -i "s/^shell=.*/shell=C:\\\\$name\r/" "$work/c/WINDOWS/SYSTEM.INI"
fi
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
${REFRUN_PRE:-}
cd \\WINDOWS
WIN $mode > C:\\OUT.TXT
echo WINEXIT >> C:\\OUT.TXT
exit
CONF
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${REFRUN_TIMEOUT:-120} dosbox -conf "$work/dosbox.conf" -noconsole >/dev/null 2>&1 || true
[ -f "$work/c/OUT.TXT" ] || { echo "no output (DOSBox failed or timed out)" >&2; exit 3; }
if [ -n "${REFRUN_OUT:-}" ]; then
    mkdir -p "$REFRUN_OUT"
    for f in "$work"/c/*.BMP; do [ -f "$f" ] && cp "$f" "$REFRUN_OUT/"; done
    for f in ${REFRUN_GET:-}; do [ -f "$work/c/$f" ] && cp "$work/c/$f" "$REFRUN_OUT/"; done
fi
for log in "$work/c/WINDOWS/TK.LOG" "$work/c/TK.LOG"; do   # cwd varies
    [ -f "$log" ] && tr -d '\r' < "$log"
done
tr -d '\r' < "$work/c/OUT.TXT"
