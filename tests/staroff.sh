#!/bin/sh
# StarOffice 3.1's CD (the OEM English edition, an ISO 9660 image) on
# Windows 3.1 and on w16's install tree, 386 Enhanced mode, the CD mounted
# as D: (DOSBox's imgmount -t iso, with its MSCDEX; tests/dropin.sh
# DROPIN_CDROM), as a user reported problems with it (2026-11-25):
#   ole    TK70 (tests/apps/tk70.c) from WIN.INI run= with Microsoft's OLE 2
#          libraries from the CD (ENGLISH\PROD_W31\*.0: ZIP archives of
#          COMPOBJ.DLL, OLE2.DLL, STORAGE.DLL, ...) in the system
#          directory: GetDriveType of A:-E: (D: the CD: DRIVE_REMOTE, as
#          3.1), CoInitialize (w16's LocalInit refused the task
#          allocator's heap: StarOffice's "Error initializing
#          object-system."), SHARE's INT 2Fh 1000h, the compound files,
#          OleInitialize; Windows 3.1 (Microsoft's KRNL386 and WIN386)
#          and w16's tree (C:\W16, KRNL386, its WIN386) must log the same.
# StarOffice and Microsoft's OLE 2 are not w16's: nothing of them is
# committed. The image comes from $W16_STAROFF_ISO (default
# ~/Downloads/staroffice/STAROFF.ISO); without it the test says so and
# passes ("skip"). 7z (p7zip) reads the image, python3 the ZIPs.
#
#   usage: tests/staroff.sh [-o OUTDIR] [PART ...]   (default: ole)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
iso=${W16_STAROFF_ISO:-$HOME/Downloads/staroffice/STAROFF.ISO}
out=
while getopts o: opt; do
    case $opt in
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
parts=${*:-ole}
if [ ! -f "$iso" ]; then
    echo "skip: no StarOffice CD image at $iso (W16_STAROFF_ISO)"
    exit 0
fi
[ -n "$out" ] || out=$(mktemp -d)
mkdir -p "$out"
out=$(cd "$out" && pwd)
iso=$(cd "$(dirname "$iso")" && pwd)/$(basename "$iso")
dos=$root/build/dos
fails=0

# Microsoft's OLE 2 DLLs from the CD
ole=$out/ole2
mkdir -p "$ole/zip"
set --
for z in COMPOBJD OLE2DLL STORAGED OLE2NLSD OLE2DISP TYPELIBD OLE2PROX OLE2CONV; do
    set -- "$@" "ENGLISH/PROD_W31/$z.0"
done
7z x -y -o"$ole/zip" "$iso" "$@" > "$out/7z.log" 2>&1 || { echo "FAIL staroff: 7z could not read $iso ($out/7z.log)"; exit 1; }
python3 -I - "$ole/zip/ENGLISH/PROD_W31" "$ole" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
for f in sorted(os.listdir(src)):
    with zipfile.ZipFile(os.path.join(src, f)) as z:
        for n in z.namelist():
            open(os.path.join(dst, n.upper()), 'wb').write(z.read(n))
PY

for p in $parts; do
    case $p in
    ole)
        for k in ref tree; do
            set -- -f "$dos/TK70.EXE=TK70.EXE" -i 'WIN.INI:windows:run=C:\TK70.EXE'
            if [ $k = ref ]; then o="-r -3" w=WINDOWS; else o="-T $root/build/w16 -3" w=W16; fi
            for d in "$ole"/*.DLL; do set -- "$@" -f "$d=$w/SYSTEM/$(basename "$d")"; done
            # shellcheck disable=SC2086
            DROPIN_CDROM=$iso DROPIN_MEMSIZE=32 "$root/tests/dropin.sh" $o -c 'WIN /3' -t 180 \
                -W TK70.LOG -o "$out/ole-$k" "$@" > "$out/ole-$k.txt" 2>&1 || true
            tr -d '\r' < "$out/ole-$k/c/TK70.LOG" > "$out/ole-$k.log" 2>/dev/null || : > "$out/ole-$k.log"
        done
        r=ok
        grep -q '^T done$' "$out/ole-ref.log" || r="ref did not finish ($out/ole-ref.log)"
        [ "$r" != ok ] || diff "$out/ole-ref.log" "$out/ole-tree.log" > "$out/ole.diff" ||
            r="the tree differs from 3.1 ($out/ole.diff)"
        if [ "$r" = ok ]; then echo "ok   staroff ole: OLE 2 starts up as on 3.1, the CD is DRIVE_REMOTE"
        else echo "FAIL staroff ole: $r"; fails=$((fails + 1)); fi ;;
    *) echo "unknown part $p" >&2; exit 2 ;;
    esac
done
exit $fails
