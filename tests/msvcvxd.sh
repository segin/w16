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
#   wps    Windows Process Status (BIN\WPS.EXE) from WIN.INI run= on
#          w16's tree alone (C:\W16, its KRNL386 and WIN386) and on
#          Windows 3.1: w16 must not fault (it did: GetModuleHandle's DX,
#          the module list WPS walks, was a stray selector), and WPS's
#          window must be there, at the bottom of the screen, as under
#          Windows 3.1: its caption and menu bar (the active caption's
#          blue within 5% of 3.1's; the tree's USER, fonts and lists are
#          w16's).
#   ole2view  the OLE 2 object viewer (BIN\OLE2VIEW.EXE) the same way,
#          with the OLE 2.01 libraries Visual C++'s Setup puts in the
#          system directory (COMPOBJ.DLL, OLE2.DLL, STORAGE.DLL,
#          OLE2DISP.DLL, OLE2NLS.DLL, OLE2PROX.DLL, OLE2CONV.DLL,
#          TYPELIB.DLL, STDOLE.TLB; and REG.DAT, the registration
#          database, when there is one) from $W16_OLE2_DIR (default: OLE2
#          beside the MSVC directory): w16 must not fault (it did: USER
#          called MFC's WH_CBT hook with its own DS, 3.1 with AX = DS =
#          ES = SS), and its window must be there as under 3.1 (its
#          caption, menu and tool bar, as for wps).
#   heapwalk  HeapWalk (BIN\HEAPWALK.EXE, TOOLHELP's global heap walk) the
#          same way: no fault, its window (caption, menu bar) there as
#          under 3.1.
#   msquery  Microsoft Query (MSAPPS\MSQUERY\MSQUERY.EXE, which Visual
#          C++ 1.52's Setup installs in the Windows directory, with its
#          Cue Cards and the ODBC libraries) from WIN.INI run= on w16's
#          tree alone and on Windows 3.1 without Arial: the Cue Cards' box
#          "Cue Cards uses Arial font" must take Return, then Alt+F4 closes
#          the Cue Cards, Alt+F4 Query, Alt+F4 and Return end Windows
#          (tests/scripts/input/msquery.txt); w16 must not fault and must
#          end with code 0 (it did not: USER's SetFocus activated Query's
#          disabled window again, the box took no keys, and closing Query
#          closed the Cue Cards under their box, which faulted). The
#          Windows directory's files (MSAPPS\MSQUERY, CCARD200.EXE,
#          MSQUERY.INI, ODBC.INI, ODBCINST.INI, SYSTEM\: CCAPI200.DLL,
#          CTL3D.DLL, CTL3DV2.DLL, CUECARD2.DLL, QRYINTL.DLL, ODBC.DLL,
#          ODBCINST.DLL, ODBCCURS.DLL) come from $W16_MSQUERY_DIR (default:
#          MSQUERY beside the MSVC directory), laid out as there.
#   cvdos  CodeView for MS-DOS (BIN\CV.EXE, a DPMI client) in a full-screen
#          DOS box on w16's tree with Visual C++'s four devices, started
#          from the MS-DOS prompt in C:\MSVC\BIN, the mouse moved over it,
#          quit with Escape, EXIT (tests/scripts/input/cvdos.txt): DOSBox-X
#          must stay alive (the mouse ended it: "IRET: Illegal descriptor
#          type 12", MOUSE.DRV's PS/2 handler run in CodeView's VM),
#          CodeView's screen a text screen whose menu bar equals the same
#          run's under Microsoft's WIN386, and Windows back after EXIT.
#          (The Load dialog's file list is empty under w16's WIN386: not
#          compared.)
# Visual C++ is not w16's: nothing of it is committed. boot and cvw take
# the installed C:\MSVC directory from $W16_MSVC_DIR (default
# ~/Downloads/msvc152/MSVC; boot needs BIN's four .386 files, cvw BIN's
# CVW.EXE and its DLLs too); without it they say so and pass ("skip").
#
#   usage: tests/msvcvxd.sh [-o OUTDIR] [PART ...]   (default: vxc boot cvw wx cl;
#          msquery and cvdos by name: boot-test cases msquery and cvdos)
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
parts=${*:-vxc boot cvw wx cl}
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
wps|ole2view|heapwalk)
    prog=WPS.EXE
    [ "$part" = ole2view ] && prog=OLE2VIEW.EXE
    [ "$part" = heapwalk ] && prog=HEAPWALK.EXE
    [ -n "$(mfile BIN $prog)" ] || { echo "skip $part: no $prog in $msvc/BIN (W16_MSVC_DIR)"; continue; }
    ole=${W16_OLE2_DIR:-$(dirname "$msvc")/OLE2}
    # BIN's programs and libraries (the program's directory)
    set --
    for f in "$msvc"/BIN/*; do
        case $(basename "$f" | tr a-z A-Z) in
        *.EXE|*.DLL|*.HLP) set -- "$@" -f "$f=MSVC/BIN/$(basename "$f" | tr a-z A-Z)" ;;
        esac
    done
    if [ "$part" = ole2view ]; then
        for x in COMPOBJ.DLL OLE2.DLL STORAGE.DLL OLE2DISP.DLL OLE2NLS.DLL OLE2PROX.DLL \
                 OLE2CONV.DLL TYPELIB.DLL STDOLE.TLB; do
            [ -f "$ole/$x" ] || { set -- missing "$x"; break; }
        done
        if [ "${1:-}" = missing ]; then
            echo "skip ole2view: no OLE 2 libraries at $ole (W16_OLE2_DIR: $2 missing)"
            continue
        fi
    fi
    # the system directory's files: WINDOWS on 3.1, W16 on the tree
    sysf() {
        [ "$part" = ole2view ] || return 0
        for x in COMPOBJ.DLL OLE2.DLL STORAGE.DLL OLE2DISP.DLL OLE2NLS.DLL OLE2PROX.DLL \
                 OLE2CONV.DLL TYPELIB.DLL STDOLE.TLB; do
            printf ' -f %s=%s/SYSTEM/%s' "$ole/$x" "$1" "$x"
        done
        [ ! -f "$ole/REG.DAT" ] || printf ' -f %s=%s/REG.DAT' "$ole/REG.DAT" "$1"
    }
    # (the paths have no blanks: the MSVC directory's files are listed
    # word by word above too)
    # shellcheck disable=SC2046
    DROPIN_FIXED=1 run $part-ref -r -3 -c "WIN /3" -t 60 -s "$out/$part-ref.png" \
        -i "WIN.INI:windows:run=C:\\MSVC\\BIN\\$prog" "$@" $(sysf WINDOWS) &
    # shellcheck disable=SC2046
    DROPIN_FIXED=1 run $part-w16 -T "$root/build/w16" -3 -c "WIN /3 /B" -t 60 -s "$out/$part-w16.png" \
        -i "WIN.INI:windows:run=C:\\MSVC\\BIN\\$prog" "$@" $(sysf W16) &
    wait
    r=ok
    grep -q '^boot: complete' "$out/$part-w16.txt" || r="Windows did not come up ($out/$part-w16.txt)"
    [ "$r" != ok ] || ! grep -q '^w16: exception' "$out/$part-w16.txt" ||
        r="$(grep -m1 '^w16: exception' "$out/$part-w16.txt") ($out/$part-w16.txt)"
    # the program's frame: WPS at the bottom (its caption and menu bar),
    # OLE2VIEW maximised (caption, menu and tool bar)
    case $part in
    wps) crop=640x36+0+304 ;;
    heapwalk) crop=640x36+0+0 ;;
    *) crop=640x70+0+0 ;;
    esac
    for s in ref w16; do
        magick "$out/$part-$s.png" -crop $crop +repage "$out/$part-$s-frame.png" 2>/dev/null
    done
    # (w16's tree has its own USER, fonts and system bitmaps, so the
    # frames are not compared to the pixel: the active caption's blue,
    # #0000AA, must cover as much, within 5%, as on 3.1)
    blue() {
        magick "$1" -fill black +opaque '#0000AA' -fill white -opaque '#0000AA' \
            -format '%[fx:round(mean*w*h)]' info: 2>/dev/null || echo 0
    }
    nr=$(blue "$out/$part-ref-frame.png") nw=$(blue "$out/$part-w16-frame.png")
    if [ "$r" = ok ] && [ "$nr" -lt 5000 ]; then
        r="no $prog window under Windows 3.1 ($out/$part-ref.png)"
    elif [ "$r" = ok ] && { [ $(( (nw - nr) * 20 )) -gt "$nr" ] || [ $(( (nr - nw) * 20 )) -gt "$nr" ]; }; then
        r="$prog's window is not there as under Windows 3.1: $nw caption pixels, 3.1 $nr ($out/$part-w16.png)"
    fi
    if [ "$r" = ok ]; then echo "ok   $part: $prog comes up on w16's tree as on Windows 3.1"
    else echo "FAIL $part: $r"; fails=$((fails + 1)); fi ;;
msquery)
    mq=${W16_MSQUERY_DIR:-$(dirname "$msvc")/MSQUERY}
    [ -f "$mq/MSAPPS/MSQUERY/MSQUERY.EXE" ] || { echo "skip msquery: no Microsoft Query at $mq (W16_MSQUERY_DIR)"; continue; }
    # the Windows directory's files: WINDOWS on 3.1, W16 on the tree
    # (their paths have no blanks)
    mqf() {
        (cd "$mq" && find . -type f) | while read -r f; do
            f=${f#./}
            printf ' -f %s=%s/%s' "$mq/$f" "$1" "$(printf '%s' "$f" | tr a-z A-Z)"
        done
    }
    scr=$root/tests/scripts/input/msquery.txt
    for s in ref w16; do
        rm -rf "$out/msquery-$s"
        if [ $s = ref ]; then
            # Windows 3.1 without Arial, as w16's tree (the Cue Cards' box)
            set -- -r -3 -c "WIN /3 /B" -i 'WIN.INI:windows:run=C:\WINDOWS\MSAPPS\MSQUERY\MSQUERY.EXE' \
                -i "WIN.INI:fonts:-Arial (TrueType)=" -i "WIN.INI:fonts:-Arial Bold (TrueType)=" \
                -i "WIN.INI:fonts:-Arial Bold Italic (TrueType)=" -i "WIN.INI:fonts:-Arial Italic (TrueType)="
            for x in ARIAL ARIALBD ARIALBI ARIALI; do
                set -- "$@" -f "WINDOWS/SYSTEM/$x.TTF=-" -f "WINDOWS/SYSTEM/$x.FOT=-"
            done
            # shellcheck disable=SC2046
            set -- "$@" $(mqf WINDOWS)
        else
            # shellcheck disable=SC2046
            set -- -T "$root/build/w16" -3 -c "WIN /3 /B" \
                -i 'WIN.INI:windows:run=C:\W16\MSAPPS\MSQUERY\MSQUERY.EXE' $(mqf W16)
        fi
        timeout 500 "$root/tests/inputrun.sh" -s -b 150 -t 300 "$scr" "$out/msquery-$s" -p 386 "$@" \
            > "$out/msquery-$s.txt" 2>&1
    done
    r=ok
    grep -q '^Windows ended' "$out/msquery-ref/input.log" 2>/dev/null ||
        r="Windows 3.1 did not end after Query and its Cue Cards were closed ($out/msquery-ref)"
    [ "$r" != ok ] || ! grep -q '^w16: exception' "$out/msquery-w16.txt" ||
        r="$(grep -m1 '^w16: exception' "$out/msquery-w16.txt") ($out/msquery-w16.txt)"
    [ "$r" != ok ] || { grep -q '^Windows ended' "$out/msquery-w16/input.log" 2>/dev/null &&
        grep -q '^w16: back to DOS, code 0000' "$out/msquery-w16.txt"; } ||
        r="w16 did not end Windows after Query and its Cue Cards were closed ($out/msquery-w16)"
    if [ "$r" = ok ]; then echo "ok   msquery: Microsoft Query's Cue Cards box takes the keys, Query closes and Windows ends as under 3.1"
    else echo "FAIL msquery: $r"; fails=$((fails + 1)); fi ;;
cvdos)
    [ -n "$(mfile BIN CV.EXE)" ] || { echo "skip cvdos: no CV.EXE in $msvc/BIN (W16_MSVC_DIR)"; continue; }
    set -- -p 386 -T "$root/build/w16" -3 -c "WIN /3 /B"
    for f in "$msvc"/BIN/*; do
        case $(basename "$f" | tr a-z A-Z) in
        *.EXE|*.DLL|*.386|*.HLP) set -- "$@" -f "$f=MSVC/BIN/$(basename "$f" | tr a-z A-Z)" ;;
        esac
    done
    for x in DOSXNT MMD CVW1 VMB; do
        set -- "$@" -i "386Enh:+device=C:\\MSVC\\BIN\\$x.386"
    done
    scr=$root/tests/scripts/input/cvdos.txt
    # w16's WIN386 first, then Microsoft's (its run's menu bar the reference)
    for s in w16 ms; do
        rm -rf "$out/cvdos-$s"
        [ $s = ms ] && set -- "$@" -f "$ref/SYSTEM/WIN386.EXE=W16/SYSTEM/WIN386.EXE"
        # (the user's machine: an S3 card, 32 MB)
        DROPIN_MACHINE=svga_s3 DROPIN_MEMSIZE=32 DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
            timeout 500 "$root/tests/inputrun.sh" -s -b 200 -t 300 \
            "$scr" "$out/cvdos-$s" "$@" > "$out/cvdos-$s.txt" 2>&1
    done
    r=ok
    for s in ms w16; do
        [ "$r" != ok ] || ! grep -aq 'E_Exit' "$out/cvdos-$s/dosbox.log" ||
            r="DOSBox-X ended ($s): $(grep -a -m1 'E_Exit' "$out/cvdos-$s/dosbox.log")"
        for n in 01-cv 02-moved 04-back; do
            [ "$r" != ok ] || [ -f "$out/cvdos-$s/$n.png" ] || r="no $n screen ($s: $out/cvdos-$s)"
        done
    done
    if [ "$r" = ok ]; then
        for n in 01-cv 02-moved; do
            [ "$(magick identify -format '%wx%h' "$out/cvdos-w16/$n.png")" = 720x400 ] || { r="$n: not a text screen"; break; }
            # CodeView's menu bar (the top 16 rows)
            for s in ms w16; do magick "$out/cvdos-$s/$n.png" -crop 720x16+0+0 +repage "$out/cvdos-$s-$n-bar.png"; done
            d=$(pixdiff "$out/cvdos-ms-$n-bar.png" "$out/cvdos-w16-$n-bar.png")
            [ "$d" = 0 ] || { r="$n: CodeView's menu bar differs from the run under Microsoft's WIN386: $d pixels"; break; }
        done
    fi
    [ "$r" != ok ] || [ "$(magick identify -format '%wx%h' "$out/cvdos-w16/04-back.png")" = 640x480 ] ||
        r="Windows not back after EXIT ($out/cvdos-w16/04-back.png)"
    if [ "$r" = ok ]; then echo "ok   cvdos: CodeView for MS-DOS starts, takes the mouse moving and quits in a DOS box; DOSBox-X stays alive"
    else echo "FAIL cvdos: $r"; fails=$((fails + 1)); fi ;;
*)
    echo "FAIL $part: no such part"; fails=$((fails + 1)) ;;
esac
done
[ $fails = 0 ]
