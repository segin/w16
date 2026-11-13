#!/bin/sh
# tk8x.sh N: TK8 as run-tests.sh runs it (DOSBox-X, coprocessor), N times
root=/home/segin/w16/.claude/worktrees/agent-a179f2e48b5cfbfc0
dos=$root/build/dos
stage=/tmp/dropin/tk8stage
rm -rf "$stage"; mkdir -p "$stage/WINDOWS/SYSTEM"
cp "$dos/KRNL286.EXE" "$dos/WIN87EM.DLL" "$stage/WINDOWS/SYSTEM/"
cp "$dos/WINENV.COM" "$dos/TK8.EXE" "$stage/"
DOSRUN_X=1 DOSRUN_FPU=true
export DOSRUN_X DOSRUN_FPU
k=1
while [ $k -le $1 ]; do
  rm -f "$stage/TK.LOG"
  timeout 170 "$root/tests/dosrun.sh" "$stage" "WINENV C:\\WINDOWS WINDOWS\\SYSTEM\\KRNL286.EXE TK8.EXE" > /dev/null 2>&1
  echo "run $k: $(tr -d '\r' < "$stage/TK.LOG" | grep -a 'FAIL\|TK8 done' | tr '\n' ' ')"
  k=$((k + 1))
done
