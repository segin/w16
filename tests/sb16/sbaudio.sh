#!/bin/sh
# What w16's SB16.DRV plays, recorded (ARCHITECTURE.md 24.4): the probe
# SBP.EXE (tests/sb16/sbp.c) runs as the shell of a scratch copy of the
# reference Windows 3.1 with SB16.DRV as its wave, MIDI and aux driver, in
# DOSBox-X as a Sound Blaster 16 (sbtype=sb16, 220h, IRQ 5, DMA 1 and 5,
# MPU-401 at 330h) with the run's sound recorded (tests/dropin.sh
# DROPIN_CAPTURE: DX-CAPTURE /A at a 44100 Hz mixer), and plays the test
# material of tests/sb16/sbaudio.py: eight WAV files in 8 and 16 bits,
# mono and stereo, 8000 to 44100 Hz (four headers each; one looped three
# times; one at a lower wave volume), Windows 3.1's CHIMES.WAV through MCI
# (MCIWAVE) and DING.WAV through sndPlaySound, records twice (input), plays seven
# notes through the MPU-401 and a MIDI file through MCISEQ on port 0.
# DOSBox-X's own synthesiser (mididevice=synth: FluidSynth into its mixer)
# plays the MIDI with tools/mksf2.py's sine
# SoundFont, so every note comes out at its true pitch. Then
# tests/sb16/sbaudio.py cmp checks the recording against the material and
# SBP's log is checked: every header done, the positions, the recordings'
# sizes.
# The sides qtms and qtw16 play QuickTime for Windows' sample movie
# through its MCI driver instead, on $W16_QTDIR/ref-installed/c (Windows
# 3.1 with QuickTime installed by its own installer, as tests/qtplay.sh
# makes it): its sound track must come out (4 s of sound or more).
#   usage: tests/sb16/sbaudio.sh [-o OUTDIR] [ms|w16|qtms|qtw16 ...]
# ms: Microsoft's MMSYSTEM.DLL, w16: w16's (and MMTASK.TSK); default ms
# and w16, and qtms and qtw16 when the QuickTime drive is there.
# Exit status 0 when every check passes.
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
qtc=${W16_QTDIR:-$HOME/Downloads/qtw16}/ref-installed/c
if [ $# -gt 0 ]; then sides=$*
elif [ -d "$qtc" ]; then sides="ms w16 qtms qtw16"
else sides="ms w16"; fi
[ -n "$out" ] || out=$root/build/sbaudio-$(date +%H%M%S)
mkdir -p "$out"
out=$(cd "$out" && pwd)
dos=$root/build/dos
mat=$out/mat
rm -rf "$mat"
python3 "$root/tests/sb16/sbaudio.py" gen "$mat"
python3 "$root/tools/mksf2.py" "$mat/sine.sf2"
cp "$root/.tools/ref/win31/cdrive/WINDOWS/CHIMES.WAV" "$root/.tools/ref/win31/cdrive/WINDOWS/DING.WAV" "$mat/"
run() {
    s=$1 d=$out/$1 script=$mat/SBP.TXT
    rm -rf "$d" "$d.cap"
    case $s in qt*) script=$mat/SBPQT.TXT ;; esac
    set -- -r -3 -p 386 -f "$dos/SBP.EXE=SBP.EXE" -f "$script=SBP.TXT" \
        -f "$dos/SB16.DRV=WINDOWS/SYSTEM/SB16.DRV" -i 'boot:shell=C:\SBP.EXE' \
        -i 'drivers:wave=sb16.drv' -i 'drivers:midi=sb16.drv' -i 'drivers:aux=sb16.drv' \
        -i 'sb16.drv:port=220' -i 'sb16.drv:int=5' -i 'sb16.drv:dma=1' -i 'sb16.drv:hdma=5' \
        -i 'sb16.drv:mpuport=330' -t "${SBAUDIO_SECS:-600}" -o "$d/run" -c 'WIN /S'
    for f in "$mat"/T*.WAV "$mat/SCALE.MID"; do
        set -- "$@" -f "$f=SB16T/$(basename "$f")"
    done
    case $s in qt*) export DROPIN_REF="$qtc" ;; *) unset DROPIN_REF ;; esac
    if [ "$s" = w16 ] || [ "$s" = qtw16 ]; then
        set -- "$@" -f "$dos/MMSYSTEM.DLL=WINDOWS/SYSTEM/MMSYSTEM.DLL" \
            -f "$dos/MMTASK.TSK=WINDOWS/SYSTEM/MMTASK.TSK"
    fi
    DROPIN_CAPTURE="$d.cap" DROPIN_CONF="[sblaster]
sbtype=sb16
sbbase=220
irq=5
dma=1
hdma=5
[midi]
mpu401=intelligent
mpubase=330
mididevice=synth
midiconfig=$mat/sine.sf2
[mixer]
rate=44100" "$root/tests/dropin.sh" "$@" > "$d.txt" 2>&1 || true
    tr -d '\r' < "$d/run/c/SBP.LOG" > "$d.log" 2>/dev/null || : > "$d.log"
}
for s in $sides; do run $s & done
wait
fails=0
for s in $sides; do
    d=$out/$s
    cap=$(ls "$d.cap"/*.wav 2>/dev/null | head -n 1)
    if ! grep -q '^SBP end' "$d.log"; then
        echo "FAIL $s: SBP did not finish ($d.log, $d.txt)"; fails=$((fails + 1)); continue
    fi
    # SBP's log: every header back, the positions at the end, every
    # recording header filled, nothing refused
    bad=$(python3 - "$d.log" <<'EOF'
import re, sys
log = open(sys.argv[1]).read().splitlines()
bad = []
fmt = None
for l in log:
    m = re.match(r'play \S+: format 1 (\d) (\d+) (\d+), (\d+) bytes, (\d+) headers(, looped)?', l)
    if m:
        ch, rate, bits, size = map(int, m.group(1, 2, 3, 4))
        fmt = (ch * bits // 8, rate, size, 3 if m.group(6) else 1)
        continue
    m = re.match(r'  done (\d+) of (\d+)', l)
    if m and m.group(1) != m.group(2):
        bad.append(l)
    m = re.match(r'  position: bytes (\d+), samples (\d+), ms (\d+) \(type (\d+)\)', l)
    if m and fmt:
        b, s, ms, t = map(int, m.groups())
        align, rate, size, loops = fmt
        want = size * loops
        if b != want or s != want // align or t != 1 or abs(ms - want // align * 1000 // rate) > 1:
            bad.append(l + ' (want %d bytes)' % want)
    if re.search(r'= [1-9]\d*$', l) and not l.startswith('snd '):
        bad.append(l)
    m = re.match(r'rec .*: (\d+) headers of (\d+) bytes', l)
    if m:
        recwant = int(m.group(1)) * int(m.group(2))
    m = re.match(r'  recorded (\d+) bytes', l)
    if m and int(m.group(1)) != recwant:
        bad.append(l + ' (want %d)' % recwant)
    if l.startswith('snd ') and not l.endswith('= 1'):
        bad.append(l)
    m = re.match(r'mci ".*" = (\d+)', l)
    if m and m.group(1) != '0':
        bad.append(l)
print('; '.join(bad))
EOF
)
    if [ -n "$bad" ]; then
        echo "FAIL $s log: $bad"; fails=$((fails + 1))
    else
        echo "ok   $s log: $(grep -c '^  done' "$d.log") plays, their positions, $(grep -c '^  recorded' "$d.log") recordings"
    fi
    if [ -z "$cap" ]; then
        echo "FAIL $s: no recording ($d.cap)"; fails=$((fails + 1)); continue
    fi
    man=manifest.txt
    case $s in qt*) man=qtmanifest.txt ;; esac
    if python3 "$root/tests/sb16/sbaudio.py" cmp "$cap" "$mat" $man > "$d.cmp" 2>&1; then
        echo "ok   $s sound: $(grep -c '^ok' "$d.cmp") checks ($d.cmp)"
    else
        echo "FAIL $s sound ($d.cmp):"; grep -v '^ok' "$d.cmp" | head -20
        fails=$((fails + 1))
    fi
done
exit $fails
