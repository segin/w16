#!/bin/sh
# One part of the boot-test case toolhelp (tests/boottest.sh; ARCHITECTURE.md
# 22): w16's TOOLHELP.DLL against Microsoft's. Prints "ok PART: ..." or
# "FAIL PART: ..." as its last line; exits 1 on FAIL.
#   usage: tests/toolhelp/thcase.sh PART OUTDIR
# Parts:
#   ref286 ref386 ref3  the probe THP.EXE (tests/toolhelp/thp.c, every
#             section) on the reference Windows 3.1 (KRNL286, KRNL386 in
#             Standard mode, 386 Enhanced mode) with Microsoft's TOOLHELP
#             and with w16's: the logs equal section by section (nothing
#             masked but the lines tests/toolhelp/same.mask names, which
#             two runs of Microsoft's do not repeat either), and section d
#             (both TOOLHELPs side by side in one session, every structure
#             compared byte for byte) without a difference
#   tree286 tree386     w16's install tree alone under its KRNL286 /
#             KRNL386 with w16's TOOLHELP, against Windows 3.1 with
#             Microsoft's (tests/toolhelp/tree.mask: what differs because
#             w16's modules are not Microsoft's, each line saying why)
#   watson    Dr. Watson (3.1's DRWATSON.EXE, which imports 17 of
#             TOOLHELP's functions) catching the child's GP fault, with
#             each TOOLHELP (tests/toolhelp/thwatson.sh): the same entries
#             in its log
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
part=$1 out=$2
export THSCEN_JOBS=${THSCEN_JOBS:-6}    # beside the other boot-test cases
mkdir -p "$out"
t=$root/tests/toolhelp
case $part in
ref286|ref386|ref3)
    "$t/thscen.sh" -o "$out" -s "$part" -l "ms w16" -c "ms-$part:w16-$part" > "$out/scen.txt" 2>&1 || true
    r=ok
    for l in ms w16; do
        grep -q '^!!' "$out/$l-$part/thp.log" 2>/dev/null &&
            r="FAIL $part: $(grep -m1 '^!!' "$out/$l-$part/thp.log") ($l, $out)"
    done
    if [ "$r" = ok ]; then
        python3 "$t/thcmp.py" --mask "$t/same.mask" "$out/ms-$part/thp.log" "$out/w16-$part/thp.log" \
            > "$out/cmp.txt" 2>&1 || r="FAIL $part: $(grep -m1 '^FAIL' "$out/cmp.txt") ($out/cmp.txt)"
    fi
    if [ "$r" = ok ] && ! grep -q '^d: 0 difference' "$out/w16-$part/thp.log"; then
        r="FAIL $part: section d: $(grep -m1 '^d .*differs' "$out/w16-$part/thp.log" | cut -c1-120) ($out)"
    fi
    [ "$r" = ok ] && r="ok $part: the probe's log as with Microsoft's TOOLHELP, both side by side identical"
    ;;
tree286|tree386)
    k=${part#tree}
    "$t/thscen.sh" -o "$out" -r "ms-ref$k w16-$part" -c "ms-ref$k:w16-$part" > "$out/scen.txt" 2>&1 || true
    r=ok
    grep -q '^!!' "$out/w16-$part/thp.log" 2>/dev/null &&
        r="FAIL $part: $(grep -m1 '^!!' "$out/w16-$part/thp.log") ($out)"
    if [ "$r" = ok ]; then
        python3 "$t/thcmp.py" --mask "$t/tree.mask" "$out/ms-ref$k/thp.log" "$out/w16-$part/thp.log" \
            > "$out/cmp.txt" 2>&1 || r="FAIL $part: $(grep -m1 '^FAIL' "$out/cmp.txt") ($out/cmp.txt)"
    fi
    [ "$r" = ok ] && r="ok $part: w16's tree and TOOLHELP as 3.1 with Microsoft's"
    ;;
watson)
    for h in ms w16 tree; do "$t/thwatson.sh" "$out/$h" "$h" > /dev/null 2>&1 || true; done
    r=ok
    for h in w16 tree; do
        # the log's entries, the times and Windows' own addresses left out
        for f in ms $h; do
            sed -e 's/[0-9][0-9]:[0-9][0-9]:[0-9][0-9]//g' -e 's/[0-9A-F]\{4\}:[0-9A-F]\{4\}/xxxx:xxxx/g' \
                "$out/$f/drwatson.log" 2>/dev/null > "$out/$f.wat" || true
        done
        cmp -s "$out/ms.wat" "$out/$h.wat" || { r="FAIL watson: Dr. Watson's log with $h's differs ($out)"; break; }
    done
    [ "$r" = ok ] && r="ok watson: Dr. Watson's log as with Microsoft's TOOLHELP (3.1, w16's tree)"
    ;;
*)
    echo "FAIL $part: no such part"; exit 1 ;;
esac
echo "$r"
case $r in ok*) exit 0 ;; *) exit 1 ;; esac
