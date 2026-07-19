# OTA Update: Check and Apply

`host/updater.c` holds two independent state machines behind two mutexes:
`check_state` (`:12`) and `apply_state` (`:152`). `tests/Updater.tla` models
both, plus what a restart does to a call in progress and to escrowed coins.

Run with `make ota-check` (properties that hold) and `make ota-check-open`
(the open findings).

## Verified (`make ota-check`, 758 states)

- Nothing is ever installed without having been pulled first — the pipeline's
  step ordering is genuinely enforced.
- Coins survive a restart: `daemon_save_state` runs after every insert, so the
  persisted file and the daemon ledger agree at restart time.

Worth noting what is *not* claimed: "installed = new implies build = new" is
**false**, and correctly so. A second apply runs `make clean` and wipes the
build tree while the previously installed binary stays in place. That is
expected behaviour, not a defect — the first version of this spec asserted it
and TLC was right to reject it.

## Fixed

### 1. The check FSM was a one-way ratchet — `RecheckAlwaysPossible`

`check_state` moves `0 → 1 → 2` and **never returns to 0**. The only writes are
`:89` (=2), `:100` (=1), `:106` (=0, only on `pthread_create` failure) and
`:127` (=2). `updater_check_async` requires state 0 to start (`:99`).

So `/api/check-update` (`web_server.c:1624`) was inert after the first call for
the life of the process. Worse when the **first** check failed: `:90` clears
`latest_version`, so the phone reported "no update known" permanently and only a
daemon restart cleared it. A phone that booted with the network down never
learned about an update again.

**Fixed**: the guard is now `check_state != 1` — start a check unless one is
already running. `check_state` stays 2 after completion so the last known
version keeps being reported while a re-check runs.

Stated as `ENABLED CheckStart` rather than a bad-state predicate, because the
harm here is the *absence of a possible action*, not a state you can point at.

### 2. "Update applied successfully" was reported without checking — `StatusHonest`

`updater.c:273` calls `run_command("sudo systemctl restart daemon.service")`
and **discards the return code**, then `:276` sets the status to
`"Update applied successfully"` unconditionally. If the restart failed, the
dashboard showed success while the old binary kept running. Every other step in
the pipeline checked its return code; this one did not.

**Fixed**: the return code is checked and the status reports
`"Error: installed, but daemon restart failed"`. On success systemd kills the
process before that line runs, so reaching it at all means the restart did not
take.

## Fixed (continued)

### 3. A failed build stranded the tree — `NoStrandedTree`

Step 2 is `make clean && make daemon` (`:250`). `make clean` runs first and
unconditionally, and step 1 has already moved the working tree to the new
commit. A build failure therefore leaves **new source with no binary**, and
nothing records the previous commit to return to. There is no rollback at any
step of the pipeline.

### 4. The restart ignored call state — `NoCallDroppedByUpdate`

Nothing in the update path consults `web_server_is_in_call()` (`web_server.h:148`)
or `daemon_state->current_state`. An OTA applied while someone is on the phone
drops the call with no warning. Coins are not lost (see above), but the call is.

**Fixed (3)**: the pre-update commit is captured with `git rev-parse HEAD`
before the pull, and a failure at build or install resets the tree back to it.
The *installed binary* is deliberately not restored — undoing an install needs a
backup that does not exist — and the status string says so rather than implying
a full rollback.

**Fixed (4)**: `/api/update` refuses with **409 Conflict** while the phone is in
a call. That alone was not enough, and the model said so: the build takes
minutes, and the handset can be lifted in between. So `updater_apply` re-checks
immediately before `systemctl restart`, via a guard predicate the daemon
installs (`updater_set_restart_guard`), and **defers** the restart if the phone
is in use. The new binary is installed and takes effect at the next restart;
nothing is lost by waiting, whereas restarting would cut off a live call.

The guard is a function pointer so `updater.c` stays free of daemon/web_server
dependencies — `updater.o` is linked into the unit tests, which link neither.

## A note on how these properties are stated

Three of the six are stated over ghost flags rather than state predicates,
because the obvious state forms are false for legitimate reasons:

- `installed = new => build = new` is false: a second apply runs `make clean`
  and wipes the build tree while the previous binary is still installed.
- `installed = new => src = new` is false since the rollback: a failed apply
  restores the source and deliberately leaves the binary alone.
- `status = restarting => ~inCall` is false: the phone can legitimately be
  picked up while an apply sits at that step. What must not happen is the
  restart firing anyway.

`make ota-check-open` has been removed. Both findings it tracked are fixed, and
its `INVARIANT` block was empty — so it passed while checking nothing. Rollback would mean recording the pre-update
commit and restoring it on failure. Call-awareness would mean either refusing
the update while `CALL_ACTIVE` or deferring it until the call ends — the latter
needs somewhere to park the request.

Items 1 and 2 are fixed in `updater.c` and their properties are now checked by
`make ota-check`.
