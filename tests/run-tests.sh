#!/bin/sh
# Run the DOS-side test programs under KERNEL in headless DOSBox + HDPMI16.
#
# Each test is "NAME EXPECTED_EXIT [REQUIRED_OUTPUT_LINE]". A test passes when
# it exits with the expected code, prints no FAIL line, and (if given) prints
# the required line. The test runs in $dir (C:\), with KERNEL $kernel.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
dos="$root/build/dos"
status=0
dir=$dos kernel=KRNL286

run() {
    app=$1 want=$2 need=${3:-}
    echo "== $app"
    rm -f "$dir/TK.LOG"
    out=$("$root/tests/dosrun.sh" "$dir" "$kernel $app.EXE" 2>&1)
    [ -f "$dir/TK.LOG" ] && out=$(tr -d '\r' < "$dir/TK.LOG"; printf '%s\n' "$out")
    printf '%s\n' "$out"
    if [ "$want" = 0 ]; then
        printf '%s\n' "$out" | grep -q 'EXIT=0$' || { echo "== $app: FAILED (exit code)"; status=1; return; }
    else
        printf '%s\n' "$out" | grep -q 'EXIT=NONZERO$' || { echo "== $app: FAILED (expected nonzero exit)"; status=1; return; }
    fi
    if printf '%s\n' "$out" | grep -q '^FAIL'; then
        echo "== $app: FAILED"; status=1; return
    fi
    if [ -n "$need" ] && ! printf '%s\n' "$out" | grep -qxF "$need"; then
        echo "== $app: FAILED (missing: $need)"; status=1; return
    fi
    echo "== $app: ok"
}

run TK1 0
run TK3 0
run TK4 0
run TK2 255 "w16: call to unimplemented KERNEL.204"

# TK5 (DLL loading) and TK11 (module tables), laid out like the reference
# runs (tests/tk5ref.sh): the DLLs in the Windows directory C:\WINDOWS,
# TK5.EXE, TK11.EXE and TKID.TPL in C:\, C:\TKPATH on the PATH. KERNEL is
# started as WIN.COM would, with windir=C:\WINDOWS in its environment
# (WINENV.COM); it lives in C:\W16SYS, so the Windows directory can only
# have come from windir.
stage="$root/build/tk5root"
rm -rf "$stage"
mkdir -p "$stage/WINDOWS" "$stage/W16SYS"
cp "$dos/KRNL286.EXE" "$stage/W16SYS/"
cp "$dos/WINENV.COM" "$dos/TK5.EXE" "$dos/TK11.EXE" "$dos/TKID.TPL" "$stage/"
for f in TKDLL.DLL TKDLB.DLL TKBAD.DLL TKNODEP.DLL TKNOD.DLL TKUSEX.DLL TKEXEM.EXE; do
    cp "$dos/$f" "$stage/WINDOWS/"
done
DOSRUN_PRE='SET PATH=C:\TKPATH;Z:\'
export DOSRUN_PRE
dir=$stage kernel='WINENV C:\WINDOWS W16SYS\KRNL286.EXE'
run TK5 0
run TK11 0
exit $status
