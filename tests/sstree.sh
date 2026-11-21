#!/bin/sh
# The screen saver on w16's install tree alone (ARCHITECTURE.md 15.8):
# build/w16 booted by itself (tests/dropin.sh -T, at C:\WINDOWS so paths
# are 3.1's) under its KRNL286, or with 386 its KRNL386, SNAP.EXE as the
# shell running tests/scripts/ss/tree/run.txt, WIN.INI ScreenSaveActive=1,
# ScreenSaveTimeOut=15 and SYSTEM.INI SCRNSAVE.EXE=C:\WINDOWS\SSSTARS.SCR.
# Passes when, dump by dump: the saver is not up 12 s after the last key
# and is up 15 s later (USER's 10-second idle check); a key ends it;
# SC_SCREENSAVE to Notepad starts it and a key ends it; Control Panel's
# Desktop lists "Starfield Simulation", chosen; Test starts it and a key
# ends it; and the two screens taken while it runs pass
# tests/scripts/ss/stars.py.
#   usage: tests/sstree.sh OUTDIR [286|386]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1 k=${2:-286}
rm -rf "$out"; mkdir -p "$out"
sed -e 's/^snap \([^:]*\)\r*$/snap C:\\\1/' -e 's/\r*$/\r/' "$root/tests/scripts/ss/tree/run.txt" > "$out/SNAP.TXT"
set -- -f "$root/build/dos/SNAP.EXE=SNAP.EXE" -f "$out/SNAP.TXT=SNAP.TXT"
[ "$k" = 386 ] && set -- "$@" -3
DROPIN_TREEDIR=WINDOWS "$root/tests/dropin.sh" -T "$root/build/w16" -t 200 -o "$out/work" \
    -i 'boot:shell=C:\SNAP.EXE' -i 'boot:SCRNSAVE.EXE=C:\WINDOWS\SSSTARS.SCR' \
    -i 'WIN.INI:windows:ScreenSaveActive=1' -i 'WIN.INI:windows:ScreenSaveTimeOut=15' \
    -i 'w16:watchdog=0' "$@" > "$out/dropin.txt" 2>&1 || true
c=$out/work/c
# SNAP writes its log in its current directory, the Windows directory
log=$c/WINDOWS/TK.LOG
[ -f "$log" ] || { echo "sstree: no TK.LOG ($out/dropin.txt)"; exit 1; }
tr -d '\r' < "$log" > "$out/tk.log"
for f in "$c"/R*.BMP; do [ -f "$f" ] && cp "$f" "$out/"; done
status=0
python3 -I - "$out/tk.log" <<'PY' || status=1
import sys
dumps, cur = [], None
for l in open(sys.argv[1], encoding='latin-1'):
    if l.startswith('SNAP dump'):
        cur = []
        dumps.append(cur)
    elif l.startswith('SNAP '):
        cur = None
    elif cur is not None and l.startswith('WIN'):
        cur.append(l)
saver = lambda d: any('class="WindowsScreenSaverClass"' in l for l in d)
want = [(False, 'before the time-out'), (True, 'after the time-out'), (False, 'after a key'),
        (True, 'after SC_SCREENSAVE'), (False, 'after a key'), (False, 'in Desktop'),
        (True, 'after Test'), (False, 'after a key'), (False, 'after OK')]
bad = 0
if len(dumps) != len(want):
    print('sstree: %d dumps, %d expected' % (len(dumps), len(want)))
    bad = 1
for i, (d, (up, what)) in enumerate(zip(dumps, want)):
    if saver(d) != up:
        print('sstree: the saver %s %s (dump %d)' % ('not up' if up else 'up', what, i + 1))
        bad = 1
if len(dumps) > 5 and not any('entry "Starfield Simulation"' in l and '*' in l for l in dumps[5]):
    print('sstree: Desktop does not show Starfield Simulation chosen')
    bad = 1
sys.exit(bad)
PY
for s in R1 R2; do
    if [ -f "$out/$s.BMP" ]; then
        r=$(python3 -I "$root/tests/scripts/ss/stars.py" "$out/$s.BMP" 25) || { echo "sstree: $s: $r"; status=1; }
    else
        echo "sstree: no $s.BMP"; status=1
    fi
done
grep -q 'SCRNSAVE.EXE=C:\\WINDOWS\\SSSTARS.SCR' "$c/WINDOWS/SYSTEM.INI" || { echo "sstree: SYSTEM.INI lost SCRNSAVE.EXE"; status=1; }
[ $status = 0 ] && echo "sstree: ok ($k)"
exit $status
