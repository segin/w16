#!/bin/sh
# 86box.sh - run a Win16 program under the reference Windows 3.1 on an
# emulated PC with a real coprocessor (86Box), headless, for what DOSBox
# and DOSBox-X can not show: their coprocessors raise no exceptions (no
# IRQ 13), so an unmasked exception on a coprocessor is observable only
# here (TESTING.md, "hardware FPU"). Black-box observation only.
#
#   usage: tests/86box.sh PROGRAM.EXE [EXTRA_FILE...]
#
# Builds a hard disk image with MS-DOS 4.00 (built from Microsoft's
# MIT-licensed source, tools/msdos-build.sh; the image as tests/w386run.sh
# makes it, HIMEM.SYS from the Windows directory in CONFIG.SYS), the
# reference Windows 3.1 install (.tools/ref/win31/cdrive), the program and
# the extra files in C:\, with WIN.INI's run= naming the program. 86Box
# boots it on a virtual X display (tests/xvfb.sh); once the program's
# TK.LOG (C:\WINDOWS\TK.LOG, or C:\TK.LOG) holds a line "... done" or
# B86_TIMEOUT seconds have passed, 86Box is ended and TK.LOG is printed.
# Settings (environment):
#   B86_MODE     WIN.COM's switch, /S (default) or /3
#   B86_WIN87EM  a WIN87EM.DLL to put in WINDOWS\SYSTEM instead of
#                Microsoft's (e.g. build/dos/WIN87EM.DLL, w16's)
#   B86_MACHINE  86Box machine (default asus386: ASUS ISA-386C, 386DX);
#   B86_CPU      CPU family (i386dx), B86_SPEED its clock in Hz
#                (33333333), B86_FPU the coprocessor (387), B86_MEM KB (8192)
#   B86_TIMEOUT  seconds (default 300)
#   B86_IDLE     seconds: also end once TK.LOG has not grown for that long
#                (a program that ends or hangs without "done")
#   B86_SHOT     a directory: a screenshot there every 10 seconds
#   B86_KEEP     a directory: the image and 86Box's files are kept there
# Needs: 86Box and its ROMs (Arch: the AUR packages 86box, 86box-roms),
# Xvfb, mtools, ImageMagick's import (B86_SHOT only), and the MS-DOS
# build (.tools/msdos).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
dos=$root/.tools/msdos/build/src
ref=$root/.tools/ref/win31/cdrive
[ $# -ge 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
command -v 86Box >/dev/null || { echo "86box.sh: 86Box is not installed" >&2; exit 2; }
[ -f "$dos/BIOS/IO.SYS" ] || { echo "86box.sh: MS-DOS 4.00 not built (tools/msdos-build.sh)" >&2; exit 2; }
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "86box.sh: no reference install at $ref" >&2; exit 2; }
[ -f "$root/build/hdd/MBR.BIN" ] || make -C "$root" build/hdd/MBR.BIN >/dev/null
prog=$1; shift
name=$(basename "$prog" | tr a-z A-Z)
work=$(mktemp -d "${TMPDIR:-/tmp}/w16-86box.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/vm"

# C:\WINDOWS: the reference install, the program run from WIN.INI
cp -r "$ref/WINDOWS" "$work/WINDOWS"
sed -i "s/^run=.*/run=C:\\\\$name\r/" "$work/WINDOWS/WIN.INI"
if [ -n "${B86_WIN87EM:-}" ]; then
    cp "$B86_WIN87EM" "$work/WINDOWS/SYSTEM/WIN87EM.DLL"
fi
printf 'FILES=30\r\nBUFFERS=20\r\nDEVICE=C:\\WINDOWS\\HIMEM.SYS\r\n' > "$work/CONFIG.SYS"
printf '@ECHO OFF\r\nPATH C:\\WINDOWS\r\nCD \\WINDOWS\r\nWIN %s\r\nCD \\\r\nECHO WINEXIT> C:\\WINEXIT.TXT\r\n' \
    "${B86_MODE:-/S}" > "$work/AUTOEXEC.BAT"
img=$work/vm/hdd.img
python3 "$root/tools/mkhdd.py" "$img" --mbr "$root/build/hdd/MBR.BIN" \
    --boot "$dos/BOOT/MSBOOT.BIN" --io "$dos/BIOS/IO.SYS" --dos "$dos/DOS/MSDOS.SYS" \
    "$dos/CMD/COMMAND/COMMAND.COM" "$work/CONFIG.SYS" "$work/AUTOEXEC.BAT" \
    "$work/WINDOWS=WINDOWS" "$prog" "$@" 2>&1 | grep -v '^$' || true

# 86Box: one IDE disk (130 cylinders, 16 heads, 63 sectors: mkhdd.py's
# geometry), no floppy drives, VGA, no sound; the CMOS made here
# (tools/atcmos.py: the disk as type 47, so the BIOS boots without
# stopping in its setup)
mkdir -p "$work/vm/nvr"
fpu=${B86_FPU:-387}
python3 "$root/tools/atcmos.py" "$work/vm/nvr/${B86_MACHINE:-asus386}.nvr" \
    --cyl 130 --heads 16 --sectors 63 --mem "${B86_MEM:-8192}" \
    $( [ "$fpu" = none ] && echo --no87 )
cat > "$work/vm/86box.cfg" <<CFG
[General]
vid_renderer = qt_software
confirm_exit = 0
confirm_reset = 0
confirm_save = 0

[Machine]
machine = ${B86_MACHINE:-asus386}
cpu_family = ${B86_CPU:-i386dx}
cpu_speed = ${B86_SPEED:-33333333}
cpu_multi = 1
cpu_use_dynarec = 0
fpu_type = $fpu
mem_size = ${B86_MEM:-8192}

[Video]
gfxcard = vga

[Input devices]
keyboard_type = keyboard_at
mouse_type = none

[Storage controllers]
hdc_1 = ide_isa

[Hard disks]
hdd_01_parameters = 63, 16, 130, 0, ide
hdd_01_fn = $img
hdd_01_ide_channel = 0:0

[Floppy and CD-ROM drives]
fdd_01_type = none
fdd_02_type = none
CFG

# run until TK.LOG says done (polled from the image) or the time is up
cat > "$work/run.sh" <<'RUN'
#!/bin/sh
work=$1 timeout=$2 shots=$3 idle=$4
img=$work/vm/hdd.img
cd "$work/vm"
QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null \
    86Box -P "$work/vm" -N > "$work/86box.out" 2>&1 &
pid=$!
t=0 i=0 size=0 still=0
log() {
    for f in WINDOWS/TK.LOG TK.LOG; do
        MTOOLS_SKIP_CHECK=1 mtype -i "$img@@32256" "::$f" 2>/dev/null
    done
}
while kill -0 "$pid" 2>/dev/null && [ "$t" -lt "$timeout" ]; do
    sleep 5
    t=$((t + 5))
    if [ -n "$shots" ] && [ $((t % 10)) = 0 ]; then
        i=$((i + 1))
        import -window root "$shots/shot$(printf %03d $i).png" 2>/dev/null || true
    fi
    if log | tr -d '\r' | grep -q ' done'; then
        sleep 2
        break
    fi
    if [ "$idle" -gt 0 ]; then
        n=$(log | wc -c)
        if [ "$n" -gt 0 ] && [ "$n" = "$size" ]; then
            still=$((still + 5))
            [ "$still" -lt "$idle" ] || break
        else
            still=0
        fi
        size=$n
    fi
done
# (86Box does not always end on SIGTERM)
kill "$pid" 2>/dev/null || true
sleep 3
kill -9 "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
RUN
shots=
if [ -n "${B86_SHOT:-}" ]; then
    mkdir -p "$B86_SHOT"
    shots=$(cd "$B86_SHOT" && pwd)
fi
"$root/tests/xvfb.sh" sh "$work/run.sh" "$work" "${B86_TIMEOUT:-300}" "$shots" \
    "${B86_IDLE:-0}" || true
for f in WINDOWS/TK.LOG TK.LOG; do
    MTOOLS_SKIP_CHECK=1 mtype -i "$img@@32256" "::$f" 2>/dev/null | tr -d '\r' || true
done
if MTOOLS_SKIP_CHECK=1 mtype -i "$img@@32256" ::WINEXIT.TXT >/dev/null 2>&1; then
    echo WINEXIT
fi
if [ -n "${B86_KEEP:-}" ]; then
    mkdir -p "$B86_KEEP"
    cp -r "$work/vm" "$work/86box.out" "$B86_KEEP/"
fi
