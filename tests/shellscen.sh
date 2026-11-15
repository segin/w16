#!/bin/sh
# SHELL.DLL scenarios (ARCHITECTURE.md 13): each script in
# tests/scripts/shell/*.txt (a SNAP.EXE script) runs on the reference
# Windows 3.1 twice, with Microsoft's SHELL.DLL and with w16's
# (build/dos/SHELL.DLL as WINDOWS\SYSTEM\SHELL.DLL), everything else
# Microsoft's: About boxes, Program Manager's Change Icon, a File Manager
# drag onto Program Manager, opening documents from File Manager
# (ShellExecute and the associations), REGEDIT.
#
# Afterwards REG.DAT, WIN.INI and the group files must be byte-identical,
# and no screenshot may differ from the reference by more pixels than
# tests/scripts/shell.baseline allows ("scenario/SHOT count"; what is
# allowed is w16's own About-box text and picture, ARCHITECTURE.md 13).
# SHELL_BASELINE=update rewrites the baseline from this run.
#   usage: tests/shellscen.sh [-o DIR] [scenario...]   (default: all)
# Results (screenshots as PNG, logs, diffs) go to DIR (default
# build/shellscen-HHMMSS, a new directory each run).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=
while getopts o: opt; do
    case $opt in
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -n "$out" ] || out=$root/build/shellscen-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
files="WINDOWS/REG.DAT WINDOWS/WIN.INI WINDOWS/PROGMAN.INI WINDOWS/MAIN.GRP WINDOWS/ACCESSOR.GRP WINDOWS/GAMES.GRP WINDOWS/STARTUP.GRP"
names=${*:-$(cd "$root/tests/scripts/shell" && ls *.txt | sed 's/\.txt$//')}
base=$root/tests/scripts/shell.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
status=0
for n in $names; do
    for who in orig w16; do
        d=$out/$n/$who
        mkdir -p "$d"
        sed 's/$/\r/' "$root/tests/scripts/shell/$n.txt" > "$d/SNAP.TXT"
        extra=
        [ $who = w16 ] && extra="$root/build/dos/SHELL.DLL=WINDOWS/SYSTEM/SHELL.DLL"
        REFRUN_TIMEOUT=240 REFRUN_FILES="$d/SNAP.TXT $extra" REFRUN_OUT=$d REFRUN_GET="$files" \
            "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt" 2>&1 || true
        for f in "$d"/*.BMP; do
            [ -f "$f" ] && magick "$f" "${f%.BMP}.png" && rm -f "$f"
        done
    done
    result=ok
    for f in $files; do
        b=$(basename "$f")
        if [ -f "$out/$n/orig/$b" ] || [ -f "$out/$n/w16/$b" ]; then
            cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" || { result=FAIL; echo "  $n: $b differs"; }
        fi
    done
    shots=""
    for f in "$out/$n"/orig/*.png; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .png)
        if [ ! -f "$out/$n/w16/$s.png" ]; then
            result=FAIL; echo "  $n: screenshot $s missing"; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/$n/w16/$s.png" "$out/$n/$s-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $s has no baseline ($px pixels differ)"
            [ "${SHELL_BASELINE:-}" = update ] || result=FAIL
        elif [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $s differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $s improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${SHELL_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
echo "shellscen: results in $out"
exit $status
