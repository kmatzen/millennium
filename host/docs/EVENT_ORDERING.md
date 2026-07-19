# Event Ordering and Idempotency (#100)

Events are processed asynchronously: the PJSIP (PJSUA worker) thread queues call-state events to the main loop, while hook/coin/keypad events come from the serial/Arduino. Order is not guaranteed when multiple events occur close together.

## Hook + Call State Race

**Scenario**: User hangs up (hook_down) at roughly the same time the remote party hangs up (CALL_CLOSED → EVENT_CALL_STATE_INVALID).

### Order 1: Hook first, then INVALID

1. `handle_hook_event(hook_down)`: Transitions to IDLE_DOWN, clears keypad/coins, calls `millennium_client_hangup`, sends coin-validator commands.
2. `handle_call_state_event(INVALID)`: `current_state` is already IDLE_DOWN. Condition `current_state == CALL_ACTIVE || current_state == CALL_INCOMING` is false → **no action**. PJSIP tolerates a second hangup if one was already sent.

### Order 2: INVALID first, then hook

1. `handle_call_state_event(INVALID)`: Transitions to IDLE_UP, clears keypad/coins, calls `millennium_client_hangup`, sends coin-validator commands.
2. `handle_hook_event(hook_down)`: Transitions to IDLE_DOWN again, clears keypad/coins (already clear), sends hangup and coin-validator commands again. **Idempotent** — redundant but safe.

### Idempotency

- **INVALID handler**: Only acts when `current_state` is CALL_ACTIVE or CALL_INCOMING. If already IDLE (from prior hook_down), does nothing.
- **Hook-down handler**: Always transitions to IDLE_DOWN and clears state. Safe to run multiple times.
- **Double-hangup**: PJSIP tolerates (`pjsua_call_hangup` on an already-closed call is a no-op). Coin-validator commands (`c`, `z`) sent twice are assumed harmless.

## Testing

Scenario tests `test_remote_hangup.scenario` and `test_incoming_handset_up.scenario` cover call-state transitions.

## Model checking (`make tla-check`)

The idempotency argument above is a hand analysis of two orderings of two events. `tests/EventOrdering.tla` discharges it mechanically over *all* interleavings of every event type, across the two producers that actually race: the serial/Arduino path and the PJSUA worker thread, feeding the single-consumer main loop.

The model separates `hook` (the *physical* handset position) from `state` (the daemon's *belief*). The daemon's five-state enum conflates the two, so they can silently disagree — and that disagreement is only statable as an invariant once they are distinct variables.

**Verified (`make tla-check`, 3389 states):** under every interleaving, not just the two above — no reachable `INVALID`, balance stays non-negative, `IDLE_DOWN` always implies zero credit, and once the queue drains the daemon never sits in `CALL_ACTIVE` on-hook (`SettledNotActiveOnHook`) and its belief agrees with the physical handset (`Converged`).

**Confirms the analysis above:** the hook-down vs. `CALL_INVALID` race really is idempotent; both orders settle in `IDLE_DOWN`. The intermediate `IDLE_UP` in Order 2 is a genuine transient.

### Finding: the daemon could not tell an on-hook ring from an off-hook one

The first run of this spec produced three violations with a **single structural
root cause**, not a missing guard.

`daemon_state_data_t` had no field for the handset position, so the daemon
*inferred* it from `current_state` — and that inference is unsound, because
`CALL_INCOMING` is reachable both on-hook (a phone ringing in its cradle) and
off-hook (#92 interrupt dialing). No test on `current_state` can distinguish
them, so no guard placed there could be correct. Two intermediate attempts at a
`current_state` guard were both refuted by TLC before this became clear.

| Invariant | Failing behaviour |
|---|---|
| `SettledNotActiveOnHook` | A `CALL_ESTABLISHED` dispatched after the user hung up left the phone parked in `CALL_ACTIVE` with the handset cradled, with nothing left in the queue to recover it. |
| `Converged` | `CALL_INVALID` ending an unanswered on-hook ring dropped to `IDLE_UP` (`daemon.c:407`, `:411`), leaving the daemon believing the handset was lifted while it sat in the cradle. |
| `ActiveImpliesOffHook` | The instantaneous version of the first — see "By design" below. |

**Fix applied.** `daemon_state_data_t` gained an explicit `handset_up` field,
recorded in `handle_hook_event` from the physical hook direction. The
`EVENT_CALL_STATE_ACTIVE` arm now guards on it, and both `EVENT_CALL_STATE_INVALID`
arms return to `IDLE_DOWN` or `IDLE_UP` to match it. Mirrored in `simulator.c`
and `tests/cbmc_state_machine.c`.

Two consequences worth knowing:

- **`handset_up` is not persisted.** The keypad firmware only emits hook
  *transitions*, never the position at boot, so a restart cannot observe it. It
  is derived from the restored `last_state` instead, which keeps the two
  self-consistent.
- **Six scenario assertions changed** in `test_call_duration_metrics`,
  `test_failed_call_metrics`, and `test_missed_call_metrics`. Each was a
  `call_ended` with no preceding `hook_up`, asserting `IDLE_UP`. They were
  encoding the bug: the handset was never lifted, so `IDLE_DOWN` is correct.

### By design: `ActiveImpliesOffHook` (`make tla-check-race`)

The *instantaneous* property — never `CALL_ACTIVE` while the handset is
physically cradled, at every moment — cannot hold in an event-queue
architecture, and its violation is not a defect. The handset moves, the firmware
queues a hook event, and until the main loop dispatches it the daemon has not
yet learned. Every queued-event design has this window.

What is verified is that the window always **closes**: `SettledNotActiveOnHook`
and `Converged` both hold once the queue drains. Closing the instantaneous
window would require the firmware to report hook position synchronously — a
hardware change, not a daemon one.

### How the three tools divide the work

`make state-check` (CBMC) proves the **inductive step**: given a consistent
state, one transition preserves consistency — and that `simulator.c` and
`daemon.c` agree, now including `handset_up`. It cannot express two events in
flight.

`make tla-check` (TLC) proves the **reachability** half: no interleaving of
queued events can reach an inconsistent state to begin with. Neither tool covers
both halves alone, which is why the `CALL_ACTIVE` bug survived until now — CBMC
transcribed the unguarded arm faithfully and asserted nothing about it.

## Related: lock-order verification (`make lock-check`)

`tests/LockOrder.tla` model-checks the mutex acquisition graph for circular
wait, as a follow-up to #231. The full lock order is documented at
`daemon.c:59`.

Three things about it are worth knowing:

**The edge set was extracted, not remembered.** `tests/extract_lock_edges.py`
walks the source tracking lock/unlock nesting and takes the transitive closure
over the call graph. It found two edges the original #231 comment had missed —
`daemon_state_mutex -> plugins_mutex` (via `daemon_broadcast_state` calling
`plugins_get_active_name`) and `daemon_state_mutex -> logger_mutex`.

**Static analysis cannot see through function pointers.** The
`g_monitor_mutex -> daemon_state_mutex` edge runs through a registered health
check callback and had to be found by reading. The script says so in its own
docstring; treat its "no cycle" as necessary, not sufficient.

**`make lock-check-mutant` is what makes `make lock-check` mean anything.** It
reverses one edge and must report a violation. The first version of this spec
relied on TLC's built-in deadlock detection, which only flags a *global* stall
— and reported "no error" even on the deliberately-cycled model, because two
threads deadlocking while three keep running is not a global stall. The spec
now states circular wait directly as a waits-for cycle.

## Auditing the specs themselves (`make mutation-audit`)

A green TLC run is not evidence that a property is doing anything. Twice while
writing these specs a property passed for a reason unrelated to the system:

- `LockOrder.tla` relied on TLC's built-in deadlock detection, which flags only
  a state with *no successor*. A lock-order bug does not produce one — two
  threads deadlock while others keep running — so it reported "no error" on a
  deliberately-cycled version of itself.
- `Updater.tla`'s `RestartOk` both assigned `callDropped'` and listed it in
  `UNCHANGED`. That contradiction silently disabled the action whenever a call
  was up, so the guard under test was never exercised.

Both were caught by accident. `tests/mutation_audit.sh` makes it systematic:
for each property, break the system in the way that property is meant to notice
and confirm it fires. Anything reported `NOT CAUGHT` is vacuous or subsumed, and
its green result means nothing.

The audit also identified three properties that are **subsumed** and can never
be the invariant TLC reports — `NonNegative` in both specs (implied by
`TypeOK`'s `\in Nat`) and `NeverPromiseMoreThanHeld` (implied by
`LedgerAgreement`). They are kept for readability and now say so in place, so
nobody counts them as coverage.
