#!/bin/sh
# e10run.sh: the MS-DOS Prompt scenario with E10 recording, 3.1's WIN.COM (R) and w16's (W)
cd /home/segin/w16/.claude/worktrees/agent-a179f2e48b5cfbfc0 || exit 1
scr=/home/segin/w16/.claude/worktrees/agent-a7335d1d9fc5cbe6f/tests/scripts/input/woados.txt
printf 'C:\\E10\r\nSET TEMP=C:\\WINDOWS\\TEMP\r\nWIN /S\r\nC:\\E10 D\r\n' > /tmp/dropin/GOE.BAT
for v in ${E10V:-R W}; do
  o=""; [ $v = W ] && o=-w
  rm -rf /tmp/dropin/e10$v
  INPUTRUN_STAY=1 tests/inputrun.sh -b 30 -t 140 $scr /tmp/dropin/e10$v -r $o -c GOE \
      -f /tmp/dropin/GOE.BAT=WINDOWS/GOE.BAT -f /tmp/dropin/wc/E10.COM=E10.COM > /tmp/dropin/e10$v.txt 2>&1
done
echo finished > /tmp/dropin/e10run.txt
