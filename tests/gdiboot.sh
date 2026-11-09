#!/bin/sh
# Boot Windows 3.1 with w16's GDI.EXE and see how far it gets.
#
# A scratch copy of the reference install (.tools/ref/win31/cdrive) gets
# our GDI.EXE as WINDOWS\SYSTEM\GDI.EXE; "WIN /S /B" runs in headless
# DOSBox with Microsoft's WIN.COM, DOSX, KERNEL (KRNL386, as on any 386),
# USER, drivers and fonts. /B makes KERNEL write BOOTLOG.TXT.
#
#   usage: tests/gdiboot.sh [options]
#     -g FILE    the GDI to drop in (default build/dos/GDI.EXE)
#     -r         keep Microsoft's GDI (the reference run)
#     -k         also drop in w16's KRNL286.EXE (KRNL386 removed, as
#                tests/dropin.sh does)
#     -l         log the display driver interface: GD1.DRV (make gdprobes;
#                linked here with VGA.DRV's resources) as
#                display.drv, in front of VGA.DRV renamed VGAREAL; the log
#                is C:\GD.LOG (copied to the -d directory)
#     -L         log GDI's own interface: GD2 (make gdprobes; linked here
#                with that GDI's resources) as GDI.EXE in front of the GDI
#                (ours, or Microsoft's with -r) renamed GDJ; also C:\GD.LOG, the two interleaved
#     -c CMD     the command line (default "WIN /S /B")
#     -t SECS    run time (default 45)
#     -f SRC=DST copy SRC to C:\DST as well (repeatable)
#     -i S:K=V   set key K to V in section S of SYSTEM.INI (repeatable)
#     -s         take a screenshot after SECS seconds (DOSBox on a virtual
#                X display, xvfb-run, never the user's desktop)
#     -d DIR     where results go (default: a temp dir, printed); gets
#                OUT.TXT, BOOTLOG.TXT, GD.LOG, screen.png
#     -C PNG     compare the screenshot with PNG (ImageMagick AE count;
#                diff.png in DIR)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
gdi=$root/build/dos/GDI.EXE
cmd='WIN /S /B' secs=45 shot= out= useref= ourk= log= glog= files= inis= cmpwith=
while getopts g:rklLc:t:f:i:sd:C: opt; do
    case $opt in
    g) gdi=$OPTARG ;;
    r) useref=1 ;;
    k) ourk=1 ;;
    l) log=1 ;;
    L) glog=1 ;;
    c) cmd=$OPTARG ;;
    t) secs=$OPTARG ;;
    f) files="$files
$OPTARG" ;;
    i) inis="$inis
$OPTARG" ;;
    s) shot=1 ;;
    d) out=$OPTARG ;;
    C) cmpwith=$OPTARG; shot=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
[ -n "$out" ] || out=$(mktemp -d)
mkdir -p "$out"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp -r "$ref" "$work/c"
sys=$work/c/WINDOWS/SYSTEM
rm -f "$work/c/WINDOWS/BOOTLOG.TXT"
[ -n "$useref" ] || cp "$gdi" "$sys/GDI.EXE"
if [ -n "$ourk" ]; then
    cp "$root/build/dos/KRNL286.EXE" "$sys/KRNL286.EXE"
    rm -f "$sys/KRNL386.EXE"
    inis="$inis
w16:watchdog=0"
fi
if [ -n "$log" ]; then
    python3 "$root/tools/nerename.py" "$sys/VGA.DRV" "$sys/VGAREAL.DLL" VGAREAL
    # GDI and USER load the display driver's resources (stock fonts,
    # configuration, cursors, bitmaps) from the module DISPLAY: link
    # VGA.DRV's into this scratch copy of GD1 (tools/ne2res.py)
    python3 "$root/tools/ne2res.py" "$sys/VGA.DRV" "$work/vga.res"
    b=$root/build/gdi
    (cd "$root" && PATH=${WATCOM:-/opt/watcom}/binl64:$PATH WATCOM=${WATCOM:-/opt/watcom} \
        wlink @$b/gd1.lnk name "$sys/GD1.DRV" libpath ${WATCOM:-/opt/watcom}/lib286/win \
        option resource="$work/vga.res" file { $b/gdlog.o $b/gd1stubs.o $b/gd1log.o }) >/dev/null
    inis="$inis
boot:display.drv=GD1.DRV"
fi
if [ -n "$glog" ]; then
    # GD2 as GDI.EXE in front of the GDI that would have run, renamed GDJ
    python3 "$root/tools/nerename.py" "$sys/GDI.EXE" "$sys/GDJ.DLL" GDJ
    python3 "$root/tools/ne2res.py" "$sys/GDI.EXE" "$work/gdi.res"
    b=$root/build/gdi
    (cd "$root" && PATH=${WATCOM:-/opt/watcom}/binl64:$PATH WATCOM=${WATCOM:-/opt/watcom} \
        wlink @$b/gd2.lnk name "$sys/GDI.EXE" libpath ${WATCOM:-/opt/watcom}/lib286/win \
        $( [ -s "$work/gdi.res" ] && echo option resource="$work/gdi.res" ) \
        file { $b/gdlog.o $b/gd2stubs.o $b/gd2log.o }) >/dev/null
fi
printf '%s\n' "$files" | while IFS= read -r f; do
    [ -n "$f" ] || continue
    src=${f%%=*} dst=${f#*=}
    if [ "$dst" = - ]; then rm -f "$work/c/$src"; else cp "$src" "$work/c/$dst"; fi
done
printf '%s\n' "$inis" | while IFS= read -r f; do
    [ -n "$f" ] || continue
    python3 - "$work/c/WINDOWS/SYSTEM.INI" "$f" <<'PY'
import sys
path, spec = sys.argv[1], sys.argv[2]
sec, kv = spec.split(':', 1)
key, val = kv.split('=', 1)
lines = open(path, 'rb').read().decode('latin-1').split('\r\n')
out, cur, done = [], None, False
for l in lines:
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
$cmd > C:\\OUT.TXT
echo WINEXIT >> C:\\OUT.TXT
exit
CONF
if [ -z "$shot" ]; then
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "$secs" \
        dosbox -noconsole -conf "$work/dosbox.conf" >/dev/null 2>&1 || true
else
    cat > "$work/shot.sh" <<SH
#!/bin/sh
SDL_AUDIODRIVER=dummy timeout $((secs + 5)) dosbox -noconsole -conf "$work/dosbox.conf" >/dev/null 2>&1 &
sleep $secs
wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
import -window "\${wid:-root}" "$work/s.png"
wait
SH
    chmod +x "$work/shot.sh"
    xvfb-run -a -s '-screen 0 1024x768x24' "$work/shot.sh" || true
    [ -f "$work/s.png" ] && cp "$work/s.png" "$out/screen.png"
fi
for f in OUT.TXT WINDOWS/BOOTLOG.TXT GD.LOG W16GDI.LOG; do
    [ -f "$work/c/$f" ] && tr -d '\r' < "$work/c/$f" > "$out/$(basename "$f")"
done
echo "== results in $out"
echo "== OUT.TXT"; cat "$out/OUT.TXT" 2>/dev/null || echo "(none: still running at the time limit?)"
echo "== BOOTLOG.TXT"; cat "$out/BOOTLOG.TXT" 2>/dev/null || echo "(none)"
log=$out/BOOTLOG.TXT
has() { [ -f "$log" ] && grep -qi "$1" "$log"; }
echo "== summary"
if has '^LoadSuccess = GDI.EXE'; then echo "GDI: initialised"; elif has '^LoadStart = GDI.EXE'; then echo "GDI: initialisation failed or did not return"; else echo "GDI: not reached"; fi
if has '^LoadSuccess = USER.EXE'; then echo "USER: initialised"; elif has '^LoadStart = USER.EXE'; then echo "USER: initialisation did not return"; else echo "USER: not reached"; fi
if has '^INITDONE=Final USER'; then echo "boot: complete (shell started)"; fi
[ -f "$out/GD.LOG" ] && echo "GD.LOG: $(grep -c '^ *D>' "$out/GD.LOG") display driver calls, $(grep -c '^ *G>' "$out/GD.LOG") GDI calls"
if [ -n "$cmpwith" ] && [ -f "$out/screen.png" ]; then
    d=$(magick compare -metric AE "$cmpwith" "$out/screen.png" "$out/diff.png" 2>&1 || true)
    echo "screen: $d pixels differ from $cmpwith"
fi
