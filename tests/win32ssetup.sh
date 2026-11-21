#!/bin/sh
# Build a Win32s variant of a reference install: a scratch copy of
# .tools/ref/win31/cdrive (or .tools/ref/wfw311a/cdrive) with Microsoft
# Win32s 1.30c installed by its own SETUP.EXE, run in 386 Enhanced mode
# in headless DOSBox-X from the user's disk images in .tools/win32s/src
# (disk1.img, disk2.img, disk3.img, found anywhere below it), then a
# 32-bit test program run on it. Test-only (ARCHITECTURE.md 3): the
# images and everything Setup writes are Microsoft's, used only in this
# scratch drive, never committed, never in the install tree; .tools is
# never modified. Black-box only: Setup is run, screenshotted and its
# files and INI lines recorded, never read.
#
#   usage: tests/win32ssetup.sh OUTDIR [win31|wfw311]
# OUTDIR/c is the C: drive (tests/dropin.sh DROPIN_REF=OUTDIR/c; stored
# as .tools/ref/win31s/cdrive and .tools/ref/wfw311s/cdrive, TESTING.md);
# OUTDIR/install.log says what Setup changed and what the test program
# wrote; OUTDIR/shot-*.png are the screens of each step, OUTDIR/emu*.log
# DOSBox-X's output, OUTDIR/steps.log the steps' timing.
# WIN32SSETUP_STEPS=FILE: run that steps file instead of the built-in one
# (the language is below; for finding out what a changed Setup asks).
#
# How it goes:
#  - the copy's SYSTEM.INI must allow paging, which Win32s needs: both
#    references have a temporary swap file ([386Enh] PagingFile=); a
#    Paging=no is removed (install.log says if anything was changed);
#  - session 1: WIN /3 A:\SETUP.EXE with disk 1's image as drive A.
#    Setup's screens (shot-1-* to shot-13-*, the same on 3.1 and WfW
#    3.11), each answered with Return (its default button):
#    the welcome ("You will also have the option of installing the Win32
#    game Freecell"; Continue); "Win32s Setup: SHARE.EXE is not loaded"
#    (OK; on both, though DOSBox-X's [dos] share=true is on, and WfW has
#    VSHARE; Setup goes on); the target, C:\WINDOWS\SYSTEM\ ("the other
#    components ... into the WIN32S subdirectory"; Continue); the copy,
#    then "Please insert the diskette labeled: Microsoft Win32s Setup Disk
#    2" (and 3): the next image is written over the mounted copy
#    (DOSBox-X reads the image file as it goes; the disks have no volume
#    labels) and OK; "Win32s files successfully installed." (OK);
#    "Freecell Setup" (Continue is the default: installed, as a user
#    taking the defaults would); "Verify Freecell Path",
#    C:\WIN32APP\FREECELL\ (Continue); "Freecell was successfully
#    installed." (OK); "Win32s System Setup": "Setup will now exit and
#    restart Microsoft Windows" (Continue): Windows ends, Setup's
#    _MSRSTRT.EXE and _MSSETUP.BAT run and are removed, Windows comes back
#    with Program Manager's new "Win32 Applications" group (Freecell);
#    then Alt+F4 and OK end Windows;
#  - the record: the files Setup added and changed, and the INI files'
#    diffs, against the copy as it was before (install.log);
#  - session 2: WIN /3 C:\W32T.EXE, the test program tests/win32s/w32t.c
#    (built here with OpenWatcom's wcc386 and wlink, as a PE for
#    Win32): it writes C:\W32T.LOG and shows a message box, which a key
#    closes; the log must say it ran. W32T.EXE and its log are removed
#    from OUTDIR/c afterwards, so the drive holds only Setup's work.
#
# Steps (WIN32SSETUP_STEPS; blank and ';' lines ignored):
#   session N CMD   start DOSBox-X session N, whose autoexec runs CMD
#                   (after C: and CD \WINDOWS), then exits
#   sleep S         wait S seconds
#   shot NAME       screenshot OUTDIR/shot-NAME.png
#   key KEYS        xdotool key names (Return, alt+c, Tab ...)
#   type TEXT       type TEXT
#   settle MAX S    wait until the screen has not changed for S seconds,
#                   at most MAX
#   still MAX S     wait until no file on C: has changed for S seconds, at
#                   most MAX
#   disk N          write disk N's image over drive A's
#   waitfile F MAX  wait until C:\F exists, at most MAX seconds
#   waitexit MAX    wait until the session's DOSBox-X has exited, at most
#                   MAX seconds (then end it)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)

if [ "${1:-}" = --run ]; then
    # (on the virtual display, from tests/xvfb.sh below)
    out=$2 steps=$3
    w= p= n=0
    log() { echo "$(date +%T) $*" >> "$out/steps.log"; }
    win() { xdotool search --name DOSBox 2>/dev/null | head -1; }
    grab() { import -window "$(win)" "$@" 2>/dev/null; }
    endsession() {
        [ -n "$p" ] || return 0
        kill "$p" 2>/dev/null || true
        wait "$p" 2>/dev/null || true
        p=
    }
    trap 'endsession' EXIT
    while IFS= read -r line; do
        set -f; set -- $line; set +f
        [ $# -gt 0 ] || continue
        case $1 in \;*) continue ;; esac
        log "$line"
        op=$1; shift
        case $op in
        session)
            endsession
            n=$1; shift
            { sed '/^@CMD@$/,$d' "$out/dosbox.conf"; printf '%s\nexit\n' "$*"; } > "$out/session$n.conf"
            k=0
            # (DOSBox-X now and then ends at once with "Can't init SDL":
            # started again, as tests/dropin.sh does, at most twice; its
            # standard input is not the steps file, which it would read)
            while :; do
                SDL_AUDIODRIVER=dummy timeout 1800 dosbox-x -nogui -fastlaunch \
                    -conf "$out/session$n.conf" < /dev/null > "$out/emu$n.log" 2>&1 &
                p=$!
                sleep 5
                if ! kill -0 "$p" 2>/dev/null && grep -q "Can't init SDL" "$out/emu$n.log" && [ $k -lt 2 ]; then
                    k=$((k + 1)); continue
                fi
                break
            done ;;
        sleep) sleep "$1" ;;
        shot) grab "$out/shot-$1.png" || true ;;
        key)
            w=$(win)
            xdotool windowactivate --sync "$w" 2>/dev/null || xdotool windowfocus "$w" 2>/dev/null || true
            xdotool key --window "$w" --delay 150 "$@" ;;
        type)
            w=$(win)
            xdotool windowactivate --sync "$w" 2>/dev/null || xdotool windowfocus "$w" 2>/dev/null || true
            xdotool type --window "$w" --delay 150 "$*" ;;
        settle)
            t0=$(date +%s) last= since=$t0
            while [ $(($(date +%s) - t0)) -lt "$1" ]; do
                now=$(grab rgb:- | md5sum)
                [ "$now" = "$last" ] || { last=$now since=$(date +%s); }
                [ $(($(date +%s) - since)) -ge "$2" ] && break
                sleep 0.5
            done
            log "  settled after $(($(date +%s) - t0)) s" ;;
        still)
            t0=$(date +%s) last= since=$t0
            while [ $(($(date +%s) - t0)) -lt "$1" ]; do
                now=$(find "$out/c" -type f -printf '%s %T@ %p\n' 2>/dev/null | sort | md5sum)
                [ "$now" = "$last" ] || { last=$now since=$(date +%s); }
                [ $(($(date +%s) - since)) -ge "$2" ] && break
                sleep 1
            done
            log "  still after $(($(date +%s) - t0)) s" ;;
        disk) dd if="$(cat "$out/disk$1")" of="$out/fd.img" conv=notrunc 2>/dev/null ;;
        waitfile)
            t0=$(date +%s)
            while [ ! -e "$out/c/$1" ] && [ $(($(date +%s) - t0)) -lt "$2" ]; do sleep 1; done
            log "  $1: $([ -e "$out/c/$1" ] && echo there || echo missing) after $(($(date +%s) - t0)) s" ;;
        waitexit)
            t0=$(date +%s)
            while [ -n "$p" ] && kill -0 "$p" 2>/dev/null && [ $(($(date +%s) - t0)) -lt "$1" ]; do sleep 1; done
            log "  exited: $(kill -0 "$p" 2>/dev/null && echo no || echo yes) after $(($(date +%s) - t0)) s"
            endsession ;;
        *) log "  unknown step $op" ;;
        esac
    done < "$steps"
    exit 0
fi

unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
out=${1:?usage: tests/win32ssetup.sh OUTDIR [win31|wfw311]}
ver=${2:-win31}
case $ver in
win31) ref=$root/.tools/ref/win31/cdrive ;;
wfw311) ref=$root/.tools/ref/wfw311a/cdrive ;;
*) echo "win32ssetup: unknown Windows version $ver (win31 or wfw311)" >&2; exit 2 ;;
esac
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "no reference install at $ref" >&2; exit 2; }
src=$root/.tools/win32s/src
for i in 1 2 3; do
    f=$(find "$src" -iname "disk$i.img" 2>/dev/null | head -1)
    [ -n "$f" ] || { echo "no Win32s disk image disk$i.img below $src" >&2; exit 2; }
    eval "d$i=\$f"
done
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
c=$out/c
cp -r "$ref" "$c"
for i in 1 2 3; do eval "printf '%s\n' \"\$d$i\"" > "$out/disk$i"; done
cp "$d1" "$out/fd.img"                  # drive A's disk (a copy)

# the test program
W=${WATCOM:-/opt/watcom}
mkdir -p "$out/w32t"
PATH=$W/binl64:$PATH WATCOM=$W wcc386 -q -bt=nt -zl -s -3s -ox -i="$W/h" -i="$W/h/nt" \
    -fo="$out/w32t/w32t.obj" "$root/tests/win32s/w32t.c"
PATH=$W/binl64:$PATH WATCOM=$W wlink option quiet system nt_win runtime windows=3.10 \
    name "$out/w32t/W32T.EXE" file "$out/w32t/w32t.obj" option start=_w32tmain \
    library kernel32,user32 libpath "$W/lib386/nt"

# paging, which Win32s needs
sys=$c/WINDOWS/SYSTEM.INI
prep=
if tr -d '\r' < "$sys" | grep -qi '^paging *= *no'; then
    python3 -I "$root/tests/scripts/iniset.py" "$sys" '386Enh:-Paging='
    prep="$prep [386Enh] Paging=no removed;"
fi
if ! tr -d '\r' < "$sys" | grep -qi '^PagingFile *='; then
    python3 -I "$root/tests/scripts/iniset.py" "$sys" '386Enh:PagingFile=C:\WINDOWS\WIN386.SWP'
    prep="$prep [386Enh] PagingFile=C:\\WINDOWS\\WIN386.SWP added;"
fi
cp -r "$c" "$out/before"

cat > "$out/dosbox.conf" <<EOF
[sdl]
output=surface
[dosbox]
quit warning=false
memsize=16
[cpu]
core=normal
cputype=486
cycles=max
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c "$c"
imgmount a "$out/fd.img" -t floppy
c:
cd \\WINDOWS
@CMD@
exit
EOF

steps=${WIN32SSETUP_STEPS:-}
if [ -z "$steps" ]; then
    steps=$out/steps.txt
    cat > "$steps" <<'EOF'
; session 1: Setup
session 1 WIN /3 A:\SETUP.EXE
sleep 20
settle 120 6
shot 1-welcome
key Return
settle 60 5
shot 2-share
key Return
settle 60 5
shot 3-target
key Return
settle 120 8
shot 4-copying
still 300 15
shot 5-disk2
disk 2
sleep 2
key Return
sleep 10
still 300 15
shot 6-disk3
disk 3
sleep 2
key Return
sleep 10
still 300 15
shot 7-installed
key Return
settle 120 8
shot 8-freecell
key Return
settle 120 8
shot 9-freecellpath
key Return
sleep 5
still 120 10
settle 60 5
shot 10-freecelldone
key Return
sleep 5
settle 120 10
shot 11-restart
key Return
sleep 30
settle 180 10
shot 12-restarted
key alt+F4
sleep 3
settle 30 3
shot 13-exit
key Return
waitexit 120
shot 14-after
EOF
fi
"$root/tests/xvfb.sh" "$0" --run "$out" "$steps" > "$out/run.out" 2>&1 || true
[ -f "$c/WINDOWS/SYSTEM/W32SYS.DLL" ] || {
    echo "win32ssetup: Setup did not finish (no C:\\WINDOWS\\SYSTEM\\W32SYS.DLL; $out/shot-*.png)" >&2; exit 1; }
rm -f "$c/WINDOWS/WIN386.SWP"           # the temporary swap file, if left

# what Setup did, against the copy as it was before
{
    [ -z "$prep" ] || echo "== prepared before Setup:$prep"
    echo "== files added (size, path)"
    (cd "$c" && find . -type f | sort) > "$out/after.lst"
    (cd "$out/before" && find . -type f | sort) > "$out/before.lst"
    comm -13 "$out/before.lst" "$out/after.lst" | while read -r f; do
        printf '%8s %s\n' "$(stat -c %s "$c/$f")" "${f#./}"; done
    echo "== files removed"
    comm -23 "$out/before.lst" "$out/after.lst" | sed 's|^\./||'
    echo "== files changed"
    comm -12 "$out/before.lst" "$out/after.lst" | while read -r f; do
        cmp -s "$out/before/$f" "$c/$f" || echo "${f#./}"; done
    echo "== directories added"
    (cd "$c" && find . -type d | sort) > "$out/afterd.lst"
    (cd "$out/before" && find . -type d | sort) > "$out/befored.lst"
    comm -13 "$out/befored.lst" "$out/afterd.lst" | sed 's|^\./||'
    comm -12 "$out/before.lst" "$out/after.lst" | grep -i '\.ini$' | while read -r f; do
        cmp -s "$out/before/$f" "$c/$f" && continue
        echo "== ${f#./} (diff against before Setup)"
        diff "$out/before/$f" "$c/$f" | tr -d '\r' || true
    done
    comm -13 "$out/before.lst" "$out/after.lst" | grep -i '\.ini$' | while read -r f; do
        echo "== ${f#./} (new)"; tr -d '\r' < "$c/$f"; echo
    done
} > "$out/install.log"
rm -f "$out"/*.lst

# session 2: the test program
cp -r "$c" "$out/setup"
cp "$out/w32t/W32T.EXE" "$c/W32T.EXE"
cat > "$out/test.txt" <<'EOF'
session 2 WIN /3 C:\W32T.EXE
waitfile W32T.LOG 180
sleep 3
settle 60 4
shot 20-w32t
key Return
sleep 5
settle 60 4
shot 21-w32tdone
key alt+F4
sleep 3
settle 30 3
shot 22-exit
key Return
waitexit 120
EOF
"$root/tests/xvfb.sh" "$0" --run "$out" "$out/test.txt" > "$out/run2.out" 2>&1 || true
{
    echo "== C:\\W32T.LOG (tests/win32s/w32t.c under Win32s)"
    if [ -f "$c/W32T.LOG" ]; then tr -d '\r' < "$c/W32T.LOG"; else echo "(none: the program did not run)"; fi
} >> "$out/install.log"
ok=0
grep -q '^W32T box=1' "$c/W32T.LOG" 2>/dev/null && ok=1
rm -f "$c/W32T.EXE" "$c/W32T.LOG" "$c/WINDOWS/WIN386.SWP" "$out/fd.img"
# (what the test session itself left, if anything: kept, and said)
diff -rq "$out/setup" "$c" > "$out/session2.diff" 2>&1 || true
{
    echo "== left by the test session (kept)"
    sed "s|$out/||g" "$out/session2.diff"
} >> "$out/install.log"
rm -rf "$out/before" "$out/setup" "$out/session2.diff"
[ $ok = 1 ] || { echo "win32ssetup: the 32-bit test program did not run ($out/install.log, $out/shot-2*.png)" >&2; exit 1; }
echo "win32ssetup: $out/c ($out/install.log)"
