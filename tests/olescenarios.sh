#!/bin/sh
# OLE regression (ARCHITECTURE.md 16): each scenario tests/ole/obs/NAME.txt
# (a script for the probe client OC1, tests/ole/oc1.c, with the DDE spy
# OCSPY) runs on the reference Windows 3.1 once with Microsoft's OLECLI.DLL
# and OLESVR.DLL and once per other combination, and every run is compared
# with the all-Microsoft one (tests/ole/olecmp.py: the client's calls,
# return codes and callbacks, the whole DDE conversation, the streams it
# saved byte for byte, OleDraw's pixels, with what differs by nature
# masked).
#
#   usage: tests/olescenarios.sh [-o OUTDIR] [-c COMBOS] [NAME...]
#     NAME       scenarios (default: every tests/ole/obs/*.txt)
#     -o OUTDIR  results (default build/olescen-HHMMSS, a new directory)
#     -c COMBOS  the combinations to test against ms:ms, CLIENT:SERVER with
#                ms (Microsoft's) or w16 (build/dos/OLECLI.DLL,
#                build/dos/OLESVR.DLL); default "w16:ms", plus "ms:w16
#                w16:w16" when build/dos/OLESVR.DLL exists
#   OLESCEN_JOBS  runs at once (default 1: one DOSBox at a time)
# Exit status: the number of failed scenario/combination pairs.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out= combos=
while getopts o:c: opt; do
    case $opt in
    o) out=$OPTARG ;; c) combos=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -n "$out" ] || out=$root/build/olescen-$(date +%H%M%S)
mkdir -p "$out"
if [ -z "$combos" ]; then
    combos=w16:ms
    [ -f "$root/build/dos/OLESVR.DLL" ] && combos="$combos ms:w16 w16:w16"
fi
names=${*:-$(cd "$root/tests/ole/obs" && ls *.txt | sed 's/\.txt$//')}
jobs=${OLESCEN_JOBS:-1}
# the launch scenario's stand-in server
printf 'PBRUSH.EXE\r\n' > "$out/OCSRV.TXT"
export OCRUN_FILES="$root/build/dos/OCSRV.EXE $out/OCSRV.TXT"
export OCRUN_TIMEOUT=${OCRUN_TIMEOUT:-240}

run() {     # run NAME CLIENT SERVER
    files=$OCRUN_FILES
    [ "$3" = w16 ] && files="$files $root/build/dos/OLESVR.DLL=WINDOWS/SYSTEM/OLESVR.DLL"
    dll=
    [ "$2" = w16 ] && dll=$root/build/dos/OLECLI.DLL
    OCRUN_FILES=$files "$root/tests/ole/ocrun.sh" "$root/tests/ole/obs/$1.txt" "$out/$1/$2-$3" $dll
}

# every run, OLESCEN_JOBS at a time
n=0
for name in $names; do
    for cs in ms:ms $combos; do
        run "$name" "${cs%:*}" "${cs#*:}" &
        n=$((n + 1))
        [ $n -lt "$jobs" ] || { wait; n=0; }
    done
done
wait

fails=0
for name in $names; do
    if ! grep -q '^OC1 done' "$out/$name/ms-ms/oc.log" 2>/dev/null; then
        echo "FAIL $name: the reference run did not finish ($out/$name/ms-ms)"
        fails=$((fails + 1))
        continue
    fi
    for cs in $combos; do
        d=$out/$name/${cs%:*}-${cs#*:}
        if python3 "$root/tests/ole/olecmp.py" "$out/$name/ms-ms" "$d" > "$d.diff" 2>&1; then
            echo "ok   $name ($cs)"
        else
            echo "FAIL $name ($cs): $(head -1 "$d.diff") ($d.diff)"
            fails=$((fails + 1))
        fi
    done
done
exit $fails
