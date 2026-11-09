#!/bin/sh
# Clock regression against the original: each scenario in
# tests/scripts/clock/CKn.txt (a SNAP.EXE script that starts "run
# CLOCK.EXE") runs once with Windows 3.1's Clock and once with w16's
# (build/dos/CLOCK.EXE) on the reference Windows 3.1, in DOSBox-X (its
# DOS can set the time: SNAP's freeze holds the clock at a known second,
# so the screenshots are comparable). CKn.win holds WIN.INI changes
# ("section:key=value" per line) and CKn.ini a CLOCK.INI to start from.
# Afterwards CLOCK.INI must be byte-identical; every screenshot may
# differ by no more pixels than tests/scripts/clock.baseline allows
# ("CKn/SHOT count": our icon art and the free-memory figures in the About
# box, the edit caret's blink in the Font dialog); the window and
# menu dumps and the results of SNAP's send command must be the same.
# CK16 (no timer left: Clock's message box is system-modal, so SNAP can't
# take it) is a whole-screen capture of a WIN /S boot under xvfb
# (tests/gdiboot.sh -s).
#   usage: tests/clockscenarios.sh [-o DIR] [CKn...]   (default: all)
#   DIR defaults to build/ref/clock/<date-time>; CK_JOBS runs in
#   parallel (default 4). CK_BASELINE=update rewrites the baseline.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=
if [ "${1:-}" = -o ]; then out=$2; shift 2; fi
[ -n "$out" ] || out=$root/build/ref/clock/$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"
out=$(cd "$out" && pwd)
sc=$root/tests/scripts/clock
names=${*:-$(cd "$sc" && ls CK*.txt | sed 's/\.txt$//' | sort -V) CK16}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
base=$root/tests/scripts/clock.baseline
[ -f "$base" ] || : > "$base"
make -C "$root" -s build/dos/CLOCK.EXE build/dos/SNAP.EXE >/dev/null

run() {     # NAME WHO
    n=$1 who=$2 d=$out/$1/$2
    rm -rf "$d"; mkdir -p "$d"
    if [ "$who" = w16 ]; then
        sed 's/^run CLOCK.EXE$/run C:\\CLOCK.EXE/; s/$/\r/' "$sc/$n.txt" > "$d/SNAP.TXT"
        files="$d/SNAP.TXT $root/build/dos/CLOCK.EXE"
    else
        sed 's/$/\r/' "$sc/$n.txt" > "$d/SNAP.TXT"
        files=$d/SNAP.TXT
    fi
    cp "$ref/WIN.INI" "$d/WIN.INI.in"
    if [ -f "$sc/$n.win" ]; then
        while IFS= read -r kv; do
            [ -n "$kv" ] && python3 -I "$root/tests/scripts/iniset.py" "$d/WIN.INI.in" "$kv"
        done < "$sc/$n.win"
    fi
    files="$files $d/WIN.INI.in=WINDOWS/WIN.INI"
    if [ -f "$sc/$n.ini" ]; then
        sed 's/$/\r/' "$sc/$n.ini" > "$d/CLOCK.INI.in"
        files="$files $d/CLOCK.INI.in=WINDOWS/CLOCK.INI"
    fi
    REFRUN_X=1 REFRUN_TIMEOUT=${CK_TIMEOUT:-240} REFRUN_FILES="$files" REFRUN_OUT=$d \
        REFRUN_GET="WINDOWS/CLOCK.INI" "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S \
        > "$d/log.txt" 2>&1 || true
}

run16() {   # WHO: no timer left (SNAP takes them all, then starts Clock)
    d=$out/CK16/$1
    rm -rf "$d"; mkdir -p "$d"
    exe=CLOCK.EXE ours=
    if [ "$1" = w16 ]; then exe='C:\CLOCK.EXE' ours="-f $root/build/dos/CLOCK.EXE=CLOCK.EXE"; fi
    printf '%s\r\n' 'timers' "run $exe" 'wait 3000' > "$d/SNAP.TXT"
    "$root/tests/gdiboot.sh" -r -s -t 45 -d "$d" -f "$root/build/dos/SNAP.EXE=SNAP.EXE" \
        -f "$d/SNAP.TXT=SNAP.TXT" $ours \
        -i 'boot:shell=C:\SNAP.EXE' > "$d/log.txt" 2>&1 || true
    [ -f "$d/screen.png" ] && cp "$d/screen.png" "$d/NOTIMER.png"
}

# run everything, CK_JOBS at a time
jobs=0
for n in $names; do
    for who in orig w16; do
        if [ "$n" = CK16 ]; then run16 $who & else run $n $who & fi
        jobs=$((jobs + 1))
        if [ $jobs -ge "${CK_JOBS:-4}" ]; then wait; jobs=0; fi
    done
done
wait

status=0
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
for n in $names; do
    o=$out/$n/orig w=$out/$n/w16 result=ok shots=
    if [ -f "$o/CLOCK.INI" ] || [ -f "$w/CLOCK.INI" ]; then
        cmp -s "$o/CLOCK.INI" "$w/CLOCK.INI" || { result=FAIL; echo "  $n: CLOCK.INI differs"; }
    fi
    # the window and menu dumps and send's results
    grep -a '^WIN \|^MENU \|^SNAP send' "$o/log.txt" 2>/dev/null | tr -d '\0' > "$o/dump.txt" || true
    grep -a '^WIN \|^MENU \|^SNAP send' "$w/log.txt" 2>/dev/null | tr -d '\0' > "$w/dump.txt" || true
    if ! cmp -s "$o/dump.txt" "$w/dump.txt"; then
        lines=$(diff "$o/dump.txt" "$w/dump.txt" | grep -c '^[<>]' || true)
        echo "$n/dump $lines" >> "$newbase"
        allowed=$(awk -v k="$n/dump" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ] || [ "$lines" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: window dumps differ ($lines lines, $o/dump.txt)"
        fi
    fi
    for f in "$o"/*.BMP "$o"/NOTIMER.png; do
        [ -f "$f" ] || continue
        s=$(basename "$f"); s=${s%.*}
        g=$w/$(basename "$f")
        if [ ! -f "$g" ]; then result=FAIL; echo "  $n: screenshot $s missing"; continue; fi
        magick "$f" "$o/$s.png" 2>/dev/null || true
        magick "$g" "$w/$s.png" 2>/dev/null || true
        px=$(magick compare -metric AE "$f" "$g" "$out/$n/$s-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        [ -n "$allowed" ] || allowed=0
        if [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $s differs by $px pixels (allowed $allowed)"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $s improved: $px pixels (baseline $allowed)"
        fi
    done
    title=$(head -1 "$sc/$n.txt" 2>/dev/null | sed 's/^; //') || title=
    [ "$n" = CK16 ] && title='no timer left'
    echo "== $n: $result ($title; pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${CK_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | awk '$2 != 0' | sort -V > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
echo "results: $out"
exit $status
