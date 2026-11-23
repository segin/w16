#!/bin/sh
# w16's SB16.DRV (drivers/sb16; ARCHITECTURE.md 24) under Windows 3.1's
# MMSYSTEM and w16's (the boot-test case sb16):
#   dev    MMP's device sections (tests/mmsystem/mmdev.c: n w i m j a x b,
#          and all of them without the logging drivers) over SB16.DRV in
#          DOSBox-X as a Sound Blaster 16 (tests/mmsystem/mdscen.sh, sound
#          sb16w): Microsoft's MMSYSTEM and w16's must agree
#          (tests/mmsystem/mcmp.py); and the same sections over Windows
#          3.1's SNDBLST2.DRV under Microsoft's MMSYSTEM against SB16.DRV's
#          under it (tests/sb16/sbcmp.py: what MMSYSTEM sees of the two
#          drivers, the expected differences listed there)
#   audio  tests/sb16/sbaudio.sh: what SB16.DRV plays, recorded and
#          checked against what it was given
#   prog   Sound Recorder playing DING.WAV, Control Panel's Sound applet
#          testing a sound and Media Player playing CHIMES.WAV and opening
#          CANYON.MID (tests/mmsystem/mprog.sh, MPROG_SND=sb16w) under
#          Microsoft's MMSYSTEM and w16's: every screen of w16's as
#          Microsoft's, and as with Windows 3.1's own SNDBLST2.DRV under
#          Microsoft's but Sound Recorder's Record button (enabled: the
#          SB16 records DING.WAV's 22050 Hz, SNDBLST2 does not); and
#          SB16.DRV's configuration dialog opened through USER and
#          cancelled (tests/scripts/input/sb16cfg.txt)
#   enh    tests/sb16/sbaudio.sh in 386 Enhanced mode with w16's VSBD.386:
#          sides mse (Microsoft's WIN386 and MMSYSTEM) and w16e (w16's),
#          and QuickTime's qtmse and qtw16e when $W16_QTDIR has it
#   cont   the card between Windows and a DOS box (tests/sbvxdprobe.sh,
#          tools/sbvxdcmp.py) under w16's WIN386, SB16.DRV and VSBD.386
#          against Windows 3.1's own, SNDBLST2.DRV and VSBD.386: sbbox (a
#          box plays while Windows has nothing open) and sbwin (a box
#          wants the card while Windows plays: the contention message,
#          screenshot 03, and no card for the box)
#   usage: tests/sb16/sbscen.sh [-o OUTDIR] [dev] [audio] [prog] [enh] [cont]
# Exit status 0 when every comparison agrees.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=
while getopts o: opt; do
    case $opt in
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
parts=${*:-dev audio prog}
[ -n "$out" ] || out=$root/build/sbscen-$(date +%H%M%S)
mkdir -p "$out"
out=$(cd "$out" && pwd)
fails=0
for p in $parts; do
    case $p in
    dev)
        # SB16.DRV under both MMSYSTEMs, and SNDBLST2 under Microsoft's, at once
        "$root/tests/mmsystem/mdscen.sh" -o "$out/dev" sbn:sb16w:1:n sbw:sb16w:1:w sbi:sb16w:1:i \
            sbm:sb16w:1:mja sbx:sb16w:1:x sbb:sb16w:1:b sbplain:sb16w:0:nwimjaxb \
            > "$out/dev.txt" 2>&1 &
        "$root/tests/mmsystem/mdscen.sh" -m -o "$out/sb2" sb2n:sb2:1:n sb2w:sb2:1:w sb2i:sb2:1:i \
            sb2m:sb2:1:mja sb2x:sb2:1:x > "$out/sb2.txt" 2>&1 &
        wait
        if grep -q FAIL "$out/dev.txt" || [ "$(grep -c '^ok' "$out/dev.txt")" != 7 ]; then
            echo "FAIL dev: $(grep -m1 FAIL "$out/dev.txt" || echo 'no result') ($out/dev.txt)"
            fails=$((fails + 1))
        else
            echo "ok   dev: Microsoft's MMSYSTEM and w16's alike over SB16.DRV ($(grep -c '^ok' "$out/dev.txt") cases)"
        fi
        for s in n w i m x; do
            if python3 "$root/tests/sb16/sbcmp.py" "$out/sb2/sb2$s-ms/mmp.log" "$out/dev/sb$s-ms/mmp.log" \
                > "$out/sbcmp-$s.txt" 2>&1; then
                echo "ok   sbcmp $s: $(tail -n 1 "$out/sbcmp-$s.txt")"
            else
                echo "FAIL sbcmp $s: $(tail -n 1 "$out/sbcmp-$s.txt") ($out/sbcmp-$s.txt)"
                fails=$((fails + 1))
            fi
        done ;;
    audio)
        if "$root/tests/sb16/sbaudio.sh" -o "$out/audio" > "$out/audio.txt" 2>&1; then
            echo "ok   audio: $(grep -c '^ok' "$out/audio.txt") checks ($out/audio.txt)"
        else
            echo "FAIL audio: $(grep -m1 FAIL "$out/audio.txt" || echo 'no result') ($out/audio.txt)"
            fails=$((fails + 1))
        fi ;;
    enh)
        qtc=${W16_QTDIR:-$HOME/Downloads/qtw16}/ref-installed/c
        sides="mse w16e"
        # (qtw16e, QuickTime under w16's WIN386 with Windows 3.1's
        # TIMER.DRV and VTDAPI.386, stopped at "play" in most runs until
        # 2026-11-23: ARCHITECTURE.md 12.14)
        [ -d "$qtc" ] && sides="$sides qtmse qtw16e"
        if "$root/tests/sb16/sbaudio.sh" -o "$out/enh" $sides > "$out/enh.txt" 2>&1; then
            echo "ok   enh: $(grep -c '^ok' "$out/enh.txt") checks, sides $sides ($out/enh.txt)"
        else
            echo "FAIL enh: $(grep -m1 FAIL "$out/enh.txt" || echo 'no result') ($out/enh.txt)"
            fails=$((fails + 1))
        fi ;;
    cont)
        for sc in sbbox sbwin; do
            b=200; sh=
            [ "$sc" = sbwin ] && b=14 sh="-s 03"
            SBVXD_BOOT=$b "$root/tests/sbvxdprobe.sh" -o "$out/cont-ms-$sc" ms $sc > /dev/null 2>&1 &
            SBVXD_BOOT=$b "$root/tests/sbvxdprobe.sh" -o "$out/cont-w16-$sc" w16 $sc > /dev/null 2>&1 &
            wait
            if r=$(python3 "$root/tools/sbvxdcmp.py" $sh "$out/cont-ms-$sc" "$out/cont-w16-$sc" 2>&1); then
                echo "ok   cont $sc: $r"
            else
                echo "FAIL cont $sc: $r ($out/cont-w16-$sc)"
                fails=$((fails + 1))
            fi
        done ;;
    prog)
        r=ok
        for g in soundrec sound mplayer; do
            d=$out/prog-$g
            mkdir -p "$d"
            # what the SB16 does more shows: Sound Recorder's Record
            # button is enabled for DING.WAV (22050 Hz, 8 bits, mono),
            # which SNDBLST2 cannot record (11025 Hz only)
            mask=
            [ $g = soundrec ] && mask='rectangle 271,143 285,157'
            "$root/tests/mmsystem/mprog.sh" "$d/sb2" ms $g > /dev/null 2>&1 || true
            MPROG_SND=sb16w "$root/tests/mmsystem/mprog.sh" "$d/ms" ms $g -e "$d/sb2" > /dev/null 2>&1 || true
            MPROG_SND=sb16w "$root/tests/mmsystem/mprog.sh" "$d/w16" w16 $g -e "$d/ms" > /dev/null 2>&1 || true
            ls "$d/sb2"/0*.png > /dev/null 2>&1 || { [ "$r" = ok ] && r="$g: no screens with SNDBLST2 ($d)"; continue; }
            for f in "$d/sb2"/0*.png; do
                sn=$(basename "$f")
                if [ ! -f "$d/ms/$sn" ] || [ ! -f "$d/w16/$sn" ]; then
                    [ "$r" = ok ] && r="$g: no $sn over SB16.DRV ($d)"
                    continue
                fi
                # w16's MMSYSTEM as Microsoft's, both over SB16.DRV: exactly
                n=$(magick compare -metric AE "$d/w16/$sn" "$d/ms/$sn" "$d/w16-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { [ "$r" = ok ] && r="$g $sn: w16's MMSYSTEM $n pixels off Microsoft's ($d)"; }
                # SB16.DRV as SNDBLST2 (Microsoft's MMSYSTEM), the mask aside
                if [ -n "$mask" ]; then
                    magick "$f" -fill black -draw "$mask" "$d/sb2-m-$sn"
                    magick "$d/ms/$sn" -fill black -draw "$mask" "$d/ms-m-$sn"
                    n=$(magick compare -metric AE "$d/ms-m-$sn" "$d/sb2-m-$sn" "$d/ms-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                else
                    n=$(magick compare -metric AE "$d/ms/$sn" "$f" "$d/ms-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                fi
                n=${n%% *}
                [ "$n" = 0 ] || { [ "$r" = ok ] && r="$g $sn: $n pixels off SNDBLST2's ($d)"; }
            done
        done
        # the configuration dialog (DRV_CONFIGURE through USER from
        # SBP.EXE's "config"), cancelled: shown, and answered 0
        d=$out/prog-config
        rm -rf "$d"
        mkdir -p "$d"
        printf 'config\r\n' > "$d/SBP.TXT"
        "$root/tests/inputrun.sh" -b 30 -t 150 "$root/tests/scripts/input/sb16cfg.txt" "$d/run" -r \
            -f "$root/build/dos/SBP.EXE=SBP.EXE" -f "$d/SBP.TXT=SBP.TXT" \
            -f "$root/build/dos/SB16.DRV=WINDOWS/SYSTEM/SB16.DRV" -i 'boot:shell=C:\SBP.EXE' \
            -i 'drivers:wave=sb16.drv' -i 'sb16.drv:port=220' -i 'sb16.drv:int=5' \
            > "$d/run.txt" 2>&1 || true
        tr -d '\r' < "$d/run/work/c/SBP.LOG" > "$d/sbp.log" 2>/dev/null || : > "$d/sbp.log"
        if ! grep -q 'DRV_QUERYCONFIGURE = 1' "$d/sbp.log" || ! grep -q '  DRV_CONFIGURE = 0' "$d/sbp.log" ||
            [ ! -f "$d/run/01-config.png" ]; then
            [ "$r" = ok ] && r="config: the dialog not shown and cancelled ($d)"
        fi
        if [ "$r" = ok ]; then
            echo "ok   prog: Sound Recorder, Sound and Media Player over SB16.DRV as over SNDBLST2 (but Sound Recorder's Record button), w16's MMSYSTEM as Microsoft's"
        else
            echo "FAIL prog: $r"
            fails=$((fails + 1))
        fi ;;
    *) echo "unknown part $p" >&2; exit 2 ;;
    esac
done
exit $fails
