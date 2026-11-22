#!/bin/sh
# Windows Help on w16's install tree alone (ARCHITECTURE.md 15.10):
# build/w16 booted by itself (tests/dropin.sh -T, at C:\WINDOWS) under its
# KRNL286 (or with 386 its KRNL386), SNAP.EXE as the shell running
# tests/scripts/winhelp/tree/run.txt, with the reference install's
# NOTEPAD.HLP, GLOSSARY.HLP and REGEDIT.HLP copied into the scratch drive
# as test input (they are not part of w16). w16's own USER, GDI and
# WINHELP: a help file opened by hand, a jump, a popup, Search, and the
# Registration Info Editor's Help > Contents through USER's WinHelp.
# Passes when each SNAP dump shows the windows it should (the help
# window's class, the popup, the Search dialog); the screens are kept in
# OUTDIR for a look.
#   usage: tests/whtree.sh OUTDIR [286|386]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1 k=${2:-286}
ref=$root/.tools/ref/win31/cdrive/WINDOWS
rm -rf "$out"; mkdir -p "$out"
sed -e 's/\r*$/\r/' "$root/tests/scripts/winhelp/tree/run.txt" > "$out/SNAP.TXT"
set -- -f "$root/build/dos/SNAP.EXE=SNAP.EXE" -f "$out/SNAP.TXT=SNAP.TXT"
for h in NOTEPAD GLOSSARY REGEDIT; do
    set -- "$@" -f "$ref/$h.HLP=WINDOWS/$h.HLP"
done
[ "$k" = 386 ] && set -- "$@" -3
DROPIN_TREEDIR=WINDOWS "$root/tests/dropin.sh" -T "$root/build/w16" -t 200 -o "$out/work" \
    -i 'boot:shell=C:\SNAP.EXE' -i 'WIN.INI:windows:CursorBlinkRate=65000' \
    -i 'w16:watchdog=0' "$@" > "$out/dropin.txt" 2>&1 || true
c=$out/work/c
log=$c/WINDOWS/TK.LOG
[ -f "$log" ] || { echo "whtree: no TK.LOG ($out/dropin.txt)"; exit 1; }
tr -d '\r' < "$log" > "$out/tk.log"
for f in "$c"/*.BMP; do [ -f "$f" ] && magick "$f" "$out/$(basename "$f" .BMP).png"; done
python3 -I - "$out/tk.log" <<'PY'
import sys
dumps, cur = [], None
for l in open(sys.argv[1], encoding='latin-1'):
    if l.startswith('SNAP dump'):
        cur = []
        dumps.append(cur)
    elif l.startswith('SNAP '):
        cur = None
    elif cur is not None and l.startswith('WIN '):
        cur.append(l)
want = [
    ('the help window opened by hand, NOTEPAD.HLP', 'class="MS_WINDOC" text="Notepad Help"'),
    ('a jump: the scroll bar', 'class="MS_WINTOPIC" text="" style=50200000'),
    ('a glossary popup', 'class="MS_WINNOTE"'),
    ('Search', 'class="#32770" text="Search"'),
    ('WinHelp from the Registration Info Editor', 'class="MS_WINHELP" text="Registration Info Editor Help"'),
]
bad = 0
for i, (what, s) in enumerate(want):
    ok = i < len(dumps) and any(s in l for l in dumps[i])
    print('%s: %s' % ('ok' if ok else 'FAIL', what))
    bad += not ok
sys.exit(1 if bad else 0)
PY
