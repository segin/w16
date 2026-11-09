#!/bin/sh
# Probe US1 (ARCHITECTURE.md 10): record the calls into USER and the calls
# USER makes into GDI and the drivers while Windows 3.1 boots.
#
# In a scratch copy of the reference install (tests/dropin.sh does the
# copying; the reference itself is never touched):
#   SYSTEM\XUSR.DLL      Microsoft's USER.EXE, module renamed XUSR, its
#                        imports of GDI, DISPLAY, KEYBOARD, MOUSE, SYSTEM,
#                        SOUND and COMM pointed at the forwarders below
#                        (tools/nerename.py: header tables only)
#   SYSTEM\USER.EXE      w16's forwarder: module USER, every USER ordinal,
#                        recorded and passed to XUSR (tools/mkinterpose.py)
#   SYSTEM\GDX.DLL ...   forwarders GDX, DISPLAX, KEYBOARX, MOUSX, SYSTEX,
#                        SOUNX, COMX to the real modules, returns recorded
#   SYSTEM\USTRACE.DLL   the recorder (tests/user/ustrace.c)
# then WIN /S boots (Microsoft's KRNL286 by default) and tools/ustrace.py
# decodes C:\USTRACE.BIN and C:\USCOUNT.BIN.
#
#   usage: tests/ustrace.sh [-k] [-t SECS] [-c CMD] [-i S:K=V]... [-f SRC=DST]... OUTDIR
#     -k     boot w16's KRNL286.EXE instead of Microsoft's
#     -t, -c, -i, -f as for tests/dropin.sh; OUTDIR must not exist; it
#     receives trace.txt (decoded), the raw files, a screenshot (shot.png)
#     and the dropin.sh report (boot.txt)
# Microsoft's binaries are only read for their NE header tables (names,
# ordinals, imports), never disassembled.
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM
specs=$root/.tools/wine/specs
secs=60 kflag=-r extra=
while getopts kt:c:i:f: opt; do
    case $opt in
    k) kflag= ;;
    t) secs=$OPTARG ;;
    c) extra="$extra -c '$OPTARG'" ;;
    i) extra="$extra -i '$OPTARG'" ;;
    f) extra="$extra -f '$OPTARG'" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
out=${1:?usage: tests/ustrace.sh [options] OUTDIR}
[ -e "$out" ] && { echo "$out exists" >&2; exit 2; }
mkdir -p "$out/gen"
out=$(cd "$out" && pwd)
make -C "$root" -s build/dos/USTRACE.DLL >/dev/null
gen() {   # NAME TARGET REALFILE MODID SPEC RET [--args ...]
    n=$1 t=$2 f=$3 id=$4 sp=$5 r=$6; shift 6
    python3 "$root/tools/mkinterpose.py" --name "$n" --target "$t" --real "$f" \
        --modid "$id" --spec "$specs/$sp.spec" --ret "$r" "$@" "$out/gen" >/dev/null
    nasm -f obj -o "$out/gen/$n.o" "$out/gen/$n.asm"
    env WATCOM=/opt/watcom /opt/watcom/binl64/wlink @"$out/gen/$n.lnk" \
        name "$out/gen/$n.DLL" file "$out/gen/$n.o" >/dev/null
}
gen USER XUSR "$ref/USER.EXE" 0 user ''
mv "$out/gen/USER.DLL" "$out/gen/USER.EXE"
gen GDX GDI "$ref/GDI.EXE" 1 gdi all
# DISPLAY's cursor entries: argument layouts from the DDK documentation
# (Inquire(lpCursorInfo), SetCursor(lpCursorShape), MoveCursor(x, y),
# CheckCursor())
gen DISPLAX DISPLAY "$ref/VGA.DRV" 2 display all \
    --args 101:ptr --args 102:ptr --args 103:word,word --args 104:
gen KEYBOARX KEYBOARD "$ref/KEYBOARD.DRV" 3 keyboard all
gen MOUSX MOUSE "$ref/MOUSE.DRV" 4 mouse all
gen SYSTEX SYSTEM "$ref/SYSTEM.DRV" 5 system all
gen SOUNX SOUND "$ref/MMSOUND.DRV" 6 sound all
gen COMX COMM "$ref/COMM.DRV" 7 comm all
cp "$ref/USER.EXE" "$out/gen/XUSR.DLL"
python3 "$root/tools/nerename.py" "$out/gen/XUSR.DLL" --module XUSR \
    --import GDI=GDX --import DISPLAY=DISPLAX --import KEYBOARD=KEYBOARX \
    --import MOUSE=MOUSX --import SYSTEM=SYSTEX --import SOUND=SOUNX --import COMM=COMX
files=
for f in USER.EXE XUSR.DLL GDX.DLL DISPLAX.DLL KEYBOARX.DLL MOUSX.DLL SYSTEX.DLL SOUNX.DLL COMX.DLL; do
    files="$files -f $out/gen/$f=WINDOWS/SYSTEM/$f"
done
files="$files -f $root/build/dos/USTRACE.DLL=WINDOWS/SYSTEM/USTRACE.DLL"
eval "\"$root/tests/dropin.sh\" $kflag -t $secs -s \"$out/shot.png\" -o \"$out/c\" $files $extra" \
    > "$out/boot.txt" 2>&1 || true
cp "$out/c/c/USTRACE.BIN" "$out/c/c/USCOUNT.BIN" "$out/c/c/USSEGS.TXT" "$out/" 2>/dev/null || true
python3 "$root/tools/ustrace.py" "$out" > "$out/trace.txt" || true
tail -n 8 "$out/boot.txt"
echo "trace: $out/trace.txt ($(wc -l < "$out/trace.txt") lines)"
