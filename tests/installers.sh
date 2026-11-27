#!/bin/sh
# Third-party Windows 3.x installers run on w16's install tree in 386
# Enhanced mode, the user's set-up (build/w16 as C:\W16, WIN /3), each
# driven by real keys from Program Manager's File > Run
# (tests/scripts/installers/NAME.txt); with a reference install, the same
# on Windows 3.1 too, and the files each one installed are compared.
#
#   usage: tests/installers.sh OUTDIR [NAME ...]
#
# The installers are not part of w16 and are never committed: they come
# from $W16_INSTDIR (default ~/Downloads/w16-installers), one directory
# per installer, copied to C:\INST\NAME (subdirectories too); an
# installer whose directory is missing is skipped ("skip NAME"). NAMEs
# (default: all):
#   WEP1 WEP2 WEP3  Microsoft Entertainment Pack 1, 2 and 3, each pack's
#                   disks in one directory (SETUP.EXE, BIN\ or BINXZ.EXE)
#   BW              The Best of Microsoft Entertainment Pack: BW\DISK1
#                   (Microsoft Setup looks for its disks in ..\DISK1)
#   W4              Entertainment Pack 4: W4\DISK1, W4\DISK2
#   JOHNNY          Screen Antics (Johnny Castaway): Sierra's SETUP.EXE,
#                   INSTALL.EX$, INSTALL.INS ...
#   WP3             WinPlay3 2.3 beta 5's self-extracting installer,
#                   WP3V23B5.EXE (a self-loading program: Blinker's loader)
#   MP52            an MPEG player beta's, MP52BETA.EXE (the same loader)
# What each fixed (ARCHITECTURE.md 9.12): WEP1-3 zero bytes free
# (FindResource through a name table), WEP2-3 hanging at 0% (GetMessage
# with a filter), BW and W4 deaf to Return (a dialog's focus; W4 also
# COMMDLG.DLL's version), JOHNNY's cancel loop (the name table too), WP3
# and MP52 not starting (self-loading programs; EntryAddrProc's registers).
#
# Each script's "; dest DIR" line names the directory the installer
# fills and "; expect FILE ..." files that must be there afterwards
# (paths under C:\); "; anysize NAME ..." files under DIR compared by name
# only (a log that holds paths of the Windows directory). PASS/FAIL per installer and per check; with the
# reference (.tools/ref/win31/cdrive), also the list of files (names and
# sizes) under DIR on w16 against 3.1's. Results in OUTDIR/NAME-w16 and
# NAME-ref (inputrun.sh's: shots, the scratch drive).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
src=${W16_INSTDIR:-$HOME/Downloads/w16-installers}
[ $# -ge 1 ] || { sed -n '2,/^set -u/p' "$0" | sed '$d'; exit 2; }
out=$1; shift
names=${*:-WEP1 WEP2 WEP3 BW W4 JOHNNY WP3 MP52}
mkdir -p "$out"
out=$(cd "$out" && pwd)
ref=
[ -d "$root/.tools/ref/win31/cdrive/WINDOWS" ] && ref=1
fails=0

# dropin.sh -f options for every file under $src/$1, at C:\INST\$1
files_of() {
    (cd "$src/$1" && find . -type f) | sed 's|^\./||' | while IFS= read -r f; do
        printf -- '-f\n%s=INST/%s/%s\n' "$src/$1/$f" "$1" "$f"
    done
}

# NAME SIDE: one run; SIDE w16 or ref
run_one() {
    n=$1 side=$2 d=$out/$1-$2
    rm -rf "$d"
    set --
    while IFS= read -r a; do set -- "$@" "$a"; done <<EOF
$(files_of "$n")
EOF
    if [ "$side" = ref ]; then set -- -r -3 "$@"; else set -- -T "$root/build/w16" -3 "$@"; fi
    "$root/tests/inputrun.sh" -s -b 90 -t "${INSTALLERS_SECS:-900}" \
        "$root/tests/scripts/installers/$n.txt" "$d" -c 'WIN /3' "$@" > "$d.txt" 2>&1
}

# NAME SIDE DIR [ANYSIZE...]: "size name" lines of the files under DIR on
# that side's drive (size "*" for the ANYSIZE names)
listing() {
    c=$out/$1-$2/work/c d=$3
    shift 3
    [ -d "$c/$d" ] || return 0
    (cd "$c/$d" && find . -type f -printf '%s %p\n' | tr 'a-z' 'A-Z' | sort -k2) |
        awk -v any=" $* " '{ n = $2; sub(/.*\//, "", n); if (index(any, " " n " ")) $1 = "*"; print }'
}

for n in $names; do
    s=$root/tests/scripts/installers/$n.txt
    [ -f "$s" ] || { echo "FAIL $n: no script $s"; fails=$((fails + 1)); continue; }
    [ -d "$src/$n" ] || { echo "skip $n: no $src/$n"; continue; }
    dest=$(sed -n 's/^; dest //p' "$s" | tr '\\' /)
    anysize=$(sed -n 's/^; anysize //p' "$s" | tr 'a-z' 'A-Z')
    run_one "$n" w16 &
    [ -n "$ref" ] && run_one "$n" ref &
    wait
    r=ok
    for f in $(sed -n 's/^; expect //p' "$s" | tr '\\' /); do
        [ -f "$out/$n-w16/work/c/$f" ] || { r="$f missing"; break; }
    done
    if [ "$r" = ok ] && [ -n "$ref" ]; then
        # shellcheck disable=SC2086
        listing "$n" ref "$dest" $anysize > "$out/$n-ref.files"
        # shellcheck disable=SC2086
        listing "$n" w16 "$dest" $anysize > "$out/$n-w16.files"
        if [ ! -s "$out/$n-ref.files" ]; then
            r="3.1 installed nothing under $dest ($out/$n-ref)"
        elif ! diff "$out/$n-ref.files" "$out/$n-w16.files" > "$out/$n.diff"; then
            r="the files under $dest differ from 3.1's ($out/$n.diff)"
        fi
    fi
    if [ "$r" = ok ]; then echo "PASS $n"
    else echo "FAIL $n: $r"; fails=$((fails + 1)); fi
done
[ $fails = 0 ]
