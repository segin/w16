#!/bin/sh
# W32M.EXE (tests/win32s/w32m.c, w16's own Win32 memory probe) under Win32s
# 1.30c with w16's W32S device against Windows 3.1 with Microsoft's
# W32S.386 (ARCHITECTURE.md 12.20). Parts:
#   ref    the Win32s reference install (.tools/ref/win31s/cdrive: 3.1 with
#          Win32s installed by its own Setup), Microsoft's KRNL386 and
#          WIN386: W32M's log, the reference
#   tree   w16's install tree alone (its WIN386 and W32S device) with
#          Win32s's files taken from the reference install at test time
#          (WINDOWS\SYSTEM's W32SYS.DLL, WIN32S16.DLL, WINMM16.DLL,
#          WIN32S.INI and WIN32S, and the SYSTEM.INI lines Win32s's Setup
#          writes): W32M's log the same as the reference's
# The logs are compared by tests/win32s/w32mcmp.py, which writes Win32s's
# addresses relative to the first reservation's (3.1's own differ from run
# to run). Win32s is not w16's: its files are copied into the scratch drive
# only and never committed. Without the reference install the test says so
# and passes ("skip").
#
#   usage: tests/win32s/w32mrun.sh [-o OUTDIR] [-x] [PART ...]  (default: ref tree)
#     -x   W32M's exception section too ("x" on its command line; it hung
#          under 3.1 itself: ARCHITECTURE.md 12.20)
# W32MRUN_ARGS: more tests/dropin.sh options for the tree part (blanks split
# them), e.g. "-p 386 -i w16:vmmtrace=1" with DROPIN_CONF's serial1=file.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
ref=${W16_WIN32S_REF:-$root/.tools/ref/win31s/cdrive}
out= arg=
while getopts o:x opt; do
    case $opt in
    o) out=$OPTARG ;;
    x) arg=' x' ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
parts=${*:-ref tree}
[ -f "$ref/WINDOWS/SYSTEM/W32SYS.DLL" ] ||
    { echo "skip: no Win32s reference install at $ref (W16_WIN32S_REF)"; exit 0; }
[ -n "$out" ] || out=$(mktemp -d)
mkdir -p "$out"
out=$(cd "$out" && pwd)
fails=0

# the probe (as tests/win32ssetup.sh builds W32T)
W=${WATCOM:-/opt/watcom}
mkdir -p "$out/w32m"
PATH=$W/binl64:$PATH WATCOM=$W wcc386 -q -bt=nt -zl -s -3s -ox -ecc -i="$W/h" -i="$W/h/nt" \
    -fo="$out/w32m/w32m.obj" "$root/tests/win32s/w32m.c"
nasm -f obj -o "$out/w32m/w32mx.obj" "$root/tests/win32s/w32mx.asm"
PATH=$W/binl64:$PATH WATCOM=$W wlink option quiet system nt_win runtime windows=3.10 \
    name "$out/w32m/W32M.EXE" file "$out/w32m/w32m.obj","$out/w32m/w32mx.obj" \
    option start=_w32mmain library kernel32,user32 libpath "$W/lib386/nt"

# PART dropin.sh-options...: W32M in $out/PART, its log in $out/PART.log
run() {
    p=$1; shift
    rm -rf "$out/$p"
    "$root/tests/dropin.sh" -c 'WIN /3' -t "${W32MRUN_SECS:-300}" -W W32M.END -o "$out/$p" \
        -f "$out/w32m/W32M.EXE=W32M.EXE" -i "WIN.INI:windows:run=C:\\W32M.EXE$arg" \
        "$@" > "$out/$p.txt" 2>&1 || true
    if [ -f "$out/$p/c/W32M.LOG" ]; then tr -d '\r' < "$out/$p/c/W32M.LOG" > "$out/$p.log"
    else : > "$out/$p.log"; fi
    python3 -I "$root/tests/win32s/w32mcmp.py" "$out/$p.log" > "$out/$p.cmp"
}

for p in $parts; do
    case $p in
    ref)
        DROPIN_REF=$ref run ref -3 -r
        if grep -q '^W32M end' "$out/ref.log"; then echo "ok   ref: W32M ran on 3.1 with Microsoft's W32S.386"
        else echo "FAIL ref: W32M did not finish on 3.1 ($out/ref.log)"; fails=$((fails + 1)); fi ;;
    tree)
        set --
        for f in W32SYS.DLL WIN32S16.DLL WINMM16.DLL WIN32S.INI; do
            set -- "$@" -f "$ref/WINDOWS/SYSTEM/$f=W16/SYSTEM/$f"
        done
        for f in $(cd "$ref/WINDOWS/SYSTEM/WIN32S" && ls); do
            set -- "$@" -f "$ref/WINDOWS/SYSTEM/WIN32S/$f=W16/SYSTEM/WIN32S/$f"
        done
        # shellcheck disable=SC2086
        run tree -T "$root/build/w16" -3 "$@" -i '386Enh:+device=C:\W16\SYSTEM\WIN32S\W32S.386' \
            -i 'boot:drivers=mmsystem.dll winmm16.dll' ${W32MRUN_ARGS:-}
        if [ ! -s "$out/ref.cmp" ]; then
            echo "FAIL tree: no reference log (part ref first)"; fails=$((fails + 1))
        elif ! grep -q '^W32M end' "$out/tree.log"; then
            echo "FAIL tree: W32M did not finish on w16's tree ($out/tree.log)"; fails=$((fails + 1))
        elif diff "$out/ref.cmp" "$out/tree.cmp" > "$out/tree.diff"; then
            echo "ok   tree: W32M's log on w16's W32S device as on 3.1 ($(wc -l < "$out/tree.cmp") lines)"
        else echo "FAIL tree: W32M's log differs from 3.1's ($out/tree.diff)"; fails=$((fails + 1)); fi ;;
    *) echo "FAIL w32m: unknown part $p"; fails=$((fails + 1)) ;;
    esac
done
[ $fails = 0 ]
