#!/bin/sh
# Notepad regression against the original: each scenario in
# tests/scripts/np/*.txt (a SNAP.EXE script) runs once with Windows 3.1's
# NOTEPAD.EXE and once with w16's (build/dos/NOTEPAD.EXE copied over it in
# the scratch install) on the reference Windows 3.1, Standard mode.
# Afterwards the files the scenario names must be byte-identical, and no
# screenshot may differ from the original's by more pixels than
# tests/scripts/np.baseline allows ("scenario/SHOT count": what our own
# icon art costs). NP_BASELINE=update rewrites the baseline from this run.
# Directives in a scenario's comment lines:
#   ; files A B     copy tests/scripts/np/data/A ... to C:\ first
#   ; gen N L NAME  generate C:\NAME: N bytes of L-character lines
#   ; get P Q       compare these files (paths under C:\) afterwards
#   ; time          DOSBox-X with the clock set to 09:05 (F5, .LOG)
#   ; help          a logging stand-in WINHELP.EXE (tests/apps/thelp.c);
#                   the WinHelp requests must be the same
#   ; log           compare SNAP's log (dump, info, text) as well
#   usage: tests/npscenarios.sh [-o DIR] [scenario...]   (default: all)
# Output: DIR (default build/ref/np/<timestamp>)/<scenario>/{orig,w16}/.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/ref/np/$(date +%Y%m%d-%H%M%S)
if [ "${1:-}" = -o ]; then out=$2; shift 2; fi
sd=$root/tests/scripts/np
names=${*:-$(cd "$sd" && ls *.txt | sed 's/\.txt$//')}
status=0
base=$root/tests/scripts/np.baseline
[ -f "$base" ] || : > "$base"
newbase=$(mktemp); gen=$(mktemp -d); trap 'rm -rf "$newbase" "$gen"' EXIT
mkdir -p "$out"
for n in $names; do
    s=$sd/$n.txt
    files= get= x= pre= help=
    for f in $(sed -n 's/^; files //p' "$s"); do files="$files $sd/data/$f"; done
    sed -n 's/^; gen //p' "$s" | while read -r size len name; do
        python3 -I -c "
import sys
n, l = int(sys.argv[1]), int(sys.argv[2]); line = b'x' * l + b'\r\n'
open(sys.argv[3], 'wb').write((line * (n // len(line) + 1))[:n])" "$size" "$len" "$gen/$name"
    done
    for f in $(sed -n 's/^; gen [0-9]* [0-9]* //p' "$s"); do files="$files $gen/$f"; done
    get=$(sed -n 's/^; get //p' "$s" | tr '\n' ' ')
    if grep -q '^; time' "$s"; then x=1 pre="TIME 09:05:00"; fi
    grep -q '^; help' "$s" && help="$root/build/dos/THELP.EXE=WINDOWS/WINHELP.EXE"
    for who in orig w16; do
        d=$out/$n/$who
        rm -rf "$d"; mkdir -p "$d"
        sed 's/$/\r/' "$s" > "$d/SNAP.TXT"
        extra=
        [ $who = w16 ] && extra=$root/build/dos/NOTEPAD.EXE=WINDOWS/NOTEPAD.EXE
        REFRUN_X=$x REFRUN_PRE=$pre REFRUN_TIMEOUT=${REFRUN_TIMEOUT:-300} \
        REFRUN_FILES="$d/SNAP.TXT $files $help $extra" REFRUN_OUT=$d REFRUN_GET="$get" \
            "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" /S > "$d/log.txt" 2>&1 || true
        # one line per WinHelp request: mark, command, file, data (the
        # topic only where the command has one)
        # (strings past the block's own size, its first word, are left
        # over in the memory block and not part of the request)
        grep -a '^HELP \(msg 0477\|block\|text\)' "$d/log.txt" | awk '
            function hex(s,  i, v) { v = 0; for (i = 1; i <= length(s); i++)
                                     v = v * 16 + index("0123456789ABCDEF", substr(s, i, 1)) - 1; return v }
            /msg 0477/ { mark = substr($4, 4); next }
            /block/    { cmd = $8 $7; topic = (cmd == "0001" ? $12 $11 $10 $9 : "-"); cb = hex($6 $5)
                         printf "%smark %s cmd %s topic %s", sep, mark, cmd, topic; sep = "\n"; next }
            /text/     { o = hex(substr($3, 2)); if (o < 16 || o >= cb) next
                         sub(/^HELP text @[0-9A-F]+ /, ""); printf " %s", $0 }
            END { print "" }' > "$d/requests.txt"
        # the log without SNAP's own progress lines and window handles
        grep -a '^\(WIN\|INFO\|MENU\|TEXT\|[^S]\)' "$d/log.txt" | grep -av '^HELP\|^WINEXIT' |
            sed 's/ hinst=[0-9A-F]*//; s/ icon=[0-9A-F]*//; s/ cursor=[0-9A-F]*//' > "$d/state.txt" || true
    done
    result=ok
    for f in $get; do
        b=$(basename "$f")
        if [ -f "$out/$n/orig/$b" ] || [ -f "$out/$n/w16/$b" ]; then
            cmp -s "$out/$n/orig/$b" "$out/$n/w16/$b" || { result=FAIL; echo "  $n: $b differs"; }
        fi
    done
    if [ -n "$help" ] && ! cmp -s "$out/$n/orig/requests.txt" "$out/$n/w16/requests.txt"; then
        result=FAIL; echo "  $n: WinHelp requests differ"
        diff "$out/$n/orig/requests.txt" "$out/$n/w16/requests.txt" | sed 's/^/     /' || true
    fi
    if grep -q '^; log' "$s" && ! cmp -s "$out/$n/orig/state.txt" "$out/$n/w16/state.txt"; then
        result=FAIL; echo "  $n: logged state differs"
        diff "$out/$n/orig/state.txt" "$out/$n/w16/state.txt" | head -20 | sed 's/^/     /' || true
    fi
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        sh=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$sh.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $sh missing"; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/$n/w16/$sh.BMP" "$out/$n/$sh-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $sh=$px"
        echo "$n/$sh $px" >> "$newbase"
        allowed=$(awk -v k="$n/$sh" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            [ "$px" = 0 ] || { echo "  $n: $sh differs by $px pixels (no baseline)"; [ "${NP_BASELINE:-}" = update ] || result=FAIL; }
        elif [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $sh differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $sh improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${NP_BASELINE:-}" = update ]; then
    awk '$2 != 0' "$newbase" > "$newbase.nz"
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase.nz" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    rm -f "$newbase.nz"
    echo "baseline updated: $base"
fi
echo "output: $out"
exit $status
