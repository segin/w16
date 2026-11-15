#!/bin/sh
# suite.sh REV OUT: snapshot of REV; make test, dosxtest, run-tests on DOSX, full boot test
rev=$1; out=$2
rm -rf /tmp/dropin/snap "$out.bt"
mkdir -p /tmp/dropin/snap
cd /home/segin/w16/.claude/worktrees/agent-a179f2e48b5cfbfc0 || exit 1
git archive "$rev" | tar -x -C /tmp/dropin/snap
ln -s /home/segin/w16/.tools /tmp/dropin/snap/.tools
cd /tmp/dropin/snap || exit 1
make > /dev/null 2>&1
make install-tree > /dev/null 2>&1
tests/run-tests.sh > $out.mt.log 2>&1
echo "make test: rc=$? ok=$(grep -a -c ': ok' $out.mt.log) $(grep -a FAILED $out.mt.log | tr '\n' ' ')"
tests/dosxtest.sh > $out.dx.log 2>&1
echo "dosxtest: rc=$? $(tail -2 $out.dx.log | tr '\n' ' ')"
RUNTESTS_DPMI=dosx tests/run-tests.sh > $out.mtd.log 2>&1
echo "run-tests dosx: rc=$? ok=$(grep -a -c ': ok' $out.mtd.log) $(grep -a FAILED $out.mtd.log | tr '\n' ' ')"
tests/boottest.sh -o $out.bt > $out.bt.log 2>&1
echo "boottest: rc=$?"
grep -v '^ok' $out.bt.log
cp build/boottest-retries.log $out.retries.log 2>/dev/null
echo "retries: $(cat $out.retries.log 2>/dev/null | wc -l)"
echo finished
