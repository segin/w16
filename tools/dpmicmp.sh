#!/bin/sh
# dpmicmp.sh KIND REFDIR DIR: compare DPMIPRB's logs from tests/dpmirun.sh
# (KIND p, g or c) in DIR with Windows 3.1's in REFDIR. Lines starting
# with INFO (the host's own addresses, handles, linear addresses) and MEM
# (0500h's numbers, which depend on the memory and on 3.1's paging file)
# are left out. Prints "ok ..." or "FAIL ..." and exits 0 or 1.
#   p  every log as 3.1's (DPH.LOG: the nested 32-bit client refused)
#   g  DPG.LOG as 3.1's; neither run went on after the fault (no DPH.LOG,
#      no DPGAFT.TXT); the fault's message box on the screen as 3.1's
#   c  both boxes' logs as 3.1's: their memory intact while the other ran
set -u
kind=$1 a=$2 b=$3
case $kind in
p) logs="A B C D E F G H" ;;
g) logs="G" ;;
c) logs="X Y" ;;
*) echo "FAIL kind $kind"; exit 1 ;;
esac
for t in $logs; do
    [ -s "$a/DP$t.LOG" ] || { echo "FAIL DP$t.LOG: none from Windows 3.1 ($a)"; exit 1; }
    [ -s "$b/DP$t.LOG" ] || { echo "FAIL DP$t.LOG: none ($b; the reference: $(tail -n 1 "$a/DP$t.LOG"))"; exit 1; }
    grep -v '^INFO\|^MEM' "$a/DP$t.LOG" > "$b/DP$t.ref"
    grep -v '^INFO\|^MEM' "$b/DP$t.LOG" > "$b/DP$t.cmp"
    if ! cmp -s "$b/DP$t.ref" "$b/DP$t.cmp"; then
        echo "FAIL DP$t.LOG differs from Windows 3.1's: $(diff "$b/DP$t.ref" "$b/DP$t.cmp" | grep '^[<>]' | head -2 | tr '\n' ' ')(diff $b/DP$t.ref $b/DP$t.cmp)"
        exit 1
    fi
done
case $kind in
g)  for f in DPH.LOG DPGAFT.TXT; do
        [ ! -e "$a/$f" ] || { echo "FAIL Windows 3.1's box went on after the fault ($a/$f)"; exit 1; }
        [ ! -e "$b/$f" ] || { echo "FAIL the box went on after the fault ($b/$f)"; exit 1; }
    done
    # the message box (w16's box shows the MS-DOS Prompt's banner above
    # it, which Windows 3.1 leaves out for a batch file's PIF: not DPMI's)
    d=$(magick compare -metric AE "$a/shot.png[400x205+120+140]" "$b/shot.png[400x205+120+140]" null: 2>&1 | cut -d' ' -f1)
    [ "$d" = 0 ] || { echo "FAIL the fault's message box differs from Windows 3.1's: $d pixels ($b/shot.png)"; exit 1; }
    echo "ok   the probe's lines and the fault's end of the box as under Windows 3.1" ;;
p)  echo "ok   16- and 32-bit clients, twice each, and a nested one: every line as under Windows 3.1" ;;
c)  echo "ok   two boxes' clients at once, each one's memory intact, as under Windows 3.1" ;;
esac
exit 0
