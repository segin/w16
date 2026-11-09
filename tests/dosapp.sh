#!/bin/sh
# A DOS program started from Standard-mode Windows, under Microsoft's DOSX
# and under w16's (ARCHITECTURE.md 9.6): WIN /S with Microsoft's KRNL286
# (WINOLDAP needs it), DX7.EXE as the shell, which runs DXD.COM through
# WinExec, so that WINOLDAP and WSWAP swap Windows out and back.
#
#   usage: tests/dosapp.sh [-m ms|w16|both] [-s] [-n N] [-t SECS] [-o DIR]
#     -m   which DOSX (default both)
#     -s   also put WC3 (tests/dos/wc3.asm, the INT 2Fh spy) between WSWAP
#          and DOSX: C:\WC.LOG gets every INT 2Fh call DOSX and the
#          programs below it make in real mode
#     -n   start DXD N times (default 1)
#     -o   keep the logs in DIR (default build/dosapp)
# Prints, per DOSX: DX7's log (TK.LOG), DXD's (DXD.LOG), the spy's, WIN's
# output; then the diff of DXD's logs.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
which=both spy= n=1 secs=90 out=$root/build/dosapp
while getopts m:sn:t:o: opt; do
    case $opt in
    m) which=$OPTARG ;;
    s) spy=1 ;;
    n) n=$OPTARG ;;
    t) secs=$OPTARG ;;
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
case $which in both) hosts="ms w16" ;; *) hosts=$which ;; esac
mkdir -p "$out"
dos=$root/build/dos
for h in $hosts; do
    rm -rf "$out/$h"
    set -- -r -t "$secs" -o "$out/$h" -f "$dos/DX7.EXE=DX7.EXE" -f "$dos/DXD.COM=DXD.COM" \
        -i "boot:shell=C:\\DX7.EXE" -i "NonWindowsApp:SwapDisk=C:\\"
    [ "$h" = w16 ] && set -- "$@" -x
    if [ -n "$spy" ]; then
        if [ "$h" = w16 ]; then src=$dos/DOSX.EXE; else src=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/DOSX.EXE; fi
        set -- "$@" -X "$dos/WC3.COM" -f "$src=WINDOWS/SYSTEM/DOSX.EX_"
    fi
    "$root/tests/dropin.sh" "$@" > "$out/$h.dropin" 2>&1 || true
    c=$out/$h/c
    {
        echo "== DX7 (shell)"; for f in "$c/WINDOWS/TK.LOG" "$c/TK.LOG"; do [ -f "$f" ] && tr -d '\r' < "$f"; done
        echo "== DXD"; [ -f "$c/DXD.LOG" ] && tr -d '\r' < "$c/DXD.LOG"
        if [ -n "$spy" ]; then echo "== INT 2Fh spy"; [ -f "$c/WC.LOG" ] && tr -d '\r' < "$c/WC.LOG"; fi
        echo "== WIN"; sed -n '/^== KERNEL output/,/^== BOOTLOG/p' "$out/$h.dropin" | sed '1d;$d'
    } > "$out/$h.log"
    echo "==== $h DOSX"; cat "$out/$h.log"
done
if [ "$which" = both ]; then
    echo "==== DXD: Microsoft's DOSX (-) versus w16's (+)"
    sed -n '/^== DXD/,/^== /p' "$out/ms.log" > "$out/ms.dxd"
    sed -n '/^== DXD/,/^== /p' "$out/w16.log" > "$out/w16.dxd"
    diff -u "$out/ms.dxd" "$out/w16.dxd" | tail -n +3 || true
fi
