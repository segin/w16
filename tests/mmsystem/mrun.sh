#!/bin/sh
# Run the MMSYSTEM probe MMP.EXE (tests/mmsystem/mmp.c) as the shell of a
# scratch copy of the reference Windows 3.1 (black-box observation;
# ARCHITECTURE.md 23), with Microsoft's MMSYSTEM.DLL or w16's
# (build/dos/MMSYSTEM.DLL and MMTASK.TSK). OUTDIR/mmp.log is the probe's
# log.
#   usage: tests/mmsystem/mrun.sh OUTDIR ms|w16 [/S|/3] [k286|k386]
# k386 (default): Microsoft's KRNL386; k286: Microsoft's KRNL286.
# MRUN_ARG: the sections to run (C:\MMP.ARG, mmp.c's header).
# MRUN_TIMER=w16: w16's TIMER.DRV too. MRUN_SHOT=1: a screenshot at the
# end (OUTDIR/shot.png).
# MRUN_SND: the sound devices (layer 3; DOSBox emulates a Sound Blaster
# 16 at 220h, IRQ 5, DMA 1 and 5, MPU-401 at 330h, an OPL3 at 388h):
#   none (default)  the reference as it is: no wave, MIDI or aux driver,
#                   [drivers] midimapper=midimap.drv
#   sb2   Windows 3.1's own SNDBLST2.DRV (wave and MIDI, Sound Blaster 1.5)
#         and MSADLIB.DRV (MIDI, Ad Lib), expanded from the reference's
#         Setup files (.tools/ref/win31/src), with the keys Setup writes
#         (wave= midi= sndblst2.drv, midi1=msadlib.drv), [sndblst.drv]
#         port=220 int=5
#   multi  for the numbering: wave=, wave2= and wave10= (SNDBLST2, SNDBLST),
#         midi= (MSADLIB), midi1= (SNDBLST2), aux= (MSADLIB: no auxMessage)
#   sb16  Creative's Sound Blaster 16 drivers as tests/sb16setup.sh
#         installs them (MRUN_SB16=DIR: its output, default build/sb16;
#         SB16SND.DRV wave, SB16FM.DRV MIDI, SB16AUX.DRV aux), in DOSBox-X
#         with Creative's ASP.SYS loaded first (SB16SND.DRV stops with
#         "ASP.SYS is not loaded" without it)
# MRUN_SHIM=1: each of those drivers (and the MIDI mapper) behind a
# logging pass-through driver (MDS*.DRV, tests/mmsystem/mds.c, with
# MMSYSLOG.DLL), and a scratch copy of each with its imports of MMSYSTEM
# renamed to MMSYSLOG (tools/nerename.py), so that the log shows what
# MMSYSTEM sends the drivers and what they call back.
# MRUN_X=1: in DOSBox-X (sb16 always is).
# MRUN_INI: more -i settings for tests/dropin.sh, one per line.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 mm=${2:-ms} mode=${3:-/S} kern=${4:-k386}
dos=$root/build/dos
mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -rf "$out/run" "$out/mmp.log" "$out/MMP.ARG" "$out/drv"
set -- -f "$dos/MMP.EXE=MMP.EXE" -f "$dos/MMC.DLL=WINDOWS/SYSTEM/MMC.DLL" \
    -i 'boot:shell=C:\MMP.EXE' -t "${MRUN_TIMEOUT:-300}" -o "$out/run"
if [ "$mm" = w16 ]; then
    set -- "$@" -f "$dos/MMSYSTEM.DLL=WINDOWS/SYSTEM/MMSYSTEM.DLL" \
        -f "$dos/MMTASK.TSK=WINDOWS/SYSTEM/MMTASK.TSK"
fi
[ "${MRUN_TIMER:-ms}" = w16 ] && set -- "$@" -f "$dos/TIMER.DRV=WINDOWS/SYSTEM/TIMER.DRV"
if [ -n "${MRUN_ARG:-}" ]; then
    printf '%s' "$MRUN_ARG" > "$out/MMP.ARG"
    set -- "$@" -f "$out/MMP.ARG=MMP.ARG"
fi
# the [sounds] keys section x plays besides Setup's
set -- "$@" -i 'WIN.INI:sounds:MmpBad=C:\NOFILE.WAV, A missing file' \
    -i 'WIN.INI:sounds:MmpNoComma=C:\WINDOWS\TADA.WAV' \
    -i 'WIN.INI:sounds:MmpSpaces=  ding.wav  ,  Spaces' \
    -i 'WIN.INI:sounds:MmpEmpty='
snd=${MRUN_SND:-none}
sbconf='[sblaster]
sbtype=sb16
sbbase=220
irq=5
dma=1
hdma=5
mpu401=intelligent
mpubase=330'
mkdir -p "$out/drv"
# drv KEY FILE SHIMVARIANT: [drivers] KEY= the driver, or its shim
drv() {
    if [ -n "${MRUN_SHIM:-}" ]; then
        set -- "$1" "$2" "$3" "$(printf '%s' "$2" | tr a-z A-Z)"
        python3 -I "$root/tools/nerename.py" "$out/drv/$4" --import MMSYSTEM=MMSYSLOG > /dev/null
        shimargs="$shimargs -f $out/drv/$4=WINDOWS/SYSTEM/$4 -f $dos/$3.DRV=WINDOWS/SYSTEM/$3.DRV"
        shimargs="$shimargs -i drivers:$1=$(printf '%s' "$3" | tr A-Z a-z).drv -i mmslog:$3=$2"
    else
        shimargs="$shimargs -f $out/drv/$(printf '%s' "$2" | tr a-z A-Z)=WINDOWS/SYSTEM/$(printf '%s' "$2" | tr a-z A-Z)"
        shimargs="$shimargs -i drivers:$1=$2"
    fi
}
shimargs=
case $snd in
none) ;;
sb2)
    for f in SNDBLST2 MSADLIB; do
        python3 -I "$root/tools/szdd.py" "$root/.tools/ref/win31/src/$f.DR_" "$out/drv/$f.DRV"
    done
    drv wave sndblst2.drv MDSW1
    drv midi sndblst2.drv MDSW2
    drv midi1 msadlib.drv MDSM1
    set -- "$@" -i 'sndblst.drv:port=220' -i 'sndblst.drv:int=5'
    export DROPIN_CONF="$sbconf" ;;
multi)
    # the numbering: two wave drivers with a gap in the keys (wave, wave2),
    # MIDI from SNDBLST2 and MSADLIB, an aux key whose driver has no
    # auxMessage, a key past 9 (wave10)
    for f in SNDBLST2 SNDBLST MSADLIB; do
        python3 -I "$root/tools/szdd.py" "$root/.tools/ref/win31/src/$f.DR_" "$out/drv/$f.DRV"
    done
    drv wave sndblst2.drv MDSW1
    drv wave2 sndblst.drv MDSW2
    drv midi msadlib.drv MDSM1
    drv midi1 sndblst2.drv MDSW3
    drv aux msadlib.drv MDSM3
    drv wave10 sndblst.drv MDSW4
    set -- "$@" -i 'sndblst.drv:port=220' -i 'sndblst.drv:int=5'
    export DROPIN_CONF="$sbconf" ;;
sb16)
    sb=${MRUN_SB16:-$root/build/sb16}
    [ -d "$sb/c" ] || { echo "no Sound Blaster 16 variant in $sb (tests/sb16setup.sh)" >&2; exit 2; }
    for f in SB16SND SB16FM SB16AUX; do
        cp "$sb/c/WINDOWS/SYSTEM/$f.DRV" "$out/drv/"
    done
    drv wave sb16snd.drv MDSW1
    drv midi sb16fm.drv MDSF1
    drv aux sb16aux.drv MDSA1
    # SB16SND.DRV wants Creative's ASP.SYS (CONFIG.SYS), which DOSBox-X's
    # DEVICE loads; DOSBox-X emulates the card's ASP
    MRUN_PRE="DEVICE C:\\SB16\\DRV\\ASP.SYS /P:220"
    # the settings Creative's drivers read (its setup dialogs write them)
    set -- "$@" -i 'sndblst.drv:port=220' -i 'sndblst.drv:int=5' -i 'sndblst.drv:dma=1' \
        -i 'sndblst.drv:hdma=5' -i 'sndblst.drv:midiport=330'
    export DROPIN_REF=$sb/c
    export DROPIN_CONF="$(cat "$sb/dosbox.conf")"
    set -- "$@" -p 386 ;;
*) echo "MRUN_SND: none, sb2, multi or sb16" >&2; exit 2 ;;
esac
if [ -n "${MRUN_SHIM:-}" ]; then
    cp "$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/MIDIMAP.DRV" "$out/drv/"
    drv midimapper midimap.drv MDSM2
    shimargs="$shimargs -f $dos/MMSYSLOG.DLL=WINDOWS/SYSTEM/MMSYSLOG.DLL"
fi
# shellcheck disable=SC2086
[ -n "$shimargs" ] && set -- "$@" $shimargs
if [ -n "${MRUN_INI:-}" ]; then
    while IFS= read -r l; do
        [ -n "$l" ] && set -- "$@" -i "$l"
    done <<EOF
$MRUN_INI
EOF
fi
[ -n "${MRUN_X:-}" ] && [ "$snd" != sb16 ] && set -- "$@" -p 386
set -- "$@" -c "${MRUN_PRE:+$MRUN_PRE
}WIN $mode"
if [ -n "${MRUN_SHOT:-}" ]; then
    set -- "$@" -s "$out/shot.png"
else
    set -- "$@" -W MMP.LOG
fi
if [ "$kern" = k286 ]; then set -- -r "$@"; else set -- -r -3 "$@"; fi
DROPIN_WSECS=${DROPIN_WSECS:-40} "$root/tests/dropin.sh" "$@" > "$out/dropin.txt" 2>&1 || true
if [ -f "$out/run/c/MMP.LOG" ]; then
    tr -d '\r' < "$out/run/c/MMP.LOG" > "$out/mmp.log"
else
    echo "no MMP.LOG ($out/dropin.txt)" >&2
    exit 3
fi
