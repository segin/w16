#!/bin/sh
# One run of the MCI probe (layer 4; MMP.EXE's section Q,
# tests/mmsystem/mmci.c) on the reference Windows 3.1 with Microsoft's
# MMSYSTEM.DLL or w16's: the script SCRIPT (tests/mmsystem/mci/*.txt),
# the test MCI driver MCT.DRV under the [mci] names of
# tests/mmsystem/mci/mct.ini, and with "shim" Windows 3.1's own Sound
# Blaster driver (tests/mmsystem/mrun.sh's MRUN_SND=sb2) and its
# MCIWAVE.DRV and MCISEQ.DRV behind the logging drivers MDSC1 and MDSC2
# (MRUN_MCISHIM); "qt" as shim on the QuickTime-installed drive of
# $W16_QTDIR/ref-installed/c, with MCIQTW.DRV behind MDSC3 too (MRUN_QT).
# OUTDIR/mmp.log is the log.
#   usage: tests/mmsystem/mcirun.sh OUTDIR ms|w16 SCRIPT [shim|plain|qt] [/S|/3]
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1 mm=$2 script=$3 kind=${4:-plain} mode=${5:-/S}
MRUN_ARG=Q MRUN_MCI=$script MRUN_INI="$(cat "$root/tests/mmsystem/mci/mct.ini")${MRUN_INI:+
$MRUN_INI}"
MRUN_TIMEOUT=${MRUN_TIMEOUT:-900}
export MRUN_ARG MRUN_MCI MRUN_INI MRUN_TIMEOUT
if [ "$kind" = shim ] || [ "$kind" = qt ]; then
    MRUN_SND=sb2 MRUN_SHIM=1 MRUN_MCISHIM=1
    export MRUN_SND MRUN_SHIM MRUN_MCISHIM
fi
if [ "$kind" = qt ]; then
    MRUN_QT=1
    export MRUN_QT
fi
exec "$root/tests/mmsystem/mrun.sh" "$out" "$mm" "$mode"
