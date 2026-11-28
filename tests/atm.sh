#!/bin/sh
# Adobe Type Manager 3.0 on w16's install tree and on Windows 3.1 (386
# Enhanced mode, DOSBox-X), and Acrobat Reader 2.0 with it.
#
#   usage: tests/atm.sh [-o OUTDIR] [install] [acro]
#
#   install  the installer (INSTALL.EXE) run from C:\INSTALL\ATM\DISK1 with
#            the second disk's files in C:\INSTALL\ATM\DISK2 (keys:
#            tests/scripts/input/atminst.txt), Windows restarted with ATM
#            and ended. Both sides must leave SYSTEM.INI's system.drv=
#            atmsys.drv and atm.system.drv=system.drv, the same files
#            under C:\PSFONTS (names and sizes), the same ATM.INI and
#            ATMSYS.DRV and ATM16/ATM32 in the system directory; w16 must
#            restart Windows (code 0042) and end it without an exception.
#            Before the fixes the installer stopped at once with "The
#            Installer Configuration file is missing the following entry:
#            ':*'" (KERNEL's lstrcpy, TK84) and ATM.INI named the font
#            files in capitals (USER's LB_DIR, US42)
#   acro     Acrobat Reader 2.0 (from $W16_ACROREAD_DIR, as tests/acroread.sh
#            takes it) started from File > Run with its HELP\HELP_R.PDF on
#            the drive each side's install left, with Reader's own ATM
#            fonts added (the Multiple Master faces it needs: the
#            ZX______, ZY______ and ZD______ .PFB files in
#            $W16_ACROREAD_DIR/FONTS and their .PFM and .MMM files in
#            FONTS/PFM, put in the Windows directory and listed in
#            ATM.INI's [Fonts] and [MMFonts] as Reader's Setup does).
#            Both sides must show the document: its links, red text in a
#            Type 1 face that ATM draws, are counted in the document pane
#            (w16 within 10% of 3.1; OUTDIR/acro-cmp.png has the two
#            screens side by side). Before ATM's interoperability
#            interface (ARCHITECTURE.md 11.13: LoadModule's and
#            CreateDC's entries, the first instructions of the GDI
#            functions it hooks) Reader said "Adobe Type Manager was not
#            found." on w16. Not compared yet: the page's place (w16
#            shows no vertical scroll bar and no status bar there) and
#            the spacing of ATM's text
#
# The disks are never in the repository: $W16_ATM_DIR (default
# ~/Downloads/atm) must hold DISK1.IMG and DISK2.IMG, the two 1.44 MB
# disk images; their files are taken out with mtools' mcopy. Skipped
# without them (and acro without Reader). Prints "ok", "xfail", "FAIL"
# or "skip" lines; OUTDIR/install-ref and install-w16 (inputrun.sh's,
# the drives), acro-ref and acro-w16.
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
shift $((OPTIND - 1))
parts=${*:-install acro}
atm=${W16_ATM_DIR:-$HOME/Downloads/atm}
acro=${W16_ACROREAD_DIR:-$HOME/Downloads/acroread}
if [ ! -f "$atm/DISK1.IMG" ] || [ ! -f "$atm/DISK2.IMG" ]; then
    echo "skip atm: no Adobe Type Manager disk images at $atm (W16_ATM_DIR)"
    exit 0
fi
[ -n "$out" ] || out=$(mktemp -d "${TMPDIR:-/tmp}/atm.XXXXXX")
mkdir -p "$out"
out=$(cd "$out" && pwd)
export INPUTRUN_KEYDELAY=${INPUTRUN_KEYDELAY:-150}
rc=0

# the disks' files, for C:\INSTALL\ATM\DISKn
rm -rf "$out/disks"
for d in 1 2; do
    mkdir -p "$out/disks/DISK$d"
    mcopy -s -n -i "$atm/DISK$d.IMG" '::*' "$out/disks/DISK$d/" > /dev/null
done
# -f options for every file under DIR at C:\PREFIX (paths without blanks)
files() {
    (cd "$1" && find . -type f) | sed 's|^\./||' | while read -r f; do
        printf ' -f %s=%s/%s' "$1/$f" "$2" "$(printf '%s' "$f" | tr a-z A-Z)"
    done
}
side() { [ "$1" = ref ] && echo "-r -3" || echo "-T $root/build/w16 -3"; }
windir() { [ "$1" = ref ] && echo WINDOWS || echo W16; }

for p in $parts; do
case $p in
install)
    for s in ref w16; do
        rm -rf "$out/install-$s"
        # shellcheck disable=SC2046
        timeout 2400 "$root/tests/inputrun.sh" -s -b 150 -t 1800 \
            "$root/tests/scripts/input/atminst.txt" "$out/install-$s" -p 386 -c "WIN /3 /B" \
            $(side $s) $(files "$out/disks" INSTALL/ATM) > "$out/install-$s.txt" 2>&1 &
    done
    wait
    r=ok
    for s in ref w16; do
        c=$out/install-$s/work/c w=$(windir $s)
        tr -d '\r' < "$c/$w/SYSTEM.INI" > "$out/install-$s.sysini" 2>/dev/null || :
        grep -qix 'system.drv=atmsys.drv' "$out/install-$s.sysini" &&
            grep -qix 'atm.system.drv=system.drv' "$out/install-$s.sysini" ||
            { r="$s: SYSTEM.INI has no system.drv=atmsys.drv ($c/$w/SYSTEM.INI)"; break; }
        # (ATMFONTS.QLC, ATM's font list, is written by ATM when it runs:
        # on 3.1 after the restart, not on w16, see acro)
        (cd "$c/PSFONTS" 2>/dev/null && find . -type f ! -iname ATMFONTS.QLC -printf '%s %p\n' | sort -k2) > "$out/install-$s.fonts"
        # (ATM16.NDL and ATM32.NDL become .DLL when ATM first runs: on 3.1
        # at the restart, not yet on w16)
        ls "$c/$w/SYSTEM" | tr a-z A-Z | grep '^ATM' | sed 's/\.NDL$/.DLL/' > "$out/install-$s.sys" || :
    done
    w=$out/install-w16/work/c
    if [ "$r" = ok ]; then
        if [ ! -s "$out/install-ref.fonts" ]; then r="3.1 installed no fonts ($out/install-ref)"
        elif ! diff "$out/install-ref.fonts" "$out/install-w16.fonts" > "$out/install-fonts.diff"; then
            r="the files under C:\\PSFONTS differ from 3.1's ($out/install-fonts.diff)"
        elif ! diff "$out/install-ref.sys" "$out/install-w16.sys" > "$out/install-sys.diff"; then
            r="ATM's files in the system directory differ from 3.1's ($out/install-sys.diff)"
        elif ! cmp -s "$out/install-ref/work/c/WINDOWS/ATM.INI" "$w/W16/ATM.INI"; then
            r="ATM.INI differs from 3.1's ($w/W16/ATM.INI)"
        elif grep -q '^w16: exception' "$w/OUT.TXT" 2>/dev/null; then
            r="$(grep -m1 '^w16: exception' "$w/OUT.TXT") ($w/OUT.TXT)"
        elif ! grep -q '^w16: back to DOS, code 0042' "$w/OUT.TXT" ||
             ! grep -q '^w16: back to DOS, code 0000' "$w/OUT.TXT"; then
            r="w16 did not restart Windows and end it ($w/OUT.TXT)"
        fi
    fi
    if [ "$r" = ok ]; then echo "ok   atm install: Adobe Type Manager 3.0 installs as on 3.1 (fonts, ATM.INI, SYSTEM.INI), Windows restarts with ATMSYS.DRV"
    else echo "FAIL atm install: $r"; rc=1; fi ;;
acro)
    if [ ! -f "$acro/ACROREAD.EXE" ] || [ ! -f "$acro/FONTS/ZX______.PFB" ]; then
        echo "skip atm acro: no Acrobat Reader with FONTS at $acro (W16_ACROREAD_DIR)"
        continue
    fi
    [ -d "$out/install-ref/work/c/PSFONTS" ] && [ -d "$out/install-w16/work/c/PSFONTS" ] ||
        { echo "FAIL atm acro: no installed drives (run the install part first)"; rc=1; continue; }
    for s in ref w16; do
        w=$(windir $s) cov=$out/acro-$s.cover
        rm -rf "$cov" "$out/acro-$s"
        cp -a "$out/install-$s/work/c" "$cov"
        rm -f "$cov/OUT.TXT" "$cov/W16DBG.LOG"
        # w16's tree comes from the build, not the cover (its INI and group files stay)
        [ $s = ref ] || (cd "$root/build/w16" && find . -type f ! -iname '*.ini' ! -iname '*.grp') |
            while read -r f; do rm -f "$cov/W16/$f"; done
        mkdir -p "$cov/ACROREAD" "$cov/$w/PFM"
        (cd "$acro" && find . -type f ! -path './FONTS/*') | sed 's|^\./||' | while read -r f; do
            case $(printf '%s' "$f" | tr a-z A-Z) in
            ACROREAD.INI) cp "$acro/$f" "$cov/$w/ACROREAD.INI" ;;
            *) mkdir -p "$cov/ACROREAD/$(dirname "$f")"; cp "$acro/$f" "$cov/ACROREAD/$f" ;;
            esac
        done
        cp "$acro"/FONTS/*.PFB "$cov/$w/"; cp "$acro"/FONTS/PFM/* "$cov/$w/PFM/"
        python3 -I "$root/tests/scripts/atmfonts.py" "$cov/$w/ATM.INI" "C:\\$w\\"
        # shellcheck disable=SC2046
        DROPIN_COVER=$cov timeout 1200 "$root/tests/inputrun.sh" -s -b 150 -t 600 \
            "$root/tests/scripts/input/atmacro.txt" "$out/acro-$s" -p 386 -c "WIN /3 /B" \
            $(side $s) > "$out/acro-$s.txt" 2>&1 &
    done
    wait
    a=$out/acro-ref/01-reader.png b=$out/acro-w16/01-reader.png
    if [ ! -f "$a" ] || [ ! -f "$b" ]; then echo "FAIL atm acro: no screens ($out/acro-ref, acro-w16)"; rc=1
    elif grep -q '^w16: exception' "$out/acro-w16/work/c/OUT.TXT" 2>/dev/null; then
        echo "FAIL atm acro: $(grep -m1 '^w16: exception' "$out/acro-w16/work/c/OUT.TXT")"; rc=1
    else
        # the document's links, red text in its Type 1 face drawn through
        # ATM, counted in the document pane: the page sits elsewhere on
        # w16 (ARCHITECTURE.md 11.13), so counts are compared, not pixels
        red() { convert "$1" -crop 490x380+148+70 +repage -fill white +opaque red \
                    -fill black -opaque red -negate -format '%[fx:round(w*h*mean)]' info: 2>/dev/null ||
                    echo 0; }
        convert "$a" "$b" +append "$out/acro-cmp.png" 2>/dev/null || :
        ra=$(red "$a") rb=$(red "$b")
        if [ "$ra" -lt 1000 ]; then
            echo "FAIL atm acro: 3.1 shows no document ($a)"; rc=1
        elif [ $((rb * 10)) -lt $((ra * 9)) ] || [ $((rb * 10)) -gt $((ra * 11)) ]; then
            echo "FAIL atm acro: Reader shows no document through ATM on w16 ($rb red pixels of link text, 3.1 $ra; $out/acro-cmp.png)"; rc=1
        else
            echo "ok   atm acro: Acrobat Reader 2.0 shows its help document through ATM as on 3.1 ($rb red pixels of link text, 3.1 $ra; $out/acro-cmp.png)"
        fi
    fi ;;
*) echo "atm.sh: no part $p" >&2; exit 2 ;;
esac
done
exit $rc
