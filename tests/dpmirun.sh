#!/bin/sh
# dpmirun.sh OUTDIR KIND MODE [EMU]: DPMIPRB.COM (tests/dos/dpmiprb.asm), a
# raw DPMI 0.9 client, in DOS boxes in 386 Enhanced mode (ARCHITECTURE.md
# 12.16), from windowed Background PIFs started by WIN.INI run=.
#   KIND p  one box running DPMIPRB.BAT: the probe as a 16-bit client
#           twice (A, B: the second must see what the first saw, its
#           selectors and memory given back), as a 32-bit client twice
#           (C, D), then N (E): 16-bit, which EXECs the 32-bit probe (F)
#           from protected mode
#        g  one box: G16 (tag G), then G32 (tag H): the probe, then a
#           general protection fault no handler takes; DPGAFT.TXT is
#           written by the batch file after them (the box went on)
#        c  two boxes at once: C16 (tag X) and C32 (tag Y), each 256 KB of
#           its own memory checked while the other runs
#   MODE ms    Windows 3.1 with Microsoft's WIN386.EXE and KRNL386
#        w16   the same with w16's WIN386.EXE
#        tree  w16's install tree alone (build/w16), WIN /3
#   EMU  x (default) DOSBox-X, dosbox plain DOSBox
#   (Arguments after EMU are ignored: the boot test names files there for
#   its reference cache's key.)
# Environment: DPMIRUN_PIF more tools/mkpif.py settings (e.g. xmslim=4096);
# DPMIRUN_COM1=1 COM1 (w16's VMM trace) in OUTDIR/com1.txt;
# DPMIRUN_INI="S:K=V ..." SYSTEM.INI settings; DPMIRUN_WIN the command
# (default "WIN /3"; "WIN /S" for Standard mode); DPMIRUN_TIME (default
# 240 s, 100 for g); DROPIN_CYCLES (default max), DROPIN_CPUTYPE (default 386).
# Prints the logs (OUTDIR/DP?.LOG); a screenshot at the end in
# OUTDIR/shot.png.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 kind=$2 mode=$3 emu=${4:-x}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c" "$o"/DP*.LOG "$o/shot.png"
ref=$root/.tools/ref/win31/cdrive
case $mode in
ms|w16) wd=WINDOWS ;;
tree) wd=W16 ;;
*) echo "mode: ms, w16 or tree" >&2; exit 2 ;;
esac
conf=
[ -n "${DPMIRUN_COM1:-}" ] && conf="[serial]
serial1=file file:$o/com1.txt"
case $kind in
p)  logs="A B C D E F G H" boxes=P
    printf 'DPMIPRB 16 A\r\nDPMIPRB 16 B\r\nDPMIPRB 32 C\r\nDPMIPRB 32 D\r\nDPMIPRB N E\r\nDPMIPRB O G\r\nECHO done>DPDONE.TXT\r\n' > "$o/DPP.BAT"
    wait=$wd/DPDONE.TXT ;;
g)  logs="G H" boxes=P
    printf 'DPMIPRB G16 G\r\nDPMIPRB G32 H\r\nECHO after>DPGAFT.TXT\r\n' > "$o/DPP.BAT"
    wait= ;;
c)  logs="X Y" boxes="X Y"
    printf 'DPMIPRB C16 X\r\n' > "$o/DPX.BAT"
    printf 'DPMIPRB C32 Y\r\n' > "$o/DPY.BAT"
    wait=$wd/DPX.LOG,$wd/DPY.LOG ;;
*)  echo "kind: p, g or c" >&2; exit 2 ;;
esac
tl=240
[ "$kind" = g ] && tl=100           # (nothing to wait for: the box ends)
set -- -o "$o/c" -t "${DPMIRUN_TIME:-$tl}" -s "$o/shot.png" \
    -f "$root/build/dos/DPMIPRB.COM=$wd/DPMIPRB.COM"
run=
for b in $boxes; do
    # shellcheck disable=SC2086
    python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/DP$b.PIF" "title=DPMIPRB $b" \
        "program=C:\\$wd\\DP$b.BAT" "dir=C:\\$wd" windowed=1 background=1 ${DPMIRUN_PIF:-} || exit 2
    set -- "$@" -f "$o/DP$b.PIF=$wd/DP$b.PIF" -f "$o/DP$b.BAT=$wd/DP$b.BAT"
    run="$run C:\\$wd\\DP$b.PIF"
done
set -- "$@" -i "WIN.INI:windows:run=${run# }" -c "${DPMIRUN_WIN:-WIN /3}"
[ -n "$wait" ] && set -- "$@" -W "$wait"
for i in ${DPMIRUN_INI:-}; do set -- "$@" -i "$i"; done
[ "$emu" = dosbox ] || set -- "$@" -p 386
case $mode in
ms) set -- "$@" -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" ;;
w16) set -- "$@" -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
        -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" ;;
tree) set -- "$@" -T "$root/build/w16" -3 ;;
esac
DROPIN_FIXED=1 DROPIN_CONF=$conf DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_WSECS=3 \
    "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
for t in $logs; do
    l=$(find "$o/c" -iname "DP$t.LOG" | head -1)
    [ -n "$l" ] && tr -d '\r' < "$l" > "$o/DP$t.LOG"
    echo "== DP$t.LOG"
    cat "$o/DP$t.LOG" 2>/dev/null || echo "(none)"
done
rm -f "$o/DPDONE.TXT" "$o/DPGAFT.TXT"
for f in DPDONE.TXT DPGAFT.TXT; do
    g=$(find "$o/c" -iname $f | head -1)
    [ -n "$g" ] && cp "$g" "$o/$f" && echo "== $f there"
done
exit 0
