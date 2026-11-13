#!/bin/sh
# Which packets after Enable reach a mouse driver's event procedure
# (tests/apps/md2c.c, tests/dos/ms3c.asm; results in tests/apps/md2logs/c/notes.md).
# MS3 is loaded before WIN (C:\GO.BAT), MD2 runs as the shell, and a steady
# stream of small xdotool moves into DOSBox's PS/2 mouse supplies the BIOS
# callbacks that MS3 puts MD2's packets into (tests/inputrun.sh).
#   usage: tests/md2cprobe.sh [-e EXP] [-o DIR] [-t SECS] [-- dropin.sh options...]
#     -e EXP   [w16] md2exp: 1 (default), 2 or 3 (md2c.c lists them)
#     -t SECS  total run time (default 240)
#   e.g. tests/md2cprobe.sh -e 1 -o build/md2-ms-e1 -- -r                 (Windows 3.1's driver)
#        tests/md2cprobe.sh -e 1 -o build/md2-w16-e1 -- -r -f build/dos/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV
# DOSBox only (in DOSBox-X xdotool's motion does not reach the guest).
# TK.LOG is copied to DIR.log.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
exp=1 out= total=240
while getopts e:o:t: opt; do
    case $opt in
    e) exp=$OPTARG ;;
    o) out=$OPTARG ;;
    t) total=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -n "$out" ] || out=$root/build/md2cprobe-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
printf 'C:\\MS3C.COM\r\nWIN /S /B\r\n' > "$tmp/GO.BAT"
{
    echo "; md2cprobe: capture click, then small moves back and forth"
    echo "click 1"
    i=0
    while [ $i -lt "$total" ]; do
        echo "move 10 0"; echo "sleep 0.1"; echo "move -10 0"; echo "sleep 0.1"
        i=$((i + 1))
    done
} > "$tmp/md2.txt"
"$root/tests/inputrun.sh" -b 30 -t "$total" "$tmp/md2.txt" "$out" -c 'C:\GO.BAT' \
    -f "$tmp/GO.BAT=GO.BAT" -f "$root/build/dos/MS3C.COM=MS3C.COM" \
    -f "$root/build/dos/MD2C.EXE=MD2C.EXE" -i 'boot:shell=C:\MD2C.EXE' \
    -i "w16:md2exp=$exp" "$@" > "$out.txt" 2>&1 || true
tr -d '\r' < "$out/work/c/WINDOWS/TK.LOG" > "$out.log" 2>/dev/null || : > "$out.log"
grep -q '^MD2 done' "$out.log" && echo "md2cprobe: done, $out.log" || { echo "md2cprobe: MD2 did not finish ($out.log, $out.txt)"; exit 1; }
