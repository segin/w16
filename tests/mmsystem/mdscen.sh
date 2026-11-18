#!/bin/sh
# The MMSYSTEM device probes (layer 3: MMP.EXE's sections n e w i m j a y
# x b, tests/mmsystem/mmdev.c) on Microsoft's MMSYSTEM.DLL and on w16's,
# over the same Microsoft device drivers, each pair compared by
# tests/mmsystem/mcmp.py (ARCHITECTURE.md 23). A case is
# NAME:SOUND:SHIM:SECTIONS, SOUND as tests/mmsystem/mrun.sh's MRUN_SND
# (none, sb2, sb16), SHIM 1 for the logging pass-through drivers
# (MDS*.DRV) between MMSYSTEM and the drivers, 0 without.
#   usage: tests/mmsystem/mdscen.sh [-o OUTDIR] [-m] [CASE...]
#     -m   Microsoft's MMSYSTEM only (to look at its logs)
# Default cases: the list below. Exit status 0 when every pair agrees.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out= msonly=
while getopts o:m opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) msonly=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
cases=${*:-none:none:1:nwimjayexb sb2n:sb2:1:n sb2e:sb2:1:e sb2w:sb2:1:w sb2i:sb2:1:i \
sb2m:sb2:1:mja sb2y:sb2:1:y sb2x:sb2:1:x sb2b:sb2:1:b sb2plain:sb2:0:nwimjayxb}
[ -n "$out" ] || out=$root/build/mdscen-$(date +%H%M%S)
mkdir -p "$out"
sides="ms w16"
[ -n "$msonly" ] && sides=ms
for c in $cases; do
    name=${c%%:*} r=${c#*:}
    snd=${r%%:*} r=${r#*:}
    shim=${r%%:*} sec=${r#*:}
    [ "$shim" = 1 ] || shim=
    for s in $sides; do
        MRUN_SND=$snd MRUN_SHIM=$shim MRUN_ARG=$sec \
            "$root/tests/mmsystem/mrun.sh" "$out/$name-$s" $s > "$out/$name-$s.txt" 2>&1 &
    done
done
wait
[ -n "$msonly" ] && { for c in $cases; do n=${c%%:*}; echo "$n: $(wc -l < "$out/$n-ms/mmp.log" 2>/dev/null) lines"; done; exit 0; }
fails=0
for c in $cases; do
    n=${c%%:*}
    a=$out/$n-ms/mmp.log b=$out/$n-w16/mmp.log
    if [ ! -s "$a" ] || [ ! -s "$b" ]; then
        echo "FAIL $n: no log"; fails=$((fails + 1)); continue
    fi
    if python3 "$root/tests/mmsystem/mcmp.py" "$a" "$b" > "$out/cmp-$n.txt" 2>&1; then
        echo "ok   $n: $(tail -n 1 "$out/cmp-$n.txt")"
    else
        echo "FAIL $n: $(grep -m1 FAIL "$out/cmp-$n.txt") ($out/cmp-$n.txt)"; fails=$((fails + 1))
    fi
done
exit $fails
