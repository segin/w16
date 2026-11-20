#!/bin/sh
# Black-box observation of File Manager (probes WF*): run a SNAP.EXE script
# on the reference Windows 3.1 (Standard mode) and keep what it produced.
#   usage: tests/wfobs.sh <script.txt> <outdir> [host files for C:\ ...]
# The script is a SNAP.EXE script (tests/apps/snap.c); "run WINFILE.EXE"
# starts the original from C:\WINDOWS. WFOBS_EXE=path runs that file
# instead, copied over C:\WINDOWS\WINFILE.EXE (so both runs see the same
# directories). The test tree tests/scripts/winfile/mktree.py (C:\WFTEST,
# fixed dates) is laid over C:\ in every run.
# Script directives:
#   ; ini: section:key=value       a WIN.INI key
#   ; wfini: section:key=value     a WINFILE.INI key (the file is made)
#   ; get: PATH                    a file under C:\ to bring back
#   ; fmx: NAME...                 copies of the extension probe FMX.DLL
#                                  (tests/apps/fmx.c) as C:\NAME.DLL, each
#                                  with its own module name NAME (4 letters)
#   ; fmxini: section:key=value    a key of C:\FMX.INI (how FMX answers);
#                                  FMX logs to D:\FMX.LOG (an empty D:,
#                                  brought back with the rest)
#   ; put: SRC=DEST                a file of the checkout as C:\DEST
# Afterwards <outdir> holds the screenshots (*.BMP), SNAP's log (log.txt),
# WIN.INI and WINFILE.INI as the session left them, and the files asked for.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
script=$1 out=$2
shift 2
mkdir -p "$out"
sed 's/$/\r/' "$script" > "$out/SNAP.TXT"
files="$out/SNAP.TXT"
if [ -n "${WFOBS_EXE:-}" ]; then
    # padded to the original's size, so that both runs list the same
    # bytes in C:\WINDOWS (NE loaders read no further than the header
    # and segment table say)
    cp "$WFOBS_EXE" "$out/WINFILE.EXE"
    ms=$(stat -c %s "$root/.tools/ref/win31/cdrive/WINDOWS/WINFILE.EXE")
    [ "$(stat -c %s "$out/WINFILE.EXE")" -le "$ms" ] && truncate -s "$ms" "$out/WINFILE.EXE"
    files="$files $out/WINFILE.EXE=WINDOWS/WINFILE.EXE"
fi
# carets must not blink while screenshots are taken
tr -d '\r' < "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" | sed 's/$/\r/' > "$out/WIN.INI.in"
python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "windows:CursorBlinkRate=${WFOBS_BLINK:-65000}"
for s in ${WFOBS_INI:-}; do
    python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "$s"
done
sed -n 's/^; ini: *//p' "$script" | while IFS= read -r s; do
    python3 -I "$root/tests/scripts/iniset.py" "$out/WIN.INI.in" "$s"
done
files="$files $out/WIN.INI.in=WINDOWS/WIN.INI"
if grep -q '^; wfini:' "$script"; then
    printf '\r\n' > "$out/WINFILE.INI.in"
    sed -n 's/^; wfini: *//p' "$script" | while IFS= read -r s; do
        python3 -I "$root/tests/scripts/iniset.py" "$out/WINFILE.INI.in" "$s"
    done
    files="$files $out/WINFILE.INI.in=WINDOWS/WINFILE.INI"
fi
for x in $(sed -n 's/^; fmx: *//p' "$script"); do
    cp "$root/build/dos/FMX.DLL" "$out/$x.DLL"
    python3 -I "$root/tools/nerename.py" "$out/$x.DLL" --module "$x" > /dev/null
    files="$files $out/$x.DLL=$x.DLL"
done
ddrive=
if grep -q '^; fmx:' "$script"; then
    # FMX logs to D:\FMX.LOG (an empty D: drive): writing on C: would make
    # File Manager's Search Results window ask to be updated (WF17)
    ddrive=$(mktemp -d)
    printf '\r\n' > "$out/FMX.INI.in"
    python3 -I "$root/tests/scripts/iniset.py" "$out/FMX.INI.in" 'FMX:Log=D:\FMX.LOG'
    sed -n 's/^; fmxini: *//p' "$script" | while IFS= read -r s; do
        python3 -I "$root/tests/scripts/iniset.py" "$out/FMX.INI.in" "$s"
    done
    files="$files $out/FMX.INI.in=FMX.INI"
fi
# "; put: SRC=DEST": a file of the build (SRC relative to the checkout) as C:\DEST
for p in $(sed -n 's/^; put: *//p' "$script"); do
    files="$files $root/${p%%=*}=${p#*=}"
done
tree=$(mktemp -d); trap 'rm -rf "$tree" ${ddrive:+"$ddrive"}' EXIT
python3 -I "$root/tests/scripts/winfile/mktree.py" "$tree"
get="WINDOWS/WIN.INI WINDOWS/WINFILE.INI ${ddrive:+D:/FMX.LOG} ${WFOBS_GET:-} $(sed -n 's/^; get: *//p' "$script" | tr '\n' ' ')"
REFRUN_FILES="$files $*" REFRUN_TREE=$tree REFRUN_OUT=$out REFRUN_GET="$get" \
    REFRUN_DDRIVE=$ddrive REFRUN_TIMEOUT=${WFOBS_TIMEOUT:-180} \
    "$root/tests/refrun.sh" "$root/build/dos/SNAP.EXE" "${WFOBS_MODE:-/S}" > "$out/log.txt" || true
grep -v '^WIN ' "$out/log.txt" | head -${WFOBS_LINES:-40}
