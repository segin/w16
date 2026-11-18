#!/bin/sh
# Run the TOOLHELP.DLL probe THP.EXE (tests/toolhelp/thp.c, with its child
# THC.EXE and library THD.DLL) as the shell, with Microsoft's TOOLHELP.DLL
# or w16's (build/dos/TOOLHELP.DLL), on one of these systems
# (ARCHITECTURE.md 22):
#   ref286   the reference Windows 3.1, Standard mode, Microsoft's KRNL286
#            (tests/refrun.sh REFRUN_KRNL286)
#   ref386   the reference Windows 3.1, Standard mode (KRNL386)
#   ref3     the reference Windows 3.1, 386 Enhanced mode
#   tree286  w16's install tree alone (build/w16, tests/dropin.sh -T) at
#            C:\WINDOWS, its KRNL286, Standard mode
#   tree386  the same with its KRNL386, Standard mode
# OUTDIR/thp.log is the probe's log (black-box observation only).
#   usage: tests/toolhelp/thrun.sh OUTDIR [ms|w16] [SYSTEM]
# THRUN_ARG: the sections to run (C:\THP.ARG; thp.c's header);
# THRUN_TIMEOUT: seconds (default 400).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 lib=${2:-ms} sys=${3:-ref286}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
dos=$root/build/dos
mkdir -p "$out"
rm -f "$out/thp.log" "$out/THP.LOG" "$out/THP.ARG"
argf=
if [ -n "${THRUN_ARG:-}" ]; then
    printf '%s' "$THRUN_ARG" > "$out/THP.ARG"
    argf=$out/THP.ARG
fi
# Section d asks a second TOOLHELP the same questions: the other one of
# the two (Microsoft's beside w16's, or w16's beside Microsoft's), under
# the module name TOOLHLPM as C:\TOOLHLPM.DLL. A test-only combination
# (ARCHITECTURE.md 3): made here, in OUTDIR, from the user's own reference
# install, run and compared by the probe only, never kept or inspected.
other=$ref/SYSTEM/TOOLHELP.DLL
[ "$lib" = ms ] && other=$dos/TOOLHELP.DLL
cp "$other" "$out/TOOLHLPM.DLL"
chmod u+w "$out/TOOLHLPM.DLL"
python3 "$root/tools/nerename.py" "$out/TOOLHLPM.DLL" --module TOOLHLPM
case $sys in
ref*)
    files="$dos/THC.EXE $dos/THD.DLL $out/TOOLHLPM.DLL $argf"
    [ "$lib" = w16 ] && files="$files $dos/TOOLHELP.DLL=WINDOWS/SYSTEM/TOOLHELP.DLL"
    mode=/S k286=
    [ "$sys" = ref3 ] && mode=/3
    [ "$sys" = ref286 ] && k286=1
    REFRUN_KRNL286=$k286 REFRUN_FILES="$files" REFRUN_OUT=$out REFRUN_GET="THP.LOG" \
        REFRUN_TIMEOUT=${THRUN_TIMEOUT:-400} "$root/tests/refrun.sh" "$dos/THP.EXE" "$mode" \
        > "$out/run.txt" 2>&1 || true
    ;;
tree*)
    set -- -f "$dos/THP.EXE=THP.EXE" -f "$dos/THC.EXE=THC.EXE" -f "$dos/THD.DLL=THD.DLL" \
        -f "$out/TOOLHLPM.DLL=TOOLHLPM.DLL"
    [ -n "$argf" ] && set -- "$@" -f "$argf=THP.ARG"
    [ "$lib" = ms ] && set -- "$@" -f "$ref/SYSTEM/TOOLHELP.DLL=WINDOWS/SYSTEM/TOOLHELP.DLL"
    [ "$sys" = tree386 ] && set -- "$@" -3
    DROPIN_TREEDIR=WINDOWS "$root/tests/dropin.sh" -T "$root/build/w16" \
        -t "${THRUN_TIMEOUT:-400}" -o "$out/work" -c 'WIN /S /B' \
        -i 'boot:shell=C:\THP.EXE' "$@" > "$out/run.txt" 2>&1 || true
    [ -f "$out/work/c/THP.LOG" ] && cp "$out/work/c/THP.LOG" "$out/THP.LOG"
    ;;
*)
    echo "thrun: unknown system $sys" >&2
    exit 2 ;;
esac
if [ -f "$out/THP.LOG" ]; then
    tr -d '\r' < "$out/THP.LOG" > "$out/thp.log"
    rm -f "$out/THP.LOG"
    grep -q '^THP end' "$out/thp.log" || echo "thrun: $sys $lib: the probe did not finish ($out)" >&2
else
    echo "thrun: $sys $lib: no THP.LOG ($out/run.txt)" >&2
    exit 3
fi
