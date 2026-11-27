#!/bin/sh
# dosstrun.sh OUTDIR MODE [EMU]: DOSST.EXE (tests/dos/dosst.c) before WIN
# and after Windows has ended through Program Manager's File > Exit
# Windows, then a DOS command at the prompt: what Windows leaves DOS with
# (interrupt vectors, memory arena and UMBs, PIC masks, BIOS data, the
# INT 33h user handler, XMS), and whether DOS still runs.
#   MODE ms     Windows 3.1 with Microsoft's WIN386.EXE and KRNL386, WIN /3
#        w16    the same with w16's WIN386.EXE
#        tree   w16's install tree alone (build/w16), WIN /3
#   EMU x (default) DOSBox-X, dosbox plain DOSBox.
#   (Arguments after EMU are ignored: the boot test names files there for
#   its reference cache's key.)
# The mouse is moved while Windows ends and after (a PS/2 mouse's BIOS
# handler, INT 15h C207h, left pointing into Windows' memory would run
# then: under w16's WIN386, an invalid opcode that DOSBox-X loops on, INT
# 6 for ever). OUTDIR gets DSB.LOG (before), DSA.LOG (after), DONE.TXT
# (the command after DOSST A: "after ok" when the prompt still runs), the
# emulator's log (emu.log) and inputrun's files (run/).
# Environment: DOSSTRUN_TIME (default 240 s), DROPIN_CPUTYPE (default 386);
# DOSSTRUN_SHELL=FILE: that Windows program as the shell (C:\ its name),
# which ends Windows itself (tests/apps/tk55x.c: ExitKernel without
# USER's ExitWindows); no Exit Windows then;
# DOSSTRUN_PRE=FILE: inputrun.sh script lines run before Exit Windows;
# DOSSTRUN_POST=CMD: a DOS command run right after WIN (harness checks);
# DOSSTRUN_ARGS: more dropin.sh options (no blanks inside one);
# DOSSTRUN_TREE=DIR with MODE tree: that tree instead of build/w16 (with
# DROPIN_COVER=DIR, a C: drive with applications installed, kept outside
# the repository); DOSSTRUN_SOUND=1: the emulator's sound on (DOSBox-X's
# Sound Blaster 16 then plays, as in an interactive session).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
o=$1 mode=$2 emu=${3:-x}
mkdir -p "$o"
o=$(cd "$o" && pwd)
rm -rf "$o/run" "$o/DSB.LOG" "$o/DSA.LOG" "$o/DONE.TXT" "$o/emu.log"
ref=$root/.tools/ref/win31/cdrive
case $mode in
ms|w16|tree) ;;
*) echo "mode: ms, w16 or tree" >&2; exit 2 ;;
esac
{ printf '@ECHO OFF\r\nC:\\DOSST B\r\nWIN /3\r\n'
  [ -n "${DOSSTRUN_POST:-}" ] && printf '%s\r\n' "$DOSSTRUN_POST"; } > "$o/GO.BAT"
{ [ -n "${DOSSTRUN_PRE:-}" ] && cat "$DOSSTRUN_PRE"
  [ -z "${DOSSTRUN_SHELL:-}" ] && cat <<EOF
; File > Exit Windows, OK
key alt+f
sleep 1
key x
sleep 2
key Return
EOF
  cat <<EOF
; the mouse moved while Windows ends and after; DOSST A and a command at
; the DOS prompt
jiggle 20
waitexit 90
sleep 2
jiggle 20
sleep 1
type C:\\DOSST A
key Return
sleep 3
jiggle 20
type ECHO after ok> C:\\DONE.TXT
key Return
sleep 3
shot 01-dos
EOF
} > "$o/script.txt"
set -- -f "$root/build/dos/DOSST.EXE=DOSST.EXE" -f "$o/GO.BAT=GO.BAT" -c "CALL C:\\GO.BAT"
[ "$emu" = dosbox ] || set -- "$@" -p 386
case $mode in
ms) set -- "$@" -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" ;;
w16) set -- "$@" -r -f "$ref/WINDOWS/SYSTEM/KRNL386.EXE=WINDOWS/SYSTEM/KRNL386.EXE" \
        -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" ;;
tree) set -- "$@" -T "${DOSSTRUN_TREE:-$root/build/w16}" -3 ;;
esac
boot="-s -b 120"
if [ -n "${DOSSTRUN_SHELL:-}" ]; then
    b=$(basename "$DOSSTRUN_SHELL")
    set -- "$@" -f "$DOSSTRUN_SHELL=$b" -i "boot:shell=C:\\$b"
    boot="-b 3"
fi
# shellcheck disable=SC2086
[ -n "${DOSSTRUN_ARGS:-}" ] && set -- "$@" $DOSSTRUN_ARGS
if [ -n "${DOSSTRUN_SOUND:-}" ]; then
    # inputrun's own copy of the configuration: [mixer] nosound off and
    # the PC speaker on (dropin.sh's defaults are silent)
    export INPUTRUN_CONFSED='s/^nosound=true$/nosound=false/;s/^pcspeaker=false$/pcspeaker=true/'
fi
# shellcheck disable=SC2086
DROPIN_CONF="[log]
logfile=$o/emu.log" DROPIN_NOREDIR=1 DROPIN_CPUTYPE=${DROPIN_CPUTYPE:-386} INPUTRUN_STAY=1 INPUTRUN_NOLOCK=1 \
    "$root/tests/inputrun.sh" $boot -t "${DOSSTRUN_TIME:-240}" "$o/script.txt" "$o/run" "$@" \
    > "$o/run.txt" 2>&1 || true
for f in DSB.LOG DSA.LOG DONE.TXT; do
    l=$(find "$o/run/work/c" -maxdepth 2 -iname "$f" | head -1)
    [ -n "$l" ] && tr -d '\r' < "$l" > "$o/$f"
done
echo "== DONE.TXT: $(cat "$o/DONE.TXT" 2>/dev/null || echo '(none)')"
echo "== invalid opcodes (INT 6) in the emulator's log: $(grep -c 'Interrupt Called 6$' "$o/emu.log" 2>/dev/null)"
if [ -f "$o/DSB.LOG" ] && [ -f "$o/DSA.LOG" ]; then
    echo "== before / after"
    diff "$o/DSB.LOG" "$o/DSA.LOG"
else
    echo "== DSB.LOG or DSA.LOG missing"
fi
exit 0
