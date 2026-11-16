#!/bin/sh
# wcd.sh: the MS-DOS Prompt scenario with WC3 in front of DSWAP.EXE; 3.1's WIN.COM (R), w16's (W)
cd /home/segin/w16/.claude/worktrees/agent-a179f2e48b5cfbfc0 || exit 1
scr=/home/segin/w16/.claude/worktrees/agent-a7335d1d9fc5cbe6f/tests/scripts/input/woados.txt
refwin=.tools/ref/win31/cdrive/WINDOWS
printf 'SET TEMP=C:\\WINDOWS\\TEMP\r\nWIN /S\r\n' > /tmp/dropin/GO.BAT
for v in ${WCDV:-R W}; do
  o=""; [ $v = W ] && o=-w
  rm -rf /tmp/dropin/wcd$v
  INPUTRUN_STAY=1 tests/inputrun.sh -b 30 -t 140 $scr /tmp/dropin/wcd$v -r $o -c GO \
      -f /tmp/dropin/GO.BAT=WINDOWS/GO.BAT -f build/dos/WC3.COM=WINDOWS/SYSTEM/DSWAP.EXE \
      -f "$refwin/SYSTEM/DSWAP.EXE=WINDOWS/SYSTEM/DSWAP.EX_" > /tmp/dropin/wcd$v.txt 2>&1
  tr -d '\r' < /tmp/dropin/wcd$v/work/c/WC.LOG > /tmp/dropin/wcd$v.log 2>/dev/null
done
echo finished > /tmp/dropin/wcd.txt
