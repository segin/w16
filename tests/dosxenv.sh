#!/bin/sh
# What DOSX does to the environment it gives KERNEL (ARCHITECTURE.md
# 9.6): DOSX.EXE run directly (no WIN.COM, which appends windir= itself)
# in a scratch copy of the reference install, with TK18 (tests/dos/
# tk18.asm) as KRNL286.EXE logging its environment and its parents', in
# four cases: no windir at all, an upper-case WINDIR=C:\FOO (COMMAND's
# SET), a lower-case windir=C:\FOO (WINENV.COM), and DOSX started from
# another directory. Under Microsoft's DOSX and w16's; prints KERNEL's
# environment strings per case and the differences.
#   usage: tests/dosxenv.sh [-o DIR]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
out=$root/build/dosxenv
while getopts o: opt; do
    case $opt in o) out=$OPTARG ;; *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;; esac
done
mkdir -p "$out"
run() {   # run HOST CASE 'commands'
    work=$(mktemp -d)
    cp -r "$ref" "$work/c"
    rm -f "$work/c/WINDOWS/SYSTEM/KRNL386.EXE"
    cp "$root/build/dos/TK18.COM" "$work/c/WINDOWS/SYSTEM/KRNL286.EXE"
    cp "$root/build/dos/WINENV.COM" "$work/c/"
    [ "$1" = w16 ] && cp "$root/build/dos/DOSX.EXE" "$work/c/WINDOWS/SYSTEM/DOSX.EXE"
    printf '[sdl]\noutput=surface\n[dosbox]\nmemsize=16\n[cpu]\ncore=normal\ncputype=386\ncycles=max\n[mixer]\nnosound=true\n[autoexec]\nmount c "%s"\nc:\n%s\nexit\n' \
        "$work/c" "$3" > "$work/dosbox.conf"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 40 dosbox -noconsole -conf "$work/dosbox.conf" >/dev/null 2>&1 || true
    if [ -f "$work/c/TK18.LOG" ]; then
        tr -d '\r' < "$work/c/TK18.LOG" | grep '^OBS envstr0=\|^OBS tail0=\|^OBS progpath0=' > "$out/$2.$1"
    else
        echo "(no TK18.LOG)" > "$out/$2.$1"
    fi
    rm -rf "$work"
}
for h in ms w16; do
    run $h none 'cd \WINDOWS
SYSTEM\DOSX.EXE krnl286 a b'
    run $h upper 'SET WINDIR=C:\FOO
cd \WINDOWS
SYSTEM\DOSX.EXE krnl286'
    run $h lower 'cd \WINDOWS
C:\WINENV.COM C:\FOO SYSTEM\DOSX.EXE krnl286'
    run $h elsewhere 'C:\WINDOWS\SYSTEM\DOSX.EXE krnl286'
done
status=0
for c in none upper lower elsewhere; do
    echo "== $c (Microsoft's DOSX)"
    cat "$out/$c.ms"
    if cmp -s "$out/$c.ms" "$out/$c.w16"; then
        echo "== $c: w16's DOSX gives the same"
    else
        echo "== $c: w16's DOSX differs"; diff "$out/$c.ms" "$out/$c.w16" || true; status=1
    fi
done
exit $status
