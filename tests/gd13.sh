#!/bin/sh
# gd13.sh - run the TrueType probe GD13 (tests/apps/gd13.c) as the shell
# of Windows 3.1 with w16's GDI, or with Microsoft's (-r), the free test
# font copied in as C:\LIBSANS.TTF, and write its log to OUT.
#   usage: tests/gd13.sh [-r] [-f FONT.TTF] [-i KEY=VALUE] OUT
#     -i K=V   a [w16] setting for w16's GDI (repeatable), e.g.
#              TrueTypeHinting=0
# tools/gd13.py compares two logs (ARCHITECTURE.md 11.12).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
font=$root/tests/fixtures/fonts/LiberationSans-Regular.ttf ref= ini=
while getopts rf:i: opt; do
    case $opt in
    r) ref=-r ;; f) font=$OPTARG ;; i) ini="$ini -i $OPTARG" ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cp "$font" "$work/LIBSANS.TTF"
# shellcheck disable=SC2086
REFRUN_FILES="$work/LIBSANS.TTF" "$root/tests/gdiheap.sh" $ref $ini -p "$root/build/dos/GD13.EXE" > "$1" 2>&1
grep -c '^M ' "$1" > /dev/null || { echo "gd13: no results in $1" >&2; exit 1; }
