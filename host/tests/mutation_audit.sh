#!/bin/bash
#
# Mutation-test every model-checked property (#231 follow-up).
#
# WHY THIS EXISTS
#
# Twice while writing these specs, a property passed for a reason that had
# nothing to do with the system being correct:
#
#   - LockOrder.tla originally relied on TLC's built-in deadlock detection,
#     which only flags a state with NO successor. A lock-order bug does not
#     produce one -- two threads deadlock while three keep running -- so the
#     model reported "no error" on a deliberately-cycled version of itself.
#
#   - Updater.tla's RestartOk both assigned callDropped' AND listed it in
#     UNCHANGED. That contradiction silently disabled the action whenever a
#     call was up, so the guard being tested was never exercised and the
#     invariant held vacuously.
#
# Both were caught by accident. A property that cannot fail is not verifying
# anything, and nothing about a green TLC run distinguishes the two cases.
#
# So: break the system in a way each property is supposed to notice, and check
# that it actually does. Any "NOT CAUGHT" line means that property is either
# vacuous or subsumed by another, and its green result means nothing.
#
#   ./mutation_audit.sh            (from host/tests, needs tla2tools.jar)
#   TLA_TOOLS=/path/to/jar ./mutation_audit.sh
#
set -u
JAR="${TLA_TOOLS:-tla2tools.jar}"
[ -f "$JAR" ] || { echo "tla2tools.jar not found; set TLA_TOOLS=/path/to/tla2tools.jar"; exit 2; }
fails=0

run() {  # run <spec> <cfg> <label> <python-mutation>
    local spec=$1 cfg=$2 label=$3 mut=$4 out
    cp "$spec" "/tmp/_orig_$$.tla"
    if ! python3 -c "$mut"; then
        echo "  !! $label -- mutation did not apply (spec text changed?)"
        cp "/tmp/_orig_$$.tla" "$spec"; fails=$((fails+1)); return
    fi
    out=$(java -XX:+UseParallelGC -cp "$JAR" tlc2.TLC -config "$cfg" "$spec" 2>&1 \
          | grep -E "Invariant .* is violated|No error" | head -1)
    cp "/tmp/_orig_$$.tla" "$spec"
    if echo "$out" | grep -q "No error"; then
        echo "  NOT CAUGHT  $label"; fails=$((fails+1))
    else
        echo "  caught      $label -> ${out#Error: }"
    fi
}

echo "== EventOrdering =="
run EventOrdering.tla EventOrdering.cfg "hook_down keeps coins" '
s=open("EventOrdering.tla").read()
a="""      [] e.type = "hook_down" ->
            <<"IDLE_DOWN", 0, "down">>"""
b="""      [] e.type = "hook_down" ->
            <<"IDLE_DOWN", c, "down">>"""
assert a in s; open("EventOrdering.tla","w").write(s.replace(a,b))'
run EventOrdering.tla EventOrdering.cfg "hook_down reaches INVALID" '
s=open("EventOrdering.tla").read()
a="""      [] e.type = "hook_down" ->
            <<"IDLE_DOWN", 0, "down">>"""
b="""      [] e.type = "hook_down" ->
            <<"INVALID", 0, "down">>"""
assert a in s; open("EventOrdering.tla","w").write(s.replace(a,b))'
run EventOrdering.tla EventOrdering.cfg "call_active unguarded (pre-#100)" '
s=open("EventOrdering.tla").read()
a="""IF h = "up" THEN <<"CALL_ACTIVE", c, h>> ELSE <<s, c, h>>"""
assert a in s; open("EventOrdering.tla","w").write(s.replace(a,"""<<"CALL_ACTIVE", c, h>>"""))'
run EventOrdering.tla EventOrdering.cfg "call_invalid ignores handset" '
s=open("EventOrdering.tla").read()
a="""LET idle == IF h = "up" THEN "IDLE_UP" ELSE "IDLE_DOWN" IN"""
assert a in s; open("EventOrdering.tla","w").write(s.replace(a,"""LET idle == "IDLE_UP" IN"""))'

echo "== CoinLedger =="
run CoinLedger.tla CoinLedger.cfg "coin_return skips the plugin ledger (pre-#222)" '
import re
s=open("CoinLedger.tla").read()
m=re.search(r"CoinReturn ==.*?budget. = budget - 1", s, re.S); assert m
blk=m.group(0)
new=blk.replace("/\\ pcents\x27 = 0          \\* one ledger: the plugin reads sdk_balance() live\n    ","").replace("UNCHANGED state","UNCHANGED <<state, pcents>>")
assert new!=blk; open("CoinLedger.tla","w").write(s.replace(blk,new))'
run CoinLedger.tla CoinLedger.cfg "coin insert not persisted (pre-#222 keypad gap)" '
s=open("CoinLedger.tla").read()
a="""         /\\ fcents\x27 = dcents + v            \\* handle_coin_event saves"""
assert a in s
s=s.replace(a,"").replace("    /\\ UNCHANGED state\n    /\\ budget\x27 = budget - 1\n\n(* Web dashboard","    /\\ UNCHANGED <<state, fcents>>\n    /\\ budget\x27 = budget - 1\n\n(* Web dashboard")
open("CoinLedger.tla","w").write(s)'
run CoinLedger.tla CoinLedger.cfg "charge underflows past zero" '
s=open("CoinLedger.tla").read()
a="Max(0, dcents - Cost)"; assert a in s
open("CoinLedger.tla","w").write(s.replace(a,"dcents - Cost - 100"))'

echo "== Updater =="
run Updater.tla Updater.cfg "restart fires during a call (pre-#225)" '
s=open("Updater.tla").read()
a="""    /\\ ~inCall
    \\* Records whether"""
assert a in s; open("Updater.tla","w").write(s.replace(a,"""    \\* Records whether""",1))'
run Updater.tla Updater.cfg "build failure strands the tree (pre-#224)" '
s=open("Updater.tla").read()
a="""    /\\ src\x27    = "old"            \\* #224: rollback_to(prev_commit)"""
assert a in s; open("Updater.tla","w").write(s.replace(a,"""    /\\ UNCHANGED src"""))'
run Updater.tla Updater.cfg "restart exit code ignored (pre-fix)" '
s=open("Updater.tla").read()
a="""    /\\ status\x27 = "err_restart"       \\* now checked -- updater.c:273"""
assert a in s; open("Updater.tla","w").write(s.replace(a,"""    /\\ status\x27 = "success\"""" ))'

echo "== LockOrder =="
run LockOrder.tla LockOrder.cfg "plugin callback takes daemon_state under plugins_mutex" '
s=open("LockOrder.tla").read()
a="Chains == IF Mutated THEN MutatedChains ELSE RealChains"
assert a in s; open("LockOrder.tla","w").write(s.replace(a,"Chains == MutatedChains"))'

echo
if [ "$fails" -eq 0 ]; then echo "All properties caught their mutation."; else echo "$fails property/properties did not catch their mutation."; fi
exit $fails
