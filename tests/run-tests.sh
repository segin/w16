#!/bin/sh
# Run the DOS-side test programs under KERNEL in headless DOSBox + HDPMI16.
#
# Each test is "NAME EXPECTED_EXIT [REQUIRED_OUTPUT_LINE]". A test passes when
# it exits with the expected code, prints no FAIL line, and (if given) prints
# the required line. The test runs in $dir (C:\), with KERNEL $kernel.
#
# RUNTESTS_DPMI=dosx runs everything on w16's DOS extender instead of
# HDPMI16: KERNEL is started as WIN.COM's WSWAP starts it, by DOSX.EXE
# (build/dos/DOSX.EXE, placed next to KRNL286.EXE) with the tail
# "krnl286 PROGRAM.EXE ...".
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
dos="$root/build/dos"
status=0
DOSRUN_DPMI=${RUNTESTS_DPMI:-hdpmi}
export DOSRUN_DPMI
# kern DIR\KRNL286.EXE: the command line that starts that KERNEL
kern() {
    if [ "$DOSRUN_DPMI" = dosx ]; then
        printf '%sDOSX.EXE krnl286' "${1%KRNL286.EXE}"
    else
        printf '%s' "$1"
    fi
}
# dosx_to DIR: with DOSX, DOSX.EXE beside the KRNL286.EXE in DIR
dosx_to() {
    if [ "$DOSRUN_DPMI" = dosx ]; then cp "$dos/DOSX.EXE" "$1/"; fi
}
dir=$dos kernel=$(kern KRNL286.EXE)

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
run TK12 0
run TK13 0
run TK17 0
run TK17A 0
run TK19 0
run TK50 0
run TK2 255 "w16: call to unimplemented KERNEL.27"

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
dosx_to "$stage/W16SYS"
cp "$dos/WINENV.COM" "$dos/TK5.EXE" "$dos/TK11.EXE" "$dos/TKID.TPL" "$stage/"
# TK15 and TK16 (tasks) start their helpers from their own directory
cp "$dos/TK15.EXE" "$dos/TK15A.EXE" "$stage/"
cp "$dos/TK16.EXE" "$dos/TK16A.EXE" "$dos/TK16B.EXE" "$dos/TK16C.EXE" "$dos/TK16S.EXE" "$stage/"
for f in TKDLL.DLL TKDLB.DLL TKBAD.DLL TKNODEP.DLL TKNOD.DLL TKUSEX.DLL TKEXEM.EXE; do
    cp "$dos/$f" "$stage/WINDOWS/"
done
DOSRUN_PRE='SET PATH=C:\TKPATH;Z:\'
export DOSRUN_PRE
dir=$stage kernel="WINENV C:\\WINDOWS $(kern 'W16SYS\KRNL286.EXE')"
run TK5 0
run TK11 0
run TK15 0
run TK16 0

# TK14 (profiles and files), laid out like tests/tk14ref.sh: TK14.EXE in
# C:\, KERNEL in the system directory C:\WINDOWS\SYSTEM, windir=C:\WINDOWS
# (WINENV.COM), a WIN.INI of our own, C:\TKTMP made before KERNEL starts,
# PATH=C:\TKPATH;Z:\ and TEMP=C:\TKTMP.
stage="$root/build/tk14root"
rm -rf "$stage"
mkdir -p "$stage/WINDOWS/SYSTEM"
cp "$dos/KRNL286.EXE" "$stage/WINDOWS/SYSTEM/"
dosx_to "$stage/WINDOWS/SYSTEM"
cp "$dos/WINENV.COM" "$dos/TK14.EXE" "$stage/"
printf '[windows]\r\nload=\r\n' > "$stage/WINDOWS/WIN.INI"
DOSRUN_PRE='MD C:\TKTMP
SET PATH=C:\TKPATH;Z:\
SET TEMP=C:\TKTMP'
export DOSRUN_PRE
dir=$stage kernel="WINENV C:\\WINDOWS $(kern 'WINDOWS\SYSTEM\KRNL286.EXE')"
run TK14 0
unset DOSRUN_PRE

# TK6-TK10 (floating point; tests/tk6ref.sh) in DOSBox-X, with a
# coprocessor and without one: KRNL286 and WIN87EM.DLL in C:\WINDOWS\SYSTEM,
# KERNEL started through WINENV.COM with windir=C:\WINDOWS (as WIN.COM).
stage="$root/build/tk6root"
rm -rf "$stage"
mkdir -p "$stage/WINDOWS/SYSTEM"
cp "$dos/KRNL286.EXE" "$dos/WIN87EM.DLL" "$stage/WINDOWS/SYSTEM/"
dosx_to "$stage/WINDOWS/SYSTEM"
cp "$dos/WINENV.COM" "$stage/"
for f in TK6 TK6N TK7 TK8 TK8T1 TK8T2 TK8T3 TK8T4 TK8T5 TK9 TK10; do
    cp "$dos/$f.EXE" "$stage/"
done
unset DOSRUN_PRE
DOSRUN_X=1
export DOSRUN_X DOSRUN_FPU
dir=$stage kernel="WINENV C:\\WINDOWS $(kern 'WINDOWS\SYSTEM\KRNL286.EXE')"
for DOSRUN_FPU in true false; do
    echo "== (DOSBox-X, fpu=$DOSRUN_FPU)"
    for t in TK6 TK7 TK8 TK8T1 TK8T2 TK8T3 TK8T4 TK8T5 TK9 TK10; do
        run $t 0
    done
done
# TK6N only with a coprocessor: without one its floating-point code has no
# emulator (it does not import WIN87EM), so the INTs return into the
# instruction's operand bytes; Windows 3.1 computed a wrong result and
# then hung there, w16 faults.
DOSRUN_FPU=true
run TK6N 0

# w16's WIN.COM against Windows 3.1's (needs the reference install)
if [ -f "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.COM" ]; then
    echo "== WIN.COM (tests/wincom.sh)"
    "$root/tests/wincom.sh" || status=1
fi
# w16's fonts against Windows 3.1's metrics (needs the reference install)
if [ -f "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.COM" ] && [ -f "$root/build/fonts/VGASYS.FON" ]; then
    echo "== fonts (tests/fonts.sh -q)"
    "$root/tests/fonts.sh" -q || status=1
fi
exit $status
