#!/bin/sh
# Adobe Acrobat Reader 2.0 without Adobe Type Manager, on w16's install
# tree and on Windows 3.1 (386 Enhanced mode, DOSBox-X): Reader shows
# "Adobe Type Manager was not found." and, after OK, ends itself (its C
# runtime puts INT 0 back through DOS3Call AX=2500h and ends with
# AX=4C00h while its windows are still there). On w16 (2026-11-27) USER
# then sent its window procedure WM_DESTROY and the rest, Reader faulted
# on its runtime's cleared data and w16 went back to DOS with code FFh.
# Windows 3.1 calls none of the ending program's window procedures
# (TK82, boot-test case appexit).
#
#   usage: tests/acroread.sh [-o OUTDIR]
#
# Reader is never in the repository: $W16_ACROREAD_DIR (default
# ~/Downloads/acroread) must hold the installed C:\ACROREAD directory's
# files (ACROREAD.EXE, ACROCORE.DLL, ACROGRAF.DLL, VIEWLIB.DLL); an
# ACROREAD.INI there (the one Reader's Setup leaves in the Windows
# directory) is put in the Windows directory. Skipped without them.
# Both runs start Reader from WIN.INI run=, press Return at the box, then
# end Windows with Alt+F4 and Return from Program Manager. 3.1 must end
# Windows; w16 must too, without an exception, with "back to DOS, code
# 0000", and its [w16] int21log must show Reader's runtime setting INT 0
# at its start and putting it back at its exit (the 4C00h call itself
# never returns, so it is not logged).
# Prints "ok", "FAIL ..." or "skip ..." lines.
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
out=
while getopts o: opt; do
    case $opt in
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
acro=${W16_ACROREAD_DIR:-$HOME/Downloads/acroread}
if [ ! -f "$acro/ACROREAD.EXE" ]; then
    echo "skip acroread: no Acrobat Reader at $acro (W16_ACROREAD_DIR)"
    exit 0
fi
[ -n "$out" ] || out=$(mktemp -d "${TMPDIR:-/tmp}/acroread.XXXXXX")
mkdir -p "$out"
out=$(cd "$out" && pwd)

# -f options for Reader's files: C:\ACROREAD, its INI in the Windows
# directory WIN (paths without blanks)
files() {
    (cd "$acro" && find . -type f) | while read -r f; do
        f=${f#./}
        case $(printf '%s' "$f" | tr a-z A-Z) in
        ACROREAD.INI) printf ' -f %s=%s/ACROREAD.INI' "$acro/$f" "$1" ;;
        *) printf ' -f %s=ACROREAD/%s' "$acro/$f" "$(printf '%s' "$f" | tr a-z A-Z)" ;;
        esac
    done
}

scr=$root/tests/scripts/input/acroread.txt
for s in ref w16; do
    rm -rf "$out/acro-$s"
    if [ $s = ref ]; then
        # shellcheck disable=SC2046
        set -- -r -3 -c "WIN /3 /B" -i 'WIN.INI:windows:run=C:\ACROREAD\ACROREAD.EXE' $(files WINDOWS)
    else
        # shellcheck disable=SC2046
        set -- -T "$root/build/w16" -3 -c "WIN /3 /B" -i w16:int21log=1 \
            -i 'WIN.INI:windows:run=C:\ACROREAD\ACROREAD.EXE' $(files W16)
    fi
    timeout 500 "$root/tests/inputrun.sh" -s -b 150 -t 300 "$scr" "$out/acro-$s" -p 386 "$@" \
        > "$out/acro-$s.txt" 2>&1 &
done
wait

r=ok
w=$out/acro-w16/work/c
grep -q '^Windows ended' "$out/acro-ref/input.log" 2>/dev/null ||
    r="Windows 3.1 did not end after Reader's box ($out/acro-ref)"
[ "$r" != ok ] || ! grep -q '^w16: exception' "$w/OUT.TXT" 2>/dev/null ||
    r="$(grep -m1 '^w16: exception' "$w/OUT.TXT") ($w/OUT.TXT)"
[ "$r" != ok ] || { grep -q '^Windows ended' "$out/acro-w16/input.log" 2>/dev/null &&
    grep -q '^w16: back to DOS, code 0000' "$w/OUT.TXT"; } ||
    r="w16 did not end Windows after Reader's box ($out/acro-w16)"
[ "$r" != ok ] || [ "$(grep -c '^ACROBAT [0-9A-F:]* 2500 ' "$w/W16DBG.LOG" 2>/dev/null)" -ge 2 ] ||
    r="Reader did not end itself on w16 (its runtime's INT 21h 2500h at the start and at the exit not both in $w/W16DBG.LOG)"
if [ "$r" = ok ]; then echo "ok   acroread: Acrobat Reader 2.0 ends itself after its ATM box, Windows goes on and ends as under 3.1"
else echo "FAIL acroread: $r"; exit 1; fi
