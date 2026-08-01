/* Coin validator gate tracking (#239).
 *
 * The daemon opens and closes the Mars/MEI TRC-6500's coin gate with a handful
 * of one-byte commands.  When the serial link drops and comes back, the SDK
 * has to put the validator back into whatever gate state the daemon last asked
 * for -- the same way it re-sends the last display contents.  These helpers
 * keep that decision in one testable place, away from the serial layer.
 */
#ifndef COIN_GATE_H
#define COIN_GATE_H

#include <stddef.h>
#include <stdint.h>

/* Longest sequence millennium_coin_gate_resync() emits. */
#define COIN_GATE_RESYNC_MAX 2

/* Fold a byte the daemon is about to send to the validator into the tracked
 * gate state.  Returns the new state; non-gate bytes leave it unchanged. */
uint8_t millennium_coin_gate_track(uint8_t gate, uint8_t cmd);

/* Write the byte sequence that restores `gate` into `out` and return its
 * length.  `cap` must be at least COIN_GATE_RESYNC_MAX; returns 0 otherwise. */
size_t millennium_coin_gate_resync(uint8_t gate, uint8_t *out, size_t cap);

#endif /* COIN_GATE_H */
