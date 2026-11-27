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
#   wx     WXSRVR.EXE (Visual C++'s WX server, which calls VMB.386's API
#          over and over) from WIN.INI load= with the four devices on the
#          reference install, then Program Manager's File and Help menus
#          from the keyboard (tests/scripts/input/msvcwx.txt): the screens
#          under w16's WIN386 must equal Windows 3.1's, and VMB's API must
#          have been called (the user's hang: VMB.386's _HeapAllocate, a
#          service w16 lacked, halted Windows while Program Manager
#          painted its groups).
#   cl     CL.EXE, Visual C++'s 32-bit compiler (its passes run under Phar
#          Lap's DOSXNT.EXE, a DPMI client), compiling and linking a hello
#          world in a windowed DOS box on the reference install, with
#          DOSXNT.386 in SYSTEM.INI and without, under Windows 3.1's
#          WIN386 and w16's (ARCHITECTURE.md 12.16): the program must be
#          built and say hello, and CL's output must equal Windows 3.1's
#          (before, w16's boxes had no DPMI host: "Phar Lap err 35").
# Visual C++ is not w16's: nothing of it is committed. boot and cvw take
# the installed C:\MSVC directory from $W16_MSVC_DIR (default
# ~/Downloads/msvc152/MSVC; boot needs BIN's four .386 files, cvw BIN's
# CVW.EXE and its DLLs too); without it they say so and pass ("skip").
#
#   usage: tests/msvcvxd.sh [-o OUTDIR] [PART ...]   (default: vxc boot cvw wx cl cltree)
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
parts=${*:-vxc boot cvw wx cl cltree}
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
boot|cvw|wx)
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
    prog=CVW.EXE
    [ "$part" = wx ] && prog=WXSRVR.EXE
    [ -n "$(mfile BIN $prog)" ] || { echo "skip $part: no $prog in $msvc/BIN"; continue; }
    # the program and the libraries it loads from its own directory
    for f in "$msvc"/BIN/*; do
        case $(basename "$f" | tr a-z A-Z) in
        *.EXE|*.DLL) set -- "$@" -f "$f=MSVC/BIN/$(basename "$f" | tr a-z A-Z)" ;;
        esac
    done
    if [ "$part" = wx ]; then
        set -- -r -3 -p 386 -c "WIN /3 /B" -i 'WIN.INI:windows:load=C:\MSVC\BIN\WXSRVR.EXE' "$@"
        scr=$root/tests/scripts/input/msvcwx.txt
        rm -rf "$out/wx-ref" "$out/wx-w16" "$out/wx-w16.com1"
        DROPIN_MACHINE=svga_s3 timeout 600 "$root/tests/inputrun.sh" -s -b 120 -t 300 "$scr" "$out/wx-ref" "$@" \
            > "$out/wx-ref.txt" 2>&1
        DROPIN_MACHINE=svga_s3 DROPIN_CONF="[serial]
serial1=file file:$out/wx-w16.com1" timeout 600 "$root/tests/inputrun.sh" -s -b 120 -t 300 -E "$out/wx-ref" "$scr" "$out/wx-w16" \
            "$@" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE" > "$out/wx-w16.txt" 2>&1
        { tr -d '\r' < "$out/wx-w16.com1"; } 2>/dev/null > "$out/wx-w16.log"
        r=ok
        for n in 01-boot 02-file 03-help; do
            d=$(pixdiff "$out/wx-ref/$n.png" "$out/wx-w16/$n.png")
            [ "$d" = 0 ] || { r="$n differs from Windows 3.1's: $d pixels ($out/wx-w16/$n.png)"; break; }
        done
        [ "$r" != ok ] || grep -q 'VxD API device 1021 ' "$out/wx-w16.log" || r="VMB's API was not called ($out/wx-w16.log)"
        [ "$r" != ok ] || ! grep -q 'not in w16\|panic\|fatal' "$out/wx-w16.log" ||
            r="$(grep -m1 'not in w16\|panic\|fatal' "$out/wx-w16.log")"
        if [ "$r" = ok ]; then echo "ok   wx: Program Manager with WXSRVR on VMB.386 painted and answering as under Windows 3.1"
        else echo "FAIL wx: $r"; fails=$((fails + 1)); fi
        continue
    fi
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
cltree)
    # the user's way (2026-11-26): w16's tree alone with Visual C++'s four
    # devices in SYSTEM.INI, a windowed box from the tree's Command Prompt
    # PIF in C:\, MSVCVARS.BAT, then CL HELLO.C as typed (no paths), twice,
    # then a 32-bit client that DOS ends in real mode (DPMIPRB T32: as
    # Ctrl+C ends CL's DOSXNT) and CL again; machine as theirs (svga_s3, 32 MB). Before: the
    # pass found no hello.c (the box's directory moved), and after a CL
    # ended by Ctrl+C the next one crashed DOSBox-X ("Corrupt MCB chain")
    [ -n "$(mfile BIN CL.EXE)" ] && [ -n "$(mfile BIN MSVCVARS.BAT)" ] && [ -d "$msvc/LIB" ] && [ -d "$msvc/INCLUDE" ] ||
        { echo "skip cltree: no Visual C++ 1.52 compiler at $msvc (W16_MSVC_DIR)"; continue; }
    set --
    for d in BIN INCLUDE LIB; do
        for f in $(cd "$msvc" && find "$d" -type f); do
            set -- "$@" -f "$msvc/$f=MSVC/$(printf '%s' "$f" | tr a-z A-Z)"
        done
    done
    for x in DOSXNT MMD CVW1 VMB; do
        set -- "$@" -i "386Enh:+device=C:\\MSVC\\BIN\\$x.386"
    done
    printf '#include <stdio.h>\r\n\r\nint main(int argc, char *argv[])\r\n{\r\n    printf("Hello from %%d worlds!\\n", argc);\r\n    return 0;\r\n}\r\n' > "$out/HELLO.C"
    printf 'CALL C:\\MSVC\\BIN\\MSVCVARS.BAT\r\nCL HELLO.C > CL1.TXT\r\nDEL HELLO.EXE\r\nCL HELLO.C > CL2.TXT\r\nC:\\W16\\DPMIPRB T32 T\r\nCL HELLO.C > CL3.TXT\r\nHELLO > RUN.TXT\r\nECHO done > DONE.TXT\r\n' > "$out/CLT.BAT"
    python3 "$root/tools/mkpif.py" "$root/assets/CMD.PIF" "$out/CLT.PIF" "title=CL" 'program=C:\CLT.BAT' 'dir=C:\' \
        windowed=1 || exit 2
    rm -rf "$out/cltree" "$out/cltree.com1"
    # MSVCVXD_W386=FILE: that WIN386.EXE in the tree (an older one, to see
    # the failure)
    [ -n "${MSVCVXD_W386:-}" ] && set -- "$@" -f "$MSVCVXD_W386=W16/SYSTEM/WIN386.EXE"
    DROPIN_CONF="[serial]
serial1=file file:$out/cltree.com1
[log]
logfile=$out/cltree.emu" DROPIN_MACHINE=svga_s3 DROPIN_MEMSIZE=32 DROPIN_NOREDIR=1 timeout 700 "$root/tests/dropin.sh" \
        -T "$root/build/w16" -3 -p 386 -c "WIN /3" -t 600 -o "$out/cltree" -W DONE.TXT \
        -f "$out/HELLO.C=HELLO.C" -f "$out/CLT.BAT=CLT.BAT" -f "$out/CLT.PIF=W16/CLT.PIF" \
        -f "$root/build/dos/DPMIPRB.COM=W16/DPMIPRB.COM" -i 'WIN.INI:windows:run=C:\W16\CLT.PIF' "$@" \
        > "$out/cltree.txt" 2>&1
    { tr -d '\r' < "$out/cltree.com1"; } > "$out/cltree.log" 2>/dev/null
    r=ok
    for t in CL1 CL2 CL3 RUN; do
        g=$(find "$out/cltree" -maxdepth 3 -ipath "*/c/$t.TXT" | head -1)
        { tr -d '\r' < "$g"; } > "$out/cltree.$t.txt" 2>/dev/null || : > "$out/cltree.$t.txt"
    done
    for t in CL1 CL2 CL3; do
        [ "$r" != ok ] || ! grep -qi 'error\|warning' "$out/cltree.$t.txt" ||
            r="$t: $(grep -i -m1 'error\|warning' "$out/cltree.$t.txt") ($out/cltree.$t.txt)"
        [ "$r" != ok ] || [ -s "$out/cltree.$t.txt" ] || r="$t: no output from CL ($out/cltree.txt)"
    done
    [ "$r" != ok ] || [ "$(cat "$out/cltree.RUN.txt")" = "Hello from 1 worlds!" ] ||
        r="the program did not run: '$(head -c 80 "$out/cltree.RUN.txt")' ($out/cltree.txt)"
    [ "$r" != ok ] || ! grep -qi 'mcb chain\|memory error' "$out/cltree.emu" 2>/dev/null ||
        r="DOSBox-X: $(grep -i -m1 'mcb chain\|memory error' "$out/cltree.emu")"
    [ "$r" != ok ] || ! grep -q 'not in w16\|panic\|fatal' "$out/cltree.log" ||
        r="$(grep -m1 'not in w16\|panic\|fatal' "$out/cltree.log")"
    if [ "$r" = ok ]; then echo "ok   cltree: w16's tree: CL HELLO.C in C:\\ in a windowed box, three times, around a client DOS ended"
    else echo "FAIL cltree: $r"; fails=$((fails + 1)); fi ;;
cl)
    [ -n "$(mfile BIN CL.EXE)" ] && [ -n "$(mfile BIN DOSXNT.EXE)" ] && [ -d "$msvc/LIB" ] && [ -d "$msvc/INCLUDE" ] ||
        { echo "skip cl: no Visual C++ 1.52 compiler at $msvc (W16_MSVC_DIR)"; continue; }
    set --
    for d in BIN INCLUDE LIB; do
        for f in $(cd "$msvc" && find "$d" -type f); do
            set -- "$@" -f "$msvc/$f=MSVC/$(printf '%s' "$f" | tr a-z A-Z)"
        done
    done
    printf '#include <stdio.h>\r\n\r\nint main(void)\r\n{\r\n    printf("hello, world\\n");\r\n    return 0;\r\n}\r\n' > "$out/HELLO.C"
    # full paths: on DOSBox's own DOS the current directory is the
    # emulator's, not in DOS's memory, so no WIN386 can keep one per VM
    # (w16 keeps DOS's CDS per VM, which real DOS uses): KERNEL's task
    # switches in the System VM move it under the box
    printf 'SET PATH=C:\\MSVC\\BIN;C:\\DOS\r\nSET INCLUDE=C:\\MSVC\\INCLUDE\r\nSET LIB=C:\\MSVC\\LIB\r\nSET TMP=C:\\HW\r\nCL /nologo /FoC:\\HW\\HELLO.OBJ /FeC:\\HW\\HELLO.EXE C:\\HW\\HELLO.C > C:\\HW\\CL.TXT\r\nC:\\HW\\HELLO > C:\\HW\\RUN.TXT\r\nECHO done > C:\\HW\\DONE.TXT\r\n' > "$out/HW.BAT"
    python3 "$root/tools/mkpif.py" "$ref/DOSPRMPT.PIF" "$out/HW.PIF" "title=CL" 'program=C:\HW\HW.BAT' 'dir=C:\HW' \
        windowed=1 background=1 || exit 2
    set -- -r -3 -c "WIN /3" -t 360 -W HW/DONE.TXT -f "$out/HELLO.C=HW/HELLO.C" -f "$out/HW.BAT=HW/HW.BAT" \
        -f "$out/HW.PIF=HW/HW.PIF" -i 'WIN.INI:windows:run=C:\HW\HW.PIF' "$@"
    x=$(mfile BIN DOSXNT.386)
    # MSVCVXD_VXCD=1: VXCD.386 logs the box's INT 21h, 2Fh and 31h calls
    # to COM1 (OUTDIR/cl-ref.log, cl-w16.log)
    [ -n "${MSVCVXD_VXCD:-}" ] && set -- "$@" -f "$root/build/dos/VXCD.386=WINDOWS/SYSTEM/VXCD.386" \
        -i '386Enh:+device=VXCD.386'
    r=ok
    for k in cl clx; do
        if [ $k = clx ]; then set -- "$@" -f "$x=MSVC/BIN/DOSXNT.386" -i '386Enh:+device=C:\MSVC\BIN\DOSXNT.386'; fi
        [ -n "${MSVCVXD_NOREF:-}" ] || DROPIN_NOREDIR=1 run $k-ref "$@" &
        # shellcheck disable=SC2086
        DROPIN_NOREDIR=1 run $k-w16 "$@" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE" ${MSVCVXD_W16INI:+-i "$MSVCVXD_W16INI"} &
        wait
        for w in ref w16; do
            for t in CL RUN; do
                g=$(find "$out/$k-$w" -ipath '*/HW/'$t.TXT | head -1)
                { tr -d '\r' < "$g"; } > "$out/$k-$w.$t.txt" 2>/dev/null || : > "$out/$k-$w.$t.txt"
            done
        done
        [ "$r" != ok ] || [ "$(cat "$out/$k-ref.RUN.txt")" = "hello, world" ] ||
            r="$k: Windows 3.1's box did not build the program ($out/$k-ref.CL.txt)"
        [ "$r" != ok ] || [ "$(cat "$out/$k-w16.RUN.txt")" = "hello, world" ] ||
            r="$k: w16's box did not build the program: $(head -c 200 "$out/$k-w16.CL.txt") ($out/$k-w16.log)"
        [ "$r" != ok ] || cmp -s "$out/$k-ref.CL.txt" "$out/$k-w16.CL.txt" ||
            r="$k: CL's output differs from Windows 3.1's (diff $out/$k-ref.CL.txt $out/$k-w16.CL.txt)"
        [ "$r" != ok ] || ! grep -q 'not in w16\|panic\|fatal' "$out/$k-w16.log" ||
            r="$k: $(grep -m1 'not in w16\|panic\|fatal' "$out/$k-w16.log")"
    done
    if [ "$r" = ok ]; then echo "ok   cl: CL.EXE builds hello world in a DOS box as under Windows 3.1, with DOSXNT.386 and without"
    else echo "FAIL cl: $r"; fails=$((fails + 1)); fi ;;
*)
    echo "FAIL $part: no such part"; fails=$((fails + 1)) ;;
esac
done
[ $fails = 0 ]
