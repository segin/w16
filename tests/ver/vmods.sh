#!/bin/sh
# The version resources of w16's own modules (ARCHITECTURE.md 20.5): every
# file of an install tree (default build/w16) is copied to C:\VT\MOD of the
# reference install and the probe VRP.EXE (tests/ver/vrp.c, section
# "modules") reads each one's version through Microsoft's VER.DLL and
# through w16's (tests/ver/vrun.sh). Passes when the two logs are equal and
# every file that has a version resource (tools/neres.py) has a version
# that 3.1's VER.DLL reads (a size, the fixed part, a translation and
# every string), and no other file has one.
#   usage: tests/ver/vmods.sh OUTDIR [TREE]
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 tree=${2:-$root/build/w16}
mkdir -p "$out"
rm -f "$out/MODS.TXT" "$out/expect.txt"
extra=
for f in $(find "$tree" -type f | sort); do
    b=$(basename "$f")
    extra="$extra $f=VT/MOD/$b"
    printf 'C:\\VT\\MOD\\%s\r\n' "$b" >> "$out/MODS.TXT"
    if python3 "$root/tools/neres.py" "$f" 2>/dev/null | grep -q '^VERSION'; then
        echo "$b" >> "$out/expect.txt"
    fi
done
extra="$extra $out/MODS.TXT=VT/MODS.TXT"
for l in ms w16; do
    VRUN_ARG=m VRUN_EXTRA="$extra" "$root/tests/ver/vrun.sh" "$out/$l" "$l" > /dev/null 2>&1 &
done
wait
status=0
for l in ms w16; do
    [ -s "$out/$l/vrp.log" ] && grep -q '^VRP end' "$out/$l/vrp.log" ||
        { echo "FAIL: no complete log from $l's VER.DLL ($out/$l)"; exit 1; }
done
if ! cmp -s "$out/ms/vrp.log" "$out/w16/vrp.log"; then
    diff "$out/ms/vrp.log" "$out/w16/vrp.log" > "$out/modules.diff" || true
    echo "FAIL: the logs differ ($out/modules.diff)"
    status=1
fi
python3 - "$out/ms/vrp.log" "$out/expect.txt" <<'EOF' || status=1
import sys
log = open(sys.argv[1], encoding='latin-1').read().split('\n')
expect = set(open(sys.argv[2]).read().split())
got, cur, bad = {}, None, []
for line in log:
    if line.startswith(' C:\\VT\\MOD\\'):
        cur = line.split('\\')[-1].split(':')[0]
        got[cur] = [int(line.split('size ')[1])]
    elif cur and line.startswith('  '):
        got[cur].append(line.strip())
for name, lines in sorted(got.items()):
    has = lines[0] > 0
    if has != (name in expect):
        bad.append('%s: %s' % (name, 'a version 3.1 reads, but no resource?' if has
                               else 'a version resource 3.1 cannot read'))
    elif has and ('no fixed part' in lines or 'no translation' in lines or
                  any(l.endswith(' missing') for l in lines[1:])):
        bad.append('%s: incomplete: %s' % (name, '; '.join(lines[1:])))
for b in bad:
    print('FAIL: ' + b)
print('%d files, %d with a version 3.1 reads' % (len(got), sum(1 for l in got.values() if l[0])))
sys.exit(1 if bad or not got else 0)
EOF
exit $status
