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

# TK5 (DLL loading) in the reference layout (tests/tk5ref.sh): KRNL286 in
# C:\WINDOWS\SYSTEM, so the Windows directory is C:\WINDOWS, with the DLLs;
# TK5.EXE and TKID.TPL in C:\; C:\TKPATH on the PATH.
stage="$root/build/tk5root"
rm -rf "$stage"
mkdir -p "$stage/WINDOWS/SYSTEM"
cp "$dos/KRNL286.EXE" "$stage/WINDOWS/SYSTEM/"
cp "$dos/TK5.EXE" "$dos/TKID.TPL" "$stage/"
for f in TKDLL.DLL TKDLB.DLL TKBAD.DLL TKNODEP.DLL TKNOD.DLL; do
    cp "$dos/$f" "$stage/WINDOWS/"
done
DOSRUN_PRE='SET PATH=C:\TKPATH;Z:\'
export DOSRUN_PRE
dir=$stage kernel='WINDOWS\SYSTEM\KRNL286'
run TK5 0
exit $status
