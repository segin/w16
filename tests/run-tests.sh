#!/bin/sh
# Run the DOS-side test programs under KERNEL in headless DOSBox + HDPMI16.
#
# Each test is "NAME EXPECTED_EXIT [REQUIRED_OUTPUT_LINE]". A test passes when
# it exits with the expected code, prints no FAIL line, and (if given) prints
# the required line.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
dos="$root/build/dos"
status=0

run() {
    app=$1 want=$2 need=${3:-}
    echo "== $app"
    rm -f "$dos/TK.LOG"
    out=$("$root/tests/dosrun.sh" "$dos" "KRNL286 $app.EXE" 2>&1)
    [ -f "$dos/TK.LOG" ] && out=$(tr -d '\r' < "$dos/TK.LOG"; printf '%s\n' "$out")
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
exit $status
