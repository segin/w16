#!/bin/sh
# Boot Windows 3.1 with w16's USER.EXE (ARCHITECTURE.md 10) and compare the
# screen with the same boot on Microsoft's USER.
#
# Uses tests/dropin.sh (a scratch copy of the reference install; WIN /S in
# headless DOSBox): build/dos/USER.EXE replaces WINDOWS\SYSTEM\USER.EXE, on
# Microsoft's KERNEL (KRNL286; dropin.sh -r) unless -k is given. The same
# boot with Microsoft's USER is the reference. Both screens are taken after
# SECS seconds and compared pixel by pixel (ImageMagick compare -metric AE).
#
#   usage: tests/userboot.sh [options] OUTDIR
#     -k          w16's KERNEL too (build/dos/KRNL286.EXE)
#     -s PROG     PROG (a host file) is the shell instead of PROGMAN.EXE
#     -a ARGS     command tail: WIN /S /B ARGS
#     -t SECS     time before the screenshot (default 40)
#     -f SRC=DST  extra files, as for dropin.sh
#     -i S:K=V    SYSTEM.INI settings, as for dropin.sh
#     -n          no reference run (only w16's USER)
# OUTDIR (new) receives w16.png, ref.png, diff.png, boot logs (w16.txt,
# ref.txt), USER.LOG (w16's USER's own log, [w16] userlog=1) and TK.LOG of a
# probe that ran as the shell.
set -eu
unset DISPLAY WAYLAND_DISPLAY
root=$(cd "$(dirname "$0")/.." && pwd)
kflag=-r secs=40 shell= args= extra= noref=
while getopts ks:a:t:f:i:n opt; do
    case $opt in
    k) kflag= ;;
    s) shell=$OPTARG ;;
    a) args=$OPTARG ;;
    t) secs=$OPTARG ;;
    f) extra="$extra -f '$OPTARG'" ;;
    i) extra="$extra -i '$OPTARG'" ;;
    n) noref=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
out=${1:?usage: tests/userboot.sh [options] OUTDIR}
[ -e "$out" ] && { echo "$out exists" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
make -C "$root" -s build/dos/USER.EXE >/dev/null
if [ -n "$shell" ]; then
    name=$(basename "$shell" | tr a-z A-Z)
    extra="$extra -f '$shell=$name' -i 'boot:shell=C:\\$name'"
fi
cmd="WIN /S /B${args:+ $args}"
run() {   # LABEL DROPIN-OPTIONS...
    label=$1; shift
    eval "\"$root/tests/dropin.sh\" -t $secs -s \"$out/$label.png\" -o \"$out/$label-c\" -c '$cmd' \
        -i w16:watchdog=0 $extra $*" > "$out/$label.txt" 2>&1 || true
    for f in TK.LOG USER.LOG; do
        for d in "$out/$label-c/c" "$out/$label-c/c/WINDOWS"; do
            [ -f "$d/$f" ] && cp "$d/$f" "$out/$label-$f"
        done
    done
    rm -rf "$out/$label-c"
}
run w16 $kflag -f "$root/build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE" -i w16:userlog=1
if [ -z "$noref" ]; then
    run ref $kflag
    d=$(magick compare -metric AE "$out/ref.png" "$out/w16.png" "$out/diff.png" 2>&1 || true)
    echo "pixels differing from the reference: $d"
fi
sed -n '/== summary/,$p' "$out/w16.txt"
