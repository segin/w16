#!/bin/sh
# The system driver's interface, w16's against Windows 3.1's
# (drivers/system/README.md). Each probe runs with Windows 3.1's
# SYSTEM.DRV and with w16's (build/dos/SYSTEM.DRV), Microsoft's KRNL286,
# DOSX and USER around them:
#   sy1  SY1.EXE (tests/apps/sy1.c) as the shell calls every export:
#        the bytes each takes off the stack and the registers it leaves,
#        InquireSystem over its inputs, the coprocessor's save area,
#        timers (how many, handles, the ticks between calls for 32 rates,
#        the context of a call, reentrancy, a timer killing itself), the
#        msec count against the BIOS tick count, INT 8 interrupts per tick,
#        Disable/EnableSystemTimers, WEP. Their logs, without the "~"
#        lines and fields (addresses, selectors, the msec count's phase),
#        must be identical.
#   rec  SY1 with SYREC.DRV (tests/apps/syrec.asm) as SYSTEM in front of
#        the driver under test (renamed SYSTGT by tools/nerename.py): every
#        call made to the driver from the boot to Windows' exit (KERNEL,
#        USER, COMM, SY1) and the timer procedures' calls, decoded by
#        tools/syrec.py; the call lists (ordinal, arguments, results,
#        bytes taken off; not the procedures' calls, whose number is the
#        host's timing) must be identical.
#   fd   SY1's part B (InquireSystem's drives) with floppy images in A:
#        and B: (DOSBox's imgmount), then with A: only
#   usage: tests/sysprobe.sh [-o DIR] [-m] [sy1] [rec] [fd]   (default: all)
#     -m   Microsoft's driver only (to look at its logs)
# Exit status 0 when every comparison made agrees.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= msonly=
while getopts o:m opt; do
    case $opt in
    o) out=$OPTARG ;;
    m) msonly=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
probes=${*:-sy1 rec fd}
[ -n "$out" ] || out=$root/build/sysprobe-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
ms=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/SYSTEM.DRV
w16=$root/build/dos/SYSTEM.DRV
dos=$root/build/dos
sides="ms w16"
[ -z "$msonly" ] || sides=ms
fails=0

# the logs without "~" lines and fields
strip() {
    tr -d '\r' < "$1" | grep -av '^~' | grep -av '^OBS ~' | grep -av '^CSMAP\|^SYREC' | sed 's/ ~[^ ]*//g'
}
cmpf() {
    strip "$2" > "$2.cmp"
    strip "$3" > "$3.cmp"
    if cmp -s "$2.cmp" "$3.cmp"; then
        echo "ok   $1: identical ($(wc -l < "$2.cmp") lines)"
    else
        echo "FAIL $1: differs ($2.cmp $3.cmp):"
        diff "$2.cmp" "$3.cmp" | head -30
        fails=$((fails + 1))
    fi
}
# run SIDE NAME [dropin.sh options]: SY1 as the shell; its log to NAME-SIDE.log
run() {
    s=$1 n=$2; shift 2
    if [ "$s" = w16 ]; then set -- "$@" -f "$w16=WINDOWS/SYSTEM/SYSTEM.DRV"; fi
    "$root/tests/dropin.sh" -r -t 300 -o "$out/$n-$s" \
        -f "$dos/SY1.EXE=SY1.EXE" -i 'boot:shell=C:\SY1.EXE' "$@" > "$out/$n-$s.txt" 2>&1 || true
    cp "$out/$n-$s/c/WINDOWS/TK.LOG" "$out/$n-$s.log" 2>/dev/null || : > "$out/$n-$s.log"
    grep -aq '^SY1 done' "$out/$n-$s.log" || { echo "FAIL $n $s: did not finish ($out/$n-$s.log)"; fails=$((fails + 1)); }
}

for p in $probes; do
    case $p in
    sy1)
        for s in $sides; do run $s sy1 & done
        wait
        [ -n "$msonly" ] || cmpf sy1 "$out/sy1-ms.log" "$out/sy1-w16.log" ;;
    rec)
        for s in $sides; do
            if [ $s = ms ]; then src=$ms; else src=$w16; fi
            cp "$src" "$out/SYSTGT-$s.DLL"
            python3 "$root/tools/nerename.py" "$out/SYSTGT-$s.DLL" --module SYSTGT > /dev/null
            ( "$root/tests/dropin.sh" -r -t 120 -o "$out/rec-$s" \
                -f "$dos/SY1.EXE=SY1.EXE" -i 'boot:shell=C:\SY1.EXE' -i 'w16:sy1parts=AB' \
                -f "$dos/SYREC.DRV=WINDOWS/SYSTEM/SYSTEM.DRV" -f "$out/SYSTGT-$s.DLL=WINDOWS/SYSTEM/SYSTGT.DLL" \
                > "$out/rec-$s.txt" 2>&1 || true
              # Windows' exit: SYREC writes its records at each DisableSystemTimers
              cp "$out/rec-$s/c/WINDOWS/TK.LOG" "$out/rec-$s-sy1.log" 2>/dev/null || : > "$out/rec-$s-sy1.log"
              if [ -f "$out/rec-$s/c/SYREC.BIN" ]; then
                  python3 "$root/tools/syrec.py" "$out/rec-$s/c/SYREC.BIN" "$out/rec-$s-sy1.log" > "$out/rec-$s.dec"
              else
                  : > "$out/rec-$s.dec"
              fi ) &
        done
        wait
        for s in $sides; do
            grep -aq 'DisableSystemTimers' "$out/rec-$s.dec" ||
                { echo "FAIL rec $s: no records ($out/rec-$s.dec)"; fails=$((fails + 1)); }
            # the calls only (not the timer procedures', not the counts,
            # whose numbers are timing), without the sequence numbers, the
            # callers' offsets and SS, the selectors in arguments and
            # results that are the run's (procedures' segments, DS in DX)
            # Normalised: DX of the WORD and void functions (whatever the
            # driver had), AX of the void ones (Enable/Disable, Save/
            # Restore: what the driver left) and the msec count's value
            # (timing), the caller's interrupt flag (its own state; the
            # flag out is compared), segments of pointer arguments,
            # InquireSystem for drive 0101h (beyond Z:, not compared:
            # README.md)
            grep -a 'SYSTEM\.' "$out/rec-$s.dec" | sed -E -e 's/^ *[0-9]* //' -e 's/ *<.*//' \
                -e 's/CreateSystemTimer\(([0-9A-F]*), [0-9A-F]*, [0-9A-F]*\)/CreateSystemTimer(\1, proc)/' \
                -e 's/80x87State\([0-9A-F]*, ([0-9A-F]*)\)/80x87State(sel, \1)/' \
                -e 's/ changed .*//' -e 's/ IF in [01]//' \
                -e 's/= [0-9A-F]{4}:([0-9A-F]{4}) popped/= ~:\1 popped/' \
                -e 's/^(SYSTEM\.(Enable|Disable)SystemTimers\(\)|SYSTEM\.(Save|Restore)80x87State\(.*\)|SYSTEM\.GetSystemMsecCount\(\)) = [^ ]* /\1 = ~ /' \
                -e '/InquireSystem\(0001, 0101\)/d' > "$out/rec-$s.calls"
        done
        if [ -z "$msonly" ]; then
            cp "$out/rec-ms.calls" "$out/rec-ms.calls.n"
            cp "$out/rec-w16.calls" "$out/rec-w16.calls.n"
            if cmp -s "$out/rec-ms.calls.n" "$out/rec-w16.calls.n"; then
                echo "ok   rec: the same calls ($(wc -l < "$out/rec-ms.calls.n"))"
            else
                echo "FAIL rec: the calls differ ($out/rec-ms.calls.n $out/rec-w16.calls.n):"
                diff "$out/rec-ms.calls.n" "$out/rec-w16.calls.n" | head -30
                fails=$((fails + 1))
            fi
        fi ;;
    fd)
        # two blank 1.44 MB floppy images (FAT12, made here)
        python3 "$root/tests/scripts/mkfloppy.py" "$out/a.img"
        python3 "$root/tests/scripts/mkfloppy.py" "$out/b.img"
        for cfg in ab a; do
            if [ $cfg = ab ]; then
                c="imgmount a \"$out/a.img\" -t floppy
imgmount b \"$out/b.img\" -t floppy
WIN /S /B"
            else
                c="imgmount a \"$out/a.img\" -t floppy
WIN /S /B"
            fi
            for s in $sides; do run $s fd$cfg -c "$c" -i 'w16:sy1parts=B' & done
            wait
            [ -n "$msonly" ] || cmpf fd$cfg "$out/fd$cfg-ms.log" "$out/fd$cfg-w16.log"
        done ;;
    *) echo "unknown probe $p" >&2; exit 2 ;;
    esac
done
echo "sysprobe: logs in $out"
exit $fails
