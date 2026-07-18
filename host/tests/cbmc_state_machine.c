/*
 * CBMC model of the daemon phone state machine: invariants + simulator/daemon
 * equivalence.
 *
 * daemon_step() mirrors the state+balance transitions in daemon.c's
 * handle_coin/handle_hook_event/handle_call_state_event; sim_step() mirrors
 * simulator.c's sim_handle_coin/sim_handle_hook/sim_handle_call_state (the
 * memory note warns these two implementations can drift). The metrics/plugin/
 * mutex/serial side effects are omitted; only (state, balance) is modeled.
 *
 *   make state-check
 *
 *   verify_invariants : over all (state, event, coin value>=0), the daemon
 *     transition keeps balance >= 0, never lands in INVALID, credits coins only
 *     in IDLE_UP, and always reaches IDLE_DOWN on hook-down.
 *   verify_equiv      : daemon_step == sim_step for all inputs (same state
 *     machine) -- catches sim/daemon drift.
 */

/* daemon_state_t values (daemon_state.h) */
#define INVALID        0
#define IDLE_DOWN      1
#define IDLE_UP        2
#define CALL_INCOMING  3
#define CALL_ACTIVE    4

/* events */
#define EV_COIN          0
#define EV_HOOK_UP       1
#define EV_HOOK_DOWN     2
#define EV_CALL_INCOMING 3
#define EV_CALL_ACTIVE   4
#define EV_CALL_INVALID  5

/* `handset` mirrors daemon_state_data_t.handset_up: the physical handset
 * position, tracked separately because CALL_INCOMING is reachable both on-hook
 * and off-hook (#92), so current_state cannot stand in for it. Added after
 * tests/EventOrdering.tla found the races; see docs/EVENT_ORDERING.md. */
typedef struct { int state; int balance; int handset; } S;

int nondet_int(void);

/* Mirrors daemon.c handle_* (state + balance only). */
static S daemon_step(S s, int ev, int val) {
    S o = s;
    if (ev == EV_COIN) {
        if (s.state == IDLE_UP) o.balance = s.balance + val;   /* credit only when up */
    } else if (ev == EV_HOOK_UP) {
        o.handset = 1;                                         /* recorded on dispatch */
        if (s.state == CALL_INCOMING) o.state = CALL_ACTIVE;   /* answer */
        else if (s.state == IDLE_DOWN) { o.state = IDLE_UP; o.balance = 0; }
    } else if (ev == EV_HOOK_DOWN) {
        o.handset = 0;
        o.state = IDLE_DOWN; o.balance = 0;
    } else if (ev == EV_CALL_INCOMING) {
        if (s.state == IDLE_DOWN || s.state == IDLE_UP) o.state = CALL_INCOMING;
    } else if (ev == EV_CALL_ACTIVE) {
        /* Guarded on the handset, not the state: a CALL_ESTABLISHED arriving
         * while the handset is cradled must not strand us in CALL_ACTIVE. */
        if (s.handset) o.state = CALL_ACTIVE;
    } else if (ev == EV_CALL_INVALID) {
        /* Land in the idle state matching the handset, not always IDLE_UP. */
        if (s.state == CALL_ACTIVE) { o.state = s.handset ? IDLE_UP : IDLE_DOWN; o.balance = 0; }
        else if (s.state == CALL_INCOMING) { o.state = s.handset ? IDLE_UP : IDLE_DOWN; /* #91: keep coins */ }
    }
    return o;
}

/* Mirrors simulator.c sim_handle_* (state + balance only). */
static S sim_step(S s, int ev, int val) {
    S o = s;
    if (ev == EV_COIN) {
        if (s.state == IDLE_UP) o.balance = s.balance + val;
    } else if (ev == EV_HOOK_UP) {
        o.handset = 1;
        if (s.state == CALL_INCOMING) o.state = CALL_ACTIVE;
        else if (s.state == IDLE_DOWN) { o.state = IDLE_UP; o.balance = 0; }
    } else if (ev == EV_HOOK_DOWN) {
        o.handset = 0;
        o.state = IDLE_DOWN; o.balance = 0;
    } else if (ev == EV_CALL_INCOMING) {
        if (s.state == IDLE_DOWN || s.state == IDLE_UP) o.state = CALL_INCOMING;
    } else if (ev == EV_CALL_ACTIVE) {
        /* Guarded on the handset, not the state: a CALL_ESTABLISHED arriving
         * while the handset is cradled must not strand us in CALL_ACTIVE. */
        if (s.handset) o.state = CALL_ACTIVE;
    } else if (ev == EV_CALL_INVALID) {
        /* Matches the daemon after the fix: clear only on an active call; a
         * failed-to-connect incoming keeps coins for the plugin to refund (#91).
         * (The original sim cleared coins for both, which CBMC equiv flagged.) */
        if (s.state == CALL_ACTIVE) { o.state = s.handset ? IDLE_UP : IDLE_DOWN; o.balance = 0; }
        else if (s.state == CALL_INCOMING) { o.state = s.handset ? IDLE_UP : IDLE_DOWN; }
    }
    return o;
}

static void mk_input(S *in, int *ev, int *val) {
    in->state   = nondet_int();
    in->balance = nondet_int();
    *ev         = nondet_int();
    *val        = nondet_int();
    __CPROVER_assume(in->state >= IDLE_DOWN && in->state <= CALL_ACTIVE); /* a valid live state */
    /* Realistic bounds (physical coins): keeps balance + val from overflowing
     * int, which is not a reachable condition on a real coin box. */
    __CPROVER_assume(in->balance >= 0 && in->balance <= 1000000);
    __CPROVER_assume(*ev >= EV_COIN && *ev <= EV_CALL_INVALID);
    __CPROVER_assume(*val >= 0 && *val <= 1000);   /* coin denominations are small positives */
    in->handset = nondet_int();
    __CPROVER_assume(in->handset == 0 || in->handset == 1);
    /* CALL_ACTIVE with the handset cradled is not a reachable input -- assuming
     * it here makes the CALL_ACTIVE check below an INDUCTIVE step: given a
     * consistent state, one transition keeps it consistent. CBMC proves the
     * step; tests/EventOrdering.tla proves the reachability side (that no
     * interleaving of queued events can produce such a state in the first
     * place). Neither tool covers both halves on its own. */
    __CPROVER_assume(in->state != CALL_ACTIVE || in->handset);
}

void verify_invariants(void) {
    S in, d; int ev, val;
    mk_input(&in, &ev, &val);
    d = daemon_step(in, ev, val);
    __CPROVER_assert(d.balance >= 0, "balance never negative");
    __CPROVER_assert(d.state >= IDLE_DOWN && d.state <= CALL_ACTIVE, "never lands in INVALID");
    __CPROVER_assert(!(ev == EV_COIN && d.balance != in.balance) || in.state == IDLE_UP,
                     "coins credit only in IDLE_UP");
    __CPROVER_assert(ev != EV_HOOK_DOWN || d.state == IDLE_DOWN,
                     "hook-down always returns to IDLE_DOWN");
    /* The property EventOrdering.tla could only state once the handset became
     * its own field: never CALL_ACTIVE while the handset is on the hook. */
    __CPROVER_assert(d.state != CALL_ACTIVE || d.handset,
                     "never CALL_ACTIVE with the handset on hook");
}

void verify_equiv(void) {
    S in, d, s; int ev, val;
    mk_input(&in, &ev, &val);
    d = daemon_step(in, ev, val);
    s = sim_step(in, ev, val);
    __CPROVER_assert(d.state == s.state, "sim and daemon agree on next state");
    __CPROVER_assert(d.balance == s.balance, "sim and daemon agree on balance");
    __CPROVER_assert(d.handset == s.handset, "sim and daemon agree on handset");
}
