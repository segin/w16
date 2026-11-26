#!/bin/sh
# Third-party virtual devices on w16's WIN386 (ARCHITECTURE.md 12.7):
# Visual C++ 1.52's Setup adds four to SYSTEM.INI [386Enh] as full paths,
# device=C:\MSVC\BIN\dosxnt.386, mmd.386, cvw1.386 and vmb.386; with
# DOSXNT.386 or CVW1.386 w16's 386 Enhanced mode stopped at its logo
# (2026-11-26: VMM services it did not have), and CodeView for Windows
# (CVW.EXE) then wrote over the real-mode INT 21h vector (selector 40h).
#   vxc    the probe VxD VXC.386 (tests/vxd/vxc.asm, `make vxprobes`) on
#          the reference install (Microsoft's KRNL386 and the rest) under
#          Windows 3.1's WIN386 and w16's: its lines must be the same, but
#          for the first four (Get_PM_Int_Vector's addresses, which are
#          each VMM's own); w16's boot must complete with its hooks of
#          INT 21h, 2Fh and 31h in place. Then VXCU.386 (the same, calling
#          In_Debug_Chr, a VMM service w16 does not have, at
#          Sys_Critical_Init) under w16's WIN386: Windows must not start,
#          and WIN must end with the message naming the device and the
#          service. Needs nothing from outside the repository.
#   boot   w16's install tree alone (C:\W16, its KRNL386 and WIN386) with
#          the four devices: Windows must come up (BOOTLOG.TXT complete),
#          the VMM's trace (COM1) must name no service w16 lacks, and the
#          screen must equal the same boot without them.
#   cvw    CVW.EXE from WIN.INI run= with the four devices on the
#          reference install under Windows 3.1's WIN386 and w16's: CVW's
#          screen (its Load dialog over Windows) must be the same.
# Visual C++ is not w16's: nothing of it is committed. boot and cvw take
# the installed C:\MSVC directory from $W16_MSVC_DIR (default
# ~/Downloads/msvc152/MSVC; boot needs BIN's four .386 files, cvw BIN's
# CVW.EXE and its DLLs too); without it they say so and pass ("skip").
#
#   usage: tests/msvcvxd.sh [-o OUTDIR] [PART ...]   (default: vxc boot cvw)
set -u
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
msvc=${W16_MSVC_DIR:-$HOME/Downloads/msvc152/MSVC}
out=
while getopts o: opt; do
    case $opt in
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -u/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
parts=${*:-vxc boot cvw}
[ -n "$out" ] || out=$(mktemp -d)
mkdir -p "$out"
out=$(cd "$out" && pwd)
ref=$root/.tools/ref/win31/cdrive/WINDOWS
w386=$root/build/win386/WIN386.EXE
fails=0

# a file of the MSVC directory, whatever the case of its name
mfile() {
    find "$msvc/$1" -maxdepth 1 -iname "$2" 2>/dev/null | head -1
}

# run NAME [dropin.sh options...]: one boot, COM1 in OUTDIR/NAME.com1
run() {
    _n=$1; shift
    rm -rf "$out/$_n" "$out/$_n.com1"
    DROPIN_CONF="[serial]
serial1=file file:$out/$_n.com1" timeout 400 "$root/tests/dropin.sh" -p 386 -o "$out/$_n" "$@" \
        > "$out/$_n.txt" 2>&1
    { tr -d '\r' < "$out/$_n.com1"; } 2>/dev/null > "$out/$_n.log"
}

# pixels that differ between two screenshots ("none" if one is missing)
pixdiff() {
    if [ -f "$1" ] && [ -f "$2" ]; then
        magick compare -metric AE "$1" "$2" null: 2>&1 | cut -d' ' -f1
    else echo none; fi
}

for part in $parts; do
case $part in
vxc)
    v=$root/build/dos/VXC.386
    [ -f "$v" ] && [ -f "$root/build/dos/VXCU.386" ] || { echo "FAIL vxc: no $v (make vxprobes)"; fails=$((fails + 1)); continue; }
    set -- -r -3 -c "WIN /3 /B" -t 90 -f "$v=WINDOWS/SYSTEM/VXC.386" -i "386Enh:+device=vxc.386"
    run vxc-ref "$@" -s "$out/vxc-ref.png" &
    run vxc-w16 "$@" -s "$out/vxc-w16.png" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE" &
    run vxcu-w16 -r -3 -c "WIN /3 /B" -t 60 -f "$root/build/dos/VXCU.386=WINDOWS/SYSTEM/VXCU.386" \
        -i "386Enh:+device=vxcu.386" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE" &
    wait
    for s in ref w16; do
        grep -a '^VXC ' "$out/vxc-$s.log" | grep -v '^VXC Get_PM_Int_Vector 000000' > "$out/vxc-$s.obs"
    done
    r=ok
    grep -q '^VXC GDT' "$out/vxc-ref.obs" || r="no VXC lines under Windows 3.1 ($out/vxc-ref.log)"
    [ "$r" != ok ] || cmp -s "$out/vxc-ref.obs" "$out/vxc-w16.obs" || {
        diff "$out/vxc-ref.obs" "$out/vxc-w16.obs" > "$out/vxc.diff"
        r="VXC's results differ from Windows 3.1's ($out/vxc.diff)"
    }
    [ "$r" != ok ] || grep -q '^boot: complete' "$out/vxc-w16.txt" ||
        r="w16's boot with VXC's hooks did not complete ($out/vxc-w16.txt)"
    [ "$r" != ok ] || grep -q 'The virtual device VXC called the VMM service 00C4 (In_Debug_Chr)' "$out/vxcu-w16.txt" ||
        r="no message for the service w16 lacks ($out/vxcu-w16.txt)"
    [ "$r" != ok ] || ! grep -q '^LoadSuccess = USER.EXE' "$out/vxcu-w16.txt" ||
        r="Windows started with a device whose service w16 lacks ($out/vxcu-w16.txt)"
    if [ "$r" = ok ]; then echo "ok   vxc: PM vectors, callbacks, Log_Proc_Call, pages, selector 40h as 3.1's; the missing service named"
    else echo "FAIL vxc: $r"; fails=$((fails + 1)); fi ;;
boot|cvw)
    set --
    for x in DOSXNT MMD CVW1 VMB; do
        f=$(mfile BIN "$x.386")
        [ -n "$f" ] || { set -- missing "$x.386"; break; }
        set -- "$@" -f "$f=MSVC/BIN/$x.386" -i "386Enh:+device=C:\\MSVC\\BIN\\$x.386"
    done
    if [ "${1:-}" = missing ]; then
        echo "skip $part: no Visual C++ 1.52 directory at $msvc (W16_MSVC_DIR: $2 missing)"
        continue
    fi
    if [ "$part" = boot ]; then
        run boot-plain -T "$root/build/w16" -3 -c "WIN /3 /B" -t 120 -s "$out/boot-plain.png" &
        run boot-msvc -T "$root/build/w16" -3 -c "WIN /3 /B" -t 120 -s "$out/boot-msvc.png" "$@" &
        wait
        r=ok
        grep -q '^boot: complete' "$out/boot-msvc.txt" || r="Windows did not come up ($out/boot-msvc.txt)"
        for x in PharLap CV1 MMD VMB; do
            [ "$r" != ok ] || grep -q "^VMM: device $x " "$out/boot-msvc.log" || r="device $x not loaded ($out/boot-msvc.log)"
        done
        [ "$r" != ok ] || ! grep -q 'not in w16\|panic\|fatal' "$out/boot-msvc.log" ||
            r="$(grep -m1 'not in w16\|panic\|fatal' "$out/boot-msvc.log")"
        d=$(pixdiff "$out/boot-plain.png" "$out/boot-msvc.png")
        [ "$r" != ok ] || [ "$d" = 0 ] || r="the screen differs from the boot without them: $d pixels"
        if [ "$r" = ok ]; then echo "ok   boot: w16's tree with DOSXNT, MMD, CVW1 and VMB.386 comes up as without them"
        else echo "FAIL boot: $r"; fails=$((fails + 1)); fi
        continue
    fi
    cvw=$(mfile BIN CVW.EXE)
    [ -n "$cvw" ] || { echo "skip cvw: no CVW.EXE in $msvc/BIN"; continue; }
    # CVW and the libraries it loads from its own directory
    for f in "$msvc"/BIN/*; do
        case $(basename "$f" | tr a-z A-Z) in
        *.EXE|*.DLL) set -- "$@" -f "$f=MSVC/BIN/$(basename "$f" | tr a-z A-Z)" ;;
        esac
    done
    set -- -r -3 -c "WIN /3" -t 90 -i 'WIN.INI:windows:run=C:\MSVC\BIN\CVW.EXE' "$@"
    DROPIN_FIXED=1 run cvw-ref "$@" -t 60 -s "$out/cvw-ref.png"
    run cvw-w16 "$@" -s "$out/cvw-w16.png" -e "$out/cvw-ref.png" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE"
    d=$(pixdiff "$out/cvw-ref.png" "$out/cvw-w16.png")
    r=ok
    [ "$d" = 0 ] || r="CVW's screen differs from Windows 3.1's: $d pixels ($out/cvw-w16.png)"
    [ "$r" != ok ] || ! grep -q 'not in w16\|panic\|fatal' "$out/cvw-w16.log" ||
        r="$(grep -m1 'not in w16\|panic\|fatal' "$out/cvw-w16.log")"
    if [ "$r" = ok ]; then echo "ok   cvw: CodeView for Windows comes up as under Windows 3.1"
    else echo "FAIL cvw: $r"; fails=$((fails + 1)); fi ;;
*)
    echo "FAIL $part: no such part"; fails=$((fails + 1)) ;;
esac
done
[ $fails = 0 ]
