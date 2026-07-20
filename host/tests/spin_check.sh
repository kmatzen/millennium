#!/bin/sh
#
# Run one SPIN verification and exit non-zero unless it reports "errors: 0".
#
# WHY THIS EXISTS
#
# The model-check and game-check recipes used to pipe spin's output straight
# into grep:
#
#     ( cd $tmp && spin -run -safety ... ) | grep -iE "...|errors:"
#
# which makes the recipe's exit status *grep's*, and grep matches "errors: 1"
# just as happily as "errors: 0". The targets were green no matter what the
# model said. Confirmed by planting `assert(false)` in protocol.pml: spin
# printed "errors: 1" and make still exited 0.
#
# Anything wired into CI has to be able to fail, so the check is explicit here.
#
#   spin_check.sh <workdir> <label> <spin args...>
#
set -u
dir=$1; label=$2; shift 2

echo "== $label =="
out=$(cd "$dir" && ${SPIN:-spin} "$@" 2>&1) || true
echo "$out" | grep -iE "assertion viol|invalid end|acceptance|never claim|errors:" || true

if echo "$out" | grep -qE "errors: 0"; then
    echo "  OK: $label"
    exit 0
fi
echo "  FAIL: $label -- spin did not report 'errors: 0'"
echo "$out" | tail -20
exit 1
