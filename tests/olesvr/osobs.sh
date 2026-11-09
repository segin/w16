#!/bin/sh
# Run several OLESVR probe scripts (tests/olesvr/osrun.sh), OSOBS_JOBS
# (default 6) at a time, each into OUTDIR/NAME-SVR-CLI, with the probe
# files every script may use (C:\LNK.OS1, EMB.OS1, TPL.OS1, T.OS1,
# RAWO.OS1).
#   usage: tests/olesvr/osobs.sh OUTDIR SVR CLI NAME...
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 svr=$2 cli=$3
shift 3
mkdir -p "$out/files"
for f in LNK EMB TPL T RAWO; do printf 'os1 file %s\r\n' $f > "$out/files/$f.OS1"; done
files=
for f in LNK EMB TPL T RAWO; do files="$files $out/files/$f.OS1"; done
export OSRUN_FILES="$files"
printf '%s\n' "$@" | xargs -P "${OSOBS_JOBS:-6}" -I{} sh -c \
    '"$1/tests/olesvr/osrun.sh" "$2" "$3/$2-$4-$5" "$4" "$5" > /dev/null 2>&1 || true' \
    sh "$root" {} "$out" "$svr" "$cli"
for n in "$@"; do
    if [ -s "$out/$n-$svr-$cli/os.log" ]; then
        echo "$n: $(wc -l < "$out/$n-$svr-$cli/os.log") lines ($out/$n-$svr-$cli/os.log)"
    else
        echo "$n: no log"
    fi
done
