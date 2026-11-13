#!/bin/sh
# remote-test.sh - run w16's tests on a remote build host (ARCHITECTURE.md,
# testing): sync this checkout (a worktree works too) to its own directory
# there, build, run make test and/or the boot test, and copy back the
# summaries and the failing diffs.
#
#   usage: tools/remote-test.sh [options] [CASE ...]
#     -H HOST   the host (default $W16_REMOTE_HOST, else installation-05)
#     -d DIR    base directory there (default $W16_REMOTE_DIR, else
#               /mnt/build/w16-runs); each checkout gets DIR/NAME
#     -n NAME   the run directory's name (default: this checkout's directory
#               name, e.g. agent-a1b2c3 for a worktree, w16 for the main one)
#     -j JOBS   BOOTTEST_JOBS there (default 12)
#     -t        make test only          -b   boot test only
#     -k N      keep the last N boot-test result directories there (default 3)
#     -e VAR=VALUE  set an environment variable for the tests there
#               (repeatable), e.g. -e DROPIN_INI=w16:ExtendedUserHeap=1
#   CASE ...    boot-test cases (default: all)
#
# The reference installs (.tools) are not copied per run: each run directory
# links .tools to the host's shared copy ($W16_REMOTE_TOOLS, default
# /mnt/build/w16/.tools). They are Microsoft's files: never point this at a
# shared or public machine. The boot test's reference cache (build/refcache)
# is shared between runs on the host (/mnt/build/w16-refcache): its keys
# cover everything a reference depends on. At most $W16_REMOTE_SLOTS runs
# (default 3) use the host at once; others wait for a slot.
# Results come back to build/remote-NAME/ here: make-test.log, boot.log and
# the boot test's *-diff.png files of failing cases.
set -eu
host=${W16_REMOTE_HOST:-installation-05}
base=${W16_REMOTE_DIR:-/mnt/build/w16-runs}
tools=${W16_REMOTE_TOOLS:-/mnt/build/w16/.tools}
slots=${W16_REMOTE_SLOTS:-3}
root=$(git rev-parse --show-toplevel)
name=$(basename "$root") jobs=12 keep=3 dotest=1 doboot=1 envs=
while getopts H:d:n:j:tbk:e: opt; do
    case $opt in
    H) host=$OPTARG ;; d) base=$OPTARG ;; n) name=$OPTARG ;;
    j) jobs=$OPTARG ;; t) doboot=0 ;; b) dotest=0 ;; k) keep=$OPTARG ;;
    e) case $OPTARG in *=*) ;; *) echo "-e needs VAR=VALUE" >&2; exit 2 ;; esac
       envs="$envs,$(printf '%s' "$OPTARG" | base64 -w0)" ;;
    *) sed -n '2,28p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
cases="$*"
dir=$base/$name
out=$root/build/remote-$name
mkdir -p "$out"

echo "remote-test: $(git -C "$root" log --oneline -1) -> $host:$dir"
# the build's commit for build/w16ver.h (the Makefile): a worktree's copy
# there is no git checkout (first: an -e W16_BUILD=... still wins)
envs="$(printf 'W16_BUILD=%s' "$(git -C "$root" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)" | base64 -w0)$envs"
ssh -o BatchMode=yes "$host" "mkdir -p '$dir' '$base/../w16-refcache'"
rsync -a --delete --exclude=/build/ --exclude=/.claude/ --exclude=/.tools \
    "$root/" "$host:$dir/"

rc=0
# shellcheck disable=SC2087
if ssh -o BatchMode=yes "$host" sh -s -- "$dir" "$tools" "$slots" "$jobs" \
    "$keep" "$dotest" "$doboot" "x$envs" $cases <<'EOF'
set -u
dir=$1 tools=$2 slots=$3 jobs=$4 keep=$5 dotest=$6 doboot=$7 envs=${8#x}
shift 8
# -e VAR=VALUE settings, base64-encoded so values survive ssh's quoting
IFS=,; for e in $envs; do [ -n "$e" ] && export "$(printf '%s' "$e" | base64 -d)"; done; unset IFS
cases="$*"
cd "$dir" || exit 2
[ -e .tools ] || ln -s "$tools" .tools
mkdir -p build
cache=$(cd .. && pwd)/../w16-refcache
[ -e build/refcache ] || ln -s "$cache" build/refcache
# a host-wide slot (flock on one of $slots lock files)
i=0
while :; do
    exec 9> "$(dirname "$dir")/.slot$i"
    flock -n 9 && break
    i=$(( (i + 1) % slots ))
    [ $i -eq 0 ] && sleep 5
done
rc=0
make -j"$(nproc)" all win386 > build/remote-make.log 2>&1 || { tail -n 20 build/remote-make.log; exit 3; }
if [ "$dotest" = 1 ]; then
    make test > build/make-test.log 2>&1 || rc=1
    echo "make test: $( [ $rc = 0 ] && echo pass || echo FAIL)"
    grep -a '^FAIL' build/make-test.log | tr -d '\r' | head -20
fi
if [ "$doboot" = 1 ]; then
    cp tests/boottest.sh build/boottest-snapshot.sh
    # shellcheck disable=SC2086
    BOOTTEST_JOBS=$jobs sh build/boottest-snapshot.sh $cases > build/boot.log 2>&1 || rc=1
    grep -E '^(ok|FAIL|xfail)' build/boot.log | grep -v '^ok' || true
    echo "boot test: $(grep -c '^ok' build/boot.log) ok; $(tail -n 1 build/boot.log)"
    # keep only the last $keep result directories
    ls -1dt build/boottest-[0-9]* 2>/dev/null | tail -n +$((keep + 1)) | while read -r d; do rm -rf -- "$d"; done
fi
exit $rc
EOF
then :; else rc=$?; fi
last=$(ssh -o BatchMode=yes "$host" "ls -1dt '$dir'/build/boottest-[0-9]* 2>/dev/null | head -1" || true)
rsync -a "$host:$dir/build/make-test.log" "$host:$dir/build/boot.log" "$out/" 2>/dev/null || true
if [ -n "$last" ]; then
    rsync -a --include='*/' --include='*-diff.png' --exclude='*' \
        "$host:$last/" "$out/$(basename "$last")/" 2>/dev/null || true
fi
echo "remote-test: results in $out (exit $rc)"
exit $rc
