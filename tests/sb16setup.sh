#!/bin/sh
# Build the Sound Blaster 16 test variant of the reference install: a
# scratch copy of .tools/ref/win31/cdrive with Creative's Sound Blaster 16
# ASP software installed by its own INSTALL.EXE (from the user's disk
# images in .tools/sb16/src, run in headless DOSBox-X as an SB16 at 220h,
# IRQ 5, DMA 1, high DMA 5, MPU-401 at 330h) and its Windows 3.1 drivers
# set up as its WINSETUP.EXE would (WINSETUP.INF's [Windows],
# [Windows.System] and [Ini] sections, applied here: WINSETUP itself is
# interactive). Test-only (ARCHITECTURE.md 3): Creative's files are the
# user's, used only in this scratch drive, never committed, never in the
# install tree; .tools is never modified.
#
#   usage: tests/sb16setup.sh OUTDIR
# OUTDIR/c is the C: drive (tests/dropin.sh DROPIN_REF=OUTDIR/c), and
# OUTDIR/dosbox.conf the [sblaster] lines a run needs (DROPIN_CONF).
# OUTDIR/install.log says what the installer changed (see below).
#
# What INSTALL.EXE does (observed with this script's screenshots,
# OUTDIR/shot-*.png): it copies its files to C:\SB16 (DRV, WINDRV,
# WINAPPL, MMPLAY, ...), asks for disk 2 in drive A midway (this script
# writes the second image over the mounted copy of the first: DOSBox-X
# reads the image file as it goes, and the installer checks the volume
# label SB16_DISK2), adds to AUTOEXEC.BAT SET BLASTER=A220 I5 D1 H5 P330
# T6, SET SOUND=C:\SB16, SB16SET and SBCONFIG /S, to CONFIG.SYS
# DEVICE=C:\SB16\DRV\ASP.SYS /P:220, and to WIN.INI run=
# C:\SB16\WINSETUP.EXE (the old files kept as *.B~K). WINSETUP.INF then
# copies WINDRV\CTRES.DLL to WINDOWS and SB16SND.DRV, SB16FM.DRV,
# SB16AUX.DRV, VSBPD.386, MIDIMAP.CFG, ASPMAN.DLL to WINDOWS\SYSTEM and
# sets SYSTEM.INI [drivers] Wave=sb16snd.drv, MIDI=sb16fm.drv,
# Aux=sb16aux.drv and [386Enh] device=vsbpd.386; run= goes back to empty.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: tests/sb16setup.sh OUTDIR}
src=$root/.tools/sb16/src
d1="$src/Sound_Blaster_16ASP_(Disk_1).img"
d2="$src/Sound_Blaster_16ASP_(Disk_2).img"
[ -f "$d1" ] && [ -f "$d2" ] || { echo "no Sound Blaster 16 disk images in $src" >&2; exit 2; }
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
cp -r "$root/.tools/ref/win31/cdrive" "$out/c"
cp "$d1" "$out/fd.img"                  # the drive's disk (a copy)
sbconf='[sblaster]
sbtype=sb16
sbbase=220
irq=5
dma=1
hdma=5
mpu401=intelligent
mpubase=330
enable asp=true'
printf '%s\n' "$sbconf" > "$out/dosbox.conf"
cat > "$out/install.conf" <<EOF
[sdl]
output=surface
[dosbox]
quit warning=false
memsize=16
[cpu]
core=normal
cycles=max
$sbconf
[mixer]
nosound=true
[autoexec]
mount c "$out/c"
imgmount a "$out/fd.img" -t floppy
a:
INSTALL
EOF
# the keys, with the files the installer writes to say where it is:
#  - the opening text, the main menu's Begin Installation, drive C:, the
#    directory: four keys;
#  - when the copying stands still for 10 s, it asks for disk 2: the
#    second image over the first, a key;
#  - when it stands still again: the system files' explanation (a key),
#    the addresses (Y), the boot drive (C), the lines for AUTOEXEC.BAT
#    and CONFIG.SYS (a key), AUTOEXEC.BAT to be made (a key), WIN.INI's
#    path (C:\WINDOWS, a key), the end (a key)
cat > "$out/run.sh" <<EOF
#!/bin/sh
# (DOSBox-X now and then ends at once with "Can't init SDL": started
# again, as tests/dropin.sh does, at most twice)
k=0
while :; do
    SDL_AUDIODRIVER=dummy timeout 900 dosbox-x -nogui -fastlaunch -conf "$out/install.conf" > "$out/emu.log" 2>&1 &
    p=\$!
    sleep 5
    kill -0 \$p 2>/dev/null || ! grep -q "Can't init SDL" "$out/emu.log" || [ \$k -ge 2 ] || { k=\$((k + 1)); continue; }
    break
done
key() {
    w=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
    xdotool windowactivate --sync "\$w" 2>/dev/null || xdotool windowfocus "\$w" 2>/dev/null || true
    xdotool key --window "\$w" --delay 150 "\$@"
}
shot() {
    w=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
    import -window "\$w" "$out/shot-\$1.png" 2>/dev/null || true
}
# the files under C:\\SB16 have not changed for \$1 seconds (at most 400)
still() {
    last= since=\$(date +%s) t0=\$(date +%s)
    while [ \$((\$(date +%s) - t0)) -lt 400 ]; do
        now=\$(find "$out/c" -newer "$out/install.conf" -type f -printf '%s %p\n' 2>/dev/null | sort | md5sum)
        if [ "\$now" != "\$last" ]; then last=\$now since=\$(date +%s); fi
        [ \$((\$(date +%s) - since)) -ge \$1 ] && [ -d "$out/c/SB16" ] && return 0
        sleep 1
    done
    return 1
}
sleep 15
shot 1-start
key Return; sleep 3
key Down Return; sleep 4
key Return; sleep 4
key Return; sleep 4
key Return
sleep 30
still 10
shot 2-disk2
dd if="$d2" of="$out/fd.img" conv=notrunc 2>/dev/null
sleep 2
key Return
sleep 30
still 10
shot 3-system
key Return; sleep 6
shot 4-addresses
key y; sleep 6
shot 5-bootdrive
key Return; sleep 8
shot 6-lines
key Return; sleep 6
shot 7-autoexec
key Return; sleep 6
shot 8-winini
key Return; sleep 8
shot 9-done
key Return; sleep 5
kill \$p 2>/dev/null || true
wait \$p 2>/dev/null || true
EOF
chmod +x "$out/run.sh"
"$root/tests/xvfb.sh" "$out/run.sh" > "$out/run.out" 2>&1 || true
c=$out/c
for f in SB16/WINSETUP.INF SB16/WINDRV/SB16SND.DRV AUTOEXEC.B~K WINDOWS/WIN.B~K; do
    [ -f "$c/$f" ] || { echo "sb16setup: the installation did not finish (no C:\\$f; $out/shot-*.png)" >&2; exit 1; }
done
# WINSETUP.INF's work, as WINSETUP.EXE would do it
cp "$c/SB16/WINDRV/CTRES.DLL" "$c/WINDOWS/"
for f in VSBPD.386 SB16SND.DRV SB16FM.DRV SB16AUX.DRV MIDIMAP.CFG ASPMAN.DLL; do
    cp "$c/SB16/WINDRV/$f" "$c/WINDOWS/SYSTEM/"
done
python3 -I "$root/tests/scripts/iniset.py" "$c/WINDOWS/SYSTEM.INI" \
    'drivers:Wave=sb16snd.drv' 'drivers:MIDI=sb16fm.drv' 'drivers:Aux=sb16aux.drv' '386Enh:+device=vsbpd.386'
python3 -I "$root/tests/scripts/iniset.py" "$c/WINDOWS/WIN.INI" 'windows:run='
{
    echo "== AUTOEXEC.BAT"; tr -d '\r' < "$c/AUTOEXEC.BAT"
    echo "== CONFIG.SYS"; tr -d '\r' < "$c/CONFIG.SYS"
    echo "== WIN.INI against the reference's"
    diff "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$c/WINDOWS/WIN.INI" | tr -d '\r' || true
    echo "== SYSTEM.INI against the reference's"
    diff "$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM.INI" "$c/WINDOWS/SYSTEM.INI" | tr -d '\r' || true
    echo "== C:\\SB16"; ls "$c/SB16"
} > "$out/install.log"
rm -f "$out/fd.img"
echo "sb16setup: $out/c ($out/install.log)"
