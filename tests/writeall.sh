#!/bin/sh
# Write's whole black-box suite: every scenario (tests/writescenarios.sh,
# WR_BASELINE passed through) and the WinHelp requests (tests/writehelp.sh).
# Exit status: nonzero if either fails.
#   usage: tests/writeall.sh [-u]       (-u: WR_BASELINE=update)
root=$(cd "$(dirname "$0")/.." && pwd)
[ "${1:-}" = -u ] && export WR_BASELINE=update
rc=0
"$root/tests/writescenarios.sh" || rc=1
"$root/tests/writehelp.sh" || rc=1
exit $rc
