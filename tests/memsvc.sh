#!/bin/sh
# The VMM's memory, paging and page-hooking services (docs/notes/
# win386-memsvc-audit.md, ARCHITECTURE.md 12.19): Windows 3.1's WIN386
# (ms) or w16's (w16) in DOSBox-X with the probe VxD VXH.386
# (tests/vxd/vxh.c: the services at Device_Init and Init_Complete, a
# hooked V86 page, a V86 fault hook, the V86 API) writing to COM1, and
# PGT.COM (tests/dos/pgt.asm) in two MS-DOS Prompts
# (tests/scripts/input/memsvc.txt).
#   usage: tests/memsvc.sh [-o DIR] [-k FILE] ms|w16
#     -o DIR   the output directory (default build/memsvc-SIDE)
#     -k FILE  ignored (the boot test's reference cache key)
#   tests/memsvc.sh -c REFDIR W16DIR: compare two runs (the VXH lines,
#   the '~' tokens left out); exit status 1 if they differ
#   MEMSVC_WIN386=FILE: that WIN386.EXE for w16's side; MEMSVC_INI="sec:key=val ...":
#   more SYSTEM.INI settings (386Enh:VXHSkip=N leaves groups of VXH's out)
# Output: com1.txt (COM1: the VXH lines and the VMM's), vxh.txt (the VXH
# lines), run/ (the input run, its shots).
set -u
unset DISPLAY WAYLAND_DISPLAY
cd "$(dirname "$0")/.."
root=$PWD
if [ "${1:-}" = -c ]; then
    a=$2 b=$3
    for d in "$a" "$b"; do
        sed 's/ ~[^ ]*//g' "$d/vxh.txt" | grep -v '^VXH *$' > "$d/vxh.cmp"
    done
    if ! grep -q '^VXH System_Exit' "$a/vxh.cmp"; then echo "the reference did not reach System_Exit"; exit 1; fi
    if diff "$a/vxh.cmp" "$b/vxh.cmp" > "$b/vxh.diff"; then
        echo "$(wc -l < "$a/vxh.cmp") VXH lines as 3.1's"; exit 0
    fi
    echo "$(grep -c '^[<>]' "$b/vxh.diff") VXH line(s) differ from 3.1's ($b/vxh.diff)"; exit 1
fi
odir=
while getopts o:k: opt; do
    case $opt in
    o) odir=$OPTARG ;;
    k) ;;
    *) sed -n '2,/^set -u/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
side=$1
o=${odir:-$root/build/memsvc-$side}
rm -rf "$o"
mkdir -p "$o"
o=$(cd "$o" && pwd)
K=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
set -- -r -p 386 -c "WIN /3 /B" -f "$K=WINDOWS/SYSTEM/KRNL386.EXE" \
    -f "$root/build/dos/PGT.COM=WINDOWS/PGT.COM" \
    -f "$root/build/dos/VXH.386=WINDOWS/SYSTEM/VXH.386" -i "386Enh:+device=vxh.386"
for kv in ${MEMSVC_INI:-}; do set -- "$@" -i "$kv"; done
[ "$side" = w16 ] && set -- "$@" -f "${MEMSVC_WIN386:-$root/build/win386/WIN386.EXE}=WINDOWS/SYSTEM/WIN386.EXE"
DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 \
DROPIN_CONF="[serial]
serial1=file file:$o/com1.raw" timeout 600 tests/inputrun.sh -s -b 200 -t 400 \
    "$root/tests/scripts/input/memsvc.txt" "$o/run" "$@" > "$o/run.txt" 2>&1
tr -d '\r' < "$o/com1.raw" 2>/dev/null | grep -a -v '^$' > "$o/com1.txt"
grep -a '^VXH ' "$o/com1.txt" > "$o/vxh.txt"
echo "== $side: $(tail -n 1 "$o/run/input.log" 2>/dev/null)"
echo "VXH lines: $(wc -l < "$o/vxh.txt")"
