#!/bin/sh
# remote-run.sh - run one test script of this checkout on the remote host
# (tools/remote-test.sh's host and run directories), for scenario suites
# that are not part of make test or the boot test (e.g.
# tests/writescenarios.sh): sync, build, run the command there, copy back
# the directory it names.
#   usage: tools/remote-run.sh [-n NAME] [-g REMOTE_DIR] COMMAND...
#     -n NAME   the run directory's name (default: this checkout's name)
#     -g DIR    a directory (relative to the checkout) to copy back to the
#               same place here afterwards, e.g. build/ref/wrscen
# The command runs in the run directory with the build done; its output
# comes back on stdout. Runs one command at a time; it takes no slot of
# remote-test.sh's, so keep it to small suites (one DOSBox at a time).
set -eu
host=${W16_REMOTE_HOST:-installation-05}
base=${W16_REMOTE_DIR:-/mnt/build/w16-runs}
tools=${W16_REMOTE_TOOLS:-/mnt/build/w16/.tools}
root=$(git rev-parse --show-toplevel)
name=$(basename "$root") get=
while getopts n:g: opt; do
    case $opt in
    n) name=$OPTARG ;; g) get=$OPTARG ;;
    *) sed -n '2,13p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
dir=$base/$name
ssh -o BatchMode=yes "$host" "mkdir -p '$dir'"
rsync -a --delete --exclude=/build/ --exclude=/.claude/ --exclude=/.tools \
    "$root/" "$host:$dir/"
cmd=$(printf '%s ' "$@" | base64 -w0)
rc=0
ssh -o BatchMode=yes "$host" sh -s -- "$dir" "$tools" "$cmd" <<'EOF' || rc=$?
set -u
dir=$1 tools=$2 cmd=$(printf '%s' "$3" | base64 -d)
cd "$dir" || exit 2
[ -e .tools ] || ln -s "$tools" .tools
mkdir -p build
make -j8 all > build/remote-make.log 2>&1 || { tail -n 20 build/remote-make.log; exit 3; }
sh -c "$cmd"
EOF
if [ -n "$get" ]; then
    mkdir -p "$root/$get"
    rsync -a "$host:$dir/$get/" "$root/$get/" || true
fi
exit $rc
