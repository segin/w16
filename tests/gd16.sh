#!/bin/sh
# gd16.sh - run the font-substitution probe GD16 (tests/apps/gd16.c) as
# the shell of Windows 3.1, with w16's GDI or Microsoft's (-r), on a
# scratch copy of the reference install changed as w16's tree is: the
# Liberation fonts installed (build/w16/SYSTEM's LIB*.TTF and .FOT, made
# by make install-tree; WIN.INI [fonts] lines as w16's assets/WIN.INI
# has them) and w16's [FontSubstitutes] lines (Arial=Liberation Sans ...)
# added to WIN.INI. Writes the probe's log (GD16 start .. done) to OUT.
#   usage: tests/gd16.sh [-r] [-m] [-g GDI.EXE] [-i KEY=VALUE] OUT
#     -r   Microsoft's GDI (the reference)
#     -m   keep Microsoft's Arial, Times New Roman and Courier New
#          installed (by default their [fonts] lines are taken out, as
#          on w16's tree, which has no such font)
#     -g   the GDI to drop in (default build/dos/GDI.EXE)
#     -i   a [w16] setting for w16's GDI (repeatable), e.g. FontAliases=
# ARCHITECTURE.md 11.12; the boot-test case gdisubst.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/.tools/ref/win31/cdrive
w16=$root/build/w16
useref= keepms= gdi= w16ini=
while getopts rmg:i: opt; do
    case $opt in
    r) useref=-r ;; m) keepms=1 ;; g) gdi="-g $OPTARG" ;; i) w16ini="$w16ini -i $OPTARG" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
out=$1
[ -f "$ref/WINDOWS/WIN.COM" ] || { echo "skip gd16: no reference install at $ref"; exit 0; }
[ -f "$w16/SYSTEM/LIBSANS.FOT" ] || { echo "gd16: no $w16/SYSTEM/LIBSANS.FOT (make install-tree)" >&2; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
mkdir -p "$work/tree/WINDOWS/SYSTEM"
cp "$w16"/SYSTEM/LIB*.TTF "$w16"/SYSTEM/LIB*.FOT "$work/tree/WINDOWS/SYSTEM/"
ini=$work/tree/WINDOWS/WIN.INI
cp "$ref/WINDOWS/WIN.INI" "$ini"
chmod u+w "$ini"
set --
if [ -z "$keepms" ]; then
    for k in "Arial" "Arial Bold" "Arial Bold Italic" "Arial Italic" \
             "Times New Roman" "Times New Roman Bold" "Times New Roman Bold Italic" "Times New Roman Italic" \
             "Courier New" "Courier New Bold" "Courier New Bold Italic" "Courier New Italic"; do
        set -- "$@" "fonts:-$k (TrueType)="
    done
fi
# w16's [fonts] and [FontSubstitutes] lines for them (assets/WIN.INI)
sed -n '/^\[fonts\]/,/^\[/p' "$root/assets/WIN.INI" | grep '^Liberation' > "$work/fonts.txt"
while IFS= read -r l; do set -- "$@" "fonts:$l"; done < "$work/fonts.txt"
sed -n '/^\[FontSubstitutes\]/,/^\[/p' "$root/assets/WIN.INI" | grep '=' > "$work/subst.txt"
while IFS= read -r l; do set -- "$@" "FontSubstitutes:$l"; done < "$work/subst.txt"
python3 "$root/tests/scripts/iniset.py" "$ini" "$@"
# shellcheck disable=SC2086
REFRUN_TREE=$work/tree "$root/tests/gdiheap.sh" $useref $gdi $w16ini -p "$root/build/dos/GD16.EXE" > "$work/log" 2>&1 || true
sed -n '/^GD16 start/,/^GD16 done/p' "$work/log" | tr -d '\r' > "$out"
grep -q '^GD16 done' "$out" || { echo "gd16: no results ($work/log:)" >&2; tail -20 "$work/log" >&2; exit 1; }
