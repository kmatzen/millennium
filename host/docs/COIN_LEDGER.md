# Coin Ledger Consistency

The same coin balance lives in **three** places:

| Ledger | Declared | Who reads it |
|---|---|---|
| `daemon_state->inserted_cents` | `daemon_state.h:18` | web API `/api/state`, the `inserted_cents` metrics gauge, `daemon_save_state` |
| `classic_phone_data.inserted_cents` | `plugins/classic_phone.c:18` | the VFD display (`"Have: 50c"`, `:298`) and the dial gate (`:334`) |
| `persisted_state_t.inserted_cents` | `state_persistence.h:8` | restored at boot, `daemon.c:1171` |

The only sync channel between the first two is `plugins_adjust_inserted_cents`
(`daemon.c:102-110`, added by #109), and it is called from exactly **two** sites —
the charge (`classic_phone.c:396`) and the refund (`:190`). Every other write to
either ledger is unilateral.

`tests/CoinLedger.tla` models all three. Run with `make coin-check` (properties
that hold) and `make coin-check-drift` (the divergences).

## Scope

Physical coin custody is deliberately **not** modelled. Conservation against real
money would need the meaning of the validator's `'c'` / `'z'` commands, and
`docs/COIN_VALIDATOR.md` records those with a question mark — *"commit/execute
(sequence terminator?)"* — alongside an explicitly unmitigated reconnect race.
Modelling them would mean inventing semantics. The three software ledgers are
fully determined by code, so those are what is checked.

## Verified (`make coin-check`, 3316 states)

No ledger goes negative, and all three stay well-typed across every interleaving
of coin insert, hook, charge, refund, timeout, return, save and restart.

## Divergences (`make coin-check-drift`)

Four independent paths, each a separate counterexample:

| # | Path | Site | Effect |
|---|---|---|---|
| 1 | Coin return | `daemon.c:800-819` | Zeroes the **daemon** ledger and bumps the revenue metrics; the plugin keeps its balance, so the VFD keeps offering credit the daemon will not honour. |
| 2 | Idle timeout | `classic_phone.c:477` | Zeroes the **plugin** ledger from the tick handler. No event reaches the daemon, so the daemon ledger, the persisted file and the metrics gauge all keep phantom credit. |
| 3 | Boot ordering | `daemon.c:1160-1174` | `plugins_init()` activates Classic Phone, whose `handle_activation` (`classic_phone.c:246`) seeds its ledger from `daemon_state->inserted_cents` — still 0, because the persisted balance is not written until `:1171`. The re-activation at `:1174` repairs this **only** when the persisted plugin name is non-empty. |
| 4 | Call timeout | `classic_phone.c:430` | `classic_phone_end_call` zeroes the **plugin** ledger on the plugin's own call timeout, raising no daemon event. Any leftover change diverges. |

Direction matters when choosing a fix:

- **`pcents > dcents`** (path 1) — the phone promises the customer credit that is already gone.
- **`dcents > pcents`** (paths 2, 3, 4) — the daemon, the persisted file and the operator's revenue figures carry credit the customer cannot spend, and a restart can resurrect it.

The clamp at `daemon.c:107` (`if (inserted_cents < 0) inserted_cents = 0`)
**masks** rather than prevents this: once the ledgers drift, a charge silently
absorbs the difference instead of surfacing it.

## A note on bounds

`CoinLedger.cfg` uses `MaxEvents = 7`, not 5. At 5, path 4 does not appear —
reaching it needs leftover change after a paid call (lift, three coins, charge,
end = seven actions). The model reported "no error" for that path at the lower
bound. A bound is not a proof; if you add actions to the spec, re-check whether
the bound still reaches them.

## Fixing

The paths are independent, so they can be closed separately, but they share a
shape: a single balance with two owners and no invalidation protocol. Two broad
options —

1. **One ledger.** Delete `classic_phone_data.inserted_cents` and have the plugin
   read `sdk_balance()` at every use. Removes the class of bug rather than
   patching instances. Touches the plugin SDK contract, and other plugins keep
   their own balances too.
2. **Make every write go through the sync channel.** Route the four paths above
   through `plugins_adjust_inserted_cents` (or add the missing
   `handle_deactivation` hook, which the plugin API lacks entirely). Smaller, but
   leaves the next unilateral write free to reintroduce the problem.

Option 1 is what the invariant actually wants. Neither is applied yet.
