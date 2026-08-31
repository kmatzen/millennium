/* Serial link watchdog / reconnect policy (#247).
 *
 * millennium_client_check_serial() decides, once per main-loop pass, whether to
 * nudge an idle link, declare it dead, or retry the reopen.  That decision used
 * to be tangled up with the syscalls that carry it out, and both simulator.c
 * and tests/unit_tests.c stub the whole SDK serial layer -- so the policy had
 * no coverage at all, which is how #247 (the reconnect that gave up after one
 * failed attempt) survived.
 *
 * The policy lives here as pure logic so it can be unit-tested on any platform,
 * leaving check_serial as the thin shell that executes it.
 */
#ifndef SERIAL_RECOVERY_H
#define SERIAL_RECOVERY_H

#include "millennium_sdk.h"   /* SERIAL_WATCHDOG_SECONDS and friends */

typedef enum {
    SERIAL_ACTION_NONE = 0,
    SERIAL_ACTION_KEEPALIVE,   /* link is idle but alive; poke it (#59) */
    SERIAL_ACTION_MARK_DEAD,   /* declare the link down and start retrying */
    SERIAL_ACTION_RECONNECT    /* backoff elapsed; try to reopen the port */
} serial_action_t;

typedef enum {
    SERIAL_READY_WAIT = 0,
    SERIAL_READY_SEND_HELLO,
    SERIAL_READY_FAIL
} serial_ready_action_t;

typedef struct {
    int  fd_open;              /* client->display_fd != -1 */
    int  link_healthy;         /* client->serial_healthy */
    long idle_seconds;         /* now - last_serial_activity */
    long seconds_until_retry;  /* next_reconnect_time - now; <= 0 means due */
} serial_link_state_t;

/* What check_serial should do this pass.  NULL state yields NONE. */
serial_action_t serial_recovery_next_action(const serial_link_state_t *state);

/* Seconds to wait before retry number `reconnect_attempts` (1-based, i.e. the
 * count after the failure has been tallied).  Doubles per attempt, capped at
 * SERIAL_MAX_BACKOFF_SECONDS. */
int serial_recovery_backoff_seconds(int reconnect_attempts);

/* Opening an Arduino Micro resets it. Keep the descriptor open while its
 * bootloader/sketch starts, periodically retrying protocol negotiation instead
 * of closing and resetting it again. */
serial_ready_action_t serial_recovery_readiness_action(long elapsed_seconds,
                                                       long since_hello_seconds);

#endif /* SERIAL_RECOVERY_H */
