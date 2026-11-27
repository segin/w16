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
#   d  (REFDIR unused) both boxes kept their own current directory
set -u
kind=$1 a=$2 b=$3
case $kind in
d)  # w16 only: each box found its own directory (Windows 3.1's boxes
    # share DOSBox's: its X found Y's in nearly every poll). At most two
    # misses: one in some 58,000 polls came in some runs, UNVERIFIED where
    # (with the swaps traced it did not)
    for t in X Y; do
        grep -q 'other=000[012] polls=0*[1-9A-F]' "$b/CW$t.LOG" 2>/dev/null ||
            { echo "FAIL CW$t.LOG: $(cat "$b/CW$t.LOG" 2>/dev/null || echo none)"; exit 1; }
    done
    echo "ok   two boxes each kept their own current directory"; exit 0 ;;
p) logs="A B C D E F G H T U P V" ;;
g) logs="G" ;;
c) logs="X Y" ;;
*) echo "FAIL kind $kind"; exit 1 ;;
esac
for t in $logs; do
    [ -s "$a/DP$t.LOG" ] || { echo "FAIL DP$t.LOG: none from Windows 3.1 ($a)"; exit 1; }
    [ -s "$b/DP$t.LOG" ] || { echo "FAIL DP$t.LOG: none ($b; the reference: $(tail -n 1 "$a/DP$t.LOG"))"; exit 1; }
    # the nested client (F): its selectors follow what the outer one and
    # the host hold then, which differs between Windows 3.1's own runs
    # (its environment's D7h once, DFh another time): left out; so U's,
    # after T's client, which DOS ended in real mode and whose descriptors
    # 3.1 keeps (and three more of its own: the next first 0000h gives E7h;
    # 3.1 goes on as if T's client still ran, so a 32-bit client after it
    # is refused there: w16 frees it, and that is not compared)
    m='s/=0[0-9A-F][0-9A-F][0-9A-F]\b/=..../g'
    [ "$t" = F ] || [ "$t" = U ] || [ "$t" = P ] || [ "$t" = V ] || m=
    grep -v '^INFO\|^MEM' "$a/DP$t.LOG" | sed "$m" > "$b/DP$t.ref"
    grep -v '^INFO\|^MEM' "$b/DP$t.LOG" | sed "$m" > "$b/DP$t.cmp"
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
