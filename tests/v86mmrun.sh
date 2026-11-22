#!/bin/sh
# v86mmrun.sh OUTDIR [w16]: what a DOS program gets from the XMS and EMS
# drivers in a DOS box in 386 Enhanced mode (V86MMGR, ARCHITECTURE.md
# 12.13): VX15.COM (tests/dos/vx15.asm) runs in a windowed box started by
# WIN.INI run= from a PIF made from the reference install's DOSPRMPT.PIF,
# under Microsoft's WIN386.EXE, or w16's (second argument w16), with
# Microsoft's KRNL386 and the rest. Prints OUTDIR/VX15.LOG.
# Environment: V86MM_PIF="key=value ..." more tools/mkpif.py settings
# (xmslim=, xmsreq=, emslim=, emsreq=, hma=, emslock=, xmslock=);
# V86MM_TAIL VX15's command tail (Q: version and free memory only);
# V86MM_INI="S:K=V ..." SYSTEM.INI settings (e.g. 386Enh:NoEMMDriver=on);
# V86MM_EMS=false: DOSBox's own EMS off ([dos] ems=false; DOSBox has it on
# by default, and Windows 3.1 then gave its boxes no EMS);
# V86MM_VXT=1: the probe VxD VXT.386 too (DOSBox-X, OUTDIR/com1.txt);
# V86MM_COM1=1: COM1 there alone (w16's VMM trace);
# V86MM_DOSBOX=x for DOSBox-X (default plain DOSBox); DROPIN_CYCLES
# (default fixed 30000); V86MM_EXTRA more dropin.sh arguments.
# (A second argument other than w16 and any after it are ignored: the boot
# test names files there for its reference cache's key.)
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 w=${2:-}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/c" "$o/VX15.LOG"
ref=$root/.tools/ref/win31/cdrive
# shellcheck disable=SC2086
python3 "$root/tools/mkpif.py" "$ref/WINDOWS/DOSPRMPT.PIF" "$o/VX15.PIF" "title=VX15" \
    'program=C:\WINDOWS\VX15.COM' "params=${V86MM_TAIL:-}" 'dir=C:\WINDOWS' windowed=1 ${V86MM_PIF:-} || exit 2
set -- -r -c "WIN /3 /B" -t ${V86MM_TIME:-400} -o "$o/c" -W WINDOWS/VX15.LOG \
    -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
    -f "$root/build/dos/VX15.COM=WINDOWS/VX15.COM" -f "$o/VX15.PIF=WINDOWS/VX15.PIF" \
    -i 'WIN.INI:windows:run=C:\WINDOWS\VX15.PIF' ${V86MM_EXTRA:-}
for i in ${V86MM_INI:-}; do set -- "$@" -i "$i"; done
[ "${V86MM_DOSBOX:-}" = x ] && set -- "$@" -p 386
[ "$w" = w16 ] && set -- "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
conf=
[ -n "${V86MM_EMS:-}" ] && conf="[dos]
ems=${V86MM_EMS}"
if [ -n "${V86MM_VXT:-}" ]; then
    # the probe VxD VXT.386 (its V86MMGR lines) and COM1 in OUTDIR/com1.txt
    [ "${V86MM_DOSBOX:-}" = x ] || set -- "$@" -p 386
    set -- "$@" -f "$root/build/dos/VXT.386=WINDOWS/SYSTEM/VXT.386" -i "386Enh:+device=vxt.386"
    conf="$conf
[serial]
serial1=file file:$o/com1.txt"
    rm -f "$o/com1.txt"
elif [ -n "${V86MM_COM1:-}" ]; then
    [ "${V86MM_DOSBOX:-}" = x ] || set -- "$@" -p 386
    conf="$conf
[serial]
serial1=file file:$o/com1.txt"
    rm -f "$o/com1.txt"
fi
DROPIN_CONF=$conf DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} DROPIN_CYCLES=${DROPIN_CYCLES:-fixed 30000} \
    DROPIN_WSECS=5 "$root/tests/dropin.sh" "$@" > "$o/dropin.txt" 2>&1
l=$(find "$o/c" -iname VX15.LOG | head -1)
[ -n "$l" ] && tr -d '\r' < "$l" > "$o/VX15.LOG"
cat "$o/VX15.LOG" 2>/dev/null || { echo "no VX15.LOG"; tail -5 "$o/dropin.txt"; }
exit 0
