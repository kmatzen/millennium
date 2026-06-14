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

typedef struct { int state; int balance; } S;

int nondet_int(void);

/* Mirrors daemon.c handle_* (state + balance only). */
static S daemon_step(S s, int ev, int val) {
    S o = s;
    if (ev == EV_COIN) {
        if (s.state == IDLE_UP) o.balance = s.balance + val;   /* credit only when up */
    } else if (ev == EV_HOOK_UP) {
        if (s.state == CALL_INCOMING) o.state = CALL_ACTIVE;   /* answer */
        else if (s.state == IDLE_DOWN) { o.state = IDLE_UP; o.balance = 0; }
    } else if (ev == EV_HOOK_DOWN) {
        o.state = IDLE_DOWN; o.balance = 0;
    } else if (ev == EV_CALL_INCOMING) {
        if (s.state == IDLE_DOWN || s.state == IDLE_UP) o.state = CALL_INCOMING;
    } else if (ev == EV_CALL_ACTIVE) {
        o.state = CALL_ACTIVE;
    } else if (ev == EV_CALL_INVALID) {
        if (s.state == CALL_ACTIVE) { o.state = IDLE_UP; o.balance = 0; }
        else if (s.state == CALL_INCOMING) { o.state = IDLE_UP; /* #91: keep coins */ }
    }
    return o;
}

/* Mirrors simulator.c sim_handle_* (state + balance only). */
static S sim_step(S s, int ev, int val) {
    S o = s;
    if (ev == EV_COIN) {
        if (s.state == IDLE_UP) o.balance = s.balance + val;
    } else if (ev == EV_HOOK_UP) {
        if (s.state == CALL_INCOMING) o.state = CALL_ACTIVE;
        else if (s.state == IDLE_DOWN) { o.state = IDLE_UP; o.balance = 0; }
    } else if (ev == EV_HOOK_DOWN) {
        o.state = IDLE_DOWN; o.balance = 0;
    } else if (ev == EV_CALL_INCOMING) {
        if (s.state == IDLE_DOWN || s.state == IDLE_UP) o.state = CALL_INCOMING;
    } else if (ev == EV_CALL_ACTIVE) {
        o.state = CALL_ACTIVE;
    } else if (ev == EV_CALL_INVALID) {
        /* Matches the daemon after the fix: clear only on an active call; a
         * failed-to-connect incoming keeps coins for the plugin to refund (#91).
         * (The original sim cleared coins for both, which CBMC equiv flagged.) */
        if (s.state == CALL_ACTIVE) { o.state = IDLE_UP; o.balance = 0; }
        else if (s.state == CALL_INCOMING) { o.state = IDLE_UP; }
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
}

void verify_equiv(void) {
    S in, d, s; int ev, val;
    mk_input(&in, &ev, &val);
    d = daemon_step(in, ev, val);
    s = sim_step(in, ev, val);
    __CPROVER_assert(d.state == s.state, "sim and daemon agree on next state");
    __CPROVER_assert(d.balance == s.balance, "sim and daemon agree on balance");
}
