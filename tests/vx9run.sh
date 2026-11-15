#!/bin/sh
# vx9run.sh NAME [w16]: VX9 (tests/dos/vx9.asm, tests/apps/vx9a.c, `make
# vxprobes`): a task's exit as DOS sees it in 386 Enhanced mode. VX9.COM
# (resident before WIN) records the INT 21h 4Ch/00h/50h/55h/51h/62h calls
# reaching DOS, the PSPs' fields, DOS's terminate and the steps after it
# (CS:IP only); VX9A.EXE is the shell, starts TK51C.EXE (which only exits),
# waits for it and exits Windows. With w16: w16's WIN386.EXE. VX9X=1:
# DOSBox-X with the VMM's COM1 trace (build/tmp/NAME/com1.txt).
# Prints VX9A's TK.LOG and VX9.COM's records (build/tmp/NAME/VX9.LOG).
set -eu
unset DISPLAY WAYLAND_DISPLAY
cd "$(dirname "$0")/.."
n=$1 w=${2:-}
o=$PWD/build/tmp/$n
rm -rf "$o"
mkdir -p "$o"
cp -r .tools/ref/win31/cdrive "$o/c"
chmod -R u+w "$o/c"
cp build/dos/VX9.COM build/dos/VX9A.EXE build/dos/TK51C.EXE "$o/c/"
[ -n "$w" ] && cp build/win386/WIN386.EXE "$o/c/WINDOWS/SYSTEM/WIN386.EXE"
sed -i 's/^shell=.*/shell=C:\\VX9A.EXE\r/' "$o/c/WINDOWS/SYSTEM.INI"
if [ -n "${VX9X:-}" ]; then
    emu="dosbox-x -nogui -fastlaunch" extra="[dosbox]
machine=svga_s3
memsize=16
quit warning=false
[serial]
serial1=file file:$o/com1.txt"
else
    emu="dosbox -noconsole" extra="[dosbox]
memsize=16"
fi
cat > "$o/dosbox.conf" <<CONF
[sdl]
output=surface
$extra
[cpu]
core=normal
cputype=386
cycles=max
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c "$o/c"
c:
C:\\VX9.COM
cd \\WINDOWS
WIN /3
C:\\VX9.COM D
exit
CONF
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${VX9_TIMEOUT:-150} $emu -conf "$o/dosbox.conf" > "$o/emu.txt" 2>&1 || true
find "$o/c" -iname 'TK.LOG' -exec cat {} \; | tr -d '\r'
[ -f "$o/c/VX9.LOG" ] && cp "$o/c/VX9.LOG" "$o/VX9.LOG" && tr -d '\r' < "$o/VX9.LOG"
