#!/bin/sh
# Run a command on a virtual X display of its own (Xvfb, 1024x768x24),
# never the user's desktop: tests/dropin.sh and tests/inputrun.sh start
# DOSBox through this.
#
#   usage: tests/xvfb.sh COMMAND [ARGS...]
#
# The display number comes from Xvfb itself (-displayfd: the first free
# one, chosen and taken by the server), so runs started at the same time
# never share a display. xvfb-run -a picked a number by looking for lock
# files in /tmp and then started Xvfb: two runs starting together could
# pick the same number, one Xvfb failed and its run's DOSBox opened on the
# other run's display, where "xdotool search --name DOSBox" could find
# either window. xvfb-run also slept 3 seconds before the command; here
# the command starts as soon as the server is ready. Xvfb is ended by its
# PID afterwards. Exit status: COMMAND's.
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
[ $# -ge 1 ] || { sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2; }
d=$(mktemp -d "${TMPDIR:-/tmp}/w16xvfb.XXXXXX")
Xvfb -displayfd 3 -screen 0 1024x768x24 -nolisten tcp 3> "$d/display" 2> "$d/err" &
xpid=$!
# killed itself (a boot test stopped by hand), it ends its server too
cpid=
trap 'kill $cpid "$xpid" 2>/dev/null; rm -rf "$d"; exit 143' INT TERM HUP
# the display number is written when the server accepts connections
i=0
while :; do
    n=$(cat "$d/display" 2>/dev/null || true)
    case $n in *[0-9]) break ;; esac
    if ! kill -0 "$xpid" 2>/dev/null || [ $i -ge 600 ]; then
        echo "xvfb.sh: Xvfb did not start: $(cat "$d/err")" >&2
        kill "$xpid" 2>/dev/null || true
        rm -rf "$d"
        exit 2
    fi
    sleep 0.05; i=$((i + 1))
done
rc=0
# (in the background: a trap then runs at once, not after the command)
DISPLAY=:$n "$@" &
cpid=$!
wait "$cpid" || rc=$?
# the server's own messages, if it had any (seen once: DOSBox found "no
# available video device" on a display this script had started)
if ! kill -0 "$xpid" 2>/dev/null; then
    echo "xvfb.sh: Xvfb :$n ended before the command did" >&2
    sed 's/^/xvfb.sh: Xvfb: /' "$d/err" >&2
fi
kill "$xpid" 2>/dev/null || true
wait "$xpid" 2>/dev/null || true
rm -rf "$d"
exit "$rc"
