#!/bin/sh
# Microsoft Win32s 1.30c's Setup (Microsoft's Setup toolkit: SETUP.EXE,
# _MSTEST.EXE, the "Microsoft Test Driver", MS*STF.DLL) in 386 Enhanced
# mode, driven by real keys (tests/inputrun.sh, the script
# tests/scripts/win32s/setup.txt) from Program Manager's File > Run, the
# disks laid out as DISK1, DISK2, DISK3 below C:\INST\WIN32S (Setup reads
# the later disks from ..\DISK2 and ..\DISK3, as on 3.1). Parts:
#   k386   Windows 3.1 with w16's KRNL386 (tests/dropin.sh -3; Microsoft's
#          WIN386, USER, WIN.COM and a swap file): Setup must install and
#          restart Windows as on 3.1: W32SYS.DLL, W32S.386 and 63 more
#          files in SYSTEM\WIN32S, Freecell, the "Win32 Applications"
#          group (WIN32APP.GRP), SYSTEM.INI's device= line for W32S.386,
#          and no _MSSETUP.BAT left in WINDOWS (the restart runs it and
#          removes it).
#          w16's KERNEL stopped Setup at once ("Bindtime error:
#          'SETERRORMODE' not found in specified library": GetProcAddress
#          did not find KERNEL's non-resident names, TK76), and then
#          Windows did not restart (ExitKernel ended with 0, not
#          ExitWindowsExec's 44h).
#   treems w16's install tree with Microsoft's WIN386.EXE (from the
#          reference install, .tools/ref/win31/cdrive) put in its place and
#          a swap file: the same as k386, with w16's KERNEL, USER, GDI,
#          WIN.COM, Program Manager and drivers (ExitWindowsExec: TK77)
#   tree   w16's install tree alone (C:\W16, its WIN386): Setup's script
#          must get past its bindings, to its own full-screen window
#          (at least 40,000 pixels of its blue, 0000AAh, in one of the
#          first three screens). w16's WIN386 has no paging file, so Setup then says
#          "Win32s requires Virtual Memory" and "Microsoft Win32s System
#          Setup has failed.", as Windows 3.1 does with Paging=no.
# Win32s is not w16's: nothing of it is committed. Its three 1.44 MB disk
# images disk1.img, disk2.img, disk3.img are found anywhere below
# $W16_WIN32S_SRC (default .tools/win32s/src, as tests/win32ssetup.sh
# finds them); without them the test says so and passes ("skip"). mtools'
# mcopy copies their files out.
#
#   usage: tests/win32sinst.sh [-o OUTDIR] [PART ...]   (default: k386 treems tree)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
src=${W16_WIN32S_SRC:-$root/.tools/win32s/src}
out=
while getopts o: opt; do
    case $opt in
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
parts=${*:-k386 treems tree}
# (their paths can have blanks: one per line)
imgs=$(for i in 1 2 3; do find "$src" -iname "disk$i.img" 2>/dev/null | head -1; done)
for i in 1 2 3; do
    [ -n "$(printf '%s\n' "$imgs" | sed -n "${i}p")" ] ||
        { echo "skip: no Win32s disk image disk$i.img below $src (W16_WIN32S_SRC)"; exit 0; }
done
command -v mcopy > /dev/null || { echo "FAIL win32s: mtools' mcopy is needed"; exit 1; }
[ -n "$out" ] || out=$(mktemp -d)
mkdir -p "$out"
out=$(cd "$out" && pwd)
fails=0

# the disks' files, as DISK1-3
d=$out/disks
rm -rf "$d"
for i in 1 2 3; do
    f=$(printf '%s\n' "$imgs" | sed -n "${i}p")
    mkdir -p "$d/DISK$i"
    mcopy -s -n -i "$f" ::/ "$d/DISK$i/" || { echo "FAIL win32s: mcopy could not read $f"; exit 1; }
done
set --
for f in $(cd "$d" && find . -type f | sed 's|^\./||'); do
    set -- "$@" -f "$d/$f=INST/WIN32S/$f"
done

# PART dropin.sh-options...: one run of Setup in $out/PART
run() {
    p=$1; shift
    rm -rf "$out/$p"
    "$root/tests/inputrun.sh" -s -b 90 -t "${WIN32SINST_SECS:-1500}" \
        "$root/tests/scripts/win32s/setup.txt" "$out/$p" -c 'WIN /3' "$@" > "$out/$p.txt" 2>&1 || true
}

# PART WINDIR: Setup's result on that part's drive ("ok" or what is wrong)
installed() {
    c=$out/$1/work/c/$2
    for f in SYSTEM/W32SYS.DLL SYSTEM/WIN32S/W32S.386 WIN32APP.GRP ../WIN32APP/FREECELL/FREECELL.EXE; do
        [ -f "$c/$f" ] || { echo "$f missing"; return; }
    done
    n=$(find "$c/SYSTEM/WIN32S" -type f 2>/dev/null | wc -l)
    [ "$n" = 64 ] || { echo "$n files in SYSTEM\\WIN32S, not 64"; return; }
    tr -d '\r' < "$c/SYSTEM.INI" | grep -qi '^device=.*W32S\.386' ||
        { echo "no device= line for W32S.386 in SYSTEM.INI"; return; }
    [ ! -e "$c/_MSSETUP.BAT" ] || { echo "_MSSETUP.BAT left: Windows did not restart"; return; }
    echo ok
}

for p in $parts; do
    case $p in
    k386)
        run k386 -3 "$@"
        r=$(installed k386 WINDOWS)
        if [ "$r" = ok ]; then echo "ok   k386: Win32s installed and Windows restarted, with w16's KRNL386"
        else echo "FAIL k386: $r ($out/k386)"; fails=$((fails + 1)); fi ;;
    treems)
        w386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/WIN386.EXE
        if [ ! -f "$w386" ]; then echo "ok   treems: skipped (no $w386)"; continue; fi
        run treems -T "$root/build/w16" -3 -f "$w386=W16/SYSTEM/WIN386.EXE" \
            -i '386Enh:PagingFile=C:\W16\WIN386.SWP' "$@"
        r=$(installed treems W16)
        if [ "$r" = ok ]; then echo "ok   treems: Win32s installed and Windows restarted on w16's tree with Microsoft's WIN386"
        else echo "FAIL treems: $r ($out/treems)"; fails=$((fails + 1)); fi ;;
    tree)
        run tree -T "$root/build/w16" -3 "$@"
        # (the most of it in any of the first screens)
        n=0
        for s in welcome share target; do
            m=$(magick "$out/tree/$s.png" -fill black +opaque '#0000AA' -fill white -opaque '#0000AA' \
                -format '%[fx:int(mean*w*h+0.5)]' info: 2>/dev/null || echo 0)
            [ "$m" -le "$n" ] || n=$m
        done
        if [ "$n" -ge 40000 ]; then echo "ok   tree: Setup's script bound and started on w16's tree ($n pixels of its screen)"
        else echo "FAIL tree: Setup's screen did not come up ($n pixels; $out/tree/welcome.png)"; fails=$((fails + 1)); fi ;;
    *) echo "FAIL win32s: unknown part $p"; fails=$((fails + 1)) ;;
    esac
done
[ $fails = 0 ]
