#!/bin/sh
# Run a program under the reference Windows/386 2.11 on real DOS, headless
# (black-box observation). Windows/386 needs a real DOS (it patches DOS's
# internal data), so this builds a hard disk image with MS-DOS 4.00 built
# from Microsoft's MIT-licensed source (tests/msdos-build.sh), the
# Windows/386 install (tests/w386setup.sh), and the program; boots it in
# DOSBox-X; and afterwards prints the program's TK.LOG from the image.
#   usage: tests/w386run.sh PROGRAM.EXE [extra files...]
# AUTOEXEC.BAT runs "WIN386 C:\PROGRAM.EXE" then POWEROFF.COM (APM), so
# the program must end the Windows session itself (ExitWindows).
# W386_SHOT=N: take a screenshot every N seconds (xvfb-run) for debugging.
set -eu
# Never reach the user's desktop: DOSBox-X shows native dialogs (e.g. its
# quit warning) on whatever display it can find.
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
dos=$root/.tools/msdos/build/src
win=$root/.tools/ref/w386/cdrive/WIN386
[ -f "$dos/BIOS/IO.SYS" ] || { echo "MS-DOS 4.00 not built (tests/msdos-build.sh)" >&2; exit 2; }
[ -f "$win/WIN386.EXE" ] || { echo "Windows/386 not installed (tests/w386setup.sh)" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
prog=$1; shift
name=$(basename "$prog" | tr a-z A-Z)
printf 'FILES=30\r\nBUFFERS=20\r\n' > "$work/CONFIG.SYS"
printf '@ECHO OFF\r\nPATH C:\;C:\\WIN386\r\nCD \\WIN386\r\nWIN386 C:\\%s\r\nC:\\POWEROFF.COM\r\n' "$name" \
    > "$work/AUTOEXEC.BAT"
python3 "$root/tools/mkhdd.py" "$work/hdd.img" --mbr "$root/build/hdd/MBR.BIN" \
    --boot "$dos/BOOT/MSBOOT.BIN" --io "$dos/BIOS/IO.SYS" --dos "$dos/DOS/MSDOS.SYS" \
    "$dos/CMD/COMMAND/COMMAND.COM" "$work/CONFIG.SYS" "$work/AUTOEXEC.BAT" \
    "$root/build/dos/POWEROFF.COM" "$win=WIN386" "$prog" "$@" 2>&1 | grep -v '^$' || true
cat > "$work/dosbox.conf" <<CONF
[sdl]
autolock=false
[dosbox]
quit warning=false
machine=vga
memsize=16
[cpu]
core=normal
cputype=386
cycles=max
[dos]
ver=4.0
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
imgmount c "$work/hdd.img"
boot c:
CONF
if [ -n "${W386_SHOT:-}" ]; then
    # debugging: screenshots W386_SHOT seconds apart into $W386_SHOTDIR,
    # on a virtual X display (never the user's)
    shots=${W386_SHOTDIR:-$root/build/ref/w386shots}
    mkdir -p "$shots"
    cat > "$work/shot.sh" <<SH
#!/bin/sh
SDL_AUDIODRIVER=dummy timeout ${W386_TIMEOUT:-90} dosbox-x -conf "$work/dosbox.conf" -nogui -fastlaunch >"$work/dosbox.log" 2>&1 &
i=0
while kill -0 \$! 2>/dev/null; do
    sleep $W386_SHOT
    i=\$((i + 1))
    import -window root "$shots/shot\$i.png" 2>/dev/null
done
SH
    chmod +x "$work/shot.sh"
    xvfb-run -a -s '-screen 0 1024x768x24' "$work/shot.sh"
else
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${W386_TIMEOUT:-90} \
        dosbox-x -conf "$work/dosbox.conf" -nogui -fastlaunch >"$work/dosbox.log" 2>&1 || true
fi
img="$work/hdd.img@@32256"
for f in WIN386/TK.LOG TK.LOG; do
    MTOOLS_SKIP_CHECK=1 mtype -i "$img" "::$f" 2>/dev/null | tr -d '\r' || true
done
