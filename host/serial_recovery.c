#include "serial_recovery.h"

serial_action_t serial_recovery_next_action(const serial_link_state_t *state) {
    if (!state) return SERIAL_ACTION_NONE;

    /* (#247) A closed fd means the link is down, whatever the health flag says.
     * Testing this FIRST is the whole of that fix.  It used to be an early
     * return at the top of check_serial, and open_serial_port() drops the stale
     * fd before it tries the open -- so one failed reconnect left the fd closed
     * and every later pass bailed out before reaching the retry below. */
    if (!state->fd_open && state->link_healthy) {
        return SERIAL_ACTION_MARK_DEAD;
    }

    if (state->fd_open && state->link_healthy) {
        if (state->idle_seconds > SERIAL_WATCHDOG_SECONDS) {
            return SERIAL_ACTION_MARK_DEAD;
        }
        /* (#59) Poke an idle-but-alive link so the watchdog doesn't fire on a
         * phone nobody is using.  The upper bound leaves idle_seconds exactly
         * equal to SERIAL_WATCHDOG_SECONDS doing nothing for one pass, which is
         * the long-standing behaviour: by then the keepalive writes are already
         * failing, so the next pass declares the link dead anyway. */
        if (state->idle_seconds >= SERIAL_KEEPALIVE_INTERVAL &&
            state->idle_seconds < SERIAL_WATCHDOG_SECONDS) {
            return SERIAL_ACTION_KEEPALIVE;
        }
        return SERIAL_ACTION_NONE;
    }

    /* Link is already down: retry once the backoff has elapsed. */
    if (!state->link_healthy && state->seconds_until_retry <= 0) {
        return SERIAL_ACTION_RECONNECT;
    }

    return SERIAL_ACTION_NONE;
}

int serial_recovery_backoff_seconds(int reconnect_attempts) {
    int backoff;

    if (reconnect_attempts < 0) reconnect_attempts = 0;

    /* Clamp before shifting.  reconnect_attempts climbs by one per failed
     * retry and is never reset while the link stays down, so an outage lasting
     * past roughly half an hour would otherwise evaluate 1 << 31 and shift a
     * signed int past its width -- undefined behaviour.  The result is capped
     * a moment later anyway, so clamping the exponent costs nothing. */
    if (reconnect_attempts > 30) reconnect_attempts = 30;

    backoff = 1 << reconnect_attempts;
    if (backoff > SERIAL_MAX_BACKOFF_SECONDS) {
        backoff = SERIAL_MAX_BACKOFF_SECONDS;
    }
    return backoff;
}

serial_ready_action_t serial_recovery_readiness_action(long elapsed_seconds,
                                                       long since_hello_seconds) {
    if (elapsed_seconds >= 10) return SERIAL_READY_FAIL;
    if (since_hello_seconds >= 1) return SERIAL_READY_SEND_HELLO;
    return SERIAL_READY_WAIT;
}
