#!/bin/sh
# A real 3.1 caller of VER.DLL: Control Panel's Fonts applet (MAIN.CPL,
# which imports GetFileResource, VerFindFile and VerInstallFile) adds a
# font under real keys (tests/scripts/input/cplfonts.txt, tests/inputrun.sh)
# on the reference install, with Microsoft's VER.DLL or w16's. The font is
# w16's own (fonts/vgafix.txt compiled by tools/fontc.py as W16TEST.FON,
# module W16TEST, "w16 Test") in C:\VTF. OUTDIR gets the shots, the
# scratch drive (OUTDIR/work/c) and fonts.txt: what was installed (the
# system directory's copy and WIN.INI's [fonts] line).
# With VFONTS_NEWER=1 the system directory already has a W16TEST.FON of a
# later version (4.0.0.0, w16's VGAOEM font) that WIN.INI does not list:
# 3.1's MAIN.CPL then installs the new font as W16TEST0.FON beside it.
#   usage: tests/ver/vfonts.sh OUTDIR ms|w16 [-e REFDIR]
# -e REFDIR: the reference run's directory (its boot.png and shots: the
# w16 run waits for the same screens, tests/inputrun.sh -e/-E)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 lib=$2
shift 2
ref=
[ "${1:-}" = -e ] && ref=$2
mkdir -p "$out"
tmp=$out.in
rm -rf "$tmp"
mkdir -p "$tmp"
python3 "$root/tools/fontc.py" fon "$tmp/W16TEST.FON" --module W16TEST \
    --desc "FONTRES 100,96,96 : w16 Test" --info CompanyName=w16 --info FileVersion=3.10 \
    --info ProductVersion=3.10 --info _version=3.10.0.103 --info ProductName=w16 \
    --info "FileDescription=w16 test font" --info InternalName=W16TEST \
    --info OriginalFilename=W16TEST.FON 31="$root/fonts/vgafix.txt"
if [ -n "${VFONTS_NEWER:-}" ]; then
    python3 "$root/tools/fontc.py" fon "$tmp/NEWER.FON" --module W16TEST \
        --desc "FONTRES 100,96,96 : w16 Test" --info CompanyName=w16 --info FileVersion=4.0 \
        --info ProductVersion=4.0 --info _version=4.0.0.0 --info ProductName=w16 \
        --info "FileDescription=w16 test font" --info InternalName=W16TEST \
        --info OriginalFilename=W16TEST.FON 31="$root/fonts/vgaoem.txt"
fi
cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$tmp/WIN.INI"
python3 "$root/tests/scripts/iniset.py" "$tmp/WIN.INI" 'windows:run=CONTROL.EXE' \
    'windows:CursorBlinkRate=65535'
set -- -r -f "$tmp/WIN.INI=WINDOWS/WIN.INI" -f "$tmp/W16TEST.FON=VTF/W16TEST.FON"
[ "$lib" = w16 ] && set -- "$@" -f "$root/build/dos/VER.DLL=WINDOWS/SYSTEM/VER.DLL"
[ -z "${VFONTS_NEWER:-}" ] || set -- "$@" -f "$tmp/NEWER.FON=WINDOWS/SYSTEM/W16TEST.FON"
if [ -n "$ref" ]; then
    set -- -e "$ref/boot.png" -E "$ref" -b 25 -t 300 \
        "$root/tests/scripts/input/cplfonts.txt" "$out" "$@"
else
    set -- -s -b 25 -t 300 "$root/tests/scripts/input/cplfonts.txt" "$out" "$@"
fi
rm -rf "$out"
"$root/tests/inputrun.sh" "$@" > "$out.txt" 2>&1 || true
c=$out/work/c
{
    for f in "$c"/WINDOWS/SYSTEM/W16TEST*.FON "$c"/WINDOWS/W16TEST*.FON "$c"/VTF/W16TEST.FON; do
        if [ -f "$f" ]; then
            w=differs
            cmp -s "$f" "$tmp/W16TEST.FON" && w="the new one"
            [ -f "$tmp/NEWER.FON" ] && cmp -s "$f" "$tmp/NEWER.FON" && w="the newer one"
            echo "${f#$c/}: $w"
        else echo "${f#$c/}: not there"; fi
    done
    ls "$c"/WINDOWS/SYSTEM | grep -i '^temp\.' || true
    tr -d '\r' < "$c/WINDOWS/WIN.INI" | sed -n '/^\[fonts\]/,/^\[/p' | grep -i 'w16' || echo "WIN.INI: no w16 font"
} > "$out/fonts.txt" 2>&1
cat "$out/fonts.txt"
