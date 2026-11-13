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
#     -s PNG     also take a screenshot (DOSBox on a virtual X display,
#                tests/xvfb.sh, never the user's desktop) once Windows has
#                settled: BOOTLOG.TXT has reached "INITDONE=Final USER"
#                (with /B in the command line) and the screen has not
#                changed for DROPIN_SETTLE seconds (default 5; screens
#                compared every half second; a one-colour frame, a failed
#                capture, never counts as still), at the latest after SECS
#                seconds; DOSBox is then ended. DROPIN_FIXED=1 in the
#                environment: the old way, the screenshot after SECS
#                seconds whatever the screen does (for debugging).
#                With DROPIN_SHOTS="T1 T2 ..." in the environment, also
#                after T1, T2, ... seconds, as PNG with -T1 etc. before .png
#                (those still to come when DOSBox is ended are not taken)
#     -e PNG     with -s: the screen expected (another run's screenshot):
#                the screenshot is taken as soon as the screen has been
#                equal to PNG for 2 seconds (DROPIN_HOLD), instead of the
#                settling above; if it never is, after SECS seconds as
#                before, so a screen that differs is still taken at the
#                time limit
#     -W FILE    without -s: end the run as soon as C:\FILE (a path under
#                C:\, e.g. a probe's log) exists and has not changed for 2
#                seconds, at the latest after SECS seconds
#     -n         only prepare the scratch drive (with -o) and dosbox.conf;
#                do not start DOSBox
# DROPIN_NOREDIR=1 in the environment: the command's output is not
# redirected to C:\OUT.TXT (it goes to the screen).
# DROPIN_CYCLES in the environment: DOSBox's cycles= (default max, as
# fast as the host allows; "fixed N" runs N instructions per millisecond)
# DROPIN_CPUTYPE: DOSBox's cputype= instead (e.g. 486_slow, pentium_slow)
# DROPIN_CONF: more dosbox.conf lines (sections and settings), e.g. a
# [serial] section (tests/commprobe.sh)
# The report ends with "== shot: after N s (how)" for -s: settled,
# matched (-e), limit (SECS reached), exited (DOSBox ended first), fixed.
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
# And [w16] debuglog=C:\W16DBG.LOG: KERNEL's own diagnostics (unimplemented
# calls, WinOldApCall notes, [w16] schedlog, OutputDebugString) go there,
# never to the screen while Windows runs; printed after KERNEL's output.
#
# DROPIN_INI in the environment: more S:K=V settings, one per line, as -i
# (e.g. DROPIN_INI=w16:ExtendedUserHeap=1 for a whole boot-test run; they
# reach Microsoft's reference boots too, which ignore [w16]).
# Prints KERNEL's messages (C:\OUT.TXT: WIN's standard output), the boot
# log, and a summary: which modules loaded, whether USER's initialisation
# returned, whether the shell started.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
kernel=$root/build/dos/KRNL286.EXE
cmd='WIN /S /B' secs=60 shot= keep= useref= dosx= cpu=386 usex= files= expect= prep= endfile= inis='w16:watchdog=15
w16:exitmsg=1
w16:debuglog=C:\W16DBG.LOG'
# [w16] DesktopVersion=0: w16 USER's desktop version stamp (user/deskver.c)
# off, as stock Windows 3.1, so screens compare with 3.1's; a later
# DROPIN_INI or -i w16:DesktopVersion=1 replaces it (boot-test case deskver)
inis="$inis
w16:DesktopVersion=0"
[ -z "${DROPIN_INI:-}" ] || inis="$inis
$DROPIN_INI"
while getopts k:c:t:f:i:s:e:W:no:rxX:wp: opt; do
    case $opt in
    k) kernel=$OPTARG ;;
    c) cmd=$OPTARG ;;
    t) secs=$OPTARG ;;
    f) files="$files
$OPTARG" ;;
    i) inis="$inis
$OPTARG" ;;
    s) shot=$OPTARG ;;
    e) expect=$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG") ;;
    n) prep=1 ;;
    W) endfile=$OPTARG ;;
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
redir=" > C:\\OUT.TXT"
[ -n "${DROPIN_NOREDIR:-}" ] && redir=
common="[dosbox]
$xdos
memsize=16
${DROPIN_MACHINE:+machine=$DROPIN_MACHINE}
[cpu]
core=normal
cputype=${DROPIN_CPUTYPE:-$cpu}
cycles=${DROPIN_CYCLES:-max}
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c \"$work/c\"
c:
cd \\WINDOWS
$cmd$redir
echo WINEXIT >> C:\\OUT.TXT
exit"
printf '[sdl]\noutput=surface\n%s\n' "$common" > "$work/dosbox.conf"
# DROPIN_CONF in the environment: more dosbox.conf lines, put before the
# rest (e.g. "[serial]" and "serial1=nullmodem server:127.0.0.1
# port:N": tests/commprobe.sh's serial link)
if [ -n "${DROPIN_CONF:-}" ]; then
    { printf '%s\n' "$DROPIN_CONF"; cat "$work/dosbox.conf"; } > "$work/dosbox.conf.new"
    mv "$work/dosbox.conf.new" "$work/dosbox.conf"
fi
[ -z "$prep" ] || exit 0
if [ -z "$shot" ] && [ -n "$endfile" ]; then
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k 10 "$secs" \
        $emu -conf "$work/dosbox.conf" >/dev/null 2>&1 &
    dpid=$! last= since=
    while kill -0 "$dpid" 2>/dev/null; do
        sleep 0.5
        st=$(stat -c '%s %Y' "$work/c/$endfile" 2>/dev/null) || continue
        if [ "$st" != "$last" ]; then last=$st since=$(date +%s); continue; fi
        if [ $(($(date +%s) - since)) -ge 2 ]; then kill "$dpid" 2>/dev/null; break; fi
    done
    wait "$dpid" 2>/dev/null || true
elif [ -z "$shot" ]; then
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k 10 "$secs" \
        $emu -conf "$work/dosbox.conf" >/dev/null 2>&1 || true
else
    # when to take the screenshot: fixed (after SECS), expect (the screen
    # equal to -e's for DROPIN_HOLD seconds) or settle (USER's final init
    # in the boot log, with /B, and the screen still for DROPIN_SETTLE)
    mode=settle hold=${DROPIN_SETTLE:-5} bootlog=
    [ -z "$expect" ] || mode=expect hold=${DROPIN_HOLD:-2}
    [ -z "${DROPIN_FIXED:-}" ] || mode=fixed
    case $(printf '%s' "$cmd" | tr a-z A-Z) in *' /B'*) bootlog=$work/c/WINDOWS/BOOTLOG.TXT ;; esac
    cat > "$work/shot.sh" <<SH
#!/bin/sh
SDL_AUDIODRIVER=dummy timeout -k 10 $((secs + 5)) $emu -conf "$work/dosbox.conf" >/dev/null 2>&1 &
dpid=\$! pids=
for t in ${DROPIN_SHOTS:-}; do
    (sleep \$t; w=\$(xdotool search --name DOSBox 2>/dev/null | head -1); import -window "\${w:-root}" "$work/s-\$t.png") &
    pids="\$pids \$!"
done
grab() { w=\$(xdotool search --name DOSBox 2>/dev/null | head -1); timeout 30 import -window "\${w:-root}" "\$1" 2>/dev/null; }
same() { [ "\$(magick compare -metric AE "\$1" "\$2" null: 2>&1 | cut -d' ' -f1)" = 0 ]; }
t0=\$(date +%s) how=$mode
if [ $mode = fixed ]; then
    sleep $secs
else
    since= prevt=
    rm -f "$work/prev.png"
    while :; do
        if [ \$((\$(date +%s) - t0)) -ge $secs ]; then how=limit; break; fi
        if ! kill -0 \$dpid 2>/dev/null; then how=exited; break; fi
        sleep 0.5
        now=\$(date +%s)
        grab "$work/cur.png" || continue
        ok=
        if [ $mode = expect ]; then
            same "$work/cur.png" "$expect" && ok=\$now
        elif [ -n "\$prevt" ] && { [ -z "$bootlog" ] || grep -aq 'INITDONE=Final USER' "$bootlog" 2>/dev/null; } &&
             [ "\$(magick identify -format '%k' "$work/cur.png" 2>/dev/null)" != 1 ]; then
            same "$work/cur.png" "$work/prev.png" && ok=\$prevt
        fi
        mv "$work/cur.png" "$work/prev.png"; prevt=\$now
        if [ -z "\$ok" ]; then since=; continue; fi
        [ -n "\$since" ] || since=\$ok
        if [ \$((now - since)) -ge $hold ]; then
            [ $mode = expect ] && how=matched || how=settled
            break
        fi
    done
    rm -f "$work/prev.png"
fi
grab "$work/s.png"
echo "\$((\$(date +%s) - t0)) \$how" > "$work/shot.how"
# DOSBox ended now, not at its time limit (the old way waits for that)
[ $mode = fixed ] || kill \$dpid \$pids 2>/dev/null
wait
SH
    chmod +x "$work/shot.sh"
    "$root/tests/xvfb.sh" "$work/shot.sh" || true
    cp "$work/s.png" "$shot"
    for t in ${DROPIN_SHOTS:-}; do
        cp "$work/s-$t.png" "${shot%.png}-$t.png" 2>/dev/null || true
    done
fi
echo "== KERNEL output (C:\\OUT.TXT)"
if [ -f "$work/c/OUT.TXT" ]; then tr -d '\r' < "$work/c/OUT.TXT"; else echo "(none: still running at the time limit?)"; fi
if [ -f "$work/c/W16DBG.LOG" ]; then
    echo "== w16 diagnostics (C:\\W16DBG.LOG, [w16] debuglog)"
    tr -d '\r' < "$work/c/W16DBG.LOG"
fi
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
if [ -n "$shot" ] && [ -f "$work/shot.how" ]; then echo "== shot: after $(cut -d' ' -f1 "$work/shot.how") s ($(cut -d' ' -f2 "$work/shot.how"))"; fi
