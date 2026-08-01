#include "coin_gate.h"

/* (#239) The daemon gates the validator with three commands, each paired with
 * its terminator exactly as the call sites in daemon.c send them:
 *
 *   'a'       accept coins    -- hook up, on the IDLE_DOWN -> IDLE_UP edge
 *   'c' 'z'   reject coins    -- hook down, remote hangup, call ended
 *   'f' 'z'   re-init/enable  -- a call is coming in
 *
 * Note 'a' travels alone; only 'c' and 'f' take the 'z' terminator.  See
 * docs/COIN_VALIDATOR.md.
 */
uint8_t millennium_coin_gate_track(uint8_t gate, uint8_t cmd) {
    if (cmd == 'a' || cmd == 'c' || cmd == 'f') {
        return cmd;
    }
    /* 'z' (terminator) and '@' (validator hard reset) are not gate changes. */
    return gate;
}

size_t millennium_coin_gate_resync(uint8_t gate, uint8_t *out, size_t cap) {
    if (!out || cap < COIN_GATE_RESYNC_MAX) {
        return 0;
    }

    if (gate == 'a') {
        out[0] = 'a';
        return 1;
    }

    if (gate == 'f') {
        out[0] = 'f';
        out[1] = 'z';
        return 2;
    }

    /* 'c', or nothing tracked yet because the link died before the daemon ever
     * gated the validator.  Reject is the fail-safe answer either way: an
     * armed validator with the handset down physically swallows a coin that
     * handle_coin_event() then refuses to credit, because it only credits in
     * IDLE_UP.  Refusing the coin loses nobody's money. */
    out[0] = 'c';
    out[1] = 'z';
    return 2;
}
