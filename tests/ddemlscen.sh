#!/bin/sh
# DDEML scenarios (ARCHITECTURE.md, DDEML): each script of
# tests/ddeml/scripts (NAME.dm, run by DM.EXE, tests/ddeml/dm.c) runs on
# the reference Windows 3.1 with Microsoft's DDEML.DLL, and again with
# w16's (build/dos/DDEML.DLL) in each combination CLIENT-SERVER: the
# DM.EXE copies use the CLIENT library as DDEML, the DMX.EXE copies the
# SERVER library renamed DDEMX (tests/ddeml/dmrun.sh). Every probe line,
# every callback and every WM_DDE_* message (OSSPY.DLL) is logged; the
# logs, normalised by tests/ddeml/dmnorm.py (what differs between
# Microsoft's own runs), must equal the ms-ms run's.
#   usage: tests/ddemlscen.sh [-o OUTDIR] [-c COMBOS] [-n] [NAME...]  (default: all)
# -c the combinations to compare with ms-ms (default "w16-ms ms-w16 w16-w16")
# -n run ms-ms only (observation)
# OUTDIR defaults to a fresh build/ddemlscen-<date>-<time>. DDEMLSCEN_JOBS
# (default 6) runs at a time.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/build/ddemlscen-$(date +%Y%m%d-%H%M%S)
combos="w16-ms ms-w16 w16-w16"
while getopts o:c:n opt; do
    case $opt in
    o) out=$OPTARG ;;
    c) combos=$OPTARG ;;
    n) combos= ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
s=$root/tests/ddeml/scripts
names=${*:-$(cd "$s" && ls *.dm | grep -v "^x" | sed "s/\.dm$//")}
combos="ms-ms $combos"
mkdir -p "$out"
echo "ddemlscen: $out"
for c in $combos; do
    for n in $names; do echo "$n $c"; done
done | xargs -P "${DDEMLSCEN_JOBS:-6}" -L 1 sh -c \
    'c=$3; "$0/tests/ddeml/dmrun.sh" "$2" "$1/$2-$c" "${c%-*}" "${c#*-}" > /dev/null 2>&1 || true' \
    "$root" "$out"
status=0
for n in $names; do
    for c in $combos; do
        d=$out/$n-$c
        if [ -s "$d/os.log" ]; then
            python3 "$root/tests/ddeml/dmnorm.py" "$d/os.log" > "$d/os.cmp"
        else
            : > "$d/os.cmp"
        fi
    done
    ref=$out/$n-ms-ms
    for c in $combos; do
        [ "$c" = ms-ms ] && continue
        d=$out/$n-$c
        result=ok
        if [ ! -s "$ref/os.log" ] || [ ! -s "$d/os.log" ]; then
            result="FAIL (no log)"
        elif ! diff "$ref/os.cmp" "$d/os.cmp" > "$d/os.diff"; then
            result="FAIL (log differs: $d/os.diff)"
        fi
        if [ -f "$s/$n.get" ]; then
            for f in $(cat "$s/$n.get"); do
                b=$(basename "$f")
                if [ ! -f "$ref/$b" ] || ! cmp -s "$ref/$b" "$d/$b"; then
                    result="FAIL ($b differs)"
                fi
            done
        fi
        echo "== $n $c: $result"
        [ "$result" = ok ] || status=1
    done
    [ -s "$ref/os.log" ] || { echo "== $n ms-ms: no log"; status=1; }
done
exit $status
