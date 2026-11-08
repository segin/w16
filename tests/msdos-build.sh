#!/bin/sh
# Build MS-DOS 4.00 from Microsoft's MIT-licensed source (microsoft/MS-DOS,
# v4.0) for the test harness: Windows/386 2.11 needs a real DOS
# (tests/w386run.sh). Uses the build tools shipped in that release, run in
# DOSBox. Output: .tools/msdos/build/src/{BIOS/IO.SYS,DOS/MSDOS.SYS,
# CMD/COMMAND/COMMAND.COM,BOOT/MSBOOT.BIN}. Test tooling only; nothing
# from it is shipped.
#
# The GitHub copy was converted to UTF-8 with LF line endings, which the
# 1988 tools cannot read; the working copy is converted back to CP437 and
# CRLF. The build stops at the first C utility (its C library headers are
# not in the release), after the files above are made.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
d=$root/.tools/msdos
mkdir -p "$d"
[ -d "$d/repo/v4.0" ] || {
    git clone -q --depth 1 --filter=blob:none --sparse https://github.com/microsoft/MS-DOS.git "$d/repo"
    (cd "$d/repo" && git sparse-checkout set v4.0)
}
rm -rf "$d/build"
cp -r "$d/repo/v4.0" "$d/build"
python3 -I - "$d/build/src" <<'PY'
import os, sys
skip = ('.exe', '.com', '.obj', '.lib', '.bin', '.sys', '.cpi', '.hlp', '.dll', '.err')
for top, _, files in os.walk(sys.argv[1]):
    for f in files:
        if f.lower().endswith(skip):
            continue
        p = os.path.join(top, f)
        raw = open(p, 'rb').read()
        try:
            data = raw.decode('utf-8').encode('cp437', 'replace')
        except UnicodeDecodeError:
            data = raw
        data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
        if data != raw:
            open(p, 'wb').write(data)
PY
cat > "$d/build.conf" <<CONF
[dosbox]
memsize=16
[cpu]
core=dynamic
cputype=386
cycles=max
[mixer]
nosound=true
[autoexec]
mount d "$d/build"
d:
cd \\src
call setenv.bat
nmake > D:\\BUILD.LOG
exit
CONF
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 900 dosbox -conf "$d/build.conf" -noconsole >/dev/null 2>&1 || true
for f in BIOS/IO.SYS DOS/MSDOS.SYS CMD/COMMAND/COMMAND.COM BOOT/MSBOOT.BIN; do
    [ -f "$d/build/src/$f" ] || { echo "missing $f (see $d/build/BUILD.LOG)" >&2; exit 1; }
done
echo "MS-DOS 4.00 built in $d/build/src"
