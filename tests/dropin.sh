#!/bin/sh
# Drop w16's KERNEL into Windows 3.1 and see how far Standard mode boots.
#
# A scratch copy of the reference install (.tools/ref/win31/cdrive) gets
# our KRNL286.EXE as WINDOWS\SYSTEM\KRNL286.EXE. KRNL386.EXE is removed:
# on a 386, DOSX starts KRNL386 if it is there and KRNL286 only if not
# (Pietrek ch. 1; observed with TK18, ARCHITECTURE.md 9.4). Then
# "WIN /S /B" runs in headless DOSBox: Microsoft's WIN.COM, WSWAP and
# DOSX start our KERNEL, which boots Microsoft's drivers, GDI, USER and
# the shell. /B makes KERNEL write WINDOWS\BOOTLOG.TXT, as Windows does.
#
#   usage: tests/dropin.sh [options]
#     -k FILE    the KERNEL to drop in (default build/dos/KRNL286.EXE)
#     -c CMD     the command line (default "WIN /S /B")
#     -t SECS    run time limit (default 60)
#     -f SRC=DST copy SRC to C:\DST as well (repeatable), e.g. a test
#                program; DST "-" deletes SRC (a path under C:\) instead
#     -i S:K=V   set key K to V in section S of SYSTEM.INI (repeatable);
#                -i WIN.INI:S:K=V edits WIN.INI instead
#     -s PNG     also take a screenshot after SECS seconds (DOSBox on a
#                virtual X display, xvfb-run, never the user's desktop);
#                with DROPIN_SHOTS="T1 T2 ..." in the environment, also
#                after T1, T2, ... seconds, as PNG with -T1 etc. before .png
# DROPIN_CYCLES in the environment: DOSBox's cycles= (default max, as
# fast as the host allows; "fixed N" runs N instructions per millisecond)
#     -o DIR     keep the scratch C: drive in DIR (default: a temp dir)
#     -r         run the reference KERNEL instead (for comparison)
#     -x         run w16's DOS extender (build/dos/DOSX.EXE) instead of
#                Microsoft's DOSX.EXE; -X FILE: that file as DOSX.EXE
#     -p CPU     run in DOSBox-X with cputype=CPU (286: an emulated 80286,
#                KRNL286 and the 286 paths of the DOS extender; 386x:
#                DOSBox-X as a 386, which checks segment limits)
#     -w         use w16's WIN.COM (build/dos/WIN.COM) instead of Windows 3.1's
# A plain WIN (without /S) in the scratch copy picks 386 Enhanced mode on
# a 386 and stops with "Cannot find or load required file KRNL386.EXE",
# since KRNL386.EXE is deleted there: that is how 3.1 picks its mode.
# SYSTEM.INI gets [w16] watchdog=15: if the boot makes no progress for 15
# seconds, our KERNEL reports where the processor was and exits (-i
# w16:watchdog=0 turns that off). It also gets [w16] exitmsg=1: KERNEL prints
# "w16: back to DOS, code N" when Windows ends (tests look for it).
#
# Prints KERNEL's messages (C:\OUT.TXT: WIN's standard output), the boot
# log, and a summary: which modules loaded, whether USER's initialisation
# returned, whether the shell started.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
kernel=$root/build/dos/KRNL286.EXE
cmd='WIN /S /B' secs=60 shot= keep= useref= dosx= cpu=386 usex= files= inis='w16:watchdog=15
w16:exitmsg=1'
while getopts k:c:t:f:i:s:o:rxX:wp: opt; do
    case $opt in
    k) kernel=$OPTARG ;;
    c) cmd=$OPTARG ;;
    t) secs=$OPTARG ;;
    f) files="$files
$OPTARG" ;;
    i) inis="$inis
$OPTARG" ;;
    s) shot=$OPTARG ;;
    o) keep=$OPTARG ;;
    r) useref=1 ;;
    x) dosx=$root/build/dos/DOSX.EXE ;;
    X) dosx=$OPTARG ;;
    p) cpu=$OPTARG usex=1 ;;
    w) files="$files
$root/build/dos/WIN.COM=WINDOWS/WIN.COM" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
if [ -n "$keep" ]; then
    work=$keep
    [ -e "$work" ] && { echo "$work exists; choose a new directory" >&2; exit 2; }
    mkdir -p "$work"
else
    work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
fi
cp -r "$ref" "$work/c"
sys=$work/c/WINDOWS/SYSTEM
rm -f "$work/c/WINDOWS/BOOTLOG.TXT"
if [ -z "$useref" ]; then
    cp "$kernel" "$sys/KRNL286.EXE"
fi
rm -f "$sys/KRNL386.EXE"
if [ -n "$dosx" ]; then
    cp "$dosx" "$sys/DOSX.EXE"
fi
printf '%s\n' "$files" | while IFS= read -r f; do
    [ -n "$f" ] || continue
    src=${f%%=*} dst=${f#*=}
    if [ "$dst" = - ]; then rm -f "$work/c/$src"; else cp "$src" "$work/c/$dst"; fi
done
printf '%s\n' "$inis" | while IFS= read -r f; do
    [ -n "$f" ] || continue
    case $f in
    WIN.INI:*) ini=WIN.INI f=${f#WIN.INI:} ;;
    *) ini=SYSTEM.INI ;;
    esac
    python3 - "$work/c/WINDOWS/$ini" "$f" <<'PY'
import sys
path, spec = sys.argv[1], sys.argv[2]
sec, kv = spec.split(':', 1)
key, val = kv.split('=', 1)
lines = open(path, 'rb').read().decode('latin-1').split('\r\n')
out, cur, done = [], None, False
for i, l in enumerate(lines):
    s = l.strip()
    if s.startswith('['):
        if cur == sec.lower() and not done:
            out.append(f'{key}={val}'); done = True
        cur = s[1:s.find(']')].lower()
    elif cur == sec.lower() and '=' in s and s.split('=')[0].strip().lower() == key.lower():
        if not done:
            out.append(f'{key}={val}'); done = True
        continue
    out.append(l)
if not done and cur == sec.lower():
    while out and out[-1] == '':
        out.pop()
    out.append(f'{key}={val}')
elif not done:
    out += [f'[{sec}]', f'{key}={val}']
open(path, 'wb').write('\r\n'.join(out).encode('latin-1'))
PY
done
emu="dosbox -noconsole" xdos=
if [ -n "$usex" ]; then
    emu="dosbox-x -nogui -fastlaunch" xdos="quit warning=false"
    [ "$cpu" = 386x ] && cpu=386
fi
common="[dosbox]
$xdos
memsize=16
${DROPIN_MACHINE:+machine=$DROPIN_MACHINE}
[cpu]
core=normal
cputype=$cpu
cycles=${DROPIN_CYCLES:-max}
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c \"$work/c\"
c:
cd \\WINDOWS
$cmd > C:\\OUT.TXT
echo WINEXIT >> C:\\OUT.TXT
exit"
if [ -z "$shot" ]; then
    printf '[sdl]\noutput=surface\n%s\n' "$common" > "$work/dosbox.conf"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k 10 "$secs" \
        $emu -conf "$work/dosbox.conf" >/dev/null 2>&1 || true
else
    printf '[sdl]\noutput=surface\n%s\n' "$common" > "$work/dosbox.conf"
    cat > "$work/shot.sh" <<SH
#!/bin/sh
SDL_AUDIODRIVER=dummy timeout -k 10 $((secs + 5)) $emu -conf "$work/dosbox.conf" >/dev/null 2>&1 &
for t in ${DROPIN_SHOTS:-}; do
    (sleep \$t; w=\$(xdotool search --name DOSBox 2>/dev/null | head -1); import -window "\${w:-root}" "$work/s-\$t.png") &
done
sleep $secs
wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
import -window "\${wid:-root}" "$work/s.png"
wait
SH
    chmod +x "$work/shot.sh"
    xvfb-run -a -s '-screen 0 1024x768x24' "$work/shot.sh" || true
    cp "$work/s.png" "$shot"
    for t in ${DROPIN_SHOTS:-}; do
        cp "$work/s-$t.png" "${shot%.png}-$t.png" 2>/dev/null || true
    done
fi
echo "== KERNEL output (C:\\OUT.TXT)"
if [ -f "$work/c/OUT.TXT" ]; then tr -d '\r' < "$work/c/OUT.TXT"; else echo "(none: still running at the time limit?)"; fi
log=$work/c/WINDOWS/BOOTLOG.TXT
echo "== BOOTLOG.TXT"
if [ -f "$log" ]; then tr -d '\r' < "$log"; else echo "(none)"; fi
echo "== summary"
loaded=$( [ -f "$log" ] && tr -d '\r' < "$log" | sed -n 's/^LoadSuccess = //p' | tr '\n' ' ')
echo "loaded: ${loaded:-nothing}"
has() { [ -f "$log" ] && tr -d '\r' < "$log" | grep -qi "$1"; }
if has '^LoadSuccess = USER.EXE'; then echo "USER: initialised"; elif has '^LoadStart = USER.EXE'; then echo "USER: initialisation did not return"; else echo "USER: not reached"; fi
if has '^INIT=Final USER'; then echo "shell: loaded (Final USER init ran)"; elif has '^LoadStart = .*progman'; then echo "shell: load started"; else echo "shell: not reached"; fi
if has '^INITDONE=Final USER'; then echo "boot: complete"; fi
